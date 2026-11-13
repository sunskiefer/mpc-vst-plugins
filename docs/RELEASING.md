# Releasing a plugin

A release is **one zip per CPU** people can share around: `<Name>-<version>-mpc-armv7.zip` (Gen1 MPC and Force) and, for a port that
lists `"targets": ["armv7", "aarch64"]`, `<Name>-<version>-mpc-aarch64.zip` (Gen2, docs/GEN2.md). Run `release.py` once per `.so`
(`build/x.so` and `build/aarch64/x.so`) with the same arguments and attach both zips to the same release. Each zip unpacks to a folder with
the plugin as one folder (`portable/<skin>/`: the `.so`, the skin, its data, a `plugin-meta.xml` and a `plugin.json` for MPC OS 3.10.1's `VST2 Plugins` folder: copy the folder there and it is listed, no installer needed), `install.sh` /
`uninstall.sh` and a generated `INSTALL.md` (scripted and manual steps, requirements, CPU result, checksums). The folder is
the only layout: it can be dropped into any `Synths` folder by other installers too (`docs/CATALOG_SPEC.md`, "Plugin folder").

An addin (a library MPC preloads) is released with `tools/release_addin.py`, or from CI with `addin-release.yml`, instead: `docs/ADDINS.md`.

## Checklist
1. **Build** with the port's `build.sh` (armhf, `arm32v7/gcc:11-bullseye`; highest GLIBC symbol ≤ 2.32 to be MPC OS 2.x-capable; up to 2.36 is accepted and listed as 3.x only). `release.py` prints the MPC OS label it works out (`MPC OS compatibility: ...`); the skin decides the rest (docs/OS2_SKINS.md).
2. **Host test** (x86, ASan): `tools/test_port.sh <port>/vst.json` (must print PASSED), or the port's own test for a
   hand-written wrapper. It must be clean.
3. **Skin preview**: `tools/studio.py preview "<skin>/Plugin Skins" -o page_%d.png`, and look at every page.
4. **CPU**: `tools/bench.sh build/x.so <ip> -j | tee build/bench.txt`. It must PASS, or WARN with a note
   (docs/BENCH.md).
5. **Device smoke test**: install with the zip's own `install.sh` (step 6 first). Load the plugin on a track, play
   it, turn every page and Q-Link, save and reload a project, then `uninstall.sh`.
6. **Package**:
   ```
   tools/release.py --so build/x.so --skin "build/skin/<vendor> - VST - <Name>" \
       --entry build/pluginlist-entry.xml --version 1.2.0 --bench build/bench.txt \
       --about "One line about the plugin." [--extra engine:engine] [--user-data roms] -o dist
   ```
   Add `--repo owner/name --license <SPDX> [--id my-plugin]` to make it listable in the catalog
   (`docs/CATALOG_SPEC.md`), then check it: `tools/catalog_check.py dist/<zip> --catalog`.
   `--extra SRC:DEST` ships extra runtime files in the plugin folder, next to the `.so` (an engine bundle, presets; `DEST` is
   relative to the plugin folder, a leading `vst/` from older scripts is still accepted). `--user-data <folder>` marks a folder
   where the user adds files (ROMs, kits, banks); the installer keeps it. `--folder-name <name>` names the plugin folder (and the zip)
   when it differs from the plugin-list name, e.g. Monomodule's `Monomodule One` folder for the list name `Monomodule`; keep it the same
   across versions so upgrades find the user's data.
7. **Publish**: tag `<port>-vX.Y.Z` in the port's repo and attach the zip:
   `gh release create maze-voice-vst-v1.2.0 dist/Maze-Voice-1.2.0-mpc-armv7.zip [dist/Maze-Voice-1.2.0-mpc-aarch64.zip] --notes-file ...`
   Paste the zip's INSTALL.md "Requirements" and "Install" sections into the notes.

**Catalog rule:** always pass `--repo` and `--license` (CI: `plugin_id`, `license`, `requires`) and run the `--catalog` check; see PORTING.md section 5.

## Releasing from CI
`.github/workflows/vst-release.yml` is a reusable workflow that does steps 1, 2, 3 (as images) and 6 in GitHub Actions
and attaches the zip to a **draft** release in the port's repo. Steps 4 and 5 stay on a device, and they are what
you do to the draft's zip before publishing it, so the zip you tested is the zip people get.
If the port's build also leaves `build/aarch64/<so>` (vst.json `targets`), the workflow packages and attaches the `-mpc-aarch64.zip` (Gen2) too;
the CPU result is Gen1's, so that zip has none. Offline only until run on a Gen2 device (docs/GEN2.md).

A port with only a Gen2 build (no 32-bit `.so`) passes `gen2_only: true`: the workflow then packages only `build/aarch64/<so>` as the `-mpc-aarch64.zip`
(the default, false, is unchanged for every other port).

**An Akai SDK project** passes `sdk_project: <folder with plugin.json and src/>` instead of `vst_dir` and `build` (2026-11-13, offline:
the step run locally on SimpleSynth). The workflow builds it for both CPUs with the SDK pinned in this repo
(`vendor/mpc-plugin-builder-sdk`, `tools/build.py`, zig downloads itself), packages it with `tools/sdk_release.py` (same per-build zips,
3.9.1 registration included) and adds the SDK zip. A CPU result goes in `<sdk_project>/bench.txt`; the other inputs (`plugin_id`,
`license`, `about`, `user_data` ...) work as for any port. Skin previews are skipped for SDK skins (our previewer reads our format). A
whole caller:

```yaml
name: Release
on:
  workflow_dispatch:
    inputs:
      version: { required: true }
      dry_run: { type: boolean, default: true }
jobs:
  release:
    uses: open-mpc/mpc-vst-plugins/.github/workflows/vst-release.yml@<sha>
    permissions: { contents: write }
    with:
      tools_ref: <sha>
      tag: my-synth-v${{ inputs.version }}
      version: ${{ inputs.version }}
      sdk_project: plugins/MySynth
      plugin_id: my-synth
      license: MIT
      dry_run: ${{ inputs.dry_run }}
```

The Plugin Manager lives in its own repo since 2026-11 ([open-mpc/plugin-manager](https://github.com/open-mpc/plugin-manager)) and calls this workflow like any port (`asset_name: Plugin-Manager-G2`, `retire: OpenMPC - VST - Plugin Manager G2`). Its registry entry lists `previous_repos: [open-mpc/mpc-vst-plugins]`, where its releases up to 1.1.0 stay.

A port calls it from its own repo with a `workflow_dispatch` workflow that takes the version. Pin this repo to one
commit in both places:
```yaml
jobs:
  vst:
    uses: open-mpc/mpc-vst-plugins/.github/workflows/vst-release.yml@<sha>
    permissions: { contents: write }
    with:
      tag: my-port-vst-v${{ inputs.version }}
      version: ${{ inputs.version }}
      tools_ref: <sha>                 # the same commit
      vst_dir: vst                     # the build writes vst/build/<so>, skin/, pluginlist-entry.xml
      build: vst/build.sh              # run from the port repo root; MPC_VST is set
      host_test: '"$MPC_VST/tools/test_port.sh" vst/vst.json'   # optional
      about: One line about the plugin.
      dry_run: ${{ inputs.dry_run }}   # optional: zip and previews as run artifacts only
```
Optional inputs: `extra` (release.py `--extra` specs), `user_data` (release.py `--user-data` folders, space-separated: the user's ROMs/banks, kept across upgrades and moved in from an old install) and `zig` (a zig version to install). The run's artifacts hold the zip and one PNG per skin page, and its summary lists what is left
to do. CPU (step 4) comes from `<vst_dir>/bench.txt` when the port commits the `-j` output of `tools/bench.sh`;
without it INSTALL.md has no CPU section. Re-running with the same version replaces the draft's zip. It refuses a
version that is already published. Publishing the draft creates the tag.

## Versioning
- `X.Y.Z` in the zip name and INSTALL.md. Bump Z for fixes, Y for new parameters or pages, X when parameter
  indices change. Changing the indices breaks saved projects, because MPC stores values by index.
- Keep the plugin `uid` and `.so` name fixed across versions: the installer replaces the entry with the same
  `uid` or `file=`, and projects find the plugin by uid.
- Release data the user adds to (ROMs, kits, banks) with `--user-data <folder>`; `--extra SRC:DEST` ships data next to the
  `.so` (`DEST` is relative to the plugin folder).

## What the installer does
Run on the device as root (`sh install.sh [-y] [-n] [-t <synths-dir>]`):
1. Checks root, armv7, that `MPC.settings` exists and `SHA256SUMS`, and asks for confirmation.
2. Stops MPC (`systemctl stop acvs`, or `inmusic-mpc` when the device has no `acvs` service, as on some MPC OS 2.x versions and on Hakai-enabled systems) and waits for it to exit. A trap restarts MPC on any error.
   `-n` (also on `uninstall.sh`) defers this to the caller: the script neither stops nor starts MPC and refuses to run while MPC is
   running. A batch installer stops MPC once, runs every plugin's `install.sh -y -n`, then starts MPC once. The caller must
   start MPC again even if one install fails.
3. Copies `portable/<skin>/` next to its target (`/sdcard/Synths`, or `-t <folder>`), carries over the files the user
   added (the manifest's `user_data`, see `--user-data`), and swaps the new folder in.
   Then it puts back executable bits and symlinks from the package's `MODES` file (written by `release.py`): a zip unpacked
   on Windows, or copied file by file, loses both, and an engine that bundles binaries (yt-dlp, ffmpeg, a private Python) then fails.
4. Backs up `MPC.settings` to `MPC.settings.bak-<so>-<date>`. `plugin_list.awk` (BusyBox awk) drops any entry with
   the same `file=` or `uid` (an older install at another path) and inserts the folder's `plugin-meta.xml`, with
   `%payload-path%` replaced by the Synths folder, into `pluginList-arm`, creating the list if needed. The result is
   checked (exactly one entry, valid XML when python3 exists) before it replaces the original.
5. If the plugin was installed the old way (`.so` in `/sdcard/vst`): removes that `.so` and the data the package ships
   there, and moves the user's own files (`user_data`) from `/sdcard/vst/<path>` into the plugin folder. Done only after
   the settings edit succeeded; nothing else in `/sdcard/vst` is touched.
6. Starts MPC.

Why MPC is stopped for the edit: it holds its settings in memory while it runs and saves them itself, so an edit made
underneath it can be lost (then the plugin is missing after the next restart). Tools that rebuild the whole list from the
plugin folders in the `Synths` folders keep a plugin installed this way, because each one is such a folder with its own
`plugin-meta.xml`; a plugin registered any other way (by hand, or the old `/sdcard/vst` layout) is dropped by such a rebuild.

The settings edit was tested 2026-09-24 against a copy of a real Force `MPC.settings`: replacing an entry, running
twice (identical output), removing, a missing `pluginList-arm`, and a self-closing `<KNOWNPLUGINS/>`. A full
scripted install on a device (which restarts MPC) is step 5 of the checklist.

Audience: root access is needed to edit `MPC.settings`, so releases are for modded units. Say so up front.

## Keeping the plugin list in step with the folders: `tools/release/sync.sh`

`sh sync.sh [-y] [-n] [--dry-run] [-t <synths-dir>]...` (BusyBox `sh`, needs `plugin_list.awk` next to it) makes MPC.settings' plugin list
follow the plugin folders in `/sdcard/Synths` and every `/media/*/Synths` (or the `-t` folders): it registers a folder that has no entry,
replaces an entry with the same uid whose `.so` is gone, removes an entry that points into a Synths folder whose `.so` is gone, and
leaves every other entry alone. Nothing to do means no restart; otherwise it backs up `MPC.settings`, checks the result and stops and
starts MPC once (`-n`: the caller does, as for `install.sh -n`). `--dry-run` prints the plan only. It uses the same folder rule as
MockbaMod's `vstscanner.sh` (`/media/*/Synths/*/plugin-meta.xml`) but not its whole-list rebuild, so entries from other tools survive.
It is not shipped in the release zips yet; the device-side store script (docs/CATALOG.md, Phase 4) will call it after a batch of
`install.sh -y -n`. Only the first `<PLUGIN>` in a `plugin-meta.xml` is read (the packages `release.py` builds have one).

## Folder-only zips for MPC OS 3.10.1 (2026-11-02)
`tools/folder_zip.py <release zip> <out zip>` writes a zip whose only entry is the plugin folder from `portable/` (it must contain
`plugin.json`). Users on 3.10.1 unzip it and copy the folder into `VST2 Plugins`. The Plugin Manager release carries
`OpenMPC-Plugin-Manager-<ver>-Gen1.zip` / `-Gen2.zip` made this way, next to the normal zips. Tests: `tools/test_folder_zip.py`.

**SDK-format zip (Akai's plugin folder, 2026-11-03, offline; not yet loaded on a device).** The reusable release workflow
(`vst-release.yml`) also attaches `<Name>-<ver>-mpc-sdk.zip`, made by `tools/sdk_zip.py OUT.zip <armv7 zip> [<aarch64 zip>]`: one
folder for every MPC, `plugin.json` with both `file32` and `file64`. The binaries are `<slug>-arm32.so` / `<slug>-arm64.so` at the
folder root, not in the SDK's `VST/` subfolder (allowed: `plugin.json` gives the path), because our plugins take their own folder
to be the directory of their `.so` (`wrapper/plugin_dir.h`, the Plugin Manager's themes). `plugin-meta.xml` (3.9.1 registration, naming the old `.so` name) is left out, since the SDK zip is for 3.10.1+. `mpc-plugin.json` is kept with
`"arch": "universal"` and each build's `max_glibc` under `builds`. A plugin whose other files differ between builds gets no SDK zip
(a notice, not a failure). The catalog, the Plugin Manager and the installers keep using the per-build zips (`*-mpc-armv7.zip`
does not match the SDK zip's name). Tests: `tools/test_sdk_zip.py`. The Plugin Manager release has one too (from 1.1.1). The Install page writes these to a stick from the catalog site: after uploading them, set their SHA-256 (and the tag, if it changed) in `catalog/downloads.json` (docs/CATALOG.md).

## From an Akai SDK build: `tools/sdk_release.py` (2026-11-13, offline)
`tools/sdk_release.py "<Manufacturer> - VST - <Name>" --version X --id ID --repo owner/name [--license L]` (a folder made by the SDK's
`tools/build.py`, or its zip) writes the per-build zips the catalog, the Plugin Manager, the installers and the sticks install: one per
binary named in plugin.json (`file32` -> armv7, `file64` -> aarch64), each holding the whole folder with only its own binary, still at
the SDK's path (`VST/<slug>-arm32.so`). The SDK finds its folder by walking up from the .so to plugin.json, so `Content/` is found on
3.9.1 too. The 3.9.1 plugin-list entry comes from plugin.json (a four-character uid such as "MxSy" becomes 4d785379). It runs
`release.py --so-path`, so every check and the install scripts are the usual ones; `tools/sdk_zip.py` then joins the zips back into
the SDK zip with the same paths. A plugin without `Plugin Skins/TUI.json` and `version.xml` is refused. Extra options (`--bench`,
`--user-data` ...) go on to release.py. Tests: `tools/test_sdk_release.py`. In CI: `vst-release.yml` with `sdk_project` ("Releasing from CI"). Not yet: the catalog converting SDK-only releases (docs/ROADMAP.md).

## Renaming a plugin (2026-11-13)
On MPC OS 3.10.1 a plugin's folder must be `<maker> - VST - <name>`, so a new name is a new folder. Release it with the reusable
workflow's `retire` input (`'Maker - VST - Old Name'`, several separated by `|`, passed to `release.py --retire`): the zip's
`install.sh` then removes the old folder when it holds this plugin's id and moves its `user_data` in. Keep the uid, so saved projects
and MPC.settings entries stay with the plugin; keep the catalog id and the release asset names. Move (the Plugin Manager's and the
installer's) skips a Synths folder whose uid already has a folder in `VST2 Plugins`, so the old copy isn't copied back.
First use: the Plugin Manager, "Plugin Manager G2" -> "Plugin Manager" in 1.1.0 (it also removes a hand-copied old folder of itself
at start). Its asset names stay `Plugin-Manager-G2-*` because installer 2.0.0 checks stick bundles by that name.


## Plugin Manager releases: the rest after CI (`tools/pm_release.py`)
After "Run workflow" on the Plugin Manager's workflow (open-mpc/plugin-manager, dry_run off) has made the draft (start it after main's own run on that commit has passed: the release reuses it, else the tests run again):
`tools/pm_release.py <version> --firmware <3.9.1 update.img>` makes the Gen1/Gen2 folder zips and attaches them, builds the 3.9.1
stick and makes its draft. Check on a device, then `--publish` publishes both (neither becomes "Latest"), and `--pins` opens the PR
that moves catalog/downloads.json, tools/desktop/g2stick-pin.json, the Install page and the site guide to the new version (the pinned
URLs must answer 200 first). A person merges the pins PR: installers read the stick pin live from main. Test: tools/test_pm_release.py.
