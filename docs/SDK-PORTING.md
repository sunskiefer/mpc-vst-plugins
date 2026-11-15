# Building MPC plugins the SDK way (+ our additions)

**The split:** Akai's SDK builds the plugin; Open MPC delivers it: releases for every MPC (3.9.1 included), the catalog, the Plugin
Manager, installers, sticks, CPU ratings, tiles and themes. The table "Which tool for what" below lists each job.

Each numbered section after 4 is what one port taught us, in the order the ports were done; a later section can refine an earlier one.

The house method from 2026-11-04 onwards, worked out on Dexed 2.0. New plugins and major revisions of existing ones are built on
Akai's **MPC Plugin SDK** ([`mpc-plugin-builder-sdk`](https://github.com/inmusic-software/mpc-plugin-builder-sdk) from Akai, MIT). Our own tools fill the gaps.
Dexed ([sd88me/mpc-vst-dx7](https://github.com/sd88me/mpc-vst-dx7), branch `rev-2.0`) is the reference implementation: copy its `tools/`,
`build-sdk.sh` and `src/` layout.

**The SDK is vendored at `vendor/mpc-plugin-builder-sdk`** (a git submodule pinned to one commit): clone this repo with
`git clone --recursive`, or run `git submodule update --init` in an existing clone. In this document `<sdk>` means that folder.
`tools/update_sdk.sh` moves the pin to Akai's latest commit and prints what changed; commit the new pin in a PR.

## Which tool for what (2026-11-13)
New plugins start from Akai's SDK; this repo adds what the SDK doesn't do. The wrapper toolchain stays for the plugins built on it
(most of the catalog, including every outside contributor's) and gets fixes, but no new features unless an SDK plugin needs them too.

| Job | New plugin (Akai SDK) | Existing port (our wrapper) |
|---|---|---|
| Plugin code and VST2 ABI | `<sdk>/framework/mpc_plugin.h` (C++ `mpc::Plugin`) | `wrapper/vst2_wrap.c` + `engine.h` |
| Start a plugin | `<sdk>/tools/new_plugin.py`, `<sdk>/templates/` | copy a port (`docs/PORTING.md`) |
| Build | `<sdk>/tools/build.py` (zig, both CPUs) | `gen_vst.py`, `build_port.sh` |
| Screen skin | `<sdk>/ui-kit`, `gen_ui.py`, `make_knob.py` | Skin Studio: `studio.py`, `html_art.py`, `shadow_skin.py`, `skin_assets.py` |
| Test host | `<sdk>/tools/host-sim` | `host_test.c`, `test_port.sh` |
| Check a folder | `<sdk>/tools/validate.py` | `skin_check.py`, `skin_compat.py` |

**Ours, for both** (the SDK has no equivalent):
- Catalog releases: `release.py` (per-CPU zips, installer, 3.9.1 registration), `vst-release.yml`, `catalog_check.py`, the catalog and site.
- 3.10.1 downloads: `sdk_zip.py` (one folder for every MPC), `folder_zip.py`.
- MPC OS 3.9.1 and earlier: the installer zips, the Plugin Manager, the sticks, `plugin-meta.xml` registration.
- CPU: `bench.sh` / `bench.c` and the Live III fast-core pinning (`wrapper/fast_cores.h`).
- Extras: INSTRUMENTS browser tiles (`xpl.py`), switchable skin themes (`make_skinset.py`), the desktop VST3 build (`build_vst3.sh`, `desktop_skin.py`).
- Renames: `release.py --retire` / `--asset-name`.

To release an SDK-built plugin through the catalog: `tools/sdk_release.py <built folder> --version X --id ID --repo owner/name` turns the folder `<sdk>/tools/build.py` made into the catalog's per-build zips; in CI, `vst-release.yml` with `sdk_project:` builds and does it for you (MPC OS 3.9.1 registration included, SDK layout kept; docs/RELEASING.md). Before it existed, the Dexed 2 and JV-880 2 ports did this by hand (the "catalog route" notes below)
and use `vst-release.yml`, or attach per-CPU zips made with `release.py`.

## 1. Project layout

```
plugin.json            SDK manifest: name, manufacturer, version, kind, uid, file32/file64 (the folder name derives from it)
src/<Name>Plugin.cpp   mpc::Plugin subclass (framework/mpc_plugin.h); an existing engine can be #included and driven
src/*.cpp              every top-level .cpp is compiled; vendored subtrees get one-line wrapper files (MSFA headers had no guards)
src/params_gen.h       generated parameter table (tools/gen_params.py)
params.lock            FROZEN parameter order (index = VST param number = skin binding). Append only.
Content/               shipped data (carts, banks, ROM-free samples), found at <plugin folder>/Content
skins/                 hand-written native skin, GENERATED: TUI.json, Q-Links.json, PNGs (tools/make_tui.py)
tools/make_<x>_skin.py art library: panel, controls, film strips, wordmark, previews (Pillow, drawn from code)
tools/skin_pages.py    one function per tab: draws the static art and emits the widgets at the same coordinates
tools/make_tui.py      assembles TUI.json/Q-Links.json, replacementStrings, reusable components
tools/push_skin.sh     regenerate, rebuild beta, copy Plugin Skins/ (+ .so) to the Force, restart MPC
build-sdk.sh           [test|beta]: gen params, SDK build.py (native + arm32 + arm64), host_sim test, or a beta identity
docs/PLAN-*.md         plan + dated status log (what was checked on which device)
```

## 2. Build, test, install

- `./build-sdk.sh test`: SDK `build.py` (zig) plus `host_sim test`. It must PASS before anything goes to a device.
- `./build-sdk.sh beta`: the same code with its own name and uid ("<Name> 2 Beta", e.g. `FDXB`), via `--define`, so it installs
  **next to** the released version. Always test new major versions this way.
- **Force (3.9.1, arm32)**: the SDK build runs on 3.9.x too. Copy the folder to `/sdcard/Synths/<folder>/`, then add a `<PLUGIN>` entry
  (file `.../VST/<slug>-arm32.so`, uid in hex) to `pluginList-arm` in `/media/az01-internal/Settings/MPC/MPC.settings` with acvs
  stopped. Back up first. Copies are staged (`.tmp` dir + `mv`).
- **Live III (3.10.1, arm64)**: copy the folder to `/nvme/VST2 Plugins/` and restart acvs. No MPC.settings edit.
- **The MPC caches skins: restart acvs after every skin push.**
- Release: keep name/uid/param order for existing users (a normal update). Mention both install routes.

## 3. Plugin-side rules (beyond the SDK's AGENTS.md)

- **Param 0 is nudged by the host on every prepare.** If it can't be harmless (frozen order), debounce it (Dexed: preset applied only
  after ~50 ms stable).
- **Value / momentary / text params:** a momentary verb fires on 0→1 and drops back to 0 (store without re-triggering). A text param shows
  engine text via `formatValue`. A param can be both momentary and text (a tile with a name): momentary wins.
- After anything that changes many values (patch/bank load, page flip), re-read the params and call `requestDisplayUpdate()` once. Never call it
  periodically (it re-lists programs and closes the PRESET menu).
- **Favourites = VST programs**: a fixed count at load (0 = "current patch" no-op, 1..64 = slots), names changed live, applied in `reset()`
  (the audio thread, after setProgram). The voice is stored with its edits in `<plugin folder>/favourites.dat` (write .tmp + rename). Ignore the
  program re-pick right after a state load. ±FAV are momentary params. The installer must keep the file.
- Don't use `mpc::contentPath()`/`pluginFolder()` (a function-local static std::string segfaults at dlclose in the zig/libc++ build). Use
  `mpc::detail::computePluginFolder()`.
- Avoid static destructors in general (generated tables: `static const T& t = *new T{...}`).
- `build.py --debug` makes zig's UBSan trap on intentional integer wraps (e.g. FM phase): use release builds or gcc for debugging.
- Upgrading users: look for their data in the old 1.x places too (e.g. `<old folder>/dx7_carts`, `/sdcard/vst/...`).

## 4. Skin rules (TUI.json), learned on the device

- Every page: one background PNG with all static art (legends, rules, tick scales, numbers, wordmark, windows) plus the live widgets at the same
  coordinates. Fonts only as baked art (licence-clean, e.g. Roboto Condensed OFL). Live text is Roboto/Titillium/Nexa only.
- **Widgets that work on hardware:** `Knob` FilmStrip with `dragOrientation: "Horizontal"` (= horizontal slider) and Vertical (= fader),
  wrapped in a reusable component (focus ring, Q-Link on Mouse Down, knob overlay on double tap / Enter). `Button` toggles and radio groups
  (one group of 32 on a param = a lit-current-item selector). `Image` FilmStrip on a param (LED digits, algorithm pictures; Image scales, so
  use smaller strips). `Label type:"Value"` for live text. `Envelope`.
- **A single (toggle/momentary) Button needs `buttonId: 1`** (lit when step == buttonId); `buttonId: 0` leaves it permanently lit and the first tap only switches it off ("needs two presses"). Radio groups use 0..N-1. Don't set `acceptsHWFocus` Yes on buttons.
- **Choice pick-lists: draw them, the native `MenuOverlay`/`ComboBox` opens EMPTY for an external VST2** (even with `replacementStrings`; the SDK doc is
  wrong for us, see mpc-vst-plugins NOTES.md 2026-09-24). Recipe (verified on a Live III, Welle 2): ONE UI-only param `ui_open` (int 0..N, appended last, not
  an engine key; `kUi`). The field = a Value label (the choice text) under a transparent radio `Button` on `ui_open` (buttonId k+1, numButtonsInGroup N+1).
  The list = a local component (panel image + one radio `Button` per option on the enum, text baked into the images, + an X `Button` buttonId 0 on `ui_open`),
  instanced with `Data` remapped to the enum and `bounds` v2 `additionalInvalidatingHandles: ["IndexedEnabling/<k+1>/<N+1>/Parameter <ui_open>"]`, placed
  LAST on the page. The plugin clears `ui_open` in `parameterChanged` when any choice param changes while open, and tells the host with
  `notifyHostParameterChanged` from a worker thread. Share the component per option list; 40 options = 5 columns.
- **Text/momentary params: the host only re-reads text when the value changes.** Flip a text param's value when its text changes (+ `notifyHostParameterChanged`
  from the worker); for momentary tiles that show names call `requestDisplayUpdate()` once after a list change.
- **Labels on top of Buttons swallow taps** (contrary to the doc). Bake captions into button images, or put the Label *under* a Button whose
  image has a transparent middle.
- `Label type:"Text"` with handle `Invalid` didn't render: bake fixed text into the art.
- **Envelope (`envelopeType: "Pitch"`) is a live, draggable 5-level/4-time EG** (DX7 style): rates via `Invert/`, two handles can share
  one param. Hide its stage letters with `handleTextColour: "00000000"`. Its `fillColour` draws a box, not under the curve, so leave it at
  `00000000`. Make its background transparent and draw a recessed window in the panel art behind it.
- File names must be **exFAT-safe** (no `< > : " / \ | ? *`): `/nvme` and USB sticks reject them.
- A skin that shows **no tabs** failed to parse. Bisect with switches in make_tui (`DEXED_SKIN_TEST=...`) and grep TUI.json for
  `"object at"` (a Python object leaked into the JSON when a module is imported twice).
- **Tabs: one page per tab, separate keys** (familiar from 1.x; keep the same order). The Force shows a 5-key window with
  "<" / ">" keys that slide it along. Sub-tabs (`fnKeySubIndex`) only step **forward** (the dots under the key are indicators, not buttons;
  OP1→OP5 took 4 taps), and the SDK has no "go to tab" action, so don't use sub-tabs to squeeze repeated pages onto one key.
- Assume the **Q-Link bounds overlay is on**: lay controls in blocks of four inside printed panel sections, with one
  `qlinkBoundsData` rectangle per block and `Bank Direction` matching the block shape. The right edge doesn't need to be kept clear.
- Design process: mock up in the art library first (previews in `docs/skin-preview/`), agree the look, then build the real tab and
  screenshot it on the device (`tools/screenshot.sh root@<ip>`).

## 5. Welle 2 lessons (porting an existing engine fast)

- **Order that worked:** (1) `mpc::Plugin` around the engine + generated param table + SDK default skin, test with `host_sim` (`build-sdk.sh test`), beta
  identity on the device; (2) native skin page by page; (3) release build = same name/uid/param order, new version. Plugin first, art second.
- **Engine not built by zig?** (C++20 + C + asm + JUCE): compile the front end with the project's own aarch64 Docker gcc against `-I<sdk>/framework`
  with `-Wl,--version-script=<sdk>/tools/exports.map -static-libstdc++ -static-libgcc`. Beware build scripts that write ONE archive path for x86 and
  aarch64: rebuild the target arch right before linking.
- **Generate the param table** from the engine's own param list + the frozen `params.lock` (fail if the order changed); flags per param
  (momentary / text / float-display / choice). Append UI-only params (e.g. `ui_open`) LAST, flagged so they never reach the engine.
- **Skins from an old layout file:** parse the old per-tab layout (positions of knobs/toggles/popups) and emit native widgets at the same coordinates on
  the old page art (`layout_tab` in Welle's `tools/make_tui.py`); Q-Link blocks = the old 8-key banks as blocks of four, auto rectangle = bounding box,
  >4 blocks -> sub-tabs with the same art. Choice params that were knobs become pick-lists. Redraw film strips from code (the old cap wobbled
  frame to frame): centre them on the BAKED RING's centre (measure the ring's top marker, not its open bottom).
- **Fonts:** only Titillium Web / Roboto / Nexa render; one family for the whole skin looks best (Welle: Titillium Web SemiBold).
- **A tab needs `slider_slot_def()`'s "knob overlay"** in `localComponentDefinitions` for double-tap overlays; dropping the generic pages dropped it.
- **mpc_remote.py `update` REPLACES the folder** (deletes your device-only `Content/roms`): copy ROM/test data into the folder being pushed.
- **Restart cost:** every skin/plugin change needs an acvs restart (loses unsaved work): batch changes, ask first, and say what to look at after.
- **Driving the device from the PC:** `tools/screenshot.sh root@<ip> out.png` is passive and safe; before ANY tap take a screenshot and check the screen
  shows the plugin (after a restart the MPC can be on Select Project, where a tap loads a demo project). With the remote addin, its MCP server is at
  `http://<ip>:6720/mcp`. Content area = 1280x628 starting at y=110 of the 1280x800 screen.
- **Verified on a Live III (Welle 0.2.0, 2026-11-06):** the drawn pick-lists, single-press buttons (`buttonId: 1`), the text-param flip + `requestDisplayUpdate()`
  refresh (performance tiles follow the disk), toggle placement (button centred ~10 px below the knob-row centre so it clears its caption), the red/grey
  knob strips, and favourites in the PRESET menu.
- **Catalog release of an SDK plugin (Welle 0.2.0, 2026-11-11):** the catalog and the Plugin Manager want the `release.py` package
  (`<Name>-<ver>-mpc-aarch64.zip`: `portable/<folder>/`, `mpc-plugin.json`, `install.sh`, `SHA256SUMS`), not the bare `VST2 Plugins` zip. Welle's
  `tools/catalog_release.sh` makes it from the SDK build: stage the folder without `VST/` and `plugin.json`, put the `.so` at the folder root
  (`computePluginFolder()` still finds the folder by its `plugin.json`, so `Content/` resolves the same), write a `pluginlist-entry.xml` with the
  uid as hex (`WeLe` = `57654c65`, the same VST uid; `release.py` writes `plugin.json` with `"uid": "0x57654c65"`), and pass `--user-data` +
  `--user-files` so the manager's web page can upload ROMs (`save_as` the names the engine loads; exact `size` + `signature`/`header_hex` from the real
  files). Then `catalog_check.py --catalog`. Registry entry: `"asset_pattern": "<Name>-*-mpc-aarch64.zip"` for Gen2 only, and `flags_reviewed` for
  the network/exec/dlopen flags JUCE's linked-in `juce_core` raises. Add `tested.json` (device naming "Gen2") on the default branch. Not yet run on a device in this layout.

