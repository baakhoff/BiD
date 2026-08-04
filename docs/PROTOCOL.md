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

    DAW n      0x00 + n, zero-based: playback channel n straight from the
               computer at full level, outside the monitor section — code
               0x00 carried DAW 1 to speakers and phones alike, by ear
    input n    0x10 + n: the physical input's direct tap — 0x11 put a live
               mic 2 on whatever output it was routed to, loud, by ear
    cue A      0x1e / 0x1f    a real stereo pair, ear-verified on the
               speaker outs and the phones; wore the label "alt" for years
    cue B      0x20 / 0x21    a real stereo pair, ear-verified
    (gap)      0x22           not a usable source, see below
    cue sums   0x23 = cue A summed to mono, 0x24 = cue B summed to mono —
               NOT a stereo pair, though BiD long wrote them as one
    main mix   0x25 left half of the pair, 0x26 right — the monitor
               section's feed, so volume, dim and cut ride along
    alt        0x1c / 0x1d    stereo main mix on the alt outputs - but a
               RAW tap: the volume knob, the alt trim and the ALT toggle
               all leave it untouched, and 0x29/0x2a is a second tap of
               the same nature. The monitor-processed feed (0x25/0x26)
               only exists on outputs 0/1, and nothing in 0x00..0x30
               behaves as a gated alt source. Feature unit 0x0c's
               channels 3/4 do not touch these jacks either - the output
               gains sit inside the monitor path, as the MKII showed,
               and a raw tap bypasses them all. So there is no volume
               handle over the alt outputs reachable from here at all.
               Proper ALT - silent until pressed, mains muting, the alt
               trim as its level, all of which the monitor section's
               stored alt trim (0x36 selector 0x17) clearly exists for -
               must be implemented by the official app through selectors
               still unknown: a Windows USB capture of the app assigning
               Alt Spkr, pressing ALT, moving the knob and the trim is
               the one measurement left. 0x1e/0x1f, the old "alt" label,
               is cue A

The cue codes come from listening, not from the decode, and the listening
took two rounds to understand. Monix's table reads `0x20/0x21` as cue A: on
hardware that pair carries the matrix's cells 4 and 5 — the page BiD calls
cue B — as a true stereo pair, pan and all. Their formula places the second
cue at `0x22/0x23`, but their own raw capture of the official app had
recorded `0x23/0x24`, and `0x22` is indeed not a usable source: an output
sent there plays a stuck full-level feed no fader controls.

