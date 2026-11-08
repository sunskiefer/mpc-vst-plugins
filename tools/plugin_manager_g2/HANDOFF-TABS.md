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
1. **LOCATIONS: make the current default obvious.** Today only the state text says "Default install location". Add a visible marker (e.g. a DEFAULT pill or highlighted card border) on that card.
2. **Retire the Drive exec patch** (`drive-exec` in `catalog/patches.json`, `tools/mpc_patch/drive_exec`): Auto exec replaces it. Keep uninstall working for existing users; mark it retired/hidden for new installs; update `docs/PATCHES.md` and the site.
3. **Bug: installs fail with download errors on the Force** ("8w8 failed after 3 retries"). Not investigated yet; cause unknown. Start with `/tmp/openmpc-pm/manager.log` and `apply.log` on the Force, whether libcurl works there (DNS/TLS/CA file), the 8w8 asset URL and sha in the catalog, free space (`/sdcard` had 2.4 GB), and whether the Live III does the same.

## Known smaller items
- Web page (port 8790) does not apply the per-device filter or the new tabs.
- Restart prompt on a Register-only apply no longer shows "Install to", but is otherwise unchanged.
- Full suite once showed `web` and `one-gen2-storage` failing when run concurrently; both pass alone.
- Not tried: REGISTER with a real USB stick/SD card; Force screen checks (owner).
