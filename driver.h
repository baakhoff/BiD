#include <libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include "device_properties.h"

static libusb_device_handle *devh = NULL;
static bool driver_connected = false;
static int control_iface = 0;

int device_probe()
{
  // discover devices
  libusb_device **list;
  libusb_device *found = NULL;
  libusb_context *tctx = NULL;
  libusb_init(NULL);
  ssize_t cnt = libusb_get_device_list(tctx   , &list);
  ssize_t i = 0;
  int err = 0;
  if (cnt < 0) {
    return -1;
  }

  int foundid = -1;

  std::cout << "[Begin usb probe] size: " << cnt << "\n";
  for (i = 0; i < cnt; i++) 
  {
      libusb_device *device = list[i];
      libusb_device_descriptor desc = {0};
      libusb_get_device_descriptor(device, &desc);
      std::cout << "Vendor: " << std::hex << desc.idVendor << " - Product: " << std::hex << desc.idProduct << '\n';
      for (size_t d = 0; d < devices.size(); d++)
      {
        if (desc.idVendor != 0x2708) {
          continue;
        }
        if (devices[d].usb_id == desc.idProduct) {
            foundid = d;
            break;
        }
      }
  }
  libusb_free_device_list(list, 1);
  return foundid;
}

// The iD firmware accepts the mixer control requests on any of its interfaces,
// not just the AudioControl one (see issue #15). Sending them to the spare
// DFU/vendor interface means snd-usb-audio keeps the audio interfaces and
// playback continues while BiD is connected. Returns -1 if the device has
// no such interface.
int find_control_interface(libusb_device *dev)
{
  struct libusb_config_descriptor *config;
  if (libusb_get_active_config_descriptor(dev, &config) != LIBUSB_SUCCESS)
    return -1;
  int found = -1;
  for (int i = 0; i < config->bNumInterfaces; i++) {
    const struct libusb_interface_descriptor *alt = &config->interface[i].altsetting[0];
    if (alt->bInterfaceClass == LIBUSB_CLASS_APPLICATION || alt->bInterfaceClass == LIBUSB_CLASS_VENDOR_SPEC) {
      found = alt->bInterfaceNumber;
      break;
    }
  }
  libusb_free_config_descriptor(config);
  return found;
}


//--------
// First is for Left channels, Second for Right
// Order, Main Mix, Alt Speaker, Cue A, Cue B, DAW Mix
// Left is First,x - Right is x,Second
// --------
// 
// Channel 1: {0x1b,x},{0x1d,x},{0x19,x},{0x1a,x},{0x00,x}
// Channel 2: {x,0x1c},{x,0x1e},{x,0x19},{x,0x1a},{x,0x01}
// Channel 3: {0x1b, x},{0x1d, x},{0x19, x},{0x1a, x},{0x02, x}
// Channel 4: {x, 0x1c},{x, 0x1e},{x, 0x19},{x,0x1a},{x,0x03}
// Channel 3: {0x1b, x},{0x1d, x},{0x19, x},{0x1a, x},{0x04, x}
// Channel 4: {x, 0x1c},{x, 0x1e},{x, 0x19},{x,0x1a},{x,0x05}

inline std::vector<std::vector<uint8_t>> routeToggle 
{
  {0x1b,0x1d,0x19,0x1a,0x00}, //Channel 1 (Main L)
  {0x1c,0x1e,0x19,0x1a,0x01}, //Channel 2 (Main R)
  {0x1b,0x1d,0x19,0x1a,0x02}, //Channel 3
  {0x1c,0x1e,0x19,0x1a,0x03}, //Channel 4
  {0x1b,0x1d,0x19,0x1a,0x04}, //Channel 5 (HP L)
  {0x1c,0x1e,0x19,0x1a,0x05}  //Channel 6 (HP R)
};

uint16_t float_to_u16(float volume) {
  uint16_t res = -32768+32767*volume;
  return res;
}

