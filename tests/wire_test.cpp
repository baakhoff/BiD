// What BiD puts on the wire, checked without an interface on the desk.
//
// driver.h is compiled here against tests/fake/libusb.h, which records every
// control transfer and answers LIBUSB_ERROR_IO to a write outside the limits
// a test gives it. Those limits come from each model's USB descriptors, so a
// profile in device_properties.h that disagrees with its device fails here
// instead of in a tester's log.
//
// This proves addresses, not sound: whether a cell that is written does what
// its name says is still a job for ears (docs/TESTING.md).
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "../driver.h"

static int failures = 0;

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++; \
    } \
  } while (0)

static const device_properties &model(uint16_t usb_id)
{
  for (size_t i = 0; i < devices.size(); i++)
    if (devices[i].usb_id == usb_id)
      return devices[i];
  printf("no profile for usb id 0x%04x\n", usb_id);
  exit(2);
}

static void start(const device_properties &d, int mixer_cells, int routing_outs)
{
  fake_reset();
  fake.mixer_cells = mixer_cells;
  fake.routing_outputs = routing_outs;
  transfer_errors = 0;
  use_device_geometry(d);
  control_iface = d.control_on_spare ? 3 : 0;
}

static bool wrote(uint16_t wValue, uint16_t entity)
{
  for (size_t i = 0; i < fake.writes.size(); i++)
    if (fake.writes[i].wValue == wValue && (fake.writes[i].wIndex >> 8) == entity)
      return true;
  return false;
}

// Everything the app can send to the matrix: each strip into each of the
// three fader pages the state file carries, then silence for the rows that
// have no strip. This is the union of the loops in main.cpp, cue pages the
// model does not have included - the driver must drop those by itself.
static void push_whole_matrix(const device_properties &d)
{
  int strips = d.mic_inputs + d.digital_inputs;
  for (int m = 0; m < MIXER_BUSES; m++)
    for (int i = 0; i < strips; i++)
      set_channel_send(i, m, 0.5f, 0.5f);
  for (int i = strips; i < d.matrix_inputs; i++)
    for (int s = 0; s < mixer_stride; s++)
      set_mixer_cell(i, s, 0.0f);
}

// The first-generation iD14 (2708:0002), from the descriptors in issue #28:
// mixer unit 60 has four output channels and seven bytes of bmMixerControls,
// so 14 x 4 = 56 cells; routing unit 51 has four outputs.
static void test_id14_first_generation()
{
  const device_properties &d = model(0x0002);
  CHECK(d.mixer_stride == 4);
  CHECK(d.cue_mixes == 1);
  CHECK(d.matrix_inputs * d.mixer_stride == 56);
  CHECK(d.routing_outputs == 4);

  start(d, 56, 4);
  push_whole_matrix(d);
  // nothing lands outside the matrix: this is the failure issue #28 showed
  CHECK(transfer_errors == 0);
  // every cell written exactly once: no bus spilling onto a neighbour's row
  std::set<uint16_t> seen;
  for (size_t i = 0; i < fake.writes.size(); i++) {
    CHECK((fake.writes[i].wIndex >> 8) == 0x3c);
    CHECK((fake.writes[i].wIndex & 0xff) == 0);
    CHECK(seen.insert(fake.writes[i].wValue).second);
  }
  CHECK(seen.size() == 56);
  CHECK(*seen.begin() == 0x0100);
  CHECK(*seen.rbegin() == 0x0100 + 55);

  // mymixer, written on this model, sends microphone 2 to the main mix at
  // cells 4 and 5 - the second row of a four-wide matrix
  start(d, 56, 4);
  set_channel_send(1, 0, 1.0f, 0.5f);
  CHECK(fake.writes.size() == 2);
  CHECK(wrote(0x0104, 0x3c) && wrote(0x0105, 0x3c));

  // its one cue is the second pair of the row
  start(d, 56, 4);
  set_channel_send(2, 1, 1.0f, 0.5f);
  CHECK(wrote(0x010a, 0x3c) && wrote(0x010b, 0x3c));

  // a second cue has no column here; sent anyway, it would be the next
  // input's main mix
  start(d, 56, 4);
  set_channel_send(2, 2, 1.0f, 0.5f);
  CHECK(fake.writes.empty());
  CHECK(transfer_errors == 0);

  // no routing codes are known for this model, so none are written
  start(d, 56, 4);
  for (int p = 0; p < 6; p++)
    set_route_pair(p, ROUTE_MAIN);
  CHECK(fake.writes.empty());

  // The reporter's hardware took the phase switch on channels 1 to 4 and
  // stalled on 5 to 12; the app offers it for every strip it draws.
  start(d, 56, 4);
  fake.phase_channels = 4;
  for (int i = 0; i < d.mic_inputs + d.digital_inputs; i++)
    set_phase(i, false);
  CHECK(transfer_errors == 0);
  CHECK(fake.writes.size() == 4);
  CHECK(wrote(0x0d01, 0x0b) && wrote(0x0d04, 0x0b) && !wrote(0x0d05, 0x0b));
  set_phase_state(7); // a click on a strip without the switch sends nothing
  CHECK(fake.writes.size() == 4 && transfer_errors == 0);
}

