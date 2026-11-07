# inMusic's open-source release (az0x-open-source): what it is and what it tells us

**Read 2026-11-08, offline (source only; nothing here was run on a device).** Repo:
<https://github.com/inmusic-software/az0x-open-source> (inMusic's licence-compliance archive for the MPC and Force
firmware). Facts below come from the G1 and G2 kernel trees, the MPC Sample kernel tree and `edisksd`. Where a fact is
checked against a device it points to `docs/NOTES.md`. A source fact says what the code allows, not what a given unit does:
check the device before you rely on it.

## What it is, and what it is not

- **Is:** patched upstream **source archives** (`*-patched.tar.xz`, in Git LFS) of the open-source parts of the Linux image:
  kernel, U-Boot, systemd, glibc, busybox, alsa-lib/utils, bluez, libusb/libusbgx, e2fsprogs/exfatprogs, and inMusic's own
  small daemons (`edisksd`). One branch per product group and OS release: `mpc-g1/<ver>`, `mpc-g2/<ver>`,
  `mpc-sample/<ver>`. `main` is only a README that links them.
- **Is not:** firmware you can install, and not Akai's closed code. There is no MPC app, no VST2 host, no plugin SDK, no
  skin loader. It answers "what is the OS under the app", never "how does MPC load a plugin".
- Branch contents seen: G1 = `arm-poky-linux-gnueabi` (armhf), G2 and Sample = `aarch64-poky-linux`. The kernel archive is
  about 160 MB each (1.6-1.7 GB unpacked); `edisksd`, `base-files` and `libusbgx` are under 1 MB.

### Getting a file without git-lfs

`git clone --depth 1 <repo>` gives LFS pointer files only. Fetch one archive from its pointer (`oid sha256:...`, `size N`)
with the public LFS batch endpoint, no login:

```python
import json, urllib.request
oid, size = "<sha256 from the pointer>", 159720512
req = urllib.request.Request(
    "https://github.com/inmusic-software/az0x-open-source.git/info/lfs/objects/batch",
    data=json.dumps({"operation": "download", "transfers": ["basic"],
                     "objects": [{"oid": oid, "size": size}]}).encode(),
    headers={"Accept": "application/vnd.git-lfs+json", "Content-Type": "application/vnd.git-lfs+json"})
url = json.load(urllib.request.urlopen(req))["objects"][0]["actions"]["download"]["href"]
urllib.request.urlretrieve(url, "out.tar.xz")
```

Treat the archives as untrusted data: extract with `tar --no-same-owner --no-same-permissions` into an empty directory on
a disk with room (`/tmp` may be a small tmpfs).

## Hardware map (from the device trees)

Each board has `inmusic,product-code` in its device tree. Source dumps carry other inMusic brands too (Denon DJ, Rane); the
table lists only the MPC and Force parts.

| Group | SoC / userland | Kernel in the drop | Product (code, device tree) |
|---|---|---|---|
| **G1** | Rockchip **RK3288**, 4x Cortex-A17, armhf | `6.18.44`, `-az01-2026-08-10`, `-rt6` | Live II (ACVB), X SE (ACV5S; ACV5, ACV8 have USB hub configs), One (ACVA), One+ (ACVA2, "az05" board), Key 61 (ACVM), Key 37 (ACVR, az05), **Force (ADA2)** |
| **G2** | Rockchip **RK3588** (One G2 and Key 37 G2: **RK3588S**), aarch64 | `6.18.44`, `-imb-2026-08-10`, `-rt6` | **Live III (ACVG)**, XL (ACVC), One G2 (ACVA3), Key 37 G2 (ACVR2) |
| **Sample** | Rockchip **RK3566**, 4x Cortex-A55, aarch64 | `6.12.60`, `-imb-2025-12-03`, `-rt13` | MPC Sample (AC50, "az07" board) |

- **Confirms** the product codes in `docs/MPC_OS_REFERENCE.md` (ACVB, ACV8, ACV5, ACV5S, ACVA, ACVA2, ACVM, ACVR) and adds
  the Force code **ADA2** and the Gen2 codes ACVG, ACVC, ACVA3, ACVR2.
- **Live III and XL** (`rk3588-az04-acvg/-acvc.dts`) are on the full RK3588 with a DSI panel and I2C touch (ILI2117 on the
  Live III, ILI9882 on the XL). The Live III also has a PCIe node and `inmusic,nvme-fitted` in `/chosen`, and a TI BQ40Z50
  battery gauge. That `chosen` flag is the same mechanism `docs/GEN2.md` describes for choosing `/nvme` vs `/storage`.
- **One G2 and Key 37 G2** use the RK3588S with an `inmusic,az04-codec` simple-audio-card on I2S; the Live III file has no
  such node.
