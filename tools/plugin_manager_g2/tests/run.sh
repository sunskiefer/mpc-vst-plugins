#!/usr/bin/env bash
# Offline tests in the gcc:12 Docker image (needs network: they read the live catalog and download one zip).
#   ONLY=<section> tests/run.sh   one section (devices addins links vst2 pick theme safety arch web) in a minute or two; no ONLY: everything (about 9 minutes)
#   tests/run.sh            the wrapper host test, each fake device in tests/devices/, then the addin flow against
#                           an offline fixture (tests/fixture.py: three addin packages and a file:// catalog)
# A fake device is a shell script that builds the device's files inside the container: MPC.settings (the real format,
# trimmed), plugin folders, a stub systemctl. FS_ANY=1 lets the container's overlay filesystem count as internal storage.
# Add one for your model (copy its MPC.settings layout from the device.txt the plugin writes) and send it with your PR.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
MV="${MPC_VST:-$here/../..}"
python3 "$MV/tools/gen_vst.py" "$here/vst/vst.json" --params-h >/dev/null
MPC_VST="$MV" python3 "$here/tests/fixture.py"
docker run --rm -e ONLY -e TRACE -e DEV -v "$here":/p -v "$MV":/mv:ro -w /p gcc:12 bash -c '
set -eo pipefail; [ -z "$TRACE" ] || set -x
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"
want() { [ -z "$ONLY" ] || [ "$ONLY" = "$1" ]; }   # ONLY=safety: just that section (devices, addins, links, pick, theme, safety, arch, web)
export PM_TAPMS=40   # the double-tap window, short (FS_ANY builds only)
export PM_GLIBC=2.39   # a Gen2 / MPC OS 3.x glibc: the container has 2.36, which reads as OS 2.x and refused the live catalog packages (a 120 s wait per device)
gcc $SAN -std=gnu11 -w -Ivst/build src/manager.c /mv/tools/host_test.c /mv/wrapper/vst2_wrap.c -lpthread -ldl -lm -o /tmp/host_test
if want devices; then
(cd /tmp && ./host_test | tail -n 1)
gcc $SAN -DFS_ANY=1 -std=gnu11 -w src/manager.c tests/drive.c -lpthread -ldl -o /tmp/drive
for dev in tests/devices/${DEV:-*}.sh; do
  echo "== $dev"
  # each device starts from a clean filesystem
  rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
  echo "   $(date +%T)"
  sh "$dev"
  mkdir -p /tmp/openmpc-pm /tmp/zipcache; cp /tmp/zipcache/*.zip /tmp/openmpc-pm/ 2>/dev/null || true   # the catalog zip downloaded once, not once per device
  /tmp/drive > /tmp/drive.out && grep -E "location|target|problem" /tmp/openmpc-pm/device.txt
  # a device that can install must get the first catalog plugin to "ready" (apply_state 3): a scenario that quietly timed out hid a broken install path for weeks
  if grep -q "^problem: none" "${dev%.sh}.expect"; then grep -q "apply_state 3" /tmp/drive.out || { echo "FAIL $dev: the install never reached ready"; grep "status(" /tmp/drive.out | tail -n 2; exit 1; }; fi
  # tests/devices/<name>.locs: text the LOCATIONS tab must show (one fixed string per line; "loc1: vis=1" ...)
  [ ! -f "${dev%.sh}.locs" ] || while IFS= read -r l; do grep -qF -- "$l" /tmp/drive.out || { echo "FAIL $dev: LOCATIONS lacks: $l"; grep "^loc" /tmp/drive.out; exit 1; }; done < "${dev%.sh}.locs"
  # tests/devices/<name>.banner: the red banner a device with a problem must show (plain words)
  [ ! -f "${dev%.sh}.banner" ] || grep -qF -- "$(cat "${dev%.sh}.banner")" /tmp/drive.out || { echo "FAIL $dev: banner is not: $(cat "${dev%.sh}.banner")"; grep banner /tmp/drive.out | head -n 2; exit 1; }
  cp -n /tmp/openmpc-pm/*.zip /tmp/zipcache/ 2>/dev/null || true
  # tests/devices/<name>.expect: lines that must be in device.txt (a test fails without them)
  exp="${dev%.sh}.expect"
  [ -f "$exp" ] || { echo "MISSING $exp: every fake device says what it must resolve to"; exit 1; }
  while IFS= read -r line; do
    grep -qxF -- "$line" /tmp/openmpc-pm/device.txt || { echo "FAIL $dev: device.txt lacks: $line"; exit 1; }
  done < "$exp"
  absent="${dev%.sh}.absent"   # lines that must NOT be in device.txt (e.g. a second path to the same folder)
  if [ -f "$absent" ]; then while IFS= read -r line; do
    ! grep -qxF -- "$line" /tmp/openmpc-pm/device.txt || { echo "FAIL $dev: device.txt has: $line"; exit 1; }
  done < "$absent"; fi
done
fi # devices
if want addins; then
echo "== addins (offline fixture)"
fi # addins-head
gcc $SAN -DFS_ANY=1 -DCATALOG_URL=\"file:///p/vst/build/tests/catalog.json\" -DPATCHES_URL=\"file:///p/vst/build/tests/patches.json\" -std=gnu11 -w src/manager.c tests/drive.c -lpthread -ldl -o /tmp/drive_addins
if want addins; then
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
sh tests/devices/mpc-one.sh && sh tests/addins.sh && /tmp/drive_addins addins
grep -E "^addin" /tmp/openmpc-pm/device.txt
echo "-- apply.sh:"; grep -E "^(sh|echo)" /tmp/openmpc-pm/apply.sh
grep -q "/install.sh'"'"' -y -n -t '"'"'/data/mpc-addins/beta-addin'"'"'" /tmp/openmpc-pm/apply.sh
grep -q "/data/mpc-addins/alpha-addin/uninstall.sh'"'"' -y -n -t '"'"'/data/mpc-addins/alpha-addin'"'"'" /tmp/openmpc-pm/apply.sh
! grep -q gamma /tmp/openmpc-pm/apply.sh
echo "addins OK"
fi # addins
if want links; then
echo "== links (offline fixture): a package that needs a symbolic link"
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
sh tests/devices/mpc-one.sh
PM_NOLINKS=1 /tmp/drive_addins links > /tmp/links-no.out
grep -F "Linky: needs a drive that stores symbolic links, and /media/az01-internal/Synths does not. Nothing on the MPC was changed." /tmp/links-no.out
grep -q "apply_state 4" /tmp/links-no.out
[ ! -f /tmp/openmpc-pm/apply.sh ] || { echo "FAIL: apply.sh written for a refused package"; exit 1; }
rm -rf /tmp/openmpc-pm; /tmp/drive_addins links > /tmp/links-yes.out
! grep -q "symbolic links" /tmp/links-yes.out
grep -q "apply_state 3" /tmp/links-yes.out
grep -q "/linky.*install.sh\|Linky-1.0.0/install.sh" /tmp/openmpc-pm/apply.sh
echo "links OK"
fi # links
if want vst2; then
echo "== vst2 (offline fixture): MPC OS 3.10.1, VST2 Support on: move the old installs, install into VST2 Plugins with a plugin.json"
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
sh tests/devices/gen2-3101-vst2.sh
# copied by hand into VST2 Plugins (no MPC.settings entry): counts as installed, read from its plugin.json
H="/nvme/VST2 Plugins/acme - VST - Handcopy"; mkdir -p "$H"; echo so > "$H/hc-arm64.so"; echo so > "$H/hc-arm32.so"
printf '"'"'{"name": "Handcopy", "uid": "0x48616e64", "file32": "hc-arm32.so", "file64": "hc-arm64.so"}'"'"' > "$H/plugin.json"
K="/nvme/VST2 Plugins/acme - VST - Sdkish"; mkdir -p "$K/VST"; echo so > "$K/VST/sdk-arm64.so"; echo so > "$K/VST/sdk-arm32.so"
printf '"'"'{"name": "Sdkish", "uid": "SdKi", "format": "vst2", "file32": "VST/sdk-arm32.so", "file64": "VST/sdk-arm64.so"}'"'"' > "$K/plugin.json"
/tmp/drive_addins links > /tmp/vst2.out
grep -qE "^plugin: Handcopy uid=48616e64 file=/nvme/VST2 Plugins/acme - VST - Handcopy/hc-arm(32|64).so$" /tmp/openmpc-pm/device.txt || { grep "^plugin:" /tmp/openmpc-pm/device.txt; exit 1; }
! grep -q "^plugin: .*Newbie" /tmp/openmpc-pm/device.txt   # a plugin.json naming no binary is not counted
# the Akai SDK layout (Acid 3, Crate Digger 2): the binary in VST/, plugin.json naming VST/<so>: listed with that file
grep -qE "^plugin: Sdkish uid=SdKi file=/nvme/VST2 Plugins/acme - VST - Sdkish/VST/sdk-arm(32|64).so$" /tmp/openmpc-pm/device.txt || { grep "^plugin:" /tmp/openmpc-pm/device.txt; exit 1; }
grep -q "apply_state 3" /tmp/vst2.out
grep -qF "APPLY copies your 1 installed plugin there" /tmp/vst2.out
A=/tmp/openmpc-pm/apply.sh
grep -qF "echo '"'"'-- move to VST2 Plugins'"'"'" $A
[ "$(grep -n "move to VST2" $A | cut -d: -f1)" -lt "$(grep -n "^item " $A | head -n 1 | cut -d: -f1)" ]   # the move runs before any install
grep -qF -- "-t '"'"'/nvme/VST2 Plugins'"'"'" $A
grep -qF -- "--json '"'"'/nvme/VST2 Plugins/" $A
# run the move for real (MPC "stopped": systemctl is a stub) and check what MPC 3.10.1 would see
mkdir -p "/nvme/Synths/acme - VST - Oldie/Plugin Skins" && echo old > "/nvme/Synths/acme - VST - Oldie/Plugin Skins/TUI.json"
ARCH=aarch64 sh /tmp/openmpc-pm/vst2.sh --apply   # the container is x86_64; this fake device is a Gen2 (pluginList-arm-64bit)
# MPC 3.10.1 draws the old copy'"'"'s skin when there is one: it is set aside (renamed), the VST2 copy keeps its own
test -f "/nvme/VST2 Plugins/acme - VST - Oldie/Plugin Skins/TUI.json" && ! test -e "/nvme/Synths/acme - VST - Oldie/Plugin Skins"
test -f "/nvme/Synths/acme - VST - Oldie/Plugin Skins.vst2-moved/TUI.json"
# a plugin moved before this fix: the next run sets its old skin aside too (and an install'"'"'s --json run does the same)
mv "/nvme/Synths/acme - VST - Oldie/Plugin Skins.vst2-moved" "/nvme/Synths/acme - VST - Oldie/Plugin Skins"
ARCH=aarch64 sh /tmp/openmpc-pm/vst2.sh --apply > /tmp/vst2-again.out; grep -qF "SKIN  acme - VST - Oldie: old skin in /nvme/Synths set aside" /tmp/vst2-again.out
mv "/nvme/Synths/acme - VST - Oldie/Plugin Skins.vst2-moved" "/nvme/Synths/acme - VST - Oldie/Plugin Skins"
ARCH=aarch64 sh /tmp/openmpc-pm/vst2.sh --json "/nvme/VST2 Plugins/acme - VST - Oldie" > /tmp/vst2-json.out; grep -qF "SKIN  acme - VST - Oldie" /tmp/vst2-json.out
! test -e "/nvme/Synths/acme - VST - Oldie/Plugin Skins"
test -f "/nvme/VST2 Plugins/acme - VST - Oldie/plugin.json" && test -f "/nvme/VST2 Plugins/acme - VST - Oldie/roms/user.bin"
test -f "/nvme/Synths/acme - VST - Oldie/oldie.so"   # the old copy stays: saved projects load it
! test -e "/nvme/VST2 Plugins/acme - VST - RomsOnly"
grep -qF "\"uid\": \"0x4f6c6469\"" "/nvme/VST2 Plugins/acme - VST - Oldie/plugin.json"
grep -qF "\"file64\": \"oldie.so\"" "/nvme/VST2 Plugins/acme - VST - Oldie/plugin.json"
# the drive menu: two drives (internal + the stick; the Akai mounts are not offered), a pick is the target and is remembered
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
sh tests/devices/gen2-3101-vst2.sh
/tmp/drive_addins drives > /tmp/drives.out
grep -qF "readout: |Internal" /tmp/drives.out
grep -qF "menu: open=1 hl=1 |Internal  ·  " /tmp/drives.out && grep -qF "|STICK  ·  " /tmp/drives.out && grep -qF "| |" /tmp/drives.out
grep -qF "after pick: open=0 readout: |STICK" /tmp/drives.out
grep -qF "prompt: |Install to: /media/STICK/VST2 Plugins  (2 of 2)|" /tmp/drives.out || { cat /tmp/drives.out; exit 1; }
grep -qxF "/media/STICK/VST2 Plugins" /data/.openmpcpm-target
# renamed in 1.1.0 ("Plugin Manager G2" -> "Plugin Manager"): this copy, running from the new folder, removes an old copy of itself that
# was not retired by install.sh (a hand copy), but never a folder of that name that is someone else'"'"'s; Move doesn'"'"'t copy the old one back
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
sh tests/devices/gen2-3101-vst2.sh
O="/nvme/VST2 Plugins/OpenMPC - VST - Plugin Manager G2"; N="/nvme/VST2 Plugins/OpenMPC - VST - Plugin Manager"; mkdir -p "$O" "$N"
echo so > "$O/openmpc_pm_g2.so"; printf '"'"'{"schema": 1, "id": "plugin-manager-g2", "version": "1.0.0"}'"'"' > "$O/mpc-plugin.json"
printf '"'"'{"name": "Plugin Manager G2", "uid": "0x4f4d4732", "file64": "openmpc_pm_g2.so"}'"'"' > "$O/plugin.json"
echo so > "$N/openmpc_pm_g2.so"; printf '"'"'{"name": "Plugin Manager", "uid": "0x4f4d4732", "file64": "openmpc_pm_g2.so"}'"'"' > "$N/plugin.json"
PM_OWN="$N" /tmp/drive_addins links > /tmp/retire.out
! test -e "$O" && test -d "$N" || { echo "the old folder was not removed"; cat /tmp/openmpc-pm/manager.log; exit 1; }
grep -qF "removed the old Plugin Manager folder $O" /tmp/openmpc-pm/manager.log
! grep -q "^plugin: Plugin Manager G2 " /tmp/openmpc-pm/device.txt   # the device was read again without it
mkdir -p "$O"; printf '"'"'{"schema": 1, "id": "someone-else"}'"'"' > "$O/mpc-plugin.json"   # same name, not ours: left alone
PM_OWN="$N" /tmp/drive_addins links > /tmp/retire2.out; test -d "$O" && grep -qF "left $O alone" /tmp/openmpc-pm/manager.log
rm -rf "$O"
# Move: the old folder still in Synths is the same plugin (same uid) as the new one in VST2 Plugins: not copied back
S="/nvme/Synths/OpenMPC - VST - Plugin Manager G2"; mkdir -p "$S"; echo so > "$S/openmpc_pm_g2.so"
printf '"'"'<PLUGIN name="Plugin Manager G2" manufacturer="OpenMPC" file="%%payload-path%%/OpenMPC - VST - Plugin Manager G2/openmpc_pm_g2.so" uid="4f4d4732" isInstrument="1" numInputs="0" numOutputs="2"/>\n'"'"' > "$S/plugin-meta.xml"
ARCH=aarch64 sh /tmp/openmpc-pm/vst2.sh --apply > /tmp/vst2-dedupe.out
grep -qF "HAVE  OpenMPC - VST - Plugin Manager G2 (as OpenMPC - VST - Plugin Manager in /nvme/VST2 Plugins, same uid)" /tmp/vst2-dedupe.out || { cat /tmp/vst2-dedupe.out; exit 1; }
! test -e "$O"
# an install into a folder with an apostrophe (L'"'"'Cronx - VST - ...) skipped plugin.json up to 1.1.0: the Manager writes it at start
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
sh tests/devices/gen2-3101-vst2.sh
L="/nvme/VST2 Plugins/L'"'"'Cronx - VST - MS20XXL"; mkdir -p "$L"; echo so > "$L/ms20xxl.so"
printf '"'"'<PLUGIN name="MS20XXL" manufacturer="L&apos;Cronx" file="%%payload-path%%/L&apos;Cronx - VST - MS20XXL/ms20xxl.so" uid="4d733230" isInstrument="1" numInputs="0" numOutputs="2"/>\n'"'"' > "$L/plugin-meta.xml"
/tmp/drive_addins links > /tmp/lcronx.out
test -f "$L/plugin.json" || { echo "no plugin.json written"; tail -n 5 /tmp/openmpc-pm/manager.log; exit 1; }
grep -qF "\"uid\": \"0x4d733230\"" "$L/plugin.json" && grep -qF "\"format\": \"vst2\"" "$L/plugin.json" && grep -qF "wrote the missing plugin.json in $L" /tmp/openmpc-pm/manager.log
grep -qE "^plugin: MS20XXL uid=4d733230 file=/nvme/VST2 Plugins/L.Cronx - VST - MS20XXL/ms20xxl.so$" /tmp/openmpc-pm/device.txt || { grep "^plugin:" /tmp/openmpc-pm/device.txt; exit 1; }
# Move covers a drive'"'"'s Synths folder that is not in SynthContentLocations (plugins on a flash drive, Live III 2026-11-13)
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
sh tests/devices/gen2-3101-vst2.sh
K="/media/STICK/Synths/acme - VST - Sticky"; mkdir -p "$K"; echo so > "$K/sticky.so"
printf '"'"'<PLUGIN name="Sticky" manufacturer="acme" file="%%payload-path%%/acme - VST - Sticky/sticky.so" uid="53746b79" isInstrument="1" numInputs="0" numOutputs="2"/>\n'"'"' > "$K/plugin-meta.xml"
/tmp/drive_addins links > /tmp/stick.out
grep -qF "vst2 to move: 2" /tmp/openmpc-pm/device.txt || { grep "vst2 to move" /tmp/openmpc-pm/device.txt; exit 1; }
ARCH=aarch64 sh /tmp/openmpc-pm/vst2.sh --apply > /tmp/stick-move.out
grep -qF "COPY  acme - VST - Sticky -> /media/STICK/VST2 Plugins/" /tmp/stick-move.out || { cat /tmp/stick-move.out; exit 1; }
test -f "/media/STICK/VST2 Plugins/acme - VST - Sticky/plugin.json" && test -f "/media/STICK/VST2 Plugins/acme - VST - Sticky/sticky.so"
echo "vst2 OK"
fi # vst2
if want pick; then
echo "== pick (offline fixture): two usable locations, CHANGE on the restart prompt"
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts
mkdir -p /media
sh tests/devices/two-targets.sh
/tmp/drive_addins pick > /tmp/pick.out
grep -qF "prompt: confirm=1 pick=1 |Install to: /sdcard/Synths  (1 of 2)|" /tmp/pick.out
grep -qF "after CHANGE: confirm=1 pick=1 |Install to: /media/CARD/Synths  (2 of 2)|" /tmp/pick.out
grep -qF -- "-t '"'"'/media/CARD/Synths'"'"'" /tmp/openmpc-pm/apply.sh
# the confirmed place is remembered: a fresh start names it first; with that drive gone it falls back and keeps the preference
grep -qx "/media/CARD/Synths" /media/az01-internal/.openmpcpm-target
rm -rf /tmp/openmpc-pm; /tmp/drive_addins pickshow > /tmp/pickshow.out
grep -qF "first prompt: confirm=1 pick=1 |Install to: /media/CARD/Synths  (2 of 2)|" /tmp/pickshow.out || { cat /tmp/pickshow.out; exit 1; }
printf "x /sdcard ext4 rw,relatime 0 0\n" > /tmp/pm-mounts   # the card is gone
rm -rf /tmp/openmpc-pm; /tmp/drive_addins pickshow > /tmp/pickgone.out
grep -qF "/sdcard/Synths" /tmp/openmpc-pm/apply.sh && ! grep -qF "/media/CARD" /tmp/openmpc-pm/apply.sh || { cat /tmp/pickgone.out; exit 1; }
grep -qx "/media/CARD/Synths" /media/az01-internal/.openmpcpm-target
# the same choice from the storage readout (a drive menu; firmware before 3.10.1: the usable Synths folders)
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
sh tests/devices/two-targets.sh
/tmp/drive_addins drives > /tmp/drives-old.out
grep -qF "readout: |Internal" /tmp/drives-old.out && grep -qF "|CARD  ·  " /tmp/drives-old.out
grep -qF "after pick: open=0 readout: |CARD" /tmp/drives-old.out
grep -qF "prompt: |Install to: /media/CARD/Synths  (2 of 2)|" /tmp/drives-old.out || { cat /tmp/drives-old.out; exit 1; }
# an install of a plugin already on the MPC from an older installer (no mpc-plugin.json, an entry under another name) is an update in
# place: it stays in /sdcard/Synths even with the card picked (Force, 2026-10-31: AW Ambience got a second copy on the SSD)
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
sh tests/devices/two-targets.sh
mkdir -p "/sdcard/Synths/acme - VST - Linky"; echo so > "/sdcard/Synths/acme - VST - Linky/linky.so"
sed -i "s|<KNOWNPLUGINS/>|<KNOWNPLUGINS><PLUGIN name=\"Linky old\" descriptiveName=\"Linky old\" format=\"VST\" category=\"Synth\" manufacturer=\"acme\" version=\"1.0\" file=\"/sdcard/Synths/acme - VST - Linky/linky.so\" uid=\"4c6e6b79\" isInstrument=\"1\" fileTime=\"0\" infoUpdateTime=\"0\" numInputs=\"0\" numOutputs=\"2\" isShell=\"0\"/></KNOWNPLUGINS>|" /media/az01-internal/Settings/MPC/MPC.settings
/tmp/drive_addins drives > /tmp/drives-upd.out
grep -qF "after pick: open=0 readout: |CARD" /tmp/drives-upd.out
grep -F "Linky" /tmp/openmpc-pm/apply.sh | grep -qF -- "-t '"'"'/sdcard/Synths'"'"'" || { grep -F install.sh /tmp/openmpc-pm/apply.sh; exit 1; }
# LOCATIONS tab: MAKE DEFAULT on the card row moves the default and is remembered; the default row has no button
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
sh tests/devices/two-targets.sh
/tmp/drive_addins locpick > /tmp/locpick.out
grep -qF "before: canpick 1 0 | Ready  ·  0 plugins | Default install location  ·  0 plugins" /tmp/locpick.out || { cat /tmp/locpick.out; exit 1; }
grep -qF "after: canpick 0 1 | Default install location  ·  0 plugins | Ready  ·  0 plugins" /tmp/locpick.out || { cat /tmp/locpick.out; exit 1; }
grep -qF "rescan: canpick 0 1" /tmp/locpick.out || { cat /tmp/locpick.out; exit 1; }
grep -qxF "/media/CARD/Synths" /media/az01-internal/.openmpcpm-target
# LOCATIONS: REGISTER for a drive MPC does not list (queue, cancel, queue again, APPLY writes a step that edits MPC.settings)
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
sh tests/devices/loc-unlisted.sh
/tmp/drive_addins locreg > /tmp/locreg.out
grep -qF "queued: reg=2 |Queued: press APPLY on the PLUGINS tab (MPC restarts)| apply=" /tmp/locreg.out || { cat /tmp/locreg.out; exit 1; }
grep -qF "cancelled: reg=1" /tmp/locreg.out && grep -qF "prepared: apply=3" /tmp/locreg.out || { cat /tmp/locreg.out; exit 1; }
grep -qF "register location /media/STICK/Synths" /tmp/openmpc-pm/apply.sh || { cat /tmp/openmpc-pm/apply.sh | tail -20; exit 1; }
cp /media/az01-internal/Settings/MPC/MPC.settings /tmp/ms.before
awk -v loc=/media/STICK/Synths -f /tmp/openmpc-pm/loc_add.awk /tmp/ms.before > /tmp/ms.after
grep -qF "<Location>/media/STICK/Synths</Location>" /tmp/ms.after
[ "$(grep -c "<Location>" /tmp/ms.after)" = 2 ] && [ "$(grep -cP "\r$" /tmp/ms.after)" = "$(wc -l < /tmp/ms.after)" ] || { cat -A /tmp/ms.after; exit 1; }
# SETTINGS: beta plugins and the web page toggle, saved beside the theme preference
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
sh tests/devices/two-targets.sh
PM_CACHE_PCT=99 PM_NO_WEB=1 /tmp/drive_addins settings > /tmp/settings.out
grep -qF "before: beta=SHOWN web=ON" /tmp/settings.out && grep -qF "after: beta=HIDDEN web=OFF" /tmp/settings.out && grep -qF "again: beta=SHOWN" /tmp/settings.out || { cat /tmp/settings.out; exit 1; }
grep -qF "cache queued: |99% used  ·  clears on APPLY| apply=1" /tmp/settings.out || { cat /tmp/settings.out; exit 1; }
grep -qx "beta=1" /media/az01-internal/.openmpcpm-settings && grep -qx "web=0" /media/az01-internal/.openmpcpm-settings || { cat /media/az01-internal/.openmpcpm-settings; exit 1; }
# ABOUT: the page names the running version (PM_VERSION) and this MPC, and never claims an installed copy it cannot see
PM_NO_WEB=1 /tmp/drive_addins about > /tmp/about.out
v=$(sed -n "s/^#define PM_VERSION .\(.*\)./\1/p" src/manager.c)
grep -qF "|Plugin Manager $v|" /tmp/about.out && grep -q "|two-targets\|mpc-\|unknown model" /tmp/about.out && grep -q "glibc" /tmp/about.out || { cat /tmp/about.out; exit 1; }
# A-Z: a letter filters the list to names starting with it (nothing for Z in the fixture), ALL brings everything back
PM_NO_WEB=1 /tmp/drive_addins az > /tmp/az.out
grep -qF "open: 1" /tmp/az.out && grep -qF "after Z: open=0 f=1 |Z| card1=| |" /tmp/az.out && grep -qF "after A: open=0 f=1 |A| card1=|A" /tmp/az.out || { cat /tmp/az.out; exit 1; }
grep -qF "after ALL: f=0" /tmp/az.out && ! grep -qF "after ALL: f=0 |A–Z| card1=| |" /tmp/az.out || { cat /tmp/az.out; exit 1; }
# PATCHES from the web page: CHECK runs, APPLY only asks (the screen shows it, one tap there runs it), CANCEL withdraws; REGISTER of an unknown path is refused
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts /tmp/fx-patch-marker; mkdir -p /media
sh tests/devices/two-targets.sh
/tmp/drive_addins webpatch > /tmp/webpatch.out
grep -qF "asked: act=3 pill=2 |The web page asks to APPLY \"Fixture stock\"" /tmp/webpatch.out && grep -qF "\"pending\":1" /tmp/webpatch.out || { cat /tmp/webpatch.out; exit 1; }
grep -qF "cancelled: act=1" /tmp/webpatch.out && grep -qF "applied: pill=3" /tmp/webpatch.out || { cat /tmp/webpatch.out; exit 1; }
grep -q "^not possible now" /tmp/webpatch.out && grep -q "^unknown patch" /tmp/webpatch.out && grep -qF "\"locations\":[" /tmp/webpatch.out || { cat /tmp/webpatch.out; exit 1; }
# PATCHES: APPLY and UNDO (two taps; the script runs install/uninstall --confirmed); a patch that restarts MPC is not offered
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts /tmp/fx-patch-marker; mkdir -p /media
sh tests/devices/two-targets.sh
/tmp/drive_addins papply > /tmp/papply.out
grep -qF "start: pill=2 act=1" /tmp/papply.out && grep -qF "asked: act=3 pill=2" /tmp/papply.out || { cat /tmp/papply.out; exit 1; }
grep -qF "applied: pill=3 act=2 |Fixture stock: applied. Now: patched.|" /tmp/papply.out || { cat /tmp/papply.out; exit 1; }
grep -qF "undone: pill=2 act=1 |Fixture stock: removed. Now: stock.|" /tmp/papply.out || { cat /tmp/papply.out; exit 1; }
grep -qF "restarting: |Fixture restarting| pill=2 act=0" /tmp/papply.out || { cat /tmp/papply.out; exit 1; }
[ ! -e /tmp/fx-patch-marker ] || { echo "the marker is still there"; exit 1; }
# per-device catalog entries: shown when the model is unknown or listed, hidden on another MPC
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
sh tests/devices/two-targets.sh
PM_MODEL="Akai Professional MPC Live III" /tmp/drive_addins models > /tmp/models-a.out
PM_MODEL="Akai Professional MPC Sample" /tmp/drive_addins models > /tmp/models-b.out
/tmp/drive_addins models > /tmp/models-c.out
grep -qF "sample entry seen: 0 (model Akai Professional MPC Live III)" /tmp/models-a.out && grep -qF "sample entry seen: 1 (model Akai Professional MPC Sample)" /tmp/models-b.out && grep -qF "sample entry seen: 1 (model unknown)" /tmp/models-c.out || { cat /tmp/models-a.out /tmp/models-b.out /tmp/models-c.out; exit 1; }
echo "pick OK"
fi # pick
if want theme; then
echo "== theme (offline fixture): the switch, the prompt, and the skin swap in apply.sh"
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts /tmp/own; mkdir -p /media
sh tests/devices/mpc-one.sh
for t in "Plugin Skins" "Plugin Skins.dark" "Plugin Skins.light"; do mkdir -p "/tmp/own/$t"; done
echo dark > "/tmp/own/Plugin Skins/.theme"; echo dark > "/tmp/own/Plugin Skins.dark/.theme"; echo light > "/tmp/own/Plugin Skins.light/.theme"
export PM_OWN=/tmp/own
# the default: no preference saved and the light skin in place: LIGHT, nothing pending
echo light > "/tmp/own/Plugin Skins/.theme"
/tmp/drive_addins themeshow > /tmp/theme0.out
grep -qF "start: can=1 theme=1 |LIGHT| apply_state=0" /tmp/theme0.out
# the user chose dark earlier and dark is in place: one tap (which MPC sends twice) switches to light
echo dark > "/tmp/own/Plugin Skins/.theme"; echo dark > /media/az01-internal/.openmpcpm-theme
/tmp/drive_addins theme > /tmp/theme.out
grep -qF "start: can=1 theme=0 |DARK| apply_state=0" /tmp/theme.out
grep -qF "pressed: theme=1 |LIGHT  after restart| apply_state=1" /tmp/theme.out
grep -qF "prompt: confirm=1 |Theme: light|" /tmp/theme.out
grep -qx light /media/az01-internal/.openmpcpm-theme
sh /tmp/openmpc-pm/apply.sh
[ "$(cat "/tmp/own/Plugin Skins/.theme")" = light ] || { echo "FAIL: the light skin was not swapped in"; exit 1; }
[ -d "/tmp/own/Plugin Skins.light" ] && [ -d "/tmp/own/Plugin Skins.dark" ] || { echo "FAIL: a skin variant was lost"; exit 1; }
/tmp/drive_addins themeshow > /tmp/theme2.out
grep -qF "start: can=1 theme=1 |LIGHT| apply_state=0" /tmp/theme2.out
unset PM_OWN
echo "theme OK"
fi # theme
if want safety; then
echo "== safety (offline fixture): refused before MPC stops; a failed install is rolled back and reported"
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
sh tests/devices/mpc-one.sh
/tmp/drive_addins one Wrongarch > /tmp/safe-arch.out
grep -qF "is built for aarch64, this MPC is armv7. Nothing on the MPC was changed." /tmp/safe-arch.out
rm -rf /tmp/openmpc-pm
/tmp/drive_addins one Evil > /tmp/safe-evil.out
grep -qF "has unsafe file names in its zip. Nothing on the MPC was changed." /tmp/safe-evil.out
[ ! -e /tmp/openmpc-pm/Evil ] || { echo "FAIL: an unsafe zip was unpacked"; exit 1; }
rm -rf /tmp/openmpc-pm
/tmp/drive_addins one Faily > /tmp/safe-fail.out
grep -q "apply_state 3" /tmp/safe-fail.out
T=$(grep -o "/media/[A-Za-z0-9_-]*/Synths\|/sdcard/Synths" /tmp/openmpc-pm/apply.sh | head -n 1)
mkdir -p "$T/Faily dir" && echo old > "$T/Faily dir/old"
sh /tmp/openmpc-pm/apply.sh
grep -q "^faily|fail|disk full" /tmp/openmpc-pm/result
[ -f "$T/Faily dir/old" ] && [ ! -e "$T/Faily dir/partial" ] || { echo "FAIL: the folder was not put back"; exit 1; }
# the same with a "big" folder (threshold 0 KB): the safety copy goes beside it on its own drive, and is gone afterwards
rm -rf /tmp/openmpc-pm; /tmp/drive_addins one Faily > /dev/null
mkdir -p "$T/Faily dir" && echo old > "$T/Faily dir/old"; rm -rf "$T/Faily dir/partial"
PM_SNAP_KB=0 sh /tmp/openmpc-pm/apply.sh
grep -q "^faily|fail|disk full" /tmp/openmpc-pm/result
[ -f "$T/Faily dir/old" ] && [ ! -e "$T/Faily dir/partial" ] && [ ! -e "$T/.pm-snap-faily" ] || { echo "FAIL: the big-folder rollback"; exit 1; }
rm -rf /tmp/openmpc-pm; /tmp/drive_addins one Faily > /dev/null
mkdir -p "$T/Faily dir/x"; mkdir -p "$T/.pm-snap-faily"
PM_SNAP_KB=0 sh /tmp/openmpc-pm/apply.sh
grep -q "^faily|fail|an earlier update left a safety copy" /tmp/openmpc-pm/result
rm -rf "$T/.pm-snap-faily" "$T/Faily dir"
rm -rf /tmp/openmpc-pm; /tmp/drive_addins one Faily > /dev/null
mkdir -p "$T/Faily dir"; echo old > "$T/Faily dir/old"; cp "$T/Faily dir/old" /tmp/old.keep
sh /tmp/openmpc-pm/apply.sh
/tmp/drive_addins status > /tmp/safe-status.out
grep -qF "Last apply: 0 done, 1 failed and put back as they were. Faily: disk full" /tmp/safe-status.out
# a tap on a drawn key arrives twice with no release (a Live III): one letter, not two; the same key a finger later is a second letter
rm -rf /tmp/openmpc-pm; /tmp/drive_addins keys > /tmp/safe-keys.out
grep -qF "one tap on A delivered twice: |A|" /tmp/safe-keys.out && grep -qF "then A again: |AA|" /tmp/safe-keys.out && grep -qF "then B, L quickly: |AABL|" /tmp/safe-keys.out && grep -qF "then L again after a pause: |AABLL|" /tmp/safe-keys.out && grep -qF "one BACK tap delivered twice: |AABL|" /tmp/safe-keys.out || { cat /tmp/safe-keys.out; echo "FAIL: a key tapped once typed more or fewer letters"; exit 1; }
/tmp/drive_addins one Newlib > /tmp/safe-glibc.out
grep -qF "Newlib: needs glibc 9.99 or newer, this MPC has" /tmp/safe-glibc.out
rm -rf /tmp/openmpc-pm; PM_GLIBC=2.39 /tmp/drive_addins one Oldos > /tmp/safe-os.out
grep -qF "Oldos: is for a different MPC OS, and this one reads as 3.x (from its glibc 2.39). Nothing on the MPC was changed." /tmp/safe-os.out
# a folder MPC does not list (plugin-meta.xml, no entry) is a card in INSTALLED; REGISTER adds its entry and nothing else changes
SY=$(find /media -type d -name Synths 2>/dev/null | head -n 1)
mkdir -p "$SY/Orphan Test Folder"; : > "$SY/Orphan Test Folder/o.so"
cat > "$SY/Orphan Test Folder/plugin-meta.xml" <<EOF
<PLUGIN name="Orphan Test" descriptiveName="Orphan Test" format="VST" category="Synth" manufacturer="OpenMPC"
 version="1.0" file="%payload-path%/Orphan Test Folder/o.so" uid="4f544f31" isInstrument="1" fileTime="0" infoUpdateTime="0"
 numInputs="0" numOutputs="2" isShell="0" hasARAExtension="0" uniqueId="1" pluginFormatName="VST"/>