void set_vinyl_dm(float volume) {
  uint16_t one = float_to_u16(volume);
  uint16_t zero = float_to_u16(0);
  int err = 0;
  err = libusb_control_transfer(devh, 0x21, 0x1, 0x0100, 0x3c00 | control_iface, (uint8_t*)&one, 2, 0);
  err = libusb_control_transfer(devh, 0x21, 0x1, 0x0101, 0x3c00 | control_iface, (uint8_t*)&zero, 2, 0);
  err = libusb_control_transfer(devh, 0x21, 0x1, 0x0104, 0x3c00 | control_iface, (uint8_t*)&zero, 2, 0);
  err = libusb_control_transfer(devh, 0x21, 0x1, 0x0105, 0x3c00 | control_iface, (uint8_t*)&one, 2, 0);
  if (err < 0) {
    printf("libusb_control_transfer failed: %s\n", libusb_error_name(err));
  }
}

void set_hp_volume(float volume) {
  assert(volume>=0 && volume<=1);
  uint16_t vol = float_to_u16(volume);
  int err = 0;
  // Feature unit 0x0c carries four output channels: 1 and 2 are the monitor
  // pair, 3 and 4 the headphones. Entity 0x0a, which this used to address,
  // declares no controls at all, so those writes went nowhere.
  err = libusb_control_transfer(devh, 0x21, 0x1, 0x0203, 0x0c00 | control_iface, (uint8_t*)&vol, 2, 0);
  err = libusb_control_transfer(devh, 0x21, 0x1, 0x0204, 0x0c00 | control_iface, (uint8_t*)&vol, 2, 0);
  if (err < 0) {
    printf("libusb_control_transfer failed: %s\n", libusb_error_name(err));
  }
}

void set_speaker_volume(float volume) {
  assert(volume>=0 && volume<=1);
  uint16_t vol = float_to_u16(volume);
  int err = 0;
  err = libusb_control_transfer(devh, 0x21, 0x1, 0x1200, 0x3600 | control_iface, (uint8_t*)&vol, 2, 0);
  if (err < 0) {
    printf("libusb_control_transfer failed: %s\n", libusb_error_name(err));
  }
}

// The matrix (entity 0x3c) is 96 cells: 16 inputs by 6 sends, addressed as a
// flat cell number, so cell = input * 6 + send. The six sends are three
// stereo buses, not six mono outputs.
enum mixer_send {
  SEND_MAIN_L  = 0, SEND_MAIN_R  = 1,
  SEND_CUE_A_L = 2, SEND_CUE_A_R = 3,
  SEND_CUE_B_L = 4, SEND_CUE_B_R = 5,
};
#define MIXER_SENDS 6

void set_mixer_cell(int input, int send, float gain)
{
  assert(gain>=0 && gain<=1);
  uint16_t v = float_to_u16(gain);
  int err = libusb_control_transfer(devh, 0x21, 0x1, 0x0100 + input * MIXER_SENDS + send,
                                    0x3c00 | control_iface, (uint8_t*)&v, 2, 0);
  if (err < 0) {
    printf("libusb_control_transfer failed: %s\n", libusb_error_name(err));
  }
}

// pan runs 0.0 hard left, 0.5 centre, 1.0 hard right. Writing one level to
// both main sends, as this used to, sums every input to the middle and
// throws away the stereo image of anything arriving as a pair.
void set_channel_volume(uint16_t chan, float volume, float pan)
{
  assert(volume>=0 && volume<=1);
  assert(pan>=0 && pan<=1);
  // centre leaves both sends at the fader level, so a centred channel is as
  // loud as it was before there was a pan control at all
  float l = 2.0f * (1.0f - pan), r = 2.0f * pan;
  set_mixer_cell(chan, SEND_MAIN_L, volume * (l > 1.0f ? 1.0f : l));
  set_mixer_cell(chan, SEND_MAIN_R, volume * (r > 1.0f ? 1.0f : r));
}


//TODO: eventually need ranges set in pre-defined device specific structs
inline std::vector<uint16_t> chanVals {0x0600,0x0601,0x0602,0x0603,0x0604,0x0605};