// The same hardware hung until replugged when the requests went to its DFU
// interface, the one BiD prefers elsewhere. This model must be opened on
// interface 0, with nothing ever addressed anywhere else.
static void test_id14_first_generation_stays_off_the_spare_interface()
{
  const device_properties &d = model(0x0002);
  CHECK(!d.control_on_spare);
  unsetenv("BID_CONTROL_IFACE");
  fake_reset();
  fake.spare_takes_control = false;
  fake.mixer_cells = 56;
  fake.foreign_driver_iface = 3; // Monix loaded, as on the reporter's machine
  transfer_errors = 0;
  use_device_geometry(d);
  CHECK(driver_init(0x0002));
  CHECK(control_iface == 0 && fake.claimed == 0);
  CHECK(fake.detaches == 0); // Monix's interface is not touched at all
  set_speaker_volume(0.5f);
  push_whole_matrix(d);
  CHECK(!fake.hung);
  CHECK(transfer_errors == 0);
  for (size_t i = 0; i < fake.writes.size(); i++)
    CHECK((fake.writes[i].wIndex & 0xff) == 0);
  driver_shutdown();
}

// The models confirmed on hardware must not move: six cells to a row, cue B
// in the last pair, the routing tables each of them answered to.
static void test_verified_models_unchanged()
{
  const device_properties &mk2 = model(0x0008);
  CHECK(mk2.mixer_stride == 6 && mk2.cue_mixes == 2 && mk2.routing_outputs == 6);
  start(mk2, 14 * 6, 6);
  push_whole_matrix(mk2);
  CHECK(transfer_errors == 0);
  CHECK(fake.writes.size() == 14 * 6);
  start(mk2, 14 * 6, 6);
  set_channel_send(11, 2, 1.0f, 0.5f);
  CHECK(wrote(0x0100 + 11 * 6 + 4, 0x3c) && wrote(0x0100 + 11 * 6 + 5, 0x3c));
  start(mk2, 14 * 6, 6);
  for (int p = 0; p < 8; p++)
    set_route_pair(p, ROUTE_MAIN); // pairs past the phones are dropped
  CHECK(transfer_errors == 0);
  CHECK(fake.writes.size() == 6);
  CHECK(fake.writes[0].wValue == 0x0600 && fake.writes[0].data[0] == 0x1b);
  CHECK(fake.writes[5].wValue == 0x0605 && fake.writes[5].data[0] == 0x1c);

  const device_properties &id24 = model(0x000d);
  CHECK(id24.mixer_stride == 6 && id24.cue_mixes == 2 && id24.routing_outputs == 16);
  start(id24, 16 * 6, 16);
  push_whole_matrix(id24);
  CHECK(transfer_errors == 0);
  CHECK(fake.writes.size() == 16 * 6);
  start(id24, 16 * 6, 16);
  set_route_pair(2, ROUTE_CUE_A);
  CHECK(fake.writes.size() == 2);
  CHECK(fake.writes[0].wValue == 0x0604 && fake.writes[0].data[0] == 0x1e);
  CHECK(fake.writes[1].wValue == 0x0605 && fake.writes[1].data[0] == 0x1f);
}

// No profile may describe a matrix its own strips do not fit in.
static void test_profiles_are_consistent()
{
  for (size_t k = 0; k < devices.size(); k++) {
    const device_properties &d = devices[k];
    if (!d.mixer_known)
      continue;
    CHECK(d.mixer_stride >= 2 && d.mixer_stride <= MIXER_SENDS);
    CHECK(d.mixer_stride == 2 * (1 + d.cue_mixes));
    CHECK(d.mic_inputs + d.digital_inputs <= d.matrix_inputs);
    if (d.routing_known)
      CHECK(d.route_scheme != ROUTE_SCHEME_NONE);
  }
}

// A refused write is counted, so a connect can tell a wall from a device.
static void test_refusals_are_counted()
{
  const device_properties &d = model(0x0002);
  start(d, 8, 4); // a device with a matrix of eight cells only
  push_whole_matrix(d);
  CHECK(transfer_errors == 56 - 8);
  CHECK(fake.writes.size() == 8);
}