EOF
SET=$(find /media /data -name MPC.settings 2>/dev/null | head -n 1 || true)
! grep -q 4f544f31 "$SET"
rm -rf /tmp/openmpc-pm; /tmp/drive_addins one "Orphan Test" 1 1 > /tmp/safe-orphan.out
grep -q "apply_state 3" /tmp/safe-orphan.out || { cat /tmp/safe-orphan.out; exit 1; }
sh /tmp/openmpc-pm/apply.sh
grep -q "^orphan-Orphan-Test-Folder|ok|register" /tmp/openmpc-pm/result || { cat /tmp/openmpc-pm/result /tmp/openmpc-pm/apply.log; exit 1; }
grep -q "uid=\"4f544f31\"" "$SET" && grep -q "file=\"$SY/Orphan Test Folder/o.so\"" "$SET" || { echo "FAIL: the entry was not added"; exit 1; }
/tmp/drive_addins one "Orphan Test" 1 1 | grep -q "card 1: OR" && { echo "FAIL: a registered folder is still offered"; exit 1; }
rm -rf "$SY/Orphan Test Folder"
# a zip put on the drive (local-plugins/) is a card and installs by the same steps
LD=$(dirname "$(find /media -type d -name Synths 2>/dev/null | head -n 1)")/local-plugins
mkdir -p "$LD"; cp /p/vst/build/tests/Linky-1.0.0-mpc-armv7.zip "$LD/Mine-1.0.0.zip"
rm -rf /tmp/openmpc-pm; /tmp/drive_addins one Mine-1.0.0 1 > /tmp/safe-local.out
grep -q "apply_state 3" /tmp/safe-local.out || { cat /tmp/safe-local.out; ls -la "$LD"; exit 1; }
sh /tmp/openmpc-pm/apply.sh
grep -q "^local-Mine-1.0.0|ok|install local" /tmp/openmpc-pm/result
# the main catalog address unreachable: the fallback address serves it, and catalog packages still show and plan
gcc $SAN -DFS_ANY=1 -DCATALOG_URL=\"file:///nonexistent/catalog.json\" -DCATALOG_URL_FALLBACK=\"file:///p/vst/build/tests/catalog.json\" -std=gnu11 -w src/manager.c tests/drive.c -lpthread -ldl -o /tmp/drive_fallback
rm -rf /tmp/openmpc-pm; /tmp/drive_fallback one Newlib > /tmp/safe-fallback.out
grep -qF "Newlib: needs glibc 9.99 or newer, this MPC has" /tmp/safe-fallback.out || { cat /tmp/safe-fallback.out; echo "FAIL: the fallback catalog address was not used"; exit 1; }
grep -qF "trying file:///p/vst/build/tests/catalog.json" /tmp/openmpc-pm/manager.log || { echo "FAIL: no log line for the fallback"; exit 1; }
# the same with no catalog at all (no network): the zip is still a card, the status says so, and it still installs
gcc $SAN -DFS_ANY=1 -DCATALOG_URL=\"file:///nonexistent/catalog.json\" -std=gnu11 -w src/manager.c tests/drive.c -lpthread -ldl -o /tmp/drive_offline
rm -rf /tmp/openmpc-pm; /tmp/drive_offline status > /tmp/safe-offline-status.out
grep -qF "showing the zips in local-plugins (1)" /tmp/safe-offline-status.out || { cat /tmp/safe-offline-status.out; exit 1; }
rm -rf /tmp/openmpc-pm; /tmp/drive_offline one Mine-1.0.0 1 > /tmp/safe-offline.out
grep -q "apply_state 3" /tmp/safe-offline.out || { cat /tmp/safe-offline.out; exit 1; }
sh /tmp/openmpc-pm/apply.sh
grep -q "^local-Mine-1.0.0|ok|install local" /tmp/openmpc-pm/result || { cat /tmp/openmpc-pm/result /tmp/openmpc-pm/apply.log; exit 1; }
rm -rf "$LD"
# a zip of a plugin that is already installed (#782): its card is INSTALLED and offers Remove, not Install forever (mpc-one has mpc-plaits)
mkdir -p "$LD"; cp /p/vst/build/tests/Plaits-local.zip "$LD/"
rm -rf /tmp/openmpc-pm; /tmp/drive_addins localcard > /tmp/safe-localcard.out
grep -q "card 1: PL .* state=3 inst=1 .* local zip" /tmp/safe-localcard.out || { cat /tmp/safe-localcard.out; echo "FAIL: a local zip of an installed plugin is not shown as installed"; exit 1; }
grep -q "card 1: PL .* state=6 " /tmp/safe-localcard.out && grep -q "apply_state 3" /tmp/safe-localcard.out || { cat /tmp/safe-localcard.out; echo "FAIL: Remove on the card of a local zip did not queue and reach ready"; exit 1; }
grep -q "^item .local-mpc-plaits. .. remove" /tmp/openmpc-pm/apply.sh || { echo "FAIL: no remove step for the local card"; exit 1; }
rm -rf "$LD"
# backups pile up: after a success only the newest 10 MPC.settings.bak-* stay, and nothing else is touched
SD=$(dirname "$(find /media /data -name MPC.settings 2>/dev/null | head -n 1)")
for k in 01 02 03 04 05 06 07 08 09 10 11 12 13 14; do echo b > "$SD/MPC.settings.bak-t$k"; touch -d "2026-01-$k 10:00" "$SD/MPC.settings.bak-t$k"; done
echo keep > "$SD/MPC.settings.keep"
# a success writes the history: Armonly (its install.sh exits 0); the apply also empties MPC image cache (MPC is stopped, nothing has it open)
mkdir -p /var/volatile/tmp/filmstrips; : > /var/volatile/tmp/filmstrips/temp_1.img; : > /var/volatile/tmp/filmstrips/keep.txt
rm -rf /tmp/openmpc-pm; /tmp/drive_addins one Armonly > /dev/null
sh /tmp/openmpc-pm/apply.sh
[ ! -e /var/volatile/tmp/filmstrips/temp_1.img ] && [ -e /var/volatile/tmp/filmstrips/keep.txt ] || { echo "FAIL: the image cache was not emptied, or too much was"; ls /var/volatile/tmp/filmstrips; exit 1; }
grep -q "image cache emptied" /tmp/openmpc-pm/apply.log
grep -q "^armonly|ok|install 1.0.0" /tmp/openmpc-pm/result
/tmp/drive_addins status > /dev/null
H=$(find /media /data -name .openmpcpm-history.json 2>/dev/null | head -n 1 || true)
grep -q "\"id\": \"armonly\", \"version\": \"1.0.0\", \"installed\": true" "$H" || { echo "FAIL: no history entry"; cat "$H"; exit 1; }
echo '"'"'garbage'"'"' > "$H"; rm -rf /tmp/openmpc-pm; /tmp/drive_addins one Armonly > /dev/null; sh /tmp/openmpc-pm/apply.sh; /tmp/drive_addins status > /dev/null
[ "$(cat "$H")" = garbage ] || { echo "FAIL: a file that is not ours was overwritten"; exit 1; }
[ "$(ls "$SD" | grep -c '"'"'^MPC.settings.bak-'"'"')" -le 10 ] && [ ! -e "$SD/MPC.settings.bak-t01" ] && [ -e "$SD/MPC.settings.bak-t14" ] && [ -e "$SD/MPC.settings.keep" ] || { echo "FAIL: backup pruning"; ls "$SD"; exit 1; }
# the image cache nearly full (black patches on a Live III): a warning, and none when there is room
rm -rf /tmp/openmpc-pm; PM_CACHE_PCT=92 /tmp/drive_addins status > /tmp/safe-cache.out
grep -qF "image cache is 92% full: restart the MPC" /tmp/safe-cache.out || { cat /tmp/safe-cache.out; exit 1; }
rm -rf /tmp/openmpc-pm; PM_CACHE_PCT=10 /tmp/drive_addins status > /tmp/safe-cache2.out
! grep -q "image cache" /tmp/safe-cache2.out
echo "safety OK"
# last: after it the next "rm -rf /media" fails in the container (cause not found), so nothing may follow it
fi # safety
if want arch; then
echo "== arch (offline fixture): the aarch64 zip on Gen2, the armv7 one otherwise, and no card when there is none"
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media; sh tests/devices/gen2.sh   # its own device: it used to reuse the one the previous section left, so ONLY=arch alone failed
rm -rf /tmp/openmpc-pm; PM_ARCH=aarch64 /tmp/drive_addins arch > /tmp/arch-64.out
grep -q "card 1: DU" /tmp/arch-64.out      # Dual only: Armonly has no aarch64 zip
! grep -q "card 1: AR\|card 2: DU" /tmp/arch-64.out
grep -q "\"id\":\"dual\"" /tmp/arch-64.out && ! grep -q "\"id\":\"armonly\"" /tmp/arch-64.out   # the web page leaves it out too
grep -q "apply_state 3" /tmp/arch-64.out   # the aarch64 zip downloaded and verified
rm -rf /tmp/openmpc-pm; /tmp/drive_addins arch 2 > /tmp/arch-32.out
grep -q "card 1: AR" /tmp/arch-32.out && grep -q "card 2: DU" /tmp/arch-32.out
grep -q "\"id\":\"armonly\"" /tmp/arch-32.out
grep -q "apply_state 4" /tmp/arch-32.out   # Dual'"'"'s armv7 zip is missing: the install fails
echo "arch OK"
fi # arch
if want web; then
echo "== web (offline fixture): the read-only page, its JSON, and a second instance taking the port over"
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
sh tests/devices/mpc-one.sh && sh tests/addins.sh && /tmp/drive_addins web > /tmp/web.out
grep -q "Content-Type: application/json" /tmp/web.out
grep -q "\"plugins\":\[{\"id\":" /tmp/web.out
PM_MODEL="Akai Professional MPC Live III" /tmp/drive_addins web > /tmp/web-live3.out 2>&1 || true
grep -q "\"id\":\"alpha-addin\"" /tmp/web-live3.out && ! grep -q "\"id\":\"sample-only\"" /tmp/web-live3.out || { echo "the web page must hide an entry made for another MPC"; exit 1; }
grep -q "\"id\":\"sample-only\"" /tmp/web.out   # an unknown model hides nothing
grep -q "<h1>Plugin Manager</h1>" /tmp/web.out
grep -q "^screen status: On your phone or computer: http://[0-9.]*:8790$" /tmp/web.out   # the address on the screen
grep -q "404 Not Found" /tmp/web.out
[ "$(grep -c "403 Forbidden" /tmp/web.out)" = 2 ]                  # no X-PM header; a foreign Origin
grep -q "\"queued\":1," /tmp/web.out && grep -q "\"id\":\"beta-addin\",[^}]*\"state\":\"q_install\"" /tmp/web.out   # the page queues like a card press
grep -q "\"targets\":\[{\"name\":" /tmp/web.out && grep -q "^no such drive" /tmp/web.out
grep -q "409 Conflict" /tmp/web.out && grep -q "^unknown plugin" /tmp/web.out && grep -q "^nothing queued" /tmp/web.out
grep -q "async function load" /tmp/web.out
[ "$(grep -c "GET /api/state:" /tmp/web.out)" = 3 ] && ! grep -q "no answer" /tmp/web.out   # the second instance answered after the first closed
echo "-- ROM uploads (live catalog: MPC Plaits on the fake MPC One, with user_files rules added)"
gcc $SAN -DFS_ANY=1 -std=gnu11 -w src/manager.c tests/drive.c -lpthread -ldl -o /tmp/drive_web
rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
sh tests/devices/mpc-one.sh
P="/media/az01-internal/Synths/poloq - VST - MPC Plaits"
echo '"'"'{"schema": 1, "id": "mpc-plaits", "version": "1.0.0", "user_files": [{"path": "roms", "label": "ROMs", "rules": [{"label": "Test ROM", "ext": [".bin"], "size": 4}]}]}'"'"' > "$P/mpc-plugin.json"
/tmp/drive_web webfiles > /tmp/webfiles.out
grep -q "\"id\":\"mpc-plaits\",[^}]*\"files\":1" /tmp/webfiles.out        # the page offers its files
grep -q "\"id\":\"acid\",[^}]*\"files\":0" /tmp/webfiles.out
grep -q "\[{\"name\":\"My ROM.bin\",\"size\":4}\]" /tmp/webfiles.out
[ "$(grep -c "this plugin has no such folder for your files" /tmp/webfiles.out)" = 2 ]   # other, acid
grep -q "^bad folder" /tmp/webfiles.out && grep -q "^that file name cannot be used" /tmp/webfiles.out && grep -q "^refused: not from this page" /tmp/webfiles.out
[ ! -e "$P/x.bin" ] && [ ! -e "$P/evil.bin" ] && [ -z "$(ls -A "$P/roms")" ]   # nothing escaped, the delete removed the file
echo "web OK"
fi # web
'