- **Sample** (`rk3566-az07-ac50.dts`): ST7796S SPI panel (320x240), UARTs for the control surface and MIDI, I2C codec,
  SD card on `sdmmc0`. **`&gpu` is `status = "disabled"`** in the AC50 file, so there is no GPU driver bound (a plugin UI
  cannot assume one, which agrees with MPC drawing on the CPU: NOTES 2026-11-04). The Wi-Fi (BCM43456) and Bluetooth
  (BCM4345C5) nodes sit in the shared az07 base file, but only the other az07 product (`hg1x`) enables the SDIO and UART
  they need, which is **why the Sample has no Wi-Fi or Bluetooth** even though the image ships the firmware and
  `brcmfmac` (NOTES 2026-11-04 saw the same on a device).

## Kernel facts (all three groups unless noted)

| Fact | Source | Cross-check |
|---|---|---|
| **PREEMPT_RT**, `CPU_FREQ_DEFAULT_GOV_PERFORMANCE` | all `*_defconfig` | matches `MPC_OS_REFERENCE.md` (Gen1) and NOTES (governor `performance` on the Live III and Sample) |
| `HZ_1000` | G1 and Sample defconfigs (not seen in the G2 one) | matches the 1000 Hz note for Gen1 |
| USB gadget through ConfigFS: serial, ACM, OBEX, NCM, ECM, RNDIS, EEM, mass storage, FunctionFS, **UAC2, MIDI, HID** (Sample also **MIDI 2.0**) | G2 and Sample defconfigs; G1 has MIDI, UAC2 | matches the Gen1 note; the Sample's NCM + UAC2 + MIDI gadget is in NOTES 2026-11-04 |
| `SND_USB_AUDIO` (+ `MIDI_V2` on G2, Sample), `SND_SEQUENCER` | defconfigs | consistent with the ALSA route Akai described for MIDI |
| `INPUT_UINPUT=y` (G1) | G1 defconfig | `uinput` lead in `MPC_OS_REFERENCE.md` stands |
| `PERF_EVENTS=y`, `FTRACE` off (G1) | G1 defconfig | matches "perf yes, ftrace no" |
| `SECURITY_LOADPIN=y`, `DM_VERITY=y` (G1) | G1 defconfig | compiled in, as the reference says; **the config cannot say whether it is enforcing** (that is a boot-time setting) |
| GPU: **Panfrost** built in (G1), **Panthor** module (G2), Panfrost module but GPU node disabled (Sample) | defconfigs, AC50 dts | matches "GPU idle, MPC draws on the CPU" |
| RGA (`VIDEO_ROCKCHIP_RGA`) on G2 and Sample | defconfigs | matches `/dev/video0` in NOTES |
| Thermal: critical trip 115 C (RK3588 az04 and the Force's `tsadc` shutdown); passive 75 C, hot 85 C (az04) | thermal dtsi, Force dts | explains throttling headroom; not measured by us |

The G2 board files do not set a big.LITTLE split; core layout (A55 0-3, A76 4-7, NOTES and `docs/EMULATOR_PORTS.md`) comes
from the SoC and was read on a device, not from this source.

## The USB mux (this is the Force "usb-mux" we decoded)

`drivers/misc/of-mux.c` (inMusic, GPL-2.0, compatible `virtual-mux`) is a **device-tree-overlay switch**: each `state-<name>`
node holds overlay fragments, and setting the state applies that overlay with an optional `switch-delay-ms` before and
`post-switch-delay-ms` after. The hubs use it as an **internal / external** selector:

- **Force (G1, `rk3288-az01-usb-mux.dtsi`):** `default-state = "internal"`, `switch-delay-ms = 25`,
  `post-switch-delay-ms = 1000`. One GPIO (bank 8, pin A1) drives the upstream select: **high = internal, low =
  external**. A **SMSC USB5537B** hub is configured over I2C (address `0x2d`) with a firmware blob (`*.usbcfg`, one per
  product, carrying the product and "Hub" strings). The same mux also toggles a `sata_reset` GPIO hog (bank 7, pin C5).
  The hub reset line is driven at 2 mA so a debug-UART pull cannot override it.
- **Live III and other G2 boards (`rk3588-az04.dtsi`):** the same `usb-mux` node, `default-state = "internal"`,
  `switch-delay-ms = 25`, but it only changes pin control on `usb_host1_xhci` (`hub_up_sel_internal` /
  `hub_up_sel_external`). `usb_host0_xhci` is in **peripheral (gadget)** mode and `usb_host1_xhci` is **host**, behind a hub
  with USB IDs `2109:0817` and `2109:2817`.
- So "internal" = the MPC's own side of the hub (its audio device, controller surface), "external" = the hub is handed to
  the USB port. After a switch, wait for the post-switch delay before expecting devices (about 1 s on the Force). This
  matches the controller-mode behaviour documented in the force-shadow findings and the Live III Controller Mode on the
  Install page; it is the likely reason a mode change drops and re-enumerates USB devices.

## Drive mounting: where the default `noexec` comes from (`edisksd`)

`edisksd` (versions 0.21 on G1 and G2, 0.19 on the Sample) is inMusic's udisks-like D-Bus daemon. In `src/filesystem.c`:

- **Mount point:** `/media/<label>`, else `/media/<uuid>`, else `/media/disk`; `/` in a label becomes `_`, **nothing else is
  changed (spaces, punctuation stay)**; a taken name gets a number suffix; the folder is made with `mkdir` 0777 and removed
  again if the mount fails. Because the mount point is made under `/media`, a read-only or non-writable `/media` makes the
  mount fail with that error. This is the "/media is tmpfs" report shape in `docs/NOTES.md`; the source alone does not say
  why one unit's `/media` was not writable.
- **Default options:** `nodev,noexec,nosuid,nosymfollow`, **unless `/etc/edisksd/main.conf` (`[General]
  DefaultMountOptions`) says otherwise**; a per-filesystem section (for example `[exfat]`) can add more. FAT gets
  `dmask=0077,showexec,utf8=1`, NTFS `iocharset=utf8`.
- **Caller options:** the `Mount` D-Bus call takes an allow-list (`exec`, `noexec`, `ro`, `rw`, `sync`, `noatime`, `utf8`,
  and similar; **never `dev` or `suid`**). Caller options come after the defaults, so a caller that passes `exec` should
  win. A read-only block device is always mounted `ro`.
- **Meaning for us:** `noexec` on a removable drive is the daemon's default, and whether a drive ends up executable depends
  on what MPC asks for and on the unit's `main.conf`. That fits our device notes (a stick not `noexec` on one Force, the SSD
  `noexec`, a drive that was `noexec` on a Live III). **To check on a unit:** `cat /etc/edisksd/main.conf` and the mount
  line, both read-only. Not yet done by us.
