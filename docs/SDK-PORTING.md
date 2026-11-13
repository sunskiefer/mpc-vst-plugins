# Building MPC plugins the SDK way (+ our additions)

**The split:** Akai's SDK builds the plugin; Open MPC delivers it: releases for every MPC (3.9.1 included), the catalog, the Plugin
Manager, installers, sticks, CPU ratings, tiles and themes. The table "Which tool for what" below lists each job.

The house method from 2026-11-04 onwards, worked out on Dexed 2.0. New plugins and major revisions of existing ones are built on
Akai's **MPC Plugin SDK** ([`mpc-plugin-builder-sdk`](https://github.com/inmusic-software/mpc-plugin-builder-sdk) from Akai, MIT). Our own tools fill the gaps. Dexed

**The SDK is vendored at `vendor/mpc-plugin-builder-sdk`** (a git submodule pinned to one commit): clone this repo with
`git clone --recursive`, or run `git submodule update --init` in an existing clone. In this document `<sdk>` means that folder.
`tools/update_sdk.sh` moves the pin to Akai's latest commit and prints what changed; commit the new pin in a PR.
([sd88me/mpc-vst-dx7](https://github.com/sd88me/mpc-vst-dx7), branch `rev-2.0`) is the reference implementation: copy its `tools/`, `build-sdk.sh` and `src/` layout.

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

To release an SDK-built plugin through the catalog, package its build in the catalog's release format, as the Dexed 2 and JV-880 2 ports do (the "catalog route" notes below)
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
- Release: keep name/uid/param order for existing users (a normal update). Mention both install routes. Don't mention 3.10.1 internals publicly
  until Akai launches.

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
- **Tabs: one page per tab, separate keys** (the user's call: familiar from 1.x; keep the same order). The Force shows a 5-key window with
  "<" / ">" keys that slide it along. Sub-tabs (`fnKeySubIndex`) only step **forward** (the dots under the key are indicators, not buttons;
  OP1→OP5 took 4 taps), and the SDK has no "go to tab" action, so don't use sub-tabs to squeeze repeated pages onto one key.
- The user runs with the **Q-Link bounds overlay on**: lay controls in blocks of four inside printed panel sections, with one
  `qlinkBoundsData` rectangle per block and `Bank Direction` matching the block shape. The right edge doesn't need to be kept clear.
- Design process: mock up in the art library first (previews in `docs/skin-preview/`), get the owner's OK, then build the real tab and
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

## 5. More lessons from Dexed 2.0 (UI, parameters, generator)

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
- **Sub-tabs** (`fnKeySubIndex`): one OP key with six sub-tabs is fine for 6 pages; it only steps forward, so the user cycles OP1 -> OP6.
- **Style-driven random generators (GENERATE).** Uniform random FM is mostly junk. What worked: keep carrier ratios fixed (coarse 1, fine 0, ratio mode) so
  the key note never moves; carriers always L4 = 0 and R4 >= 40 (L4 > 0 or a very slow release rings on forever); modulators drawn from a per-style ratio and
  level pool with 30 % off-pool choices and 35 % free-shape envelopes for variety; flat pitch EG; light key scaling on about half the operators; MUTATE (vary the
  current voice by AMOUNT) as a separate button; one unconstrained style (CHAOS) with only a "first carrier is audible" guard.
- **Keep install names stable for an in-place upgrade.** The SDK build's `.so` is renamed to the 1.x name (`dx7_dexed.so`) in the per-build release zips, so
  the old installer, the plugin-list entry and the user's data folder (`dx7_carts`, `favourites.dat`) carry over; `tools/release.py` is run once per arch
  (armv7, aarch64) with `--extra <Content/carts>:dx7_carts --user-data dx7_carts`. `tools/sdk_zip.py` then builds the one-folder SDK zip from the pair.
- **Don't let a full `/tmp` surprise you.** A tmpfs `/tmp` that fills (old scratch worktrees) makes tools print truncated output; builds that write elsewhere
  keep working, but check `df /tmp` when output looks cut off.


## 7. Crate Digger 2 lessons (a streaming player with an external engine, text readouts and result lists)

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
  `tools/sdk_zip.py`, see Dexed in section 5). Decide before publishing which path the plugin takes.
- **Skin text on the real MPC:** a Label draws text at about 0.66 x its TUI height; only Titillium Web, Roboto and Nexa exist, so preview with Titillium at that scale
  (`tools/make_tui.py --preview`) or the render and the device disagree. The MPC draws a "Q" tag at the top-left corner of the active Q-Link group: centre panel titles.
  Neighbouring touch areas must not overlap (keys get an ~8 px pad, no more). One main LCD per tab that mirrors the controls under it (same columns, numbers, names) reads best.
- **Look development.** Mock-ups first, one directed change at a time (the owner went rack sampler -> MPC60): keep the primitives (`tools/mockup_skin.py`: lcd, key, knob, plate) separate
  from the page layouts so a restyle is one file; keep earlier looks as pictures in subfolders. Keys: no drawn border; a darker side wall, a gradient cap and a specular edge read as real.
- **Disk:** `/tmp` here is a small RAM disk; put downloads and engine bundles under the repo's ignored `build/`.
- **Permission note:** pushing a branch or creating a release is blocked by the auto-mode classifier even after the user agrees in chat; the user has to allow the rule or run the push.

## 8. Mini-JV lessons (a Schwung engine that owns its state and changes its own values)

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
- Restarting the MPC app over SSH (`systemctl restart acvs`) can be refused by the session's permission rules. To reload a test project, open it again from the BROWSER (a fresh plugin instance); that needs no restart.

## 9. Euclid lessons (a MIDI sequencer: the engine inside the plugin)
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
- **Installer for a rename or a replacement.** Same uid, new folder name: the installer must remove the old folder and its plugin-list entry (by uid, from `MPC.settings`, and only a folder named like the old one that holds the old plugin's files), keep the user's data (a preset bank) by moving it, back up first, and be safe to run twice. `ls a* b*` exits non-zero if either glob is empty: test with a loop over `[ -e ]`. Test it offline against a fake device (`tools/test_installers.sh`), including a decoy folder, a second plugin and a beta; the first run found the `ls` bug. Through the catalog (Plugin Manager runs the zip's own `install.sh`, which replaces the plugin-list entry by uid but leaves a renamed folder behind) use `release.py --retire "<vendor> - VST - <old name>"`: the installer removes that folder only if its `mpc-plugin.json` has this plugin's id, and moves the `--user-data` files across (`tools/package_catalog.sh` and `tools/test_catalog_install.sh` in Euclid). Use `rm -rf "${VAR:?}"`: the session's safety check refuses a bare `rm -rf "$VAR/x"`.
- **Device sessions.** Every MPC restart needs the user's yes, and a skin or plugin change needs one. Batch changes between restarts, log what you need to know instead of screenshotting every step, and when the user says they will tap, stop driving the screen.

## 10. Profit-08 lessons (a 1.x wrapper build with a string-API engine and hidden popup flags)
- **Freeze the table the released build really had, not the one in the source.** The 0.1.3 VST table was the 245 keys plus one hidden `<key>__open` flag per popup its layout declared (37), 282 in all. Check the released `.so`/`params.json`, then keep those indices: one real `ui_open` parameter at the first popup index, the rest inert reserved slots, new parameters appended after. `params.lock` from the released build is the test.
- **One `ui_open` parameter replaces many popup flags.** Pick-lists are drawn by the plugin: a Button per option, shown with `IndexedEnabling` on `ui_open`. Opening a list is a parameter change, so the preset shows "*"; accept it or filter it from the dirty check.
- **Q-Links are at most four groups (16) per tab or sub-tab.** Pages with more controls keep their art and get sub-tabs that only move the Q-Link frames. The tab key shows the current sub-tab's name after a second press, and the first sub-tab's name when you arrive from another tab.
- **Compile a C engine as C++ only if you must.** The SDK build compiles top-level `src/*.cpp` only. Add explicit casts for `void*` conversions, keep the C initialiser valid in both, and include the `.c` files from one `.cpp` unit. Keep the original `.c` building as C for the 1.x wrapper.
- **Old state still loads.** A legacy 1.x chunk is the engine's plain state string. Wrap `effSetChunk` so it accepts both the SDK's wrapped chunk and the legacy one, and keep a real legacy chunk in `tests/data` (build it from the released tag's engine, not from the new code) so the host test proves it.
- **String-API engine: read back on a revision counter.** The engine bumps `display_rev` on large changes (program, layer, bank); the worker re-reads all values then, and skips readback for a short time after a host write so a drag is not pulled from under the finger.
- **Tile lists (banks, programs).** Momentary parameters, one per tile, with `_on` readback for the lit tile; page through them with small `<` `>` keys, not an LCD stepper. Program tiles must be full size or they are unreadable on the 7" screen.
- **Favourites as VST programs.** Slot 0 is "Current program", 1..N are slots stored in a file in the plugin folder; +FAV and -FAV are plugin-local momentary parameters on the BANKS page. They are not in the 1.x table, so they are appended.
- **A big `.syx` program file can be two banks.** A 128-program-per-file dump loads as banks "name 1" and "name 2": expect "N banks" to be higher than the file count. Banks are scanned at plugin load only: remove and re-add the plugin after copying files.
- **Device handling.** On this build the plugin and preset fields need a double tap to open their list. The MPC remote MCP in a session can point at a different unit (here a Force): check `device_status` (CPU architecture, IP) before driving anything. On-device CPU of the MPC process is dominated by screen redraw (50-70% of one core idle or playing), so bench the render offline with host_sim instead.
