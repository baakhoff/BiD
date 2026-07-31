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
  * Only the monitor section can be read back; the rest of the hardware is
    write-only, so BiD remembers its own state per device and pushes it on
    connect instead
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

Installing under a prefix inside your home directory, rather than into `/usr`,
can leave the icon missing from the menu and the task bar: an icon theme
directory is only searched when it has an `index.theme`, and a fresh
`~/.local/share/icons/hicolor` has none. If that happens:

```
cp /usr/share/icons/hicolor/index.theme ~/.local/share/icons/hicolor/
gtk-update-icon-cache -f -t ~/.local/share/icons/hicolor
```

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

## Troubleshooting

A problem with an iD interface on Linux lives in one of three layers: the
hardware and its cabling, the kernel/PipeWire audio stack, or BiD itself.
Chasing a symptom through the wrong layer eats evenings — a stereo
imbalance was once pursued here through every software layer before turning
out to be a badly seated speaker cable.

| Symptom | Layer | Fix |
| ------- | ----- | --- |
| One side quieter at any volume below 100% (iD14 MkII) | Firmware + PipeWire | The firmware ignores volume writes on the left half of `Speaker Playback Volume`, so hardware volume moves tilt the image right. Keep PipeWire off the hardware volume (rule below) and let BiD or the knob own the level. Documented in [Audientid14-linux-fix](https://github.com/grechmarlon/Audientid14-linux-fix) |
| Only surround profiles offered for a stereo interface | Kernel + PipeWire | The USB descriptors declare no channel positions, so a surround layout gets invented. Choose the `Pro Audio` profile, which exposes the raw channels without guessing |
| Mixer stops responding after suspend or a replug | BiD | Fixed in 0.2.1: BiD notices the stale device, shows an amber `reconnecting` dot and comes back on its own. On older builds, Disconnect and Connect again |
| ALSA mixer sliders look wrong or do nothing | Kernel | Most of the device is write-only and its ALSA controls are misleading by design; see [docs/PROTOCOL.md](docs/PROTOCOL.md). Use BiD for the mixer, ALSA/PipeWire for the streams |
| One side quiet everywhere, all software checked | Cables | Reseat or swap the speaker cables first. It happened here |

To keep PipeWire away from the hardware volume (the iD14 MkII fix; harmless
on the other models), drop a WirePlumber rule into
`~/.config/wireplumber/wireplumber.conf.d/50-audient-soft-mixer.conf`:

```
monitor.alsa.rules = [
  {
    matches = [ { node.name = "~alsa_output.usb-Audient.*" } ]
    actions = { update-props = { api.alsa.soft-mixer = true } }
  }
]
```

then restart it with `systemctl --user restart wireplumber`. Desktop volume
becomes a software gain, and the hardware level belongs to BiD and the
physical knob alone.

## Authors

* [@baakhoff](https://github.com/baakhoff) — BiD
* [@TheOnlyJoey](https://mastodon.online/@TheOnlyJoey) — MixiD, which BiD is forked from

## Version History

### BiD

* 0.2.1
   * Routing rewritten with the iD24's real source codes (decoded by
     [Monix](https://github.com/sKuhLight/monix) from the official app). The
     old table predated this device and could leave outputs stuck at full
     level on a source no fader controls
   * The routing window now works per output pair, starts from sane defaults,
     explains what the sources mean, and can reset to defaults
   * Routing is pushed to the device on connect like the rest of the state,
     which also recovers a device left mis-routed by older builds
   * The faders edit any of the hardware's three mixes — Main, Cue A, Cue B —
     switched with tabs above the strips. Each mix keeps its own levels and
     pans, all pushed on connect
   * Phones default to Cue A. The main-mix source turned out to be the monitor
     section's own feed, so Dim and Cut used to land in the headphones as well;
     on a cue the phones answer only to the Phones dial
   * The DAW return pair sits pinned on the left as L Out / R Out, and no
     longer scrolls away with the input strips
   * The two cue routing codes were crossed with the cue mixes: editing one
     cue changed outputs routed to the other. Caught by ear and swapped
   * L Out and R Out move as one while their Link button is lit — levels,
     mute and solo; pan stays per side to keep the stereo image. Unlink to
     trim a side; relinking snaps the right side back onto the left in
     every mix, not just the one on screen
   * The mixer remembers itself: all three mixes, pans, phase, routing, the
     link and the dials are saved per device under `~/.config/bid` on quit —
     including a logout or shutdown killing the tray — and restored on the
     next start, then pushed to the hardware on connect
   * Cue A's routing pair is `0x23/0x24`, not `0x22/0x23`: the code block
     has a gap, and an output sent to `0x22` plays a stuck full-level feed —
     which had the phones' left ear ignoring every fader on Cue A
   * Matrix rows without a fader (the iD24's DAW returns 3..6) are silenced
     on connect, so the hardware's boot-time cue contents cannot bleed in
   * The Cut button joined the monitor row and the tray menu, synced with
     the front panel like the rest
   * VU meters: every strip carries a segmented LED ladder with a peak-hold
     line, fed 30 times a second by the mixer's block metering — probed once
     on connect like the rest of the readback
   * A new look: charcoal console theme with one warm amber accent running
     through faders, knobs, tabs and lit buttons; card-style strips; the
     output pair boxed as one panel
   * The surface rebuilt the way console software lays it out. Strips read
     top to bottom: name, type-coloured chip, phase pill, a real fader — slot,
     cap and tick marks — with its meter, then a pan pot that double-clicks
     back to centre. The master panel leads with the device name and a status
     dot, then the monitor knob as the hero, phones under it, and the
     uppercase toggle grid at the bottom
   * Mute and Solo pills on every strip, and a master fader per mix at the
     right edge scaling everything that mix sends. Neither exists in the
     hardware, so both are baked into what reaches the matrix — and both are
     remembered in the state file
   * Faders tooltip their level while dragging and double-click to unity;
     the Driver Select and Routing windows fix themselves at content size
   * Every abbreviated button explains itself on a hover pause, and button
     labels are centred on the pixels they actually ink — the typeface rides
     low in its line box, which made small buttons read as off-centre
   * A device that stops answering — a suspend staling the USB handle, or a
     pulled cable — turns into a clean offline and a quiet retry every two
     seconds; when the interface returns, BiD reconnects and pushes the
     whole mixer back on its own. The status dot burns amber while it hunts
   * Input strips link in twos as well — MIC 1+2, and the digital inputs
     pair by pair — each with its own LINK bar remembered in the state
     file, following the same rules as the output pair
   * The sample rate the kernel negotiated shows under the connection
     status, read from ALSA's procfs rather than asked of the device
* 0.2.0
   * Control goes through the spare DFU interface, so audio keeps playing while connected
   * System tray icon with close-to-tray and a menu carrying the monitor toggles
   * Desktop entry and icon, installed by `make install`
   * The interface now follows the window size instead of the size it started at
   * Per channel pan, so a channel can be placed in the stereo image instead of
     being summed to the centre. Digital inputs start panned apart as pairs
   * Digital input faders now address their own matrix cells; they were writing
     over the mic channels
   * Headphone volume goes to the right entity
   * Protocol notes in [docs/PROTOCOL.md](docs/PROTOCOL.md)

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
