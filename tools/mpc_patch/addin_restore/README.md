# MPC Add-Ins patch for 3.10

Restore MPC add-ins (LD_PRELOAD addins in `/data/mpc-addins`) after a reboot on MPC OS 3.10 (Gen2 / aarch64; Gen1 / armv7 built but not yet tested on a device).

## The problem

On MPC OS 3.10, addins load through a systemd drop-in in `/run` (tmpfs). After every reboot that drop-in is gone, so the addins do not load until someone writes it again. This means SSH'ing in and re-running install scripts after every power cycle.

## What this patch does

Installs a Lua device script on the persistent content drive (`/nvme` on a Live III, `/sdcard` on others). MPC loads device scripts from `MPC Documents/Devices/` at every start. The script:

1. Checks `/data/mpc-addins/` for installed addins
2. Checks MPC's `LD_PRELOAD` to see if they are loaded
3. If any are missing: writes the `/run/systemd/system/acvs.service.d/90-mpc-addins.conf` drop-in
4. Schedules a single detached `systemctl restart acvs` (8-second delay)
5. A guard file (`/run/addin-restore-done`) prevents restart loops

The user sees one automatic MPC restart after a reboot. After that, all addins work.

**Since 1.2.0, also the 16-pad drum layout patch on Gen2.** That patch runs a patched copy of MPC (`/data/mpc-drumpad/MPC`) through
its own `/run` drop-in, `94-mpc-drumpad.conf`, so a reboot switched it off too. When the copy is installed and the drop-in is gone, the
library runs the patch's saved script with `reapply`, which writes the drop-in only if the copy was made from the MPC this system runs
(after a firmware update it leaves MPC stock). Both drop-ins share the one restart. The Plugin Manager installs this patch together with
the 16-pad patch on Gen2 (`also_installs` in `catalog/patches.json`), so no USB stick or SSH is needed.

## Updating

Install over an older version replaces it in place (`status` says `old-patch` until then). The device script (`.lua`) and its signed
`.ini` are unchanged since 1.0, so the signature MPC checks still holds; only the library changes.

## Usage

```sh
# Copy to the device
scp addin-restore-patch.sh root@<device-ip>:/tmp/

# SSH in
ssh root@<device-ip>

# Check the current state
sh /tmp/addin-restore-patch.sh status

# Install
sh /tmp/addin-restore-patch.sh install

# Remove
sh /tmp/addin-restore-patch.sh uninstall
```

Or install through the Plugin Manager app's patches section.

## Requirements

- Gen2 device (aarch64): Live III, One G2, Key G2, XL. Tested on a Live III.
- Gen1 device (armv7): Live, Live II, One, X, Key 61/37, Force. Since 1.1.0 the script carries an armv7 build of the same
  `addin_restore.c` and picks it by `uname -m`; checked offline only. The catalog entry still lists `aarch64` until a Gen1 device
  on MPC OS 3.10.1 has run it (one reboot, addins restored).
- MPC OS 3.10
- At least one addin installed in `/data/mpc-addins/`

## What it installs

Three files in `<content root>/MPC Documents/Devices/Addin Restore/`:

| File | Purpose |
|------|---------|
| `addin_restore.ini` | Device script registration (device name, checksum) |
| `addin_restore.lua` | Lua script: discovers the .so path, calls it via `package.loadlib()` |
| `addin_restore.so`  | Native library (aarch64 or armv7, by device): reads addins, checks LD_PRELOAD, writes drop-in, restarts |

All files are on the persistent content drive. They survive reboots and firmware updates. The system image is not modified.

## Source and build

`src/addin_restore.c` is the library (1.2.0 rewrote the unpublished 1.1.0 library from what it does: same guard, same drop-in, same
order of `LD_PRELOAD`, same 8 s restart). `build.sh` cross-compiles it with Zig (`python3 -m pip install ziglang==0.13.0`) for aarch64 and
armv7 against glibc 2.31, checks that the libraries need nothing but libc, and regenerates `addin-restore-patch.sh` from
`script.template.sh`. Tests: `build.sh test` (12 cases of the library under ASan/UBSan, on the host) and `test_script.sh` (the script's
status, install, update of 1.1.0 and uninstall, in Docker, for both CPUs).

## Uninstall

`sh addin-restore-patch.sh uninstall` removes the three files. Addins already loaded in the current session are not affected. After the next reboot, addins will not be restored automatically.