## 6. Acid 3 lessons (a MIDI generator with an ALSA port, state, and engine-driven params)

- **MIDI out still goes through ALSA.** The SDK discards a plugin's VST MIDI output; a sequencer keeps its `snd_seq` port (one client per instance, "Name", "Name 2"; free the slot on close).
  ALSA means the SDK's zig build cannot be used: compile arm32 (`arm32v7/gcc:11-bullseye`, glibc 2.31 so it also loads on 3.9) and arm64 (`arm64v8/gcc:12-bookworm`) with gcc in Docker against
  `-I<sdk>/framework` and `-Wl,--version-script=<sdk>/tools/exports.map`. Debian's bullseye security repo 404s: install `libasound2-dev` from main only. The validator's "libasound not on the MPC" is
  an error you can leave (both device families have `/usr/lib/libasound.so.2`).
- **Parameter 0 is nudged to 1 and back on every prepare.** If it is a momentary trigger (Acid: GENERATE), fire it only after ~40 ms high (worker thread); a real touch tap still works (verified on a Live III).
- **Params the engine changes by itself** (edit cells, a snapped BLEND, running lights, readouts): flag them "pushed", read them back from a worker thread (10 ms, bounded per pass, round-robin) and
  `notifyHostParameterChanged`. Remember the value the host was last told so a host write is not echoed. **Compute the round-robin start once per pass** (a cursor changed mid-loop skipped cells: caught by the test).
  **Do not clamp a derived value to its parameter's range** when the engine's number is larger (the running light's step 1..32 vs a 0..16 param: steps 17-32 never lit).
- **State:** the framework saves every param by id. Things that are not params (seeds, hand-locked steps) go in `saveExtraState` as text. Params that are derived from the pattern must NOT be written to the
  engine while a state loads (setting them "edits" the steps): wrap `effSetChunk` in a custom `VSTPluginMain` (copy `MPC_PLUGIN_ENTRY_BODY`, then replace `e->dispatcher`) and set a `loading_` flag. The same wrapper
  reads an old wrapper-build text chunk, so existing projects still load.
- **Freeze the order from the released build,** not from a stale lock: Acid's untracked lock lacked two later params. Dump `params.h` + the engine's defaults once into `vst/params.json`, commit it, check `params.lock`.
  Replace the old per-list "open" flags by ONE `ui_open`.
- **Test the front end offline with the real engine** (`tests/run.sh`, ASan/UBSan, a fake host with transport): port the old host test, add a sync hook (`ACID_TEST`) that runs one worker pass synchronously.
  Statistical checks need tolerances wider than the run-to-run spread. `host_sim` needs glibc >= 2.38 and the plugin's runtime libs: run it in an `ubuntu:24.04` container.
- **Skin from an old layout file** worked again: parse `layout.conf`, rebuild the page art from the old module-look PNGs (slab, panels, wordmark) at y-86, bake captions, reuse the old film strips
  (knob 64 frames scaled by radius; NOTE slider 29 frames), draw buttons/lanes/switches, the pick-lists as in section 4. `when=key:value` -> `IndexedEnabling/<i>/<n>/Parameter <key>` on the widget's bounds.
  A step-number legend that follows a switch = a 2-frame Image strip bound to it. A focus ring has a square fill behind a round knob: use a transparent one for knobs.
- **Device flow:** screenshot before touching, ask before each acvs restart; `mpc_remote.py update` per build; if SSH times out, check routing from WSL before blaming the device (it came back on its own).

## 7. More lessons from Dexed 2.0 (UI, parameters, generator)

- **`store()` is silent.** Setting a value without `parameterChanged` (derived or read-back values) does not tell the host: call `requestDisplayUpdate()`
  once afterwards (from the worker thread) or the skin keeps showing the old value. Dexed does this after patch loads, op picks, algorithm changes.
- **UI plumbing params are not automatable.** Picture selectors, "pick" verbs and highlights set `automatable = false` (`mpc::Param::automatable`
  before `addParam`) so they stay out of the host's automation lists. The host honours it.
- **Derived picture params.** A film strip (`Image`) can follow any param: use one derived param per layer (frame = value, last frame blank), e.g. six
  "algo_hl1..6" strips that each light one operator's box. Keep them out of the engine sync/push loops (a `firstVirtual_` index works).
- **Tap-to-pick on a drawing.** The skin cannot send touch coordinates. Draw the picture at a fixed scale on a grid, lay a radio group of invisible
  `Button`s (transparent on/off images) over the cells on one param, and let the plugin map cell -> object (Dexed: algorithm table -> operator). Add one
  spare "resting" value and reset the param to it after each pick, so tapping the same cell twice fires twice. Cells need ~55 x 46 px to be comfortable;
  45 x 38 was too small (so an explicit 1-6 button row won).
- **"Selected operator" proxies** (one OP page whose controls follow `op_sel`) work: proxy params read/write the real `opN_*` params and refresh when the
  selection or the algorithm changes. The cost is automation: a recorded Q-Link move lands on the proxy ("Sel L2"), which follows the selection at playback.
  Dexed shipped with six sub-tabs on one OP key instead (real params, real automation lanes); the proxy build is kept on branch `one-op-page`.
- **Sub-tabs** (`fnKeySubIndex`): one OP key with six sub-tabs is fine for 6 pages; it only steps forward, so you cycle OP1 -> OP6.
- **Style-driven random generators (GENERATE).** Uniform random FM is mostly junk. What worked: keep carrier ratios fixed (coarse 1, fine 0, ratio mode) so
  the key note never moves; carriers always L4 = 0 and R4 >= 40 (L4 > 0 or a very slow release rings on forever); modulators drawn from a per-style ratio and
  level pool with 30 % off-pool choices and 35 % free-shape envelopes for variety; flat pitch EG; light key scaling on about half the operators; MUTATE (vary the
  current voice by AMOUNT) as a separate button; one unconstrained style (CHAOS) with only a "first carrier is audible" guard.
- **Keep install names stable for an in-place upgrade.** The SDK build's `.so` is renamed to the 1.x name (`dx7_dexed.so`) in the per-build release zips, so
  the old installer, the plugin-list entry and the user's data folder (`dx7_carts`, `favourites.dat`) carry over; `tools/release.py` is run once per arch
  (armv7, aarch64) with `--extra <Content/carts>:dx7_carts --user-data dx7_carts`. `tools/sdk_zip.py` then builds the one-folder SDK zip from the pair.
- **Don't let a full `/tmp` surprise you.** A tmpfs `/tmp` that fills (old scratch worktrees) makes tools print truncated output; builds that write elsewhere
  keep working, but check `df /tmp` when output looks cut off.


## 8. Crate Digger 2 lessons (a streaming player with an external engine, text readouts and result lists)

Port of [sd88me/mpc-vst-cratedigger](https://github.com/sd88me/mpc-vst-cratedigger) (branch `rev-sdk`, 2.0.0).

- **Freeze first, then append.** 103 params from the 1.x build stay in order (`vst/params.lock`); the port only appended three (`level_l`, `level_r`, `play_state`).
  Meters and status lamps are plain int params the worker updates; a skin film strip (`Image`, 11 frames) follows each. No new engine API needed.
- **Text readouts are int params whose value text is the display.** Override `formatValue`; bump a counter param (store + `notifyHostParameterChanged`) when the text
  changes and call `requestDisplayUpdate()` once per pass. Wrap `effGetParamDisplay` in `VSTPluginMain` if the text must exceed the SDK's 31 characters (the MPC reads 99).
- **The host nudges parameter 0 on every prepare.** If param 0 is a stepper (Crate Digger: genre), apply a change only after it has held ~40 ms (worker), or the style list resets.
- **A tap on a lit toggle arrives as two events ~130 ms apart.** Flip once per tap and ignore events inside ~300 ms; radio groups (SOURCE) follow the value.
- **External engine bundle (yt-dlp, Python, ffmpeg, ~125/180 MB):** ship it in `Content/engine`, find it with `mpc::contentPath`, run it from the plugin folder.
  The Live III's exFAT NVMe ran it in place (exec bits are honoured), so no copy was needed; keep a first-run copy to an internal folder as a fallback (untested) and still
  look for a 1.x install first. Load the engine on the worker thread, never in the constructor (it takes seconds). Two zips (armv7, aarch64) because each carries its own bundle.
- **Both firmware families still need an install path.** The SDK folder only loads on firmware that scans `VST2 Plugins`. For the Force (MPC OS 3.9) ship `install-force.sh`
  + `plugin_list.awk` in the armv7 zip: stop acvs, stage the folder, restore exec bits from a `MODES` list (zips from Python/Windows lose them), keep the user's `channels.txt`,
  edit `pluginList-arm` (same uid replaces a 1.x entry), restart. Test it offline with `MPC_INSTALL_TEST=1 MPC_SETTINGS=<copy>` before the device.
  Removing a beta from the Force = acvs stopped, `plugin_list.awk` in `mode=remove`, delete the folder.
- **Package with Python zipfile, not `zip`** (not installed, and it must set `external_attr` modes): `tools/package.py`; refuse a beta build; add INSTALL.txt, licences, example config.
- **Catalog:** these simple zips (`-VST2-Plugins-<cpu>.zip`) are NOT what the catalog reads (`-mpc-armv7.zip` / `-mpc-aarch64.zip` from `tools/release.py`, plus `-mpc-sdk.zip` from
  `tools/sdk_zip.py`, see Dexed in section 7). Decide before publishing which path the plugin takes.
- **Skin text on the real MPC:** a Label draws text at about 0.66 x its TUI height; only Titillium Web, Roboto and Nexa exist, so preview with Titillium at that scale
  (`tools/make_tui.py --preview`) or the render and the device disagree. The MPC draws a "Q" tag at the top-left corner of the active Q-Link group: centre panel titles.
  Neighbouring touch areas must not overlap (keys get an ~8 px pad, no more). One main LCD per tab that mirrors the controls under it (same columns, numbers, names) reads best.
- **Look development.** Mock-ups first, one directed change at a time (Crate Digger went rack sampler -> MPC60): keep the primitives (`tools/mockup_skin.py`: lcd, key, knob, plate) separate
  from the page layouts so a restyle is one file; keep earlier looks as pictures in subfolders. Keys: no drawn border; a darker side wall, a gradient cap and a specular edge read as real.
- **Disk:** on a machine where `/tmp` is a small RAM disk, put downloads and engine bundles under the repo's ignored `build/`.

## 9. Mini-JV lessons (a Schwung engine that owns its state and changes its own values)

Port of [sd88me/mpc-vst-jv880](https://github.com/sd88me/mpc-vst-jv880) (branch `rev-sdk`, 2.0.0). The engine is a Schwung `plugin_api_v2` module that 1.x drove through
mpc-vst-plugins' generic wrapper (`wrapper/vst2_wrap.c`, `adapters/schwung`).

- **Freeze from the wrapper build's own table.** `gen_vst.py <vst.json> --params-h` on the release tag gives the exact VST order; it held two
  popup "open" flags after the 470 keys in `params.lock`. Dump it once into a committed `params.json` with the behaviour flags (momentary, text,
  `step_of`, `popup_of`, `nudge_pct`) and the CC/NRPN map, and generate the SDK table from that. The popup flags become `ui_open` + an inert reserved
  slot, so nothing after them moves.
- **The wrapper read values from the engine on every `getParameter`; the SDK caches them.** An engine that changes its own values (a patch load
  rewrites ~200 NVRAM params, bank steps, macros) needs a worker that reads them back: a cheap "patch signature" (mode, preset, patch name, loading)
  every 10 ms triggers a full sweep, otherwise round-robin (24 per pass). `display_rev` only covered the envelope view here: don't rely on it.
- **Schwung verbs fire on ANY value** (`prev_bank` acts on "0" too): send momentaries on the rising edge only. List tiles (momentary + text) are
  lit from `<key>_on` and stay lit when the host toggles them off; check the tile flag before any "text param: nothing to send" early return.
- **Settle where the host can't see it.** The 1.x Q-Link settle and PATCH's "any move up to 10 % = one step" change the value the host set;
  doing that in `parameterChanged` fails host_sim's sweep round-trip. Send the settled value to the engine and let the worker report it (~10 ms).
- **State = the engine's JSON as extra state**, engine writes suppressed while the SDK restores values (wrapped `effSetChunk`), a plain-JSON chunk is
  a 1.x project. The MPC hands over the chunk before the ROMs have loaded: keep the last state given and return it from a save until the engine
  answers (the engine refuses `state` while loading).
- **Test with a chunk the released build made itself:** `tests/make_legacy_chunk.sh` builds the tag's wrapper + engine for x86, drives it like a
  host and saves `effGetChunk`. Don't commit it when it carries ROM-derived bytes (factory patch data); regenerate it from the user's ROMs.
- **host_sim is not real time.** An engine with its own emulator thread and a background ROM load stays silent there (WARN, expected); check sound in
  the paced offline test. UBSan flags the vendored core's intentional left shifts of negative samples: `-fno-sanitize=shift` for those files only.
- **zig's arm32 target has NEON** (cortex_a17) where the 1.x gcc armv7 build had none: `vaddvq_f32` (AArch64 only) broke the build; pair the halves
  instead. Check the arm32 `.so`'s newest `GLIBC_` symbol in the build script (2.27 here, under the Force's 2.29) rather than trusting the target name (2.31).
- A front-end-only momentary (no engine key) still needs a 0..1 range. With min = max = 0, every press arrives as plain 0, which reads as a release, so it never fires. JV-880 random and favourite tiles hit this.
- Front-end functions (random patch, favourites) get their own flag (`kLocal`) and are handled before the engine path. Favourites shared across projects live in a file in the plugin folder, written to a temp file and then renamed.
- MPC skin Buttons toggle: a tap on a lit button sends 0. A front-end function that must fire on every tap has to treat any change as a tap, or use a momentary that the plugin drops back to 0.
- Do not read a value back to the host while it is being dragged (envelope handles, LCD cells): skip readback for about 300 ms after the last host change. Otherwise the readback pulls the handle under the finger and the drag "never registers".
- Envelope widget: highlightColour paints the active stage as a solid block, so keep it faint. Style the pressed handle in the skin palette, not the default red.
- Favourites in the PRESET list (programs) work for a state-JSON engine: store the engine state per slot and apply it from the worker when currentProgram() changes. Ignore the program re-pick for about 2 s after a project load.
- `mpc_remote.py update` replaces the whole plugin folder, so copy user files kept there (favourites) aside and back. Remove `:Zone.Identifier` files before the copy; tar on the device rejects them.
- On a 3.9.x Force, an SDK beta installs as in Crate Digger 2: tar the folder into /sdcard/Synths, then register it in MPC.settings (pluginList-arm, uid as lowercase hex) with the MPC app stopped.
- Release via the catalog route, as Dexed does (`tools/package_catalog.sh` in mpc-vst-jv880). Per CPU:
  - Stage the SDK build folder with the `.so` renamed to the 1.x name at the folder root, and a plugin.json with only that CPU's `file32`/`file64`. The SDK finds the plugin folder by `plugin.json`, so the root `.so` works.
  - Run `release.py` with `--user-data` for every user folder and file. It takes files too, e.g. `jv880-favourites.dat`.
  - Then run `sdk_zip.py` on the pair.
  - The canonical `install.sh` then upgrades a 1.x install in place, and `catalog_check --catalog --require-portable` passes.
- The skin generated with the SDK widgets (Envelope, Button v2, overlays) makes `release.py` mark the build "MPC OS 3.x only". A 1.x that was listed for 2.x loses 2.x: say so in the release notes.
- To reload a test project without restarting the MPC app, open it again from the BROWSER: that makes a fresh plugin instance.

## 10. Euclid lessons (a MIDI sequencer: the engine inside the plugin)
Euclidier 1.x ran its sequencer as a child process (clock over ALSA in, notes over ALSA out, settings over a socket). Euclid 2 runs it inside the plugin. Measured on the Live III, 33 s at 128 BPM, same notes both ways: clock drift -288 ppm -> 31 ppm, spread off the grid 3.2 -> 2.7 ms (one audio block is 2.9 ms: the floor), shortest gate 48 -> 64 ms, 383 -> 385 note-offs for 385 note-ons, no stray controller on Stop. Rules from it:
- **Make the engine a library, not a program.** No threads, I/O, allocation or globals after construction; the audio thread owns it. The UI/worker thread sends commands through a lock-free ring and reads the values back from a snapshot the audio thread publishes after every block; a `busy_` flag lets the worker do the same work itself when no audio is running (a host that is not processing), so a parameter change never waits for a block that does not come. Anything with global state (the Bjorklund generator) runs once at start into a table.
- **Time is samples.** Note lengths are `now + samples`, and a note-off due inside a block goes out before that block's note-ons. The clock is made from the host's song position (`vst/transport_grid.h` in Euclidier, Acid and Maze): the block's 24-PPQN ticks are sent at the block's start (look-ahead, never a block late), cut at the loop end (`cycleStartPpq`/`cycleEndPpq`), re-anchored with a Song Position Pointer at Start, at every wrap or locate and once a bar, and a small backward step in `ppqPos` (a tempo change) holds the high-water mark so nothing plays twice.
- **Notes still leave through ALSA.** The MPC discards a plugin's VST MIDI output. Open one sequencer client per instance (lowest free slot: "Name", "Name 2"), one port, and write the block's messages at its start with `snd_seq_event_output_direct` to the subscribers. The port name is what saved projects route from: renaming a plugin renames the port, and the user must pick the input again.
- **Never leave a note hanging.** Send the note-off to the channel the note-on used (a channel change under a sounding note), end a lane's sounding note before it fires again, kill everything on Stop, and release before a value that changes the note (gate, channel, mode, enable). The standalone engine sent a controller instead of a note-off for DRUM-mode lanes: check what the old code really does before copying its "kill hanging notes".
- **Prove it three ways before the device.** (1) An oracle test: step the new core and the old class with the same values and ticks (1.5 M ticks, random lanes) and compare every note. (2) A bookkeeping run (400 k blocks with edits, tempo changes, locates, randomise, stops): every on has an off, none overlap, nothing after Stop. (3) An end-to-end test through the fake host with a hook that hands over every message with its block: each note must sit in the block that holds its 16th, each lane's hits counted against a model written independently of the code (steady, loop with 30 wraps, start mid-16th, tempo changes with a step back, locates, 25 stop/starts, edits). On the device record the port with `arecordmidi -p <client>:0 -b 60 -t 10000 f.mid` (one tick = 0.1 ms) and fit the 16th grid (drift, sd, worst, gate lengths): do it before and after a change.
- **Initial values must be the real defaults.** The host reads parameter values at start and writes parameter 0 back after loading: a default that differs from what the code really holds (here lane 1 off vs on) is applied. Put the defaults in the generated table and in the constructor, before any thread.
- **The parameter order is frozen: append.** A new choice (lane keys 1-8 + ALL) is a new parameter (`view`), not a bigger `sel`; a saved pattern is a flag plus two 16-bit halves (a float holds 24 bits exactly), hidden and not automatable. On load, order the writes: selection first, modes, everything else, and a saved pattern last (steps, fill and shift rebuild the pattern and would erase it).
- **Display parameters are the plugin's.** Undo a write from outside (restore the value and let the worker tell the host), except while a project loads.
- **Relative controls over an absolute parameter (an ALL view).** Keep the existing parameters: while ALL is on, the knob shows the mean of the lanes, a change adds `plain - told_[index]` to an atomic accumulator, and the worker applies it to every lane from the snapshot (clamped); controls that make no sense in ALL are put back to the lane's value. No second set of parameters, so the Q-Link map stays static.
- **Skin: conditional visibility.** `additionalInvalidatingHandles` in a version-2 `bounds`: `IndexedEnabling/<i>/<n>/Parameter k` or `RangedEnabling/<min>/<max>/<n>/Parameter k` on a choice parameter (`n` = its number of strings in `replacementStrings`, so the parameter must be bound somewhere). **`NonZero/<param>` did not hide anything on the Live III.** Page art cannot hide: draw captions that must follow the view as Image widgets.
- **Skin: tapping a display.** Four things found on the device: a fully transparent picture has no touch area (alpha 3 is enough and invisible); buttons stacked over each other give the tap to the topmost, so overlays for several size classes starve the ones below; a button bound to a display parameter that writes a value is not a reliable "tap" (a hidden cell swallows it); and the log tells you which of these happened, so put one `ELOG` in the path before guessing. What works: dedicated momentary parameters, a button each, none overlapping (32 slices per lane row, 32 sectors round a circle), and the plugin maps slice or sector to the step it knows is drawn there.
- **Installer for a rename or a replacement.** Same uid, new folder name: the installer must remove the old folder and its plugin-list entry (by uid, from `MPC.settings`, and only a folder named like the old one that holds the old plugin's files), keep the user's data (a preset bank) by moving it, back up first, and be safe to run twice. `ls a* b*` exits non-zero if either glob is empty: test with a loop over `[ -e ]`. Test it offline against a fake device (`tools/test_installers.sh`), including a decoy folder, a second plugin and a beta; the first run found the `ls` bug. Through the catalog (Plugin Manager runs the zip's own `install.sh`, which replaces the plugin-list entry by uid but leaves a renamed folder behind) use `release.py --retire "<vendor> - VST - <old name>"`: the installer removes that folder only if its `mpc-plugin.json` has this plugin's id, and moves the `--user-data` files across (`tools/package_catalog.sh` and `tools/test_catalog_install.sh` in Euclid). Guard deletes with `rm -rf "${VAR:?}"` so an empty variable can never become `rm -rf /x`.
- **Device sessions.** Every MPC restart closes the open project, and every skin or plugin change needs one: agree on restarts with whoever is using the device, batch changes between them, and log what you need to know instead of screenshotting every step.

## 11. Profit-08 lessons (a 1.x wrapper build with a string-API engine and hidden popup flags)
- **Freeze the table the released build really had, not the one in the source.** The 0.1.3 VST table was the 245 keys plus one hidden `<key>__open` flag per popup its layout declared (37), 282 in all. Check the released `.so`/`params.json`, then keep those indices: one real `ui_open` parameter at the first popup index, the rest inert reserved slots, new parameters appended after. `params.lock` from the released build is the test.
- **One `ui_open` parameter replaces many popup flags.** Pick-lists are drawn by the plugin: a Button per option, shown with `IndexedEnabling` on `ui_open`. Opening a list is a parameter change, so the preset shows "*"; accept it or filter it from the dirty check.
- **Q-Links are at most four groups (16) per tab or sub-tab.** Pages with more controls keep their art and get sub-tabs that only move the Q-Link frames. The tab key shows the current sub-tab's name after a second press, and the first sub-tab's name when you arrive from another tab.
- **Compile a C engine as C++ only if you must.** The SDK build compiles top-level `src/*.cpp` only. Add explicit casts for `void*` conversions, keep the C initialiser valid in both, and include the `.c` files from one `.cpp` unit. Keep the original `.c` building as C for the 1.x wrapper.
- **Old state still loads.** A legacy 1.x chunk is the engine's plain state string. Wrap `effSetChunk` so it accepts both the SDK's wrapped chunk and the legacy one, and keep a real legacy chunk in `tests/data` (build it from the released tag's engine, not from the new code) so the host test proves it.
- **String-API engine: read back on a revision counter.** The engine bumps `display_rev` on large changes (program, layer, bank); the worker re-reads all values then, and skips readback for a short time after a host write so a drag is not pulled from under the finger.
- **Tile lists (banks, programs).** Momentary parameters, one per tile, with `_on` readback for the lit tile; page through them with small `<` `>` keys, not an LCD stepper. Program tiles must be full size or they are unreadable on the 7" screen.
- **Favourites as VST programs.** Slot 0 is "Current program", 1..N are slots stored in a file in the plugin folder; +FAV and -FAV are plugin-local momentary parameters on the BANKS page. They are not in the 1.x table, so they are appended.
- **A big `.syx` program file can be two banks.** A 128-program-per-file dump loads as banks "name 1" and "name 2": expect "N banks" to be higher than the file count. Banks are scanned at plugin load only: remove and re-add the plugin after copying files.
- **Device handling.** On this build the plugin and preset fields need a double tap to open their list. With more than one device on the network, check which one a remote tool drives (`device_status`: CPU architecture, IP) before tapping anything. On-device CPU of the MPC process is dominated by screen redraw (50-70% of one core idle or playing), so bench the render offline with host_sim instead.
- **Knob art and its frame must agree.** Making the knob bigger (68 to 78 px) also means the film-strip frame and the widget bounds, or the value arc clips on one side; the Q focus rectangle needs a few px more. Change the drawn size, the strip frame, the widget and the preview crop together, then look at a real knob with a high value.
- **Wheel on a long range.** The data wheel moves the host value by more than one step per click on a 0..127 parameter, so "round toward the move" skips presets. For a readout the wheel owns (program, bank), turn a small off-step move into exactly one step in its direction; drags and Q-Links still land where they point.
- **A default that depends on the CPU.** A quality or oversampling default can differ per build (`__aarch64__`): set it in the engine and in the parameter table's default from one macro, and state it in the release notes. Saved projects keep their own value.
- **Force beta by hand.** `mpc_remote.py` does not know the Force's `/sdcard/Synths` layout: tar the folder there, stop the app (`systemctl stop acvs`), add the plugin's entry to `MPC.settings` (uid as hex, here 50667442 for PftB; keep a backup), start it. Remove a beta the same way, and remove its entry too.
- **Deploying over a loaded plugin.** Upload the new folder under a temporary name and swap by rename, never overwrite a loaded `.so`; copy `Preset_Banks` and the favourites file across first. Quote paths with spaces through tar over ssh: an `scp` with nested quotes failed silently, the swap still ran and left an empty install until it was uploaded again. Check the result with `ls` and `md5sum` before moving on.
- **Release path for a port that upgrades a 1.x plugin.** Same name, uid and folder, `.so` renamed to the 1.x name (`profit_08.so`), `tools/package_catalog.sh` (Profit-08, after Dexed and JV-880) builds the two per-CPU catalog zips through `release.py` with `--user-data Preset_Banks --user-data profit08-favourites.dat`; the simple one-folder zip from the SDK's `package.py` fails `catalog_check` (it wants `mpc-plugin.json`). Repacking that zip under one top folder does not fix it.
- **Order of release steps.** Attach the per-CPU zips to the GitHub release before opening the catalog PR: its `build` check (`--require-portable`) looks for `*-mpc-armv7.zip` on the listing's release and fails with "found 0" otherwise (re-run the failed job after uploading).
- **Never publish a release (or un-draft one) without the catalog zips.** The nightly catalog build reads only `<Name>-<ver>-mpc-armv7.zip` / `-mpc-aarch64.zip`; a release with just the simple `-VST2-Plugins` zips is excluded and a bot issue opens ("expected one asset matching *-mpc-armv7.zip, found 0", Crate Digger 2.0.0 as #817, 2026-11-12), while the catalog keeps the old version. Before un-drafting: build the catalog zips (`tools/package_catalog.sh` per plugin; `tools/repack_catalog.py` for the converted TR-MPC family, which bumps the version in the previous catalog zips), run `catalog_check.py --catalog` on each, upload them, then publish. After publishing, confirm the next nightly run lists the new version. The issue closes itself once a release passes.
- **Git hygiene for the catalog PR.** The post-launch branch carries unrelated local commits: make a temporary worktree from `origin/main`, cherry-pick the one catalog commit, push that branch and open the PR from it.

## 12. Lucky Dip lessons (a string-key engine, a sample scanner, a skin restyled in rounds)
Port of [sd88me/mpc-vst-luckydip](https://github.com/sd88me/mpc-vst-luckydip) (branch `rev-sdk`, 1.0.0, 2026-11-12). The engine is the 1.x wrapper's string-key `mpc_engine_t`; the front end is
Profit-08's, trimmed (no favourites, no `display_rev`).
- **Check the released table first:** `gen_vst.py vst/vst.json --params-h` on the tag gave exactly the 161 keys of `params.lock` (no popup flags), so
  nothing reserved was needed. The 1.x chunk is the engine's state text plus its NUL (`vst2_wrap.c` effGetChunk); make a legacy chunk with the
  tag's engine (`tests/make_legacy_chunk.sh` + `legacy_dump.cpp`, a sample library at a fixed path the host test recreates).
- **Include a C engine header with C linkage.** `engine.h` has no `extern "C"` guards: the front end then looked for a mangled `mpc_engine()` and
  host_sim's load failed with an undefined symbol.
- **Any function-local static object crashes at dlclose** in the zig/libc++ build, not just `std::string`: the engine's shared library state
  (`static Shared s`) did it in host_sim's teardown. `static T& t = *new T;`.
- **`std::condition_variable::wait_for` needs glibc 2.30 in zig's libc++** (`pthread_cond_clockwait`): the arm32 `.so` stopped loading on the
  Force's 2.29. `wait_until(system_clock::now() + d)` uses `pthread_cond_timedwait`. The build's glibc check caught it.
- **Find user drives from `/proc/mounts`.** The Live III's `/media` is an empty tmpfs and has no `/sdcard`; the user drive is `/nvme` (exFAT, rw;
  `/synths` is the read-only factory part). Keep writable disk filesystems (vfat/exfat/ntfs/ext4...) mounted under `/media`, `/mnt`, `/run/media`,
  `/nvme`, `/sdcard`, drop the system mounts (`acvs-synths`, `az01-internal`), de-duplicate by `realpath`. Show folders as `Internal / Expansions`.
- **A lock-free engine render needs no front-end lock on the audio thread.** Profit-08 locks every engine call; Lucky Dip's render and MIDI are
  atomics, so only get/set take the lock (a GENERATE no longer holds audio). An engine fixed at 44.1 kHz gets a linear resampler in `process()`
  when `prepare()` says otherwise (checked in the host test by the period of a square wave).
- **Drawn pick-list with live text (verified on a Live III and a Force):** one `ui_open` *choice* param (so `RangedEnabling/1/4/5/Parameter n`
  can show the list for any of four fields; it must be in `replacementStrings`), 12 text params for the rows (Labels *under* transparent radio
  Buttons on one `list_sel` param, value = the lit row, `kListRows` = none), PREV/NEXT momentaries, a full-page dim Image-button on `ui_open`
  buttonId 0 underneath the panel (a tap outside closes). The front end fills the rows from engine keys (`fl_count/fl_cur/fl_item/fl_pick`),
  closes the list on a pick and after every state load.
- **Derived picture params:** `pad1_kind..pad16_kind, sel_kind` (0 empty, 1..23 category, 24 missing), worker-computed from the engine's text, drive
  25-frame `Image` strips (category bands on the tiles, colour bars under PAD EDIT's pad grid).
- **Pillow: `ImageDraw` on an RGBA image overwrites alpha, it does not blend.** Translucent fills (the 0.9 tiles' 34 % inner tint, sheens,
  confetti) came out solid and made the skin look dark; draw translucent shapes on a layer and `alpha_composite` it.
- **Restyling:** a from-scratch redesign was liked for its layout but not its colours; what stuck was the new layout + the old
  palette + the redesign's card/key shapes, with text wells as a deeper shade of their own card (tone-on-tone, white text). Render variants of
  one page side by side (`LD_WELL=cream|tone|glass`, `LD_SKIN_OUT` / `LD_PREVIEW` so runs don't share `skins/`) rather than describing them.
- **Release:** catalog zips as Profit-08 (`tools/package_catalog.sh`, `.so` renamed to the 1.x `lucky_dip.so`, `--user-data luckydip`);
  `release.py` marks it "3.x only" (Button v2 etc.), where 0.9.1 was also listed for 2.x: said so in the notes. `tools/scrub_zip.py` rewrites
  the canonical `install.sh` comments that name firmware versions and refreshes `SHA256SUMS` (modes kept); `catalog_check --require-portable` then
  passes with the expected "differs from template" warning.
- **Release zip installed with its own `install.sh` (2026-11-12, both devices, over 0.9.2):** Live III with `-t "/nvme/VST2 Plugins"` (where the
  0.9 folder was), Force with the default `/sdcard/Synths`. Both upgraded in place, kept `luckydip/index.cache`, backed up `MPC.settings` and
  restarted MPC; installed `.so` and `TUI.json` md5 = the build. On the Live III's exFAT, `cp -a` prints one "can't preserve ownership" line per file:
  harmless noise (exFAT has no owners), the copy is complete.

## 13. Xenia lessons (an emulator engine with its own thread, a fixed 44.1 kHz ring, and MIDI traffic of its own)
Port of [sd88me/mpc-vst-xenia](https://github.com/sd88me/mpc-vst-xenia) (branch `rev-sdk`, from v0.1.2). Gen2 (arm64) only, as released: the DSP56300 JIT has no 32-bit ARM backend.
- **The local clone can lack the release tag.** The catalog listed 0.1.2, the clone only had v0.1.1: `git fetch --tags` before deciding which commit is the release (here v0.1.2 = the default branch's head).
- **79 popups = 79 hidden slots.** `params.lock` held 253 keys but `gen_vst.py --params-h` on the tag gave 332 (the wrapper's `<key>__open` flags). Same recipe as Profit-08:
  write `params.lock` as the released 332 names, generate `ui_open` + reserved slots from it, append new keys after.
- **Options vs labels.** The 1.x wrapper sends options as index strings, but this engine matched one family (the PLAY knob targets) by label and answered with
  the label: send and read those by label in the front end, index everywhere else. Check `eng_set` per option family, not just the generic path.
- **A fixed-rate ring engine:** render only drains a lock-free ring, so the audio thread needs no front-end lock; a linear resampler in `renderOut` covers a
  host that is not at 44.1 kHz (tested at 48 kHz).
- **Run the real-time host test several times.** A single pass hid an engine bug; looping it (6-8 runs) showed notes intermittently lost or hung. Cause: after
  every boot the engine asked the emulated synth for all 200 ROM sounds (to show their names), ~4 s of sysex dumps over the emulated 31250 baud UART, and notes
  sent meanwhile were dropped. Waiting for the scan to finish made 6/6 runs pass, which proved it. Fix: cache the names per ROM (hash of the ROM bytes) in the data
  folder, scan only once per ROM, and pause the scan for ~2 s after any note. Instrument the engine (note arrival vs first non-zero output) before theorising:
  the first theory (edit-buffer dump not taken) was wrong.
- **A project's state can arrive before an emulator boots.** Hold a state dump until the firmware first answers; and return the last given state from a save
  until the engine is ready (else the saved sound is lost).
- **Engine logs on stdout break `host_sim dump`:** parse the first JSON object out of the output (`build-sdk.sh test`). `validate.py` refuses `<uiversion>` above
  `0.1.0.0` (the plugin would not load); `file32` missing is expected for an arm64-only plugin.
- **Random patch for a ROM synth:** an engine verb (`random_sound`) that picks among the slots the engine knows to hold a sound, across every loaded bank, never
  the current one, so user banks added later are included without front-end changes.
- **User SysEx banks:** MIDI files carry the dumps as SMF sysex events (`F0 <varlen> 3E 0E ...`), so scan `.mid` as well as `.syx` and parse each dump by its
  header and checksum (XT single: 265 bytes; all-singles bundle: 256 x 256). microQ dumps (`3E 10`) look similar and must be skipped. Name a bank after its
  folder when the folder holds one file. The BANKS tile page follows the current bank, so a test that steps tiles must first put the bank on page 1.
- **Two devices, two remotes:** each device's remote addin is its own JSON-RPC endpoint (`http://<ip>:6720/mcp`); check which device a tool drives
  before tapping. Screenshot before every tap; the plugin field needs a double tap; scroll the plugin list in small steps.
- **A user-switchable skin in one TUI.json:** the SDK loads one skin per folder, so build every page twice and add `IndexedEnabling/<0|1>/2/Parameter <skin>`
  to each entry's `additionalInvalidatingHandles` (on top of any pick-list handles). The setting is a local parameter kept in a file in the plugin folder: a
  project load must not override it (the worker puts the global value back) and other instances re-read the file about once a second. The doubled TUI
  (3 MB) opens in about the same time on a Live III. A second colourway of the same art is a PIL `Color3DLUT` (split each colour into the art's base colours,
  rebuild from the new ones; zero small shares to kill the panel grain).
- **Skin details found on the device:** a round key's LED drawn at the image's top edge is cut off; leave room above the cap. Labels draw smaller on the MPC
  than in an offline preview of the same font size, so judge text size on the device.
- **Tools move:** when the SDK checkout moved (before it was vendored here), the build failed quietly and the stale plugin folder nearly went out; check the build's timestamp before a push,
  and build the SDK's `host_sim` on demand (`.sdk-tools/` is a local cache that a fresh checkout lacks).
- **Release with a legacy installer:** keep the .so at the folder root as in 1.x with plugin.json naming it: `computePluginFolder()` stops at the folder that has
  plugin.json, so the plugin's data folder resolves the same in the install.sh layout and the SDK zip. List every user file as `--user-data`
  (ROMs, banks, favourites, the skin setting) so an upgrade keeps it.

## 14. Profit-5 lessons (a string-key engine whose PRESET menu is the engine's own program list)
Port of [sd88me/mpc-vst-profit5](https://github.com/sd88me/mpc-vst-profit5) (branch `rev-sdk`, 2026-11-13). The front end is Profit-08's, trimmed (no layers, favourites or pick-lists).
- **Check the released table with the tools commit the release workflow pinned**, not the current tools: `tests/check_lock.sh` checks out that commit
  (`tools_ref` in the tag's `release.yml`) in a temporary worktree and runs `gen_vst.py --params-h` on `git archive <tag>`. Profit-5 0.2.1: 64 keys, no hidden slots.
- **Make one legacy chunk per state format, from each tag's own engine**: 0.1.1, 0.2.0 and 0.2.1, and one each with an imported patch picked (a synthetic
  patch from the repo's test fixture, so nothing third-party is committed). The 1.x wrapper with `MODULE_SUBDIR` passed `<folder>/<subdir>` to `create()`,
  not the folder: the first "user" chunk silently held "U01 Init" until the dump tool was given the subdir.
- **When the 1.x build exposed the engine's slots as VST programs** (vst.json `"programs": {"param": "preset"}`), keep that instead of favourites: one
  `addProgram` per `"preset:<n>"` name at load, wrap `effSetProgram` to set the engine's program (skipped for ~2 s after a state load, as the host re-picks
  the saved program), and set `currentProgram_` from the engine's program in the worker so the menu follows the knob, the arrows and MIDI program changes.
- **Several C files of one engine in one C++ unit** can clash on file-static helpers (`clampf` in two files): `#define clampf p5_import_clampf` around
  one `#include`. A generated `_Static_assert` needs a `static_assert` branch under `__cplusplus`. Keep `-std=gnu11` for the C build (`lstat`, `strdup`).
- **Worker readback race (also in Profit-08's front end):** the worker checked "host idle for 300 ms", read the engine, then stored the value; a host write
  between those overwrote it (host_sim's sweep: "set 0.43015 but getParameter returned 1.00000", about one run in three). Load the host's last-write
  time before the engine read and compare it again just before the store.
- **Float pots** (0..10 knobs of a 1.x `min/max` param with no `display`) go in as `floatParam` and are sent as `%.6g`; only whole numbers and options use the
  settle-toward-the-move rule.
- **Beta host test:** the beta needs its uid in the test build too (`-DP5_UID=0x50,0x66,0x35,0x42` from `P5_UID_HEX`), not just its name.
- **A 1.x skin drawn as native assets already** (full-page art, 128-frame square knob strips, on/off caps) needs no redrawing: copy the PNGs and put
  `Knob` FilmStrip / `Button` widgets at the layout.conf positions (y - 86). With only two-option choices there are no pick-lists, so no `ui_open`.
- **Check Q-Link frames offline:** `make_tui.py` asserts that the frames of one sub-tab do not overlap and lists parameters on no Q-Link, and `--preview`
  draws each sub-tab's frames on the art. Give a switch's frame room for its caption above AND below (the art puts some captions under the cap).
- **A program readout that takes the wheel:** a focusable component with a Value label on the `preset` parameter, whose `formatValue` returns the slot name.
- **Deploying a final release zip by hand on a Live III** (2026-11-13): the Plugin Manager layout is the zip's `portable/<folder>/` contents with the `.so` at the
  root. Upload it under `VST2 Plugins/.stage/`, copy the user data across, move the old folder out of `VST2 Plugins` (a backup folder on the drive) and the new one
  in, compare md5 sums with the zip. A beta's favourites file was already gone: `mpc_remote.py update` replaced the beta folder on the previous deploy.

## 15. Stepped controls: patch and bank on Q-Links and the data wheel (JV-880 2.0.1, Dexed 2.0.3, Xenia 1.0.1 / 1.0.2, 2026-11-13)

How the host moves a value, measured on an MPC One+ and a Live III:
- **A Q-Link or data-wheel tick moves the host's value by 1/128 of the range.** On PATCH 0..8191 that is 64 patches, on 0..255 two sounds, on
  0..63 half a bank. A stepped value (patch, bank, cart) needs its own rule for "one step a tick"; `settle()` alone skips or needs two clicks.
- **The host's value runs ahead during a turn.** The worker holds its read-back 300 ms while the host keeps moving a param, so the host's value
  moves a tick's worth per click while the engine moves one step. Judging a move by its distance from the engine then turns a fast turn into a
  jump (JV-880: into another bank), and judging the direction from the engine makes turning back step the wrong way (Dexed's cart). Judge it from
  the host's own previous position while it is ahead (JV-880 `ahead_`/`lastPos_`); "a turn" by state, not by timing alone (two quick
  automation points or an NRPN MSB + LSB look like a turn by timing).
- **After a read-back some controls carry on from their own old value** (~30 % of ticks on the One+, no timing pattern; found with a trace of
  4,800 moves). Remember it (`stale_`) and take the move from whichever start fits; forget it on any direct set, or a later drag near the old
  value is taken as a tick.
- **The ends of the range.** The host's value reaches 0 or the top long before the patch does, then sends nothing more, so the turn stalls (Dexed:
  the cart number sits at the bottom of 0..8191 and every fast click down was lost). During a turn an end is one more step and the worker sends
  the engine's value back at once (no hold); a set to an end on its own is that value (a reset to default, automation).
- **MIDI CC / NRPN sets are always values** (mark them, `t_absolute`); never infer a step from them.
- **Send a step to the engine as a step** (JV-880 `preset_step`), never infer it from "the new number is current ± 1": RANDOM, automation and a
  recalled value can land next to the current patch.
- **Count steps where the host moves.** A front end that only marks a param dirty and reads it once per audio block merges several clicks into
  one step, and loses clicks that arrive during a file read (Dexed). Count each move in `parameterChanged` and apply the total; applying a read
  file takes no new steps, so the audio thread never loads an unread file.
- **A name on the LCD is not a control.** A label bound to the name's text gives the data wheel and Q-Links nothing to turn (Xenia 1.0.1): put an
  invisible value field (`Knob`, `ValueSlider`, transparent text, focus ring, `Mouse Down → Q-Link`) over it, bound to the value, and give it a
  free Q-Link on the page. A Live III has four physical Q-Links on the selected row; the remote addin's Q-Link turns did not reach a plugin page,
  so check Q-Links by hand.
- **Tiles the engine switches off.** The host sends no 0 for a tile the engine turned off, so a `held_` flag stays set and the next tap is
  dropped while the host lights the tile (JV-880, Xenia). Clear it when the worker reads a tile back dark; read every tile back right after a tap.
- **Mirror params** (a bank value beside PATCH) are NoAuto and span the engine's real maximum (JV-880 `bank_select`: 0..63 = MAX_BANKS, not 21).
- Offline tests that caught these: a real-time controls test whose fake host keeps its own value per param, adopts (or ignores) what the plugin
  tells it and sends nothing when a control cannot move ([`tests/controls_test.cpp`](https://github.com/sd88me/mpc-vst-jv880/blob/master/tests/controls_test.cpp) in mpc-vst-jv880); then bursts of `turn_knob WHEEL` on the
  device, one click at a time and in bursts, reading the result off the screen. Per-port notes: `docs/CONTROLS-REVIEW.md` in mpc-vst-dx7 and mpc-vst-xenia.

**Removing a beta from a device: its folder can hold the user's data.** `build-sdk.sh beta` copies the local ROMs and banks into the beta folder,
and the beta is then used with those. Removing Xenia 2 Beta took 20 sound banks off a Live III because the release folder's `xenia/banks/` was
empty. Before deleting a beta, diff its data folders (banks, carts, roms, favourites) against the release folder and copy anything missing across.


## 16. Maze family lessons (two catalog ids from one repo: Maze Voice 2 and Maze Sequencer 2, `rev-sdk`, 2026-11-14)
- **The catalog's version can lag the newest release.** The catalog listed Sequencer 1.0.7 while 1.0.8 was published on GitHub, and the two ids' last tags sat on different branches (Voice 1.0.5 was not an ancestor of Sequencer 1.0.8). Freeze against the newest *published* release of each id (`gh release list`), and start the port branch from one tag with the other merged in.
- **Read the released state, not the parameter list.** A chunk made by the tag's own engine (`tests/make_legacy_chunks.sh`) showed that Voice 1.0.5 never saved its two LFOs (the engine's `state` text has no `lfo*` keys), so they came back at defaults on reload. The 2.x chunk saves every parameter; a 1.x load leaves the LFOs as they are, as 1.x did.
- **Check what the 1.x wrapper did with a header flag.** The Sequencer's generated `params.h` had `HAS_CC_MAP`, but its hand-written wrapper never read CCs; only the generic wrapper (Voice) used the map. Port the behaviour, not the header.
- **Re-derive the released table from the tag and its pinned tools commit** (`tools/make_released_tables.sh`: `gen_vst.py --params-h` at the release workflow's `tools_ref`), then check every key against the strings in the released `.so`. Two ids, two tools commits.
- **host_sim's state check wants `ui_open` to round-trip.** Closing the lists on load fails "param N did not come back"; restore it as saved (it is UI-only and never reaches the engine).
- **`validate.py` reports `libasound.so.2` as "not on the MPC".** It is there (Euclid 2 and Maze Sequencer 1.x link it); ignore that error for ALSA-port plugins.
- **One SDK project per id in one repo:** `plugins/<id>/{plugin.json,params.json,params.lock,src}` with a shared `plugins/common/` header. The framework's build compiles `src/*.cpp` with `-Isrc` only, so the shared header and the engine are reached by relative includes, and a beta builds from a staging copy *beside* `plugins/<id>` (`plugins/.stage-<id>`) so those relative paths still resolve.
- **Name ring commands apart from table flags.** A `kStep` ring command (3) shadowed the generated `kStep` flag (4) inside an anonymous namespace; `isStep()` then matched momentaries and read past a key. ASan caught it in the first host test run: prefix commands `kCmd*`.
- **The MPC draws a skin Label's font at about 0.62 of its `height`.** A value at height 23 looks like a 14 px Pillow preview; previews that draw at full height make values look far too big. Scale preview text by that factor (`DEVICE_TEXT` in mpc-vst-maze `tools/make_tui.py`).
- **Two LFO pages fit one main tab as sub-tabs** (`fnKeySubIndex`, Q-Links SubTab = sub + 1); the tab bar shows the sub-tab's name.
- **A 1.x `.xpl` preset names the plugin's file path**, and the MPC refuses it ("not for this plugin") when that path differs from the installed plugin. To carry one across devices or into a beta, rewrite `file=` (and for a beta `name=`, `uid=` and the `fxID` inside the JUCE-encoded state, which is `<len>.<6-bit little-endian, alphabet .A-Za-z0-9+>`). In the plugin window's Load browser the LOAD button only enables after a double tap on the file.
- **Draw a bat toggle without a backing plate**; a chrome hex nut, a shaded shaft and a cast shadow read as hardware at 60 px, a 46 px flat drawing does not.

### Presets: getting 1.x `.xpl` files to load in the SDK version (Maze family, untested on a real release)
Old presets hold the 1.x state, which the SDK plugin reads through its legacy loader (same name, uid and parameter keys; test it with a chunk made by the tag's own code). The MPC only refuses a preset ("not for this plugin") when the `<PLUGIN ... file="...">` header does not match the installed plugin file. **First choice: keep the 1.x `.so` name and place** (the catalog packaging route renames the SDK `.so` to the 1.x name at the folder root), so old presets need no edits. Verify that on a device with a real 1.x preset before release; whether projects match plugins by uid or by path is also still unchecked.

If a preset is refused, a user can fix it by hand, with no tools but a text editor:
1. Load the new version on a track and save a preset called `reference` from the plugin window (disk icon).
2. Copy the old presets and `reference.xpl` to a computer (`Plugin Presets/Instruments/<plugin folder>/`).
3. In `reference.xpl`, copy the `file="..."` attribute of the `<PLUGIN>` line.
4. In each old preset, replace only its `file="..."` with that one. Leave `name=`, `uid=` and the `<state>` block alone. Save as plain text, same name, `.xpl`.
5. Copy back to the same folder. In the plugin window, tap the folder icon and **double-tap the file** (LOAD enables only after that).
Notes: the path differs per device (make a `reference` on each); a plugin whose 1.x never saved some settings (Maze Voice's LFOs) leaves them as they are on load; a preset re-saved by the new version needs no edits. This does not work for a beta with its own name and uid: there `name=`, `uid=` and the plugin ID inside the encoded `<state>` also change (decode, patch the 4 bytes after `FBCh`+version, re-encode), so convert testers' presets with a script.
- **Two catalog ids in one repo release separately.** Maze's catalog entries share `sd88me/mpc-vst-maze` and match `Maze-Voice-*` / `Maze-Sequencer-*`, so each id gets its own GitHub release (`maze-voice-vst-v2.0.0`, `maze-sequencer-vst-v2.0.0`, the 1.x tag pattern) carrying its three zips: `-mpc-armv7`, `-mpc-aarch64` and the one-folder `-mpc-sdk`. `tools/package_catalog.sh` builds all six and scrubs the install.sh before `catalog_check`; keep the scrub glob to the per-CPU zips (the one-folder zip has no install.sh and crashes it).
- **Test the upgrade with a project saved by the 1.x plugins.** Save a Live III project with the released plugins loaded (and a preset loaded), install the new build over them (`install.sh -y -n -t "/nvme/VST2 Plugins"` for each plugin with the MPC app stopped, then start it), and reload: the track found the plugin by uid and the preset values were intact. Keeping the 1.x `.so` name at the folder root is what keeps old `.xpl` presets (they hold the file path) valid.
- **Remote driving of a Live III.** The remote's `MATRIX` button opens Pad Mixer, `MENU`/`CLIP` did nothing visible, and there is no way back from a plugin window to the main screen through it; a user-held session needs the user at the screen. Do not burn screenshots hunting for it.
- **Appending popups changes `ui_open`'s range.** Two list flags appended for a 1-16 MIDI channel list made `ui_open` 7-state (k/6 normalised): update every test that wrote a fraction of the old count. A numeric range list needs `spec["opts"]` built from min..max in the skin generator.

