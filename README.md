# BiD

Unofficial Linux control panel for the Audient iD series interfaces based on libusb, glfw and imgui.

BiD is a fork of [MixiD](https://github.com/TheOnlyJoey/MixiD) by [@TheOnlyJoey](https://mastodon.online/@TheOnlyJoey), which did the original reverse engineering and protocol work this builds on.

## Description

Since there is no official support by Audient for the iD interfaces on Linux, this exists as an alternative to enable the functionality not available in the default class compliant USB driver.

Unlike the original, BiD talks to the interface over its spare DFU interface rather than claiming the audio control interface, so the kernel keeps the sound card and playback carries on while the mixer is open. It also stays resident in the system tray, and the interface follows the window when you resize it.

## Notes and To Do

* Devices are detected by USB id; the iD24 is the one verified on hardware, the rest are inherited from the original project's support list
  * If a new device gets released, please open an [Issue](https://github.com/baakhoff/BiD/issues) with your USB id and input/output counts
* The protocol is mostly figured out, just needs verification/testing
  * Reading state back from the interfaces is still missing, so the controls start at zero rather than at what the hardware is doing
  * Things like VU meters and some switches/modes have yet to be implemented
* Linux is the target; the tray and desktop integration are Linux only

## Compilation

### Dependencies

* CMake
* libglew-dev
* GCC or Clang
* libsystemd-dev, or basu on distributions without systemd (optional, enables the system tray icon)

### Compile
* git clone the repository
* mkdir Release
* cd Release
* cmake -DCMAKE_BUILD_TYPE=Release ..
* make

## Usage

* Either run through sudo, or setup appropriate udev rules for your interface
* Run the BiD executable

Optionally, `make install` also places a desktop entry and icon, so BiD can be started from your application menu.

### System tray

When the desktop provides a system tray, closing the window hides BiD there instead of quitting, so the interface stays connected and reachable.
Clicking the icon shows and hides the window again, and right clicking it opens a menu with the monitor toggles (Dim, Alt, Talk, Phase, Mono) and Quit.

This needs a StatusNotifier tray, which KDE Plasma, Cinnamon, Budgie, XFCE and LXQt provide out of the box.
GNOME needs an appindicator extension for it, and compositors such as Sway or Hyprland need a panel that implements a tray, for example Waybar.
Without one, or when built without libsystemd/basu, BiD simply quits on close.

### udev rules

By default the audio interface is grabbed by the kernel module, so we need to setup udev rules to avoid needing root permissions when opening BiD.
All we have to do is add the Audient vendor id to the udev rules.
The specific group might be different for your distro, but "plugdev" and "audio" seem to be the most commonly used.

Either:
```
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="2708", MODE="0660", GROUP="audio"' >> /etc/udev/rules.d/84-audient.rules
```
or
```
echo 'SUBSYSTEM=="usb", ATTR{idVendor}=="2708", MODE="0660", GROUP="plugdev"' >> /etc/udev/rules.d/84-audient.rules
```
depending on your distro's permission group.

Then either reboot or use the following command to reload the udev rules for the running system.
```
udevadm control --reload-rules && udevadm trigger --attr-match=idVendor=2708
```
All done!

## Authors

* [@baakhoff](https://github.com/baakhoff) — BiD
* [@TheOnlyJoey](https://mastodon.online/@TheOnlyJoey) — MixiD, which BiD is forked from

## Version History

### BiD

* 0.2.0
   * Control goes through the spare DFU interface, so audio keeps playing while connected
   * System tray icon with close-to-tray and a menu carrying the monitor toggles
   * Desktop entry and icon, installed by `make install`
   * The interface now follows the window size instead of the size it started at

### MixiD, before the fork

* 0.1.6
   * All known iD usb-id's are now known and implemented
* 0.1.4
    * Now probes usb devices based on the supported id list and selects if possible
    * Auto disconnects and re-attach to kernel when quitting the application
    * Now should properly set all faders depending on the individual device inputs
    * Small UI Fixes
* 0.1
    * Initial Release based around the iD14 and iD14 MKII with most essential features implemented.

## License

This project is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details

## Acknowledgments

* [MixiD](https://github.com/TheOnlyJoey/MixiD), the project this is forked from
* [mymixer](https://github.com/r00tman/mymixer), prior attempt to reverse engineer the original iD14 with some minimal functionality.
* [imgui](https://github.com/ocornut/imgui), a modern lightweight gui