void set_routing_value(int chan, int position) 
{
  int err = 0;

  err = libusb_control_transfer(devh, 0x21, 0x1, chanVals[chan], 0x3300 | control_iface, (uint8_t*)&routeToggle[chan][position], 1, 0);

  if (err < 0) {
    printf("libusb_control_transfer failed: %s\n", libusb_error_name(err));
  }
}

//TODO: eventually need ranges set in pre-defined device specific structs
inline std::vector<uint16_t> masterVals 
{
  0x0500,//Dim
  0x0c00,//Alt
  0x0700,//Talkback
  0x0300,//Phase
  0x0000,//Mono
  0x0400 //Speaker Mute
};
inline bool masterToggle[6] = {false,false,false,false,false,false}; // Dummy storage selection storage

void set_bool_state(int mode) 
{
  int err = 0;
  masterToggle[mode] = !masterToggle[mode];
  err = libusb_control_transfer(devh, 0x21, 0x1, masterVals[mode], 0x3600 | control_iface, (uint8_t*)&masterToggle[mode], 1, 0);

  if (err < 0) {
    printf("libusb_control_transfer failed: %s\n", libusb_error_name(err));
  }
  //return masterToggle[mode];
}

// one per matrix input: the iD44 and iD48 show more channels than the ten
// this used to hold, and the fader loop indexes it by channel
inline bool phaseToggle[16] = {};

void set_phase(int chan, bool on) //0 indexed
{
  if (chan < 0 || chan >= (int)(sizeof(phaseToggle)/sizeof(phaseToggle[0])))
    return;
  phaseToggle[chan] = on;
  int err = libusb_control_transfer(devh, 0x21, 0x1, 0x0d01+chan, 0x0b00 | control_iface, (uint8_t*)&phaseToggle[chan], 1, 0);

  if (err < 0) {
    printf("libusb_control_transfer failed: %s\n", libusb_error_name(err));
  }
}

void set_phase_state(int chan) //0 indexed
{
  if (chan < 0 || chan >= (int)(sizeof(phaseToggle)/sizeof(phaseToggle[0])))
    return;
  set_phase(chan, !phaseToggle[chan]);
}


//Boosted is range 0.0-1.1
int driver_init(uint16_t deviceid)
{
  int err;
  err = libusb_init(NULL);
  assert(err == LIBUSB_SUCCESS);

  devh = libusb_open_device_with_vid_pid(NULL, 0x2708, deviceid); //MKI
  //if (!devh)
//	devh = libusb_open_device_with_vid_pid(NULL, 0x2708, 0x0008); //MKII
  if (!devh) {
    driver_connected = false;
    return false;
  }
  assert(devh != NULL);

  // Talk to the device through its spare DFU/vendor interface, so the kernel
  // audio driver keeps the AudioControl/Streaming interfaces and playback
  // keeps running while BiD is connected. Devices without a spare
  // interface fall back to the old exclusive grab of interface 0.
  control_iface = find_control_interface(libusb_get_device(devh));
  if (control_iface < 0) {
    printf("no spare control interface found, grabbing interface 0 (audio pauses while connected)\n");
    control_iface = 0;
    err = libusb_set_auto_detach_kernel_driver(devh, 1);
    if (err < 0) {
      printf("libusb_set_auto_detach_kernel_driver failed: %s\n", libusb_error_name(err));
      driver_connected = false;
      return false;
    }
  }
  err = libusb_claim_interface(devh, control_iface);
  if (err < 0) {
    printf("libusb_claim_interface failed: %s\n", libusb_error_name(err));
    driver_connected = false;
    return false;
  }
  driver_connected = true;
  return driver_connected;
}

void driver_shutdown()
{
  driver_connected = false;
  if (!devh)
    return;
  if (control_iface == 0) {
    // exclusive mode kicked the kernel driver off interface 0, hand it back
    libusb_reset_device(devh);
    libusb_release_interface(devh, 0);
    libusb_attach_kernel_driver(devh,0);
  }
  else {
    libusb_release_interface(devh, control_iface);
  }
  libusb_close(devh);
  libusb_exit(NULL);
  devh = NULL;
}
