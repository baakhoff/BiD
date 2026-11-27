// A stand-in for libusb, for tests/wire_test.cpp only. It is found ahead of
// the real header, so driver.h compiles against it unchanged and every
// control transfer lands in a list the test can read.
//
// It also plays a device: the limits below are what the firmware enforces,
// set by each test from the model's USB descriptors. A write past them is
// answered with LIBUSB_ERROR_IO, as the real boxes do.
#ifndef BID_FAKE_LIBUSB_H
#define BID_FAKE_LIBUSB_H

#include <cstdint>
#include <cstring>
#include <sys/types.h>
#include <vector>

enum {
  LIBUSB_SUCCESS = 0,
  LIBUSB_ERROR_IO = -1,
  LIBUSB_ERROR_NO_DEVICE = -4,
  LIBUSB_ERROR_NOT_FOUND = -5,
  LIBUSB_ERROR_BUSY = -6,
  LIBUSB_ERROR_TIMEOUT = -7,
  LIBUSB_ERROR_PIPE = -9,
};
enum {
  LIBUSB_CLASS_AUDIO = 0x01,
  LIBUSB_CLASS_HID = 0x03,
  LIBUSB_CLASS_APPLICATION = 0xfe,
  LIBUSB_CLASS_VENDOR_SPEC = 0xff,
};

struct libusb_context {};
struct libusb_device {};
struct libusb_device_handle {};
struct libusb_device_descriptor {
  uint16_t idVendor;
  uint16_t idProduct;
};
struct libusb_interface_descriptor {
  uint8_t bInterfaceNumber;
  uint8_t bInterfaceClass;
};
struct libusb_interface {
  const libusb_interface_descriptor *altsetting;
};
struct libusb_config_descriptor {
  uint8_t bNumInterfaces;
  const libusb_interface *interface;
};

struct fake_write {
  uint16_t wValue;
  uint16_t wIndex;
  uint16_t len;
  uint8_t data[4];
};

struct fake_usb {
  // what the device accepts
  int mixer_cells = 96;      // entity 0x3c, cells 0..n-1
  int routing_outputs = 16;  // entity 0x33, outputs 0..n-1
  int phase_channels = 16;   // entity 0x0b, phase switch on channels 1..n
  // whether requests addressed to an interface other than 0 are served;
  // a first-generation iD14 hangs on them instead
  bool spare_takes_control = true;
  bool hung = false;
  // which interface a kernel driver other than the sound one is bound to
  int foreign_driver_iface = -1;
  // what happened
  std::vector<fake_write> writes;
  bool auto_detach = false;
  int claimed = -1;
  bool foreign_driver_bound = false;
  int detaches = 0;
  int reattaches = 0;
};
inline fake_usb fake;

inline void fake_reset()
{
  fake = fake_usb();
}

inline const char *libusb_error_name(int err)
{
  switch (err) {
    case LIBUSB_ERROR_IO: return "LIBUSB_ERROR_IO";
    case LIBUSB_ERROR_NO_DEVICE: return "LIBUSB_ERROR_NO_DEVICE";
    case LIBUSB_ERROR_BUSY: return "LIBUSB_ERROR_BUSY";
    case LIBUSB_ERROR_TIMEOUT: return "LIBUSB_ERROR_TIMEOUT";
    case LIBUSB_ERROR_PIPE: return "LIBUSB_ERROR_PIPE";
  }
  return "LIBUSB_ERROR_OTHER";
}

inline int libusb_init(libusb_context **) { return LIBUSB_SUCCESS; }
inline void libusb_exit(libusb_context *) {}
inline ssize_t libusb_get_device_list(libusb_context *, libusb_device ***list) { *list = nullptr; return 0; }
inline void libusb_free_device_list(libusb_device **, int) {}
inline int libusb_get_device_descriptor(libusb_device *, libusb_device_descriptor *) { return LIBUSB_SUCCESS; }

