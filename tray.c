// System tray icon for MixiD, speaking the StatusNotifierItem protocol
// (org.kde.StatusNotifierItem) directly over D-Bus via sd-bus, so no GUI
// toolkit dependency is needed. Works out of the box on KDE and on anything
// else with a StatusNotifier tray; when no tray watcher is running,
// tray_init() reports failure and MixiD keeps its plain quit-on-close
// behaviour. Any click on the icon requests a show/hide toggle, which the
// main loop picks up through tray_pump().
#include "tray.h"

#ifdef HAVE_TRAY
#include <systemd/sd-bus.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ICON_SIZE 22

static sd_bus *bus = NULL;
static char busname[64];
static int clicked = 0;
static uint8_t icon[ICON_SIZE * ICON_SIZE * 4]; // ARGB, network byte order

static void icon_rect(int x0, int y0, int w, int h, uint32_t argb)
{
  for (int y = y0; y < y0 + h; y++)
    for (int x = x0; x < x0 + w; x++) {
      uint8_t *p = &icon[(y * ICON_SIZE + x) * 4];
      p[0] = argb >> 24; p[1] = argb >> 16; p[2] = argb >> 8; p[3] = argb;
    }
}

// three little faders, matching what MixiD is about
static void icon_build(void)
{
  memset(icon, 0, sizeof(icon));
  const int track_x[3] = {4, 10, 16};
  const int cap_y[3]   = {4, 12, 8};
  for (int i = 0; i < 3; i++) {
    icon_rect(track_x[i], 2, 2, 18, 0xC0FFFFFF);
    icon_rect(track_x[i] - 1, cap_y[i], 4, 5, 0xFFFFFFFF);
  }
}

static int prop_string(sd_bus *b, const char *path, const char *iface,
                       const char *prop, sd_bus_message *reply, void *userdata,
                       sd_bus_error *error)
{
  const char *val = "";
  if (strcmp(prop, "Category") == 0) val = "ApplicationStatus";
  else if (strcmp(prop, "Id") == 0) val = "MixiD";
  else if (strcmp(prop, "Title") == 0) val = "MixiD";
  else if (strcmp(prop, "Status") == 0) val = "Active";
  return sd_bus_message_append(reply, "s", val);
}

static int prop_false(sd_bus *b, const char *path, const char *iface,
                      const char *prop, sd_bus_message *reply, void *userdata,
                      sd_bus_error *error)
{
  return sd_bus_message_append(reply, "b", 0);
}

static int prop_pixmap(sd_bus *b, const char *path, const char *iface,
                       const char *prop, sd_bus_message *reply, void *userdata,
                       sd_bus_error *error)
{
  int r = sd_bus_message_open_container(reply, 'a', "(iiay)");
  if (r < 0) return r;
  r = sd_bus_message_open_container(reply, 'r', "iiay");
  if (r < 0) return r;
  r = sd_bus_message_append(reply, "ii", ICON_SIZE, ICON_SIZE);
  if (r < 0) return r;
  r = sd_bus_message_append_array(reply, 'y', icon, sizeof(icon));
  if (r < 0) return r;
  r = sd_bus_message_close_container(reply);
  if (r < 0) return r;
  return sd_bus_message_close_container(reply);
}

static int method_click(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
  clicked = 1;
  return sd_bus_reply_method_return(m, NULL);
}

static int method_ignore(sd_bus_message *m, void *userdata, sd_bus_error *error)
{
  return sd_bus_reply_method_return(m, NULL);
}

static const sd_bus_vtable vtable[] = {
  SD_BUS_VTABLE_START(0),
  SD_BUS_PROPERTY("Category", "s", prop_string, 0, SD_BUS_VTABLE_PROPERTY_CONST),
  SD_BUS_PROPERTY("Id", "s", prop_string, 0, SD_BUS_VTABLE_PROPERTY_CONST),
  SD_BUS_PROPERTY("Title", "s", prop_string, 0, SD_BUS_VTABLE_PROPERTY_CONST),
  SD_BUS_PROPERTY("Status", "s", prop_string, 0, SD_BUS_VTABLE_PROPERTY_CONST),
  SD_BUS_PROPERTY("IconName", "s", prop_string, 0, SD_BUS_VTABLE_PROPERTY_CONST),
  SD_BUS_PROPERTY("IconPixmap", "a(iiay)", prop_pixmap, 0, SD_BUS_VTABLE_PROPERTY_CONST),
  SD_BUS_PROPERTY("ItemIsMenu", "b", prop_false, 0, SD_BUS_VTABLE_PROPERTY_CONST),
  SD_BUS_METHOD("Activate", "ii", "", method_click, SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD("SecondaryActivate", "ii", "", method_click, SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD("ContextMenu", "ii", "", method_click, SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_METHOD("Scroll", "is", "", method_ignore, SD_BUS_VTABLE_UNPRIVILEGED),
  SD_BUS_VTABLE_END
};

int tray_init(void)
{
  if (sd_bus_open_user(&bus) < 0)
    return 0;
  icon_build();
  snprintf(busname, sizeof(busname), "org.kde.StatusNotifierItem-%d-1", (int)getpid());
  if (sd_bus_add_object_vtable(bus, NULL, "/StatusNotifierItem",
                               "org.kde.StatusNotifierItem", vtable, NULL) < 0 ||
      sd_bus_request_name(bus, busname, 0) < 0 ||
      sd_bus_call_method(bus, "org.kde.StatusNotifierWatcher",
                         "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
                         "RegisterStatusNotifierItem", NULL, NULL, "s", busname) < 0) {
    // no tray watcher on this desktop, caller keeps quit-on-close behaviour
    sd_bus_unref(bus);
    bus = NULL;
    return 0;
  }
  return 1;
}

int tray_pump(void)
{
  if (!bus)
    return 0;
  while (sd_bus_process(bus, NULL) > 0);
  int c = clicked;
  clicked = 0;
  return c;
}

void tray_shutdown(void)
{
  if (!bus)
    return;
  sd_bus_release_name(bus, busname);
  sd_bus_flush(bus);
  sd_bus_unref(bus);
  bus = NULL;
}

#else // !HAVE_TRAY

int tray_init(void) { return 0; }
int tray_pump(void) { return 0; }
void tray_shutdown(void) {}

#endif
