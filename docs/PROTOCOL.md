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

The six sends are three **stereo** buses, not six mono outputs:

    0,1 = Main L,R      2,3 = Cue A L,R      4,5 = Cue B L,R

So an input's position in the stereo image is the ratio between its two Main
sends; there is no separate pan control. Writing one level to both sends —
which BiD did before 0.2.0 — sums every input to the centre and destroys the
image of anything arriving as a stereo pair.

## Output routing

Entity `0x33`, selector `0x06`, one byte of data; the wValue channel is the
output index. Outputs 0..5 are the analog side — 1/2 main, 3/4 line, then the
phones — and 8..11 the digital one. The byte picks what that output plays:

    main mix   0x25 left half of the pair, 0x26 right
    alt        0x1e / 0x1f    the same feed for the alternate speakers
    cue B      0x20 / 0x21
    cue A      0x22 / 0x23
    DAW n      the output's own index: a fixed full-level feed straight from
               the computer, outside the monitor section, so the volume knob
               does not apply to it

The cue order comes from listening, not from the decode: Monix's table reads
`0x20/0x21` as cue A, but with the matrix's cue A cells (2 and 3) feeding an
output routed there, it is the `0x22` pair that carries them — editing one
cue audibly changed outputs routed to the other. On the wire the block runs
alt, cue B, cue A.

`0x25/0x26` is not a raw tap of the main mix bus: it is the monitor
section's output, so the volume knob, dim and cut ride along on every output
that carries it — verified on hardware by routing the phones there and
watching dim and cut land in the headphones. The cue feeds bypass the
monitor section entirely, which is why BiD parks the phones on cue A: they
then answer only to the headphone volume on feature unit 0x0c.

These are the iD24's codes, decoded by Monix from the official app. The table
BiD carried before 0.2.1 predated this device and wrote different codes, which
selected wrong or undefined sources — the visible symptom is an output stuck
at full level that no fader controls. Routing survives power cycles and the
entity cannot be read, so BiD pushes its routing state on connect along with
everything else.

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