// The interface layout of a first-generation iD14: audio control, two
// streaming interfaces, DFU, HID.
inline int libusb_get_active_config_descriptor(libusb_device *, libusb_config_descriptor **config)
{
  static const libusb_interface_descriptor alts[5] = {
    {0, LIBUSB_CLASS_AUDIO}, {1, LIBUSB_CLASS_AUDIO}, {2, LIBUSB_CLASS_AUDIO},
    {3, LIBUSB_CLASS_APPLICATION}, {4, LIBUSB_CLASS_HID},
  };
  static const libusb_interface ifaces[5] = {{&alts[0]}, {&alts[1]}, {&alts[2]}, {&alts[3]}, {&alts[4]}};
  static libusb_config_descriptor cfg = {5, ifaces};
  *config = &cfg;
  return LIBUSB_SUCCESS;
}
inline void libusb_free_config_descriptor(libusb_config_descriptor *) {}

inline libusb_device_handle *libusb_open_device_with_vid_pid(libusb_context *, uint16_t, uint16_t)
{
  static libusb_device_handle h;
  fake.foreign_driver_bound = fake.foreign_driver_iface >= 0;
  return &h;
}
inline libusb_device *libusb_get_device(libusb_device_handle *)
{
  static libusb_device d;
  return &d;
}
inline void libusb_close(libusb_device_handle *) {}
inline int libusb_reset_device(libusb_device_handle *) { return LIBUSB_SUCCESS; }

inline int libusb_set_auto_detach_kernel_driver(libusb_device_handle *, int on)
{
  fake.auto_detach = on != 0;
  return LIBUSB_SUCCESS;
}
inline int libusb_kernel_driver_active(libusb_device_handle *, int iface)
{
  return fake.foreign_driver_bound && iface == fake.foreign_driver_iface ? 1 : 0;
}
// As on Linux: an interface a kernel driver holds cannot be claimed, unless
// auto-detach is on, in which case the driver is unbound first.
inline int libusb_claim_interface(libusb_device_handle *, int iface)
{
  if (fake.foreign_driver_bound && iface == fake.foreign_driver_iface) {
    if (!fake.auto_detach)
      return LIBUSB_ERROR_BUSY;
    fake.foreign_driver_bound = false;
    fake.detaches++;
  }
  fake.claimed = iface;
  return LIBUSB_SUCCESS;
}
inline int libusb_attach_kernel_driver(libusb_device_handle *, int iface)
{
  if (iface == fake.foreign_driver_iface && !fake.foreign_driver_bound) {
    fake.foreign_driver_bound = true;
    fake.reattaches++;
  }
  return LIBUSB_SUCCESS;
}
// ...and with auto-detach on, releasing hands the interface back.
inline int libusb_release_interface(libusb_device_handle *h, int iface)
{
  fake.claimed = -1;
  if (fake.auto_detach)
    libusb_attach_kernel_driver(h, iface);
  return LIBUSB_SUCCESS;
}

inline int libusb_control_transfer(libusb_device_handle *, uint8_t bmRequestType, uint8_t,
                                   uint16_t wValue, uint16_t wIndex, unsigned char *data,
                                   uint16_t wLength, unsigned int)
{
  if ((wIndex & 0xff) != 0 && !fake.spare_takes_control)
    fake.hung = true;
  if (fake.hung)
    return LIBUSB_ERROR_TIMEOUT;
  if (bmRequestType & 0x80)
    return LIBUSB_ERROR_IO; // reads are not modelled
  int entity = wIndex >> 8, sel = wValue >> 8, chan = wValue & 0xff;
  if (entity == 0x0b && sel == 0x0d && (chan < 1 || chan > fake.phase_channels))
    return LIBUSB_ERROR_PIPE;
  if (entity == 0x3c) {
    // the matrix is addressed flat, cell 0 at wValue 0x0100
    int cell = wValue - 0x0100;
    if (cell < 0 || cell >= fake.mixer_cells)
      return LIBUSB_ERROR_IO;
  }
  if (entity == 0x33 && sel == 0x06 && chan >= fake.routing_outputs)
    return LIBUSB_ERROR_IO;
  fake_write w = {wValue, wIndex, wLength, {0, 0, 0, 0}};
  memcpy(w.data, data, wLength < 4 ? wLength : 4);
  fake.writes.push_back(w);
  return wLength;
}

#endif
