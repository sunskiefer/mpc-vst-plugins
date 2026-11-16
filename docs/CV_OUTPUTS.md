# Driving the MPC CV outputs from a plugin

Status (2026-11-16): **lead from the community, partly corroborated offline, not yet measured on a device by us.**
Every claim below is tagged `[C]` community report, `[F]` seen in stock firmware strings (offline), `[D]` verified on a device
(none yet). Fill in the `[D]` rows and date them when someone measures.

## What is known today

Baseline (works, shipped): a plugin emits MIDI and the user routes a **CV track** to it. Glass Spheres and Meta4MPC XXL do this.
Pitch/gate/mod then follow MPC's own CV track settings (range, tuning, slew).

The lead adds a **direct path**: the CV jacks are fed by a rawmidi device inside MPC OS, so any process on the device can
write to it, no CV track needed.

## The mechanism [C, 2026-11-16: reported on a social-media thread by an engineer who worked on MPC firmware up to 3.9.x, and confirmed working by a second person]

- ALSA rawmidi port, shown as **`Private`**, usually **`hw:1,0,1`** (the card number can differ; always look it up).
- **16 addressable CV slots**, zero-based. Per the first report, 8 of them are not reachable on the hardware that was tried. Which
  jack maps to which slot is unconfirmed.
- Each slot takes **two Control Change messages on MIDI channel 16** (status `0xBF`) that together form a **14-bit value**:
  `BF 00 <hi7>` and `BF 01 <lo7>` for slot 0 in the examples. The slot number is presumably the CC number pair
  (`00/01` = slot 0, next pair = slot 1, ...) but only slot 0 was shown. **Unconfirmed.**
- Which CC is the MSB and which the LSB is **unconfirmed**: the examples only use equal bytes (`7F 7F`, `00 00`) or `66`/`32`.
- Full scale is 5 V (per the report). 0x00 0x00 = 0 V, 0x7F 0x7F = 100 % = 5 V. The `BF 00 66 BF 01 32` example (hi=0x66, lo=0x32) was said
  to give about 80 %; 0x66 is 80 % of 127, so the first byte behaves as the coarse value and the second as fine (consistent with
  MSB first; one example, not proof).
- Writing 0..127 on the coarse byte alone gave on/off and gate-like triggers and values too low to affect CV inputs: use the
  full 14-bit range for real voltages.

Commands to try (on a device that has CV jacks, MPC stopped from using them, a meter or a scope on the jack):
```
cat /proc/asound/cards
amidi -l                                   # find the 'Private' port
amidi -p hw:1,0,1 -S 'BF 00 7F BF 01 7F'   # slot 0 full scale
amidi -p hw:1,0,1 -S 'BF 00 00 BF 01 00'   # slot 0 zero
```

## Corroboration in stock firmware [F, 2026-11-16, offline]
Strings in `/usr/bin/MPC` of the stock images in `~/fwcmp` (Gen1 and Force builds both checked): `CvPort`, `CvPortAndData`,
`SerialisableCvPortAndData`, the `ACV1` Q-Link translator, `snd_rawmidi_open`, `snd_rawmidi_write`, a port named `Private`, and the format strings
`hw:%d,%d,%u`, `hw:%d,%d`. This fits the claim (MPC finds the rawmidi card by name and writes to it) but does not decode the message
layout. No disassembly done yet.

## Live III check [D, 2026-11-16, Live III, MPC OS 3.10.1, read-only]
CORRECTION (user, same day): the Live III **does** have CV: 4 stereo 1/8" TRS CV/Gate outputs on the rear panel (= 8 channels, which fits the report's 16 slots with 8 unreachable on the hardware tried). My first reading ("no jacks") was wrong.
- `amidi -l`: card 0 is MPC's own USB-audio card ("MPC LIVE III") and carries four rawmidi ports: `hw:0,0,0 Public`, **`hw:0,0,1 Private`**,
  `hw:0,0,2 DIN-A`, `hw:0,0,3 DIN-B`. So `Private` is **not** `hw:1,0,1` here: the card number depends on enumeration order (a USB
  keyboard took card 1). Look the port up by name, never hard-code `hw:1,0,1`.
