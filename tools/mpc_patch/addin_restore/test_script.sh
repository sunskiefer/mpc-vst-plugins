#!/bin/sh
# Offline test of addin-restore-patch.sh: status, install, the update of an older install (1.1.0's library) and uninstall, in a
# throwaway container (it writes /nvme and /data) with a uname shim saying aarch64, then armv7. Needs Docker.
#   tools/mpc_patch/addin_restore/test_script.sh
set -eu
cd "$(dirname "$0")"
old=$(mktemp -d)   # the published 1.1.0 script, from main: a device that has it must see "older version" and get updated
git show origin/main:tools/mpc_patch/addin_restore/addin-restore-patch.sh > "$old/old.sh" 2>/dev/null || git show main:tools/mpc_patch/addin_restore/addin-restore-patch.sh > "$old/old.sh"
grep -q "^VERSION=1.1.0" "$old/old.sh" || { echo "main does not have the 1.1.0 script any more: update this test"; exit 1; }
trap 'rm -rf "$old"' EXIT
docker run --rm -v "$PWD":/t:ro -v "$old":/old:ro gcc:12 sh -euc '
fail() { echo "FAIL $*"; exit 1; }
for arch in aarch64 armv7l; do
  rm -rf /nvme /data /tmp/shim; mkdir -p "/nvme/MPC Documents" /tmp/shim
  printf "#!/bin/sh\necho $arch\n" > /tmp/shim/uname; chmod +x /tmp/shim/uname
  export PATH=/tmp/shim:$PATH
  # the devices BusyBox 1.36.1 has no head -c and no od -A (device, 2026-11-15): the script must not use them
  grep -nE "head +-c|od +-A" /t/addin-restore-patch.sh && fail "$arch: script uses head -c / od -A (BusyBox on the device lacks them)"
  S=/t/addin-restore-patch.sh; D="/nvme/MPC Documents/Devices/Addin Restore"
  sh $S status | grep -qx "STATE state=stock supported=1" || fail "$arch: not stock before install"
  sh $S install --confirmed > /tmp/o || fail "$arch: install"; grep -q "16-pad drum layout patch on Gen2" /tmp/o || fail "$arch: install text"
  [ -f "$D/addin_restore.so" ] && [ -f "$D/addin_restore.lua" ] && [ -f "$D/addin_restore.ini" ] || fail "$arch: files"
  ls "$D" | grep -q "\.new$" && fail "$arch: .new left"
  m=$(head -c 20 "$D/addin_restore.so" | od -An -tx1 | tr -d " \n"); case $arch in aarch64) want=b700;; *) want=2800;; esac
  got=$(echo "$m" | cut -c37-40); [ "$got" = "$want" ] || fail "$arch: wrong ELF machine $got"
  sh $S status | grep -qx "STATE state=patched supported=1" || fail "$arch: not patched after install"
  sh $S install --confirmed | grep -q "Already installed" || fail "$arch: second install"
  echo "older library" > "$D/addin_restore.so"
  sh $S status | grep -qx "STATE state=old-patch supported=1" || fail "$arch: older not seen"
  sh $S install --confirmed | grep -q "is replaced by 1.2.0" || fail "$arch: update"
  sh $S status | grep -qx "STATE state=patched supported=1" || fail "$arch: not patched after update"
  sh $S uninstall --confirmed | grep -qx "STATE state=stock supported=1" || fail "$arch: uninstall"
  [ ! -d "$D" ] || fail "$arch: folder left"
  sh /old/old.sh install --confirmed >/dev/null || fail "$arch: 1.1.0 install"
  sh $S status | grep -qx "STATE state=old-patch supported=1" || fail "$arch: 1.1.0 not seen as older"
  sh $S install --confirmed >/dev/null && sh $S status | grep -qx "STATE state=patched supported=1" || fail "$arch: 1.1.0 not updated"
  grep -q "^device=Addin Restore" "$D/addin_restore.ini" || fail "$arch: ini"
  sh $S uninstall --confirmed >/dev/null
  echo "ok   $arch: status, install, older install updated, uninstall, 1.1.0 updated in place"
done'
