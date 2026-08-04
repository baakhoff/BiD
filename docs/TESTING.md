# Testing an Audient interface with BiD

*Русская версия: [TESTING.ru.md](TESTING.ru.md)*

BiD supports each model by measurement, not by trusting decodes — the
decodes have been wrong about nearly everything at least once. A tester
with working ears, the interface on the desk and twenty minutes can map
things no document knows. This page is the whole process: what to run,
what to listen for, and how to report it so a finding becomes a fix.

Everything here was refined on real hardware, mistakes included. Follow
the safety rules first — each one was learned the hard way.

## Safety rules

- **Never raise the test tones.** The scripts play beeps at −32 dB and
  that is deliberate: some routing codes are raw taps that bypass the
  volume knob entirely and reach the amplifier at full path level. A
  "too quiet" beep is a report, not a problem to fix with gain.
- **Unplug or mute microphones before a code hunt.** Some codes route a
  live microphone straight to the speakers, loud.
- **Keep the monitor volume clearly up during hunts.** A quiet beep
  stacked on a turned-down knob reads as silence and fakes a negative —
  this exact trap hid a working feature for half a night.
- **If audio dies completely mid-test:** power-cycle the interface with
  its own power switch. On DC-powered models a USB replug does not
  reboot the DSP. Then reopen BiD — it pushes your whole desk back.
- **Unmapped outputs are live until proven otherwise.** On the iD24,
  the routing unit's "spare" tail turned out to be direct feeds to the
  physical jacks, discovered by a scream. A hunt must read the current
  routing back before choosing its probe bus, touch only the outputs
  under test, and restore each one immediately after its measurement.
- **Take the headphones off your head during hunts.** Listen from a
  distance; note what sounds and when, with your ears nowhere near a
  driver.
- **Your desk is safe.** Levels, pans, names, routing all live in BiD's
  state file. Whatever a test writes to the device, reopening BiD
  restores your setup.

## What a testing session looks like

### 1. Install or update

```
curl -fsSL https://raw.githubusercontent.com/baakhoff/BiD/master/install.sh | BID_REF=master bash
```

### 2. First contact — report the basics

Open an issue with your model name and what happens out of the box:

- Does BiD connect? Paste the terminal output of running `BiD` from a
  console — the probe lines identify your device.
- Do the faders audibly change the mix? Do the meters move?
- Does the routing panel actually move sound between outputs?
- Does the sample rate readout match reality?

### 3. The per-ear check — the one everyone skips

Most breakage hides in stereo. For **each** mix page (MAIN MIX, CUE A,
CUE B), with headphones or speakers routed to that mix:

- Raise one channel's fader, pan it hard left, then hard right. Does it
  actually move between your ears, cleanly?
- Is the centre position louder than either extreme? (It should not be —
  a louder centre is the fingerprint of a mono sum pretending to be a
  pair, and exactly how broken cues were caught.)
- Does every fader on the page affect **both** ears?

One sentence per mix page ("cue A pans correctly" / "cue B only ever
plays in my left ear") is enough to act on.

### 4. The watcher — mapping knobs and buttons

Close BiD completely (tray too — one claimant per interface), then:

```
BiD --watch-monitor
```

It only reads, so it is safe on any model. Turn each knob and press each
button on the front panel, one at a time, a few seconds apart. Whatever
moves inside the device prints itself with a timestamp. Paste the whole
output into the issue with a note of what you did in what order. A knob
that prints nothing is also an answer — say so.

### 5. The beep hunt — when something needs finding

When a control or a source has no known address, we send a small script
tailored to your model. It routes one candidate code at a time to a
chosen output and plays one quiet beep per code, printing the code
before each beep. Your job is only to listen and note which codes were
audible, and in which ear. Reports like this are perfect:

```
0x14 left ear
0x15 right ear
0x22 both, strange
everything else silent
```

Found pairs then get a four-beep verification: left cell to the left
ear, right to the right, silence when the relevant toggle is off,
quieter when the volume is lowered. Four short answers close a case.

### 6. Windows capture — the last resort

For behaviour only the official console knows (trim writes, button
mechanics), we provide a PowerShell script that records USB traffic
while you press the one control in question. It needs USBPcap
(bundled with the Wireshark installer), a reboot after installing, and
an administrator PowerShell. The script is provided in the issue thread
when the need arises, with exact steps.

## How to report

- Terminal output: paste verbatim, no trimming.
- Listening tests: one line per test — "1 left, 2 right, 3 nothing".
- If output jacks are ambiguous on your model, a photo of the back
  panel with what is plugged where beats any description.
- Oddities are data: a beep that sounds like a square wave, a control
  that gets louder when it should get quieter, a mic that appears from
  nowhere — write them down even if they seem like mistakes.

## Model notes

- **iD24** — fully mapped by ear (see [PROTOCOL.md](PROTOCOL.md)):
  stereo cues, alt switching with trim, raw taps, the works. The
  reference for what a finished map looks like.
- **iD14 MKII** — routing and mixer verified by a tester; the cue codes
  in its table are single codes per cue, which on the iD24 turned out
  to mean **mono sums**. Its cues are therefore suspected mono. The
  layout pattern predicts true stereo pairs at `0x14/0x15` (cue A) and
  `0x16/0x17` (cue B) — an offset copy of where the iD24's pairs sit
  relative to Main. A per-ear check (step 3) plus one beep hunt would
  settle it. Bonus for this model: cue-fed outputs bypass the volume
  knob by design, so the turned-down-knob trap cannot bite here.
- **iD4 / iD4 MKII** — fixed 2-in/2-out, no mixer or routing to map.
- **iD22 / iD44 / iD48** — unverified; owners wanted. The iD48 shares
  the iD24's code formula, so its predictions are already written —
  they just need ears.
