# Handoff: Plugin Manager bottom tabs (2026-11-08)

Branch `local/post-launch` in `~/mpc-vst-plugins-postlaunch`, local only (keep MPC OS 3.10.1 details private until Akai launches; push as separate PRs in launch week). Design: `DESIGN.md` section 11.

## Built (all committed, device-tested on the Live III 3.10.1)
- **LOCATIONS**: one card per listed Synths folder (fs, rw, exec, used/total, plugin count, verdict), MAKE DEFAULT, RESCAN, unlisted mounted drives with REGISTER (applied via APPLY, CRLF kept, rollback), paging 4 per page.
- **SETTINGS**: theme, beta plugins, web page; saved in `<base>/.openmpcpm-settings`.
- **PATCHES**: cards from `patches.json`; CHECK (fetch, sha256, run `status`); APPLY/UNDO (two taps within 6 s) only for patches with `restarts_mpc=false`. Tested: add-in restore undone and re-applied.
- Catalog entries with `"devices"` not including this model (e.g. `mpc-sample`) are hidden unless installed.
- Tests: `tests/run.sh` (sections devices, addins, links, vst2, pick, theme, safety, arch, web; `ONLY=<section>`). Do not run two at once: they share /tmp files and cause false failures.

## On the devices
- Live III: new build in `/nvme/VST2 Plugins/...`; previous release in `/nvme/pm-r3-backup-20261108`. Unsaved test project open.
- Force (3.9.1, 192.168.1.187): new armv7 build deployed 2026-11-08, six addins loaded. Old 0.9.0 in `/sdcard/pm-090-backup-20261108` (roll back: move back, `systemctl restart acvs`). **Not yet verified on its screen; the owner is testing.** Do not apply patches there.

## To do (from the owner)
1. ~~LOCATIONS default marker~~ done: green DEFAULT pill beside USABLE (check on a device).
2. **Retire the Drive exec patch** (`drive-exec` in `catalog/patches.json`, `tools/mpc_patch/drive_exec`): Auto exec replaces it. Keep uninstall working for existing users; mark it retired/hidden for new installs; update `docs/PATCHES.md` and the site.
3. **Bug (partly diagnosed): installs fail with download errors on the Force** ("8w8 failed after 3 retries"). On the Force 2026-11-08 plain wget and libcurl (same options as the Manager, with and without the CA file) both fetched the 8w8 zip and the catalog fine and the sha matches, so the network and certs are fine. The Manager now reports the real reason in the failure message (`download failed after 3 retries (<reason>)`); retry on the Force and read it. Also check `/tmp/openmpc-pm/manager.log` and `apply.log` on the Force, whether libcurl works there (DNS/TLS/CA file), the 8w8 asset URL and sha in the catalog, free space (`/sdcard` had 2.4 GB), and whether the Live III does the same.

## Also built after the first handoff
- SETTINGS: IMAGE CACHE row (fill %, CLEAR queues emptying it at APPLY).

## Decisions (2026-11-09)
- 1.0.0 was never released publicly, so this build REPLACES r3 as Plugin Manager 1.0.0 (call the build r4; no version bump). Force screen check: owner says it worked.
- Drive exec patch retired from catalog/patches.json (source + tests kept; `sh drive-exec-patch.sh uninstall` for old installs).
- Web page (port 8790) now applies the same hiding as the screen (other-MPC entries, hidden betas) via `hidden_here()`. It still has no tabs for Locations/Patches/Settings (screen only).
- Pre-existing, unrelated: `tools/test_patches.py` test_confirmed_install_needs_no_typed_word_then_undo fails (drum-pad md5) with or without my catalog change.

## Plan to ship as 1.0.0 (r4)
1. Build r4: CI `plugin-manager-g2.yml` (dry_run first) or local; zips armv7+aarch64 and Gen1/Gen2 folder zips into `~/mpc-310-private/release-drafts/pm-1.0.0-r4/`.
2. Rebuild the 3.9.1 stick: `tools/usb_g2/build_stick.py --firmware ~/roms/Force-3.9.1-update.img --g2-dir <zips> --zip ...`.
3. Re-pin: `catalog/downloads.json` and `tools/desktop/g2stick-pin.json` (hashes of r4 zips + stick).
4. `tools/compat/run.sh` (released Managers/installers vs candidate catalog) before any catalog PR.
5. `tested.json` records (Force 3.9.1, Live III 3.10.1) and DEVICE_TESTS.md rows for the new tabs.
6. Retest the Force 8w8 download error with the new failure text; fix if it recurs.
7. Launch week: separate PRs (Manager; catalog/patches; site/docs); keep 3.10.1 details out of public branches until Akai publishes.

## Known smaller items
- Web page (port 8790) does not apply the per-device filter or the new tabs.
- Restart prompt on a Register-only apply no longer shows "Install to", but is otherwise unchanged.
- Full suite once showed `web` and `one-gen2-storage` failing when run concurrently; both pass alone.
- Not tried: REGISTER with a real USB stick/SD card; Force screen checks (owner).
