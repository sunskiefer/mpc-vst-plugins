# MPC Sample (AC50): a brief for plugin projects

One page for anyone porting or releasing a plugin that should run on the **MPC Sample**. It gathers what the repo already
verified on a device (`docs/NOTES.md`, 2026-11-03 to 2026-11-06) and what inMusic's published source adds
(`docs/AZ0X_SOURCE.md`, read 2026-11-08, offline). Each line says which it is. It does not repeat the long device logs:
follow the NOTES dates for those.

## The platform in one table

| Item | Value | Source |
|---|---|---|
| SoC | Rockchip **RK3566**, 4x Cortex-A55 at 1.8 GHz, governor `performance` | device (NOTES 2026-11-04) and device tree |
| Memory and storage | 2 GB RAM, 7.6 GB eMMC (`/content` 3.9 GB, `/data` 1.7 GB), SD card at `/media/MPCSD` | device (NOTES 2026-11-04) |
| OS | az07 image, **PREEMPT_RT** kernel 6.12.60, glibc 2.39, aarch64 | device and source |
| Screen | **320x240** ST7796S SPI panel, one plane, XR24. **No GPU driver bound** (`&gpu` disabled in the AC50 device tree) | device and source |
| Controls | Buttons, pads, pots, dial are a separate controller on a UART, read by MPC as `/dev/snd/midiC0D0` (rawmidi, ALSA timestamped frames) | device (NOTES 2026-11-04) |
| Audio | AK4558 stereo codec on I2S, 44.1 kHz, period 128. USB-C is a gadget: UAC2 audio + MIDI + NCM (the SSH link) | device; gadget functions also in the kernel config |
| Power, radios | Mains only. **No Wi-Fi or Bluetooth**: the nodes are in the shared board file but only another product enables them | device and source |
| Service, binary, settings | `ac50.service`, process `MPC Sample` (with a space), `/data/Settings/Akai/MPC Sample/MPC Sample.settings` | device (NOTES 2026-11-03/04) |
| Plugin key | `pluginList-arm-64bit`, the same aarch64 `.so` as Gen2 | device |

The version string the app shows is "MPC Sample 1.3.0" (its own numbering, not MPC OS 3.x).

## What this means for a plugin

### 1. Budget for four A55s, nothing faster

The Sample has the Live III's *little* cores only. Our benches show an A76 does about 3x the work per thread of an A55
(`docs/EMULATOR_PORTS.md`), so a plugin tuned on the Live III's big cores can be 3x over budget here. Measure on the
A55 cores (pin to cores 0-3 on a Live III to stand in). For scale, Stompbox's default chain and NAM Lite each took about
25% of one core on the Sample's audio thread with the transport stopped (NOTES 2026-11-04); that is an observation, not a
limit, and no multi-plugin figure exists yet. Build with `-mcpu=cortex-a55`. A port that is too heavy for the Sample must
say so in the catalog (see below).

### 2. Don't count on a GPU or a big screen

The skin runs on a 320x240 panel and MPC draws on the CPU; the GPU node is disabled in the device tree. A page that
repaints often costs the host: a full-screen refresh 16 times a second made pad hits up to 80 ms late; sending only the
changed rectangles removed it (NOTES 2026-11-06). Keep refreshes small, rectangle-based and event-driven.

### 3. There is no plugin browser on the Sample

The UI cannot pick a third-party plugin: SHIFT + KNOB FX opens a fixed list of 25 built-in effects. A plugin loads when it
is in the project's mixer (`inserts` of the working copy, NOTES 2026-11-04) and MPC is restarted. The RACK addin does that
from the device (add, remove, on/off, APPLY); the Plugin Manager's catalog marks Sample-capable entries
(`docs/CATALOG.md`, NOTES 2026-11-05). Test with an insert in a project, not with a browser tap.

### 4. Where a plugin can live, and the exec trap

- `/` is read-only and `/synths` is empty. The only drive is the SD card, mounted **`noexec`**.
- From the source: removable drives are mounted by `edisksd` with `nodev,noexec,nosuid,nosymfollow` by default (a caller or
  `/etc/edisksd/main.conf` can change that; see `docs/AZ0X_SOURCE.md`). This is the origin of the `noexec`.
- A bind mount of `<card>/Synths` remounted `exec` makes plugins loadable, **but it is lost at reboot**, and then every
  plugin fails to load without any message (NOTES 2026-11-05). Check the mount, not just the file, when a plugin "does
  nothing" after a restart.
- The drive's folder name keeps spaces (`edisksd` only changes `/` to `_`), so installers and scripts must quote paths.
- `/data` is executable but is not a plugin location MPC scans.

### 5. Controls: share the port, not the device

MPC holds the control surface's sequencer port, so a plugin cannot subscribe to it alongside MPC (EAGAIN, NOTES 2026-11-04).
Controls arrive through MPC (Q-Link CC, MIDI to the plugin) or through the draw-handler addin's pages. If you take a
control for a page, release it when the page closes. Map (channel 1): PAD FX note 1, KNOB FX note 0, the three keys above
the screen notes 12/13/14, dial CC 100 (relative, 1 = +1, 127 = -1) and press note 111, K1-K3 CC 16/17/18; pads are channel
10.

### 6. Build and catalog checklist

1. `vst.json` `targets` includes `aarch64`; build with `tools/build_port.sh <vst.json> all` (same path as Gen2).
2. Link for **glibc 2.39** at most. A host cross-compiler with a newer glibc leaks `libmvec` symbols (NOTES 2026-11-04: NAM
   was rebuilt in a glibc 2.36 container for that reason).
3. Add an on-screen page only if you use the draw handler's API (`sample_page` in `vst.json`, offline only so far).
4. In the catalog, mark the entry's device limits honestly (Sample-capable or not, and a `Verified Sample` badge only after a
   device check): `docs/CATALOG.md`, NOTES 2026-11-05.
5. Test on the device with the plugin in a project, MPC stopped while you edit the working copy, `.rack-backup` kept.
6. Back up `MPC Sample.settings` with MPC stopped before touching it, and stage files as `x.new` then `mv` (repo rule).

## What the source adds beyond our device notes

- **`&gpu` disabled** in the AC50 device tree: GPU absence is by design, not a driver bug.
- **No Wi-Fi or Bluetooth** is a board-file choice (the nodes exist in the shared az07 base, the Sample doesn't enable the
  SDIO and UART they need).
- **`edisksd` 0.19** (the Sample's version) has the same mount rules as 0.21 on G1 and G2: default `noexec`, `/media/<label>`,
  allow-listed caller options.
- **RT kernel:** `-rt13` here (G1 and G2 use `-rt6`), `HZ_1000`, `performance` governor: a busy audio thread is
  scheduled predictably, but do not hold a lock or allocate on it.
- **USB gadget** carries UAC2, MIDI (and MIDI 2.0) and NCM at once, so SSH and USB audio share the one USB-C.

## Open questions

- Does `/etc/edisksd/main.conf` on the Sample change `DefaultMountOptions`? (`cat` it; read-only.)
- Can a plugin page be opened without the draw-handler addin? (Not found.)
- Are two plugins on the A55s at once in budget? Only single-plugin figures exist.
