# Audient iD control protocol

Working notes on the vendor protocol, as far as BiD relies on it. Corrections
welcome; a fair amount of this is inference from other people's reverse
engineering rather than documentation.

Sources: [MixiD](https://github.com/TheOnlyJoey/MixiD) (which BiD is forked
from), its [issue #15](https://github.com/TheOnlyJoey/MixiD/issues/15) for the
interface trick below, [Monix](https://github.com/sKuhLight/monix) whose
`docs/PROTOCOL.md` documents the entity map and the mixer cell layout, and
[mymixer](https://github.com/r00tman/mymixer).

## Control transfers

    SET   bmRequestType 0x21   bRequest 0x01
    GET   bmRequestType 0xa1   bRequest 0x01
    wValue  = (selector << 8) | channel-or-cell
    wIndex  = (entity << 8) | interface
    data    little endian

`interface` can be **any** of the device's interfaces, not just the
AudioControl one. BiD sends everything to the spare DFU interface, which no
kernel driver claims, so `snd-usb-audio` keeps the card and playback continues
while BiD is connected. See `find_control_interface()` in `driver.h`.

## Entities

| Entity | What | Selector |
|--------|------|----------|
| `0x0b` | input phase invert | `0x0d`, channel 1..n |
| `0x0b` | input gain / pad | `0x0b` |
| `0x0a` | declares no controls at all, writes to it do nothing | — |
| `0x0c` | output volumes | `0x02`, channels 1 and 2 are the monitor pair, 3 and 4 the headphones |
| `0x33` | output routing | `0x06`, channel = output index, 1 byte source code — see below |
| `0x36` | monitor volume | `0x12` (single value, there is no per-side control) |
| `0x36` | monitor toggles | mono `0x00`, phase `0x03`, mute `0x04`, dim `0x05`, talkback `0x07`, alt `0x0c` |
| `0x3c` | mixer matrix | `0x01`, channel = cell number |

## The mixer matrix

96 cells: 16 inputs by 6 sends, addressed flat.

    cell = input * 6 + send

The sixteen input rows on the iD24 are the two mics, the eight digital
inputs, then DAW returns 1..6. The hardware boots with some cells open —
a fresh device plays the computer into cue A before anything ever writes
the matrix — so rows that have no fader in the app are written to silence
on connect, or their boot-time contents keep playing into the buses.

The six sends are three **stereo** buses, not six mono outputs:

    0,1 = Main L,R      2,3 = Cue A L,R      4,5 = Cue B L,R

So an input's position in the stereo image is the ratio between its two Main
sends; there is no separate pan control. Writing one level to both sends —
which BiD did before 0.2.0 — sums every input to the centre and destroys the
image of anything arriving as a stereo pair.

## Output routing

Entity `0x33`, selector `0x06`, one byte of data; the wValue channel is the
output index. Outputs 0..5 are the analog side — 1/2 main, 3/4 line, then the
phones — 8..9 the optical pair, and 10..11 the loopback pair. The byte picks
what that output plays:

    main mix   0x25 left half of the pair, 0x26 right
    alt        0x1e / 0x1f    the same feed for the alternate speakers
    cue B      0x20 / 0x21
    (gap)      0x22           not a usable source, see below
    cue A      0x23 / 0x24
    DAW n      the output's own index: a fixed full-level feed straight from
               the computer, outside the monitor section, so the volume knob
               does not apply to it

The cue codes come from listening, not from the decode, and disagree with it
twice. Monix's table reads `0x20/0x21` as cue A: on hardware that pair
carries the matrix's cue *B* cells (4 and 5) — editing one cue audibly
changed outputs routed to the other. And their formula places the second
cue at `0x22/0x23`, but their own raw capture of the official app had
recorded `0x23/0x24`, and the capture is right: `0x22` is not a usable
source at all. An output sent to `0x22` plays a stuck full-level feed that
no fader and no dial controls — with the phones on `0x22/0x23` the left ear
ignored everything while the right ear tracked cue A's *left* cells. So the
block runs alt, cue B, a one-code gap, cue A.

`0x25/0x26` is not a raw tap of the main mix bus: it is the monitor
section's output, so the volume knob, dim and cut ride along on every output
that carries it — verified on hardware by routing the phones there and
watching dim and cut land in the headphones. The cue feeds bypass the
monitor section entirely, which is why BiD parks the phones on cue A: they
then answer only to the headphone volume on feature unit 0x0c.

Outputs 10 and 11 are not jacks at all: they are the loopback pair. The
official app's `Id24ProductDefinition` declares loopback-to-host on output
`0x0a`, and whatever these two outputs play is handed back to the computer
as capture channels 11+12 — exactly the two channels the USB descriptor
exposes beyond the ten physical inputs. Any source code works there, so the
loopback can record a cue, the main mix (knob, dim and cut included, since
that feed is the monitor section's), or — as DAW `0x0a`/`0x0b` — playback
channels 11+12 looped straight back for apps that target them directly.

These are the iD24's codes, decoded by Monix from the official app. The table
BiD carried before 0.2.1 predated this device and wrote different codes, which
selected wrong or undefined sources — the visible symptom is an output stuck
at full level that no fader controls. Routing survives power cycles and the
entity cannot be read, so BiD pushes its routing state on connect along with
everything else.

## What each model has

The iD24 is the machine verified here; the rest of the family differs in
ways that matter before a single byte is written. Decoded from the official
app by [Monix](https://github.com/sKuhLight/monix) (their `docs/DEVICES.md`),
and carried in `device_properties.h`:

| Model | Mixer nodes | DAW returns | Cues | Alt spkr | Optical out | Loopback |
|-------|-------------|-------------|------|----------|-------------|----------|
| iD4   | none        | -           | -    | no       | no          | no  |
| iD14  | 14          | 4           | 2 *  | no       | no          | no  |
| iD22  | 16          | 6           | 2    | yes      | yes         | no  |
| iD24  | 16          | 6           | 2    | yes      | yes         | yes (outs 10/11) |
| iD44  | 30          | 10          | 4    | yes      | yes         | no  |
| iD48  | 32          | 8           | ?    | yes      | yes         | yes (out 0x16) |

\* Monix's decode gives the iD14 a single cue. Three things say otherwise and
BiD offers two: its mixer unit carries six output channels, which is three
stereo buses; MixiD's routing table, written on an iD14, has a source code
for cue A *and* cue B; and the official app on an iD14 MKII prints both.

One consequence BiD has to respect. **The routing scheme differs**: the iD24 and
iD48 compute a source code by formula, while the iD14, iD22 and iD44 look it
up in a table with different base offsets, which Monix decoded but nobody has
confirmed on hardware - so those codes are not written by BiD yet.

Entity IDs (mixer `0x3c`, monitor `0x36`, routing `0x33` on the iD24) are
assigned per USB descriptor and are not in the app binary, so they have to be
read from each device.

### What an iD14 MKII's descriptors say

Read off real hardware (issue #26), against an iD24's for comparison:

| | iD24 | iD14 MKII |
|---|---|---|
| Mixer unit | 60 = `0x3c`, **6 output channels** | 60 = `0x3c`, **6 output channels** |
| Routing unit | 51 = `0x33`, **16 outputs** | 51 = `0x33`, **6 outputs** |
| Monitor unit | 54 = `0x36` | 54 = `0x36` |
| Output volumes | 12 = `0x0c` | 12 = `0x0c`, four channels |
| Input gain + phase | 11 = `0x0b`, read/write | 11 = `0x0b`, read/write, ten channels |
| Clock/rate | 62 = `0x3e` | 62 = `0x3e` |
| HID interface | 3 | 3, named **Scrollcontrol** |

Two lessons. The entity map is **the same family-wide**, so the mixer,
monitor and phase work on the iD14 with the iD24's addresses. But the
**matrix spacing is six on both** — it comes from the mixer unit's output
channel count, and an earlier guess of four, reasoned from a cue count the
iD14 turned out not to have, would have written every row onto its
neighbour. Six output channels is three stereo buses, which is where the
iD14's second cue came back from.

The routing unit is where they part: sixteen outputs on the iD24 against
**six** on the iD14, which has no optical output and no loopback. Writing the
iD24's loopback pair (outputs 10 and 11) to an iD14 is out of range, and the
firmware answers `LIBUSB_ERROR_IO` — two of them, exactly what the tester's
log showed. The six writes that did land carried iD24 source codes into a
table-scheme device, which pointed the outputs at nothing: audio died until
the interface was replugged, since routing survives a disconnect.

The output volume unit is worth a line of its own. On the MKII, feature unit
`0x0c` declares volume on channels 1 to 4 and nothing on 5 and 6: 1 and 2 are
the monitors, 3 and 4 the headphones. That gain sits *ahead* of the analogue
one, and the MKII has a single encoder that serves the monitors or the
headphones depending on a button - which is why its official app prints a
speaker and a headphone button under the knob, and why BiD prints the same
pair there and moves the matching level. Boxes with a headphone knob of their
own get no such control: BiD opens `0x0c` channels 3 and 4 wide on connect
and leaves the level to the hardware. Note the scale is dB, not a fraction -
half travel is about -64 dB, which is silence with the knob apparently
half up.

### The iD14's own codes, which BiD had all along

MixiD's original routing table was never wrong - it was written for a
different machine. It is a per-output table for a **six-output** device,
which is the iD14's shape, and MixiD was developed on an iD14:

| Output | Main | Alt | Cue A | Cue B | DAW |
|--------|------|-----|-------|-------|-----|
| 0 (Main L) | `0x1b` | `0x1d` | `0x19` | `0x1a` | `0x00` |
| 1 (Main R) | `0x1c` | `0x1e` | `0x19` | `0x1a` | `0x01` |
| 2, 4 (L)   | `0x1b` | `0x1d` | `0x19` | `0x1a` | own index |
| 3, 5 (R)   | `0x1c` | `0x1e` | `0x19` | `0x1a` | own index |

So Main is `0x1b/0x1c`, Alt `0x1d/0x1e`, the cues carry one code for both
halves, and DAW Thru is the output's own index - the same convention the
iD24 uses. These codes sit just below the iD24's block (`0x1e..0x26`), which
is exactly why writing them to an iD24 selected undefined sources: the
symptom that made BiD replace the table in the first place. Both tables are
right, each on its own family, and BiD now carries both.

BiD offers the iD14 table when the user clicks a routing button, but does
not push it on connect: it is inherited, not confirmed here.

## The F buttons

F1..F3 emit nothing over USB on their own. Without the official app they do
nothing at all — no monitor function fires — and with every readable corner
watched during presses, nothing moves: entity 0x36 selectors 0x00..0x30
(selector 0x10 idles at 0xff and never budges), the system entities 0x01
and 0x14 (which hold the optical port modes), extension units 0x32 (one
selector, zero) and 0x34 (answers every selector 0x00..0x10 with the same
byte 0x11 — a stub or version echo, not state), the GET_MEM blocks past
offset 0 (offsets 1..3 do not answer on this firmware), the HID interface
(silent during presses), and the AudioControl interrupt endpoint, which the
firmware never uses — even the volume knob announces nothing there.

So the official app must arm or poll something still unknown, and it — not
the device — performs the assigned action. Until a capture of that app
exists, the buttons are out of reach from Linux. The dispatch side is the
easy half: everything an F button can trigger is already writable here.

## Volume encoding

int16, 1/256 dB. `0x0000` is 0 dB and `0x8000` is mute. BiD maps a 0..1 fader
linearly across that range in `float_to_u16()`, so the taper is linear in dB.

## Reads do not work

`GET_CUR` **stalls or returns junk** on the mixer, routing, channel phase and
headphone volume. Monix documents these as write-only, and it matches what we
see: reading the matrix back returns values that depend only on the low byte of
`wValue`, so all 16 input rows alias onto each other. Do not trust a read of
those entities — it looks plausible and is wrong.

Consequence: BiD has to track its own state, and cannot show what the hardware
is actually doing after a cold start. This is why the controls come up at zero
rather than reflecting the device.

Metering is a different request: `bRequest 0x03` (GET_MEM) on `0x3c`, read as
one block for all channels rather than per channel.

## What the kernel exposes, and why it is misleading

`snd-usb-audio` builds ALSA controls from the descriptors, and this device's
descriptors are poor:

- Feature Unit 10, which carries the output levels, declares **every**
  `bmaControls` entry as zero, so ALSA creates nothing for it — even though the
  unit works when written directly.
- Routing and the monitor section live in EXTENSION_UNITs (51 and 54), which
  carry no standard semantics, so they cannot be represented at all.
- MIXER_UNIT 60's descriptor is malformed — `lsusb` reports `iMixer 255
  (error)` and 11 junk bytes. The kernel still exposes it, as a 16 x 6 control
  named `ADAT-8 Volume`, but the name is wrong and the indices do not
  correspond to real crosspoints.

In short, `alsamixer` shows four controls for this card and two of them do not
do what they are called. That is the gap BiD fills.

## Unit ids

BiD's entity numbers are hex; USB descriptors report them in decimal.

    0x0a = 10   0x0b = 11   0x0c = 12   0x33 = 51   0x36 = 54   0x3c = 60