- `/proc/asound/card0/midi0`: MPC (the `MPC` process) holds all four outputs and inputs open. `Private` had **642,678 bytes sent and 174,912 received**
  after a day of uptime, while DIN-A/B were at 0. On this model it is a busy two-way link; that traffic may be the panel/controller
  protocol plus the CV data (unknown which). We did **not** write to it (could disturb LEDs/controls) or read it (would steal MPC's input).
- The CV slots are probably carried on this same `Private` port next to that traffic (unconfirmed). That makes the **MPC-already-writes-here conflict (question 4) certain**, not a risk: a plugin's CCs would be interleaved with MPC's own bytes
  on one stream, and the messages in the report on channel 16 (`BF`) must not collide with what MPC sends.
- The Live III is itself a valid test device. The device has no strace/gdb/tcpdump (BusyBox, kernel 6.18 rt), so logging MPC's own bytes needs a static aarch64 strace copied over, or an LD_PRELOAD shim on `snd_rawmidi_write`. Measuring a voltage needs a meter or scope on a rear jack.
- Old plan, kept for other models: A safe way there: `strace -f -e trace=write -p <MPC pid>` filtered on the fd of
  `/dev/snd/midiC*D0`, with a CV track playing, to log the exact bytes MPC itself sends (no writes from us).

## Static analysis of the stock aarch64 `MPC` [F, 2026-11-16, offline, OS 3.10.1 Live III build, md5 `b24b7955...`]
Binary pulled from the device to a scratch dir (Akai's file, never committed). Method that stays under ~1 GB of RAM (a naive capstone pass over the
69 MB `.text` got OOM-killed): find the PLT stub for an import from `.rela.plt` + `.plt`, then match `bl` by bit pattern over the `.text` words, and
disassemble only small windows (capstone needs `skipdata` or it stops at the first data word; do not name a script `dis.py`, it shadows the stdlib).
- `snd_rawmidi_write` has 12 call sites, `snd_rawmidi_open` 3, `snd_rawmidi_drain` 1, in two clusters (`0x26df5ec..0x26e2180`, `0x2700050..0x2700f08`).
  The `0x2700...` cluster is a **generic buffered rawmidi writer**: a 0x200-byte buffer at +0x38, a pending count at +0x238, a mutex,
  and a retry loop (0x33 tries) on EAGAIN (-11) and ENODEV (-19); error text goes to a logger. It knows nothing about CV.
- So the CV encoding (`BF 00 hi BF 01 lo`, slot numbering) is in the **callers** of that writer (the `CvPort` / `CVPortsConnector` code, RTTI strings are
  present but static symbols are stripped, so no names). Not found yet. A scan for `mov w,#0xbf` gave 23 hits, the ones inspected are parameter ids, not status bytes
  (the status is probably built as `0xB0 | channel`).
- Also seen in strings: CV program types and modulation (`CvLfo1..4CurveProvider`, `CvRamp1..2CurveProvider`, `CvProgramModulation::*`, `DrumCvProgramMapping`), and
  UI JSON `/usr/share/Akai/SME0/Resources/Pages/Program Edit/CV/CV GUI Panels.json`. MPC computes LFO/ramp curves itself and keeps the jacks fed
  continuously, which makes the overwrite question (open question 4) the one that decides if a plugin can use the port next to CV tracks.
- Easiest way to get the exact bytes: log the writes at runtime (see below), not more static work.

## Open questions (what a plugin author needs before relying on it)
1. Which models? Jacks exist on the MPC X (and the Key? Live? check). Does `Private` appear on devices without CV, and what does it do there?
2. Slot to jack map, and the CC numbering for slots 1..15. Test by writing each pair and reading the jack.
3. MSB/LSB order, and the voltage curve (linear 0..5 V? offset or bipolar ranges as set in MPC's CV config?).
4. **Conflict with MPC:** MPC itself writes to this port for CV tracks and Q-Link CV. Two writers: does the last one win, does MPC refresh
   continuously (overwriting a plugin), and is the port opened exclusively? `snd_rawmidi_open` by a second process may succeed (ALSA
   hw rawmidi allows several writers) but this must be tested while an MPC CV track is playing.
5. From inside a plugin: is writing to `hw:` from the audio thread safe? Use a worker thread plus a lock-free queue, open once
   in the plugin's init, never in `process`. Latency and jitter are unmeasured.
6. Does it survive an MPC OS update? It is an internal interface; treat it as unsupported and detect at runtime (see below).
7. Gen2 (aarch64) and OS 2.x/3.10.x: the report is from 3.9.x or older.

## Suggested plugin recipe (untested)
1. At init, scan `/proc/asound/cards` and `snd_rawmidi` hw ports for a port named `Private`; if absent, do nothing and leave CV to MIDI+CV track.
2. `snd_rawmidi_open(NULL, &out, name, SND_RAWMIDI_NONBLOCK)`; keep it open.
3. For slot `n`, value `v` in 0..1: `x = round(v*16383)`; send the two CCs on channel 16 with `x>>7` and `x&0x7F`, in the order the
   measurements settle (open question 3). Send only on change, then `snd_rawmidi_drain` on a worker thread.
4. Expose it as an opt-in param ("CV direct out"), off by default, and document the conflict with CV tracks.

## Next steps
- Measure on a device with CV jacks (ask the user which one; ask before restarting MPC).
- Disassemble the `CvPort` writer in MPC to get the exact bytes and slot numbering.
- Update this file with `[D]` results, then add a NOTES.md dated entry and mention it in `docs/PORTING.md` and the skill.
