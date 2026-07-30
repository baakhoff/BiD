#ifndef _H_TRAY_H_
#define _H_TRAY_H_

// StatusNotifierItem tray icon (see tray.c). All calls are safe no-ops when
// built without HAVE_TRAY or when the desktop has no tray watcher running.

#ifdef __cplusplus
extern "C" {
#endif

int tray_init(void);      // 1 if a tray icon is up, 0 if unavailable
int tray_pump(void);      // call once per frame; 1 if the icon was clicked
void tray_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