// Monix's audient_id kernel module binds the DFU interface BiD talks
// through. The connect must borrow the interface and hand it back, not
// fail as if the device could not be opened (issue #28).
static void test_borrowing_the_spare_interface()
{
  unsetenv("BID_CONTROL_IFACE");

  use_device_geometry(model(0x000d)); // a model that talks over it
  fake_reset(); // nothing bound: the ordinary case
  CHECK(driver_init(0x000d));
  CHECK(control_iface == 3 && fake.claimed == 3);
  CHECK(fake.detaches == 0);
  CHECK(!fake.auto_detach); // nobody to detach, so nothing is asked for
  driver_shutdown();
  CHECK(fake.claimed == -1);

  fake_reset();
  fake.foreign_driver_iface = 3;
  CHECK(driver_init(0x000d));
  CHECK(control_iface == 3 && fake.claimed == 3);
  CHECK(fake.detaches == 1 && !fake.foreign_driver_bound);
  driver_shutdown();
  CHECK(fake.reattaches == 1 && fake.foreign_driver_bound);
}

// With Monix's audient_id module loaded there is a way to interface 0 that
// leaves the sound card alone: its relay node. A model that would otherwise
// be opened exclusively goes through it; nothing is claimed or detached, and
// each write is one message in the module's layout.
static void test_relay_instead_of_exclusive_mode()
{
  char path[] = "/tmp/bid-relay-XXXXXX";
  int tmp = mkstemp(path);
  CHECK(tmp >= 0);
  close(tmp);
  setenv("BID_RELAY_DEV", path, 1);
  unsetenv("BID_CONTROL_IFACE");

  const device_properties &d = model(0x0002);
  fake_reset();
  fake.foreign_driver_iface = 3; // the module itself, on the DFU interface
  transfer_errors = 0;
  use_device_geometry(d);
  CHECK(driver_init(0x0002));
  CHECK(relay_fd >= 0 && control_iface == 0);
  CHECK(fake.claimed == -1 && fake.detaches == 0 && !fake.auto_detach);
  int16_t level = 0x1234;
  CHECK(ctl_set(0x1200, 0x3600, &level, 2) == 2);
  set_channel_send(1, 0, 1.0f, 0.5f);
  CHECK(transfer_errors == 0);
  CHECK(fake.writes.empty()); // none of it went by libusb
  driver_shutdown();
  CHECK(relay_fd == -1);

  unsigned char got[64] = {0};
  FILE *f = fopen(path, "rb");
  size_t n = f ? fread(got, 1, sizeof(got), f) : 0;
  if (f)
    fclose(f);
  // three messages of an eight-byte header and two bytes of data
  CHECK(n == 30);
  const unsigned char first[10] = {0x21, 0x01, 0x00, 0x12, 0x00, 0x36, 0x02, 0x00, 0x34, 0x12};
  CHECK(memcmp(got, first, 10) == 0);
  // microphone 2 into the main mix: cells 4 and 5 of entity 0x3c, interface 0
  CHECK(got[10] == 0x21 && got[12] == 0x04 && got[13] == 0x01 && got[14] == 0x00 && got[15] == 0x3c);
  CHECK(got[22] == 0x05 && got[23] == 0x01 && got[25] == 0x3c);

  // a model that talks over its spare interface has no use for the relay
  fake_reset();
  use_device_geometry(model(0x000d));
  CHECK(driver_init(0x000d));
  CHECK(relay_fd == -1 && control_iface == 3 && fake.claimed == 3);
  driver_shutdown();

  // and BID_CONTROL_IFACE=0 still means libusb on interface 0
  setenv("BID_CONTROL_IFACE", "0", 1);
  fake_reset();
  use_device_geometry(d);
  CHECK(driver_init(0x0002));
  CHECK(relay_fd == -1 && fake.claimed == 0);
  driver_shutdown();
  unsetenv("BID_CONTROL_IFACE");

  // a relay that is not there changes nothing
  setenv("BID_RELAY_DEV", "/nonexistent/audient_id0", 1);
  fake_reset();
  use_device_geometry(d);
  CHECK(driver_init(0x0002));
  CHECK(relay_fd == -1 && fake.claimed == 0);
  driver_shutdown();

  unsetenv("BID_RELAY_DEV");
  unlink(path);
}

int main()
{
  setup_devices();
  test_id14_first_generation();
  test_id14_first_generation_stays_off_the_spare_interface();
  test_verified_models_unchanged();
  test_profiles_are_consistent();
  test_refusals_are_counted();
  test_borrowing_the_spare_interface();
  test_relay_instead_of_exclusive_mode();
  if (failures) {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("wire_test: all checks passed\n");
  return 0;
}