- Filesystems the image carries: `exfatprogs`, `e2fsprogs`, `ntfs3`/`ntfs-3g` fallbacks, vfat. exFAT is the format MPC
  itself uses on user drives.

## Control surface input driver (Sample and G2)

`drivers/input/misc/inmusic-cs.c` ("a generic driver to read input multiplexed over SPI via parallel-to-serial conversion")
reads buttons and encoders on boards that use it (kernel input events, with a load GPIO and an hrtimer poll). On the Sample
the panel is instead a separate controller behind a UART and MPC reads it as a rawmidi device (NOTES 2026-11-04), so this
driver is background for other boards; it does not change how a plugin gets controls.

## Disagreements and corrections

None found. Nothing in the source contradicts a verified fact in `docs/NOTES.md` or the third-party guide. The only changes
are additions (product codes, the mux details, the `edisksd` default). A still-open question the source cannot answer:
whether LoadPin and dm-verity are enforcing on a given unit (`MPC_OS_REFERENCE.md` says not on 3.9.1).

## Not in the source (do not look here)

- Plugin hosting: how `pluginList-arm` / `pluginList-arm-64bit`, `MPC.settings`, or the VST2 folders are read.
- The skin loader and page JSON, SMEX, the web API, the script runner, update-image signing (the updater is closed).
- Anything about the MPC OS release numbers beyond the directory names in the kernel version string.

## Using it for plugin work

- Build targets stay armv7 (G1/Force), aarch64 (G2 and Sample); all three are glibc 2.39 userland, so one toolchain per
  architecture is enough.
- Budget CPU on the slowest core you target. The Sample is four A55s only; see `docs/MPC_SAMPLE.md`.
- If a feature needs a USB mode change (controller mode, drive swap), allow about a second after the mux switches.
- Don't assume a writable or executable drive: read the mount line first (`edisksd` default above).

## Follow-up checks (the source is still there to read)

The repo is public and stays fetchable (recipe above), so these can be checked later without a device or with one read-only
command. Nothing below has been done yet.

**Still to read in the source (2026-11-08 was a first pass):**
- The G1 kernel's differences from upstream (only the device trees, defconfig and `of-mux.c` were read), for example the
  Force's SATA/SD paths and the RK3288 audio setup.
- `libusbgx` and `base-files` (`fstab`, `rotation`) on each group, and `u-boot` (read only for the Sample's file list).
- Whether a later release changes `edisksd` defaults, the mux delays or the board files: diff the new branch against the
  one read here (`mpc-g1/*`, `mpc-g2/*`, `mpc-sample/*`).

**To confirm on a unit (read-only):**
- `cat /etc/edisksd/main.conf` and the `mount` lines for removable and internal drives (does the default `noexec` apply?).
- Whether LoadPin and dm-verity enforce (`dmesg | grep -i -E 'loadpin|verity'`).
- On the Live III: the mux state and hub (`lsusb -t`) before and after a Controller Mode change, to compare with the
  1 s post-switch delay noted for the Force.