What `0x23/0x24` actually are took the second round (issue: cue A "worked
wrong" on an iD24). They are not a stereo pair at all: `0x23` is cue A
**summed to mono**, `0x24` is cue B summed to mono. Writing them as a pair,
as BiD did, put cue A's sum in one ear and cue B's sum in the other —
every fader on the cue A page moved one ear only, the other ear held a mix
nothing on that page could touch, and a centred pan was 6 dB louder than
either extreme, the sum's fingerprint. The old observation that a phones
right ear "tracked cue A's left cells" was the same sum, half-read.

The true pair was found the plain way: a script that beeps once through
every candidate code while a person listens. `0x1e` carried cue A's left
cell and `0x1f` its right, cleanly separated, on the speaker outputs and
the phones alike — the codes the decode had always labelled "alt". Where
alt's real code lives is now the open question; nothing else in
`0x00..0x30` answered for it, though an alt feed would sit silent behind
the ALT toggle and a hunt with ALT engaged has not been run.

### What the measuring session actually taught (iD24)

Three instruments came out of it, each with its limits learned the hard
way.

**The monitor-feed recorder.** Capture channels 5 and 6 replay output 0
and 1 — but *only while those outputs carry the monitor feed*
(`0x25/0x26`). Any other code records silence there even when the
physical speakers are playing it, which made the recorder useless for
code hunting and, for one long evening, made every working code look
dead. It remains excellent for matrix work: Main L and R proved to be
exactly matrix columns 0 and 1, single-cell isolated, perfect stereo
separation.

**Readback.** Routing answers GET, one byte per output, the whole
16-output table at any time — and the mixer matrix answers GET per cell
too. BiD could sync both from the device instead of pushing blind. The
power-on routing state of the outputs BiD never writes reads back as the
output's index plus one (`0x07..0x10`), which briefly suggested one-based
DAW codes; the ear test settled it the other way — `0x00` is DAW 1, the
codes are zero-based, and the defaults just point one channel up for
reasons the firmware keeps to itself.

**The beep-per-code hunt.** A script that routes an output to each
candidate code in turn and plays one quiet beep riding a single matrix
cell, while a person says which side it came from. Slow, human, and the
only instrument here that measures the truth of a code on a *physical*
output. It found cue A's pair, the input taps and the DAW base in one
evening.

Three cautions for the next session. The mixer cell values share the
output volumes' scale: **dB in a u16, not a fraction** — half travel is
minus 64 dB, so a "quiet" test gain of 0.12 is silence, which
masqueraded as a router crash here and burned a power cycle on a healthy
device. Code `0x11` routes a live microphone at full level to whatever
output it lands on: mute the mics before a hunt. And the raw taps
bypass the monitor volume entirely, so a beep through them reaches the
amplifier at full path level — keep test tones at −32 dB or below, and
never assume the knob will protect the speakers.

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

What the monitor volume actually governs came back from the same MKII
(issue #26). The speaker half of the knob writes selector `0x12` on entity
`0x36` - the very node the front encoder moves, which is why the on-screen
knob and the hardware one behave identically. And on the MKII that node
sits on the **main mix path**, not on the connector: with both output pairs
fed from Main Mix, one turn took the speakers and the headphones down
together, while an output fed from a cue played at fixed level that no
volume control - on screen or on the box - could touch. That is the
hardware's own design, cue sends being fixed-level artist feeds, not a
failed write. The headphone gain on `0x0c` channels 3 and 4 made no
audible difference on a cue-fed phones output, which the same design
explains; whether it bites when the phones carry Main Mix is still
untested there.

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

The table is now confirmed on a real iD14 MKII (issue #26). The tester
steered the output pairs through Main, Cue A and Cue B in three
configurations and audio followed the matrix every time - a cue's master
fader gated exactly the outputs fed from that cue, and swapping the
assignments swapped the result. BiD therefore pushes routing on connect
for the MKII. The first-generation iD14 speaks the same table but keeps
the cautious flags until an owner of one reports.

## A rate change is no time to talk

Switching the sample rate closes the running stream and reopens it at the
new one, and in between the firmware re-clocks its DSP. Control traffic in
that window can wedge the whole control plane. On an iD14 MKII, switching
48 to 96 kHz with BiD open killed the audio every time - whoever did the
switching, BiD's menu or a pw-metadata script - while the same switch with
BiD closed was fine (issue #26). The wedge was then reproduced on an iD24
when a profile change churned every stream while an older BiD kept
polling: the kernel's own clock-validity reads began failing alongside
BiD's commands, and only a replug - or a USB reset, the same thing without
the cable - brought the control plane back.

No single request is the poison; the pressure is: meters at 30 Hz, the
monitor volume at 20 Hz, the clock selector once a second through amixer
(kernel-mediated, so it reaches the clock entity like any other), and a
device-rate read that used to fire precisely in the gap between the two
streams. BiD's answer is to watch the momentary rate in /proc/asound,
which costs no USB at all and is read before anything else each tick: any
change - the stream closing on its way to a new rate included - silences
every poll for a few seconds, and the device-rate fallback waits out two
quiet ticks besides, so it can never land inside a reopen.

## Watching the monitor section move

`BiD --watch-monitor` is the shipped form of the technique the F-button
hunt used. With the app closed - the interface takes one claimant - it
reads entity 0x36's selectors 0x00..0x30 as bytes and as levels, plus the
four output volumes on feature unit 0x0c, a few times a second, and prints
whatever moved with a seconds-since-start stamp. Turn a knob or press a
button while it runs and the selector names itself. It only ever reads,
so it is safe to hand to a tester on any model; it is how the iD14 MKII's
phones encoder is being located.

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
