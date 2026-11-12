#!/usr/bin/env bash
# Old clients against a new catalog: do Plugin Managers and desktop installers already in users' hands still show the same
# plugins from CANDIDATE as from BASELINE? Runs each released client's own code (from its git tag) on both catalogs and diffs.
#
#   tools/compat/run.sh BASELINE.json CANDIDATE.json [BASELINE.tsv CANDIDATE.tsv]
#
# BASELINE: the catalog as published today (e.g. curl https://open-mpc.github.io/catalog.json). CANDIDATE: the one a change
# builds (catalog/dist/catalog.json). Exit 0 if every client sees the same thing in both; otherwise prints the diffs.
# A change that is meant to show differently (a new plugin, a new version) diffs too: read the diff, it should be only that.
# Needs Docker (gcc:12, golang:1.26) and the repo's tags. Clients: MANAGERS / INSTALLERS below (oldest still in use, newest).
set -euo pipefail
MANAGERS="${MANAGERS:-plugin-manager-g2-v0.2.0 plugin-manager-g2-v0.2.5}"
INSTALLERS="${INSTALLERS:-desktop-v0.5.5 desktop-v1.1.2}"
here="$(cd "$(dirname "$0")" && pwd)"; repo="$(cd "$here/../.." && pwd)"
[ $# -ge 2 ] || { sed -n 2,11p "$0"; exit 2; }
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
cp "$1" "$work/base.json"; cp "$2" "$work/cand.json"
[ $# -ge 4 ] && { cp "$3" "$work/base.tsv"; cp "$4" "$work/cand.tsv"; }
cp "$here/old_manager.c" "$here/old_installer_test.go" "$work/"
fail=0

for t in $MANAGERS; do
  d="$work/$t"; mkdir -p "$d/devices"
  git -C "$repo" archive "$t" tools/plugin_manager_g2/src | tar -x -C "$d" && mv "$d/tools/plugin_manager_g2/src"/* "$d/"
  git -C "$repo" show "$t:tools/plugin_manager_g2/tests/devices/gen2.sh" > "$d/devices/gen2.sh"
  git -C "$repo" show "$t:tools/plugin_manager_g2/tests/devices/mpc-one.sh" > "$d/devices/mpc-one.sh"
done
for t in $INSTALLERS; do
  d="$work/$t"; mkdir -p "$d"
  git -C "$repo" archive "$t" tools/desktop | tar -x -C "$d"
  cp "$here/old_installer_test.go" "$d/tools/desktop/zz_compat_test.go"
done

# Managers: each on a Gen2 and a Gen1 stand-in device (the devices of that release's own tests), one catalog at a time.
timeout 900 docker run --rm -v "$work":/w -w /w -e MANAGERS="$MANAGERS" gcc:12 bash -c '
set -e
export PM_GLIBC=2.39 PM_TAPMS=40
for t in $MANAGERS; do
  gcc -DFS_ANY=1 -DCATALOG_URL=\"file:///w/cat.json\" -DCATALOG_URL_FALLBACK=\"file:///w/cat.json\" -std=gnu11 -w "$t/manager.c" old_manager.c -lpthread -ldl -o "$t/drive" 2>"$t/build.log" ||
    gcc -DFS_ANY=1 -DCATALOG_URL=\"file:///w/cat.json\" -std=gnu11 -w "$t/manager.c" old_manager.c -lpthread -ldl -o "$t/drive"
  for dev in gen2 mpc-one; do
    for c in base cand; do
      rm -rf /media /sdcard /nvme /synths /data /tmp/openmpc-pm /tmp/pm-mounts; mkdir -p /media
      sh "$t/devices/$dev.sh" >/dev/null 2>&1
      cp "$c.json" cat.json
      "$t/drive" > "$t.$dev.$c.out" 2>/dev/null || true
    done
  done
done'

for t in $MANAGERS; do for dev in gen2 mpc-one; do
  n=$(grep -c "^[^ =].* | " "$work/$t.$dev.base.out" || true)
  [ "$n" -gt 0 ] || { echo "ERROR $t on $dev showed no cards:"; head -5 "$work/$t.$dev.base.out"; fail=1; continue; }
  if grep -q "^FAIL" "$work/$t.$dev.cand.out" || ! diff -q "$work/$t.$dev.base.out" "$work/$t.$dev.cand.out" >/dev/null; then
    echo "DIFF  $t on $dev"; diff "$work/$t.$dev.base.out" "$work/$t.$dev.cand.out" | head -40; fail=1
  else echo "same  $t on $dev ($n card rows)"; fi
done; done

# Installers: their own parseCatalog + ForArch.
for t in $INSTALLERS; do
  for c in base cand; do
    timeout 600 docker run --rm -v "$work":/w -w "/w/$t/tools/desktop" -e CATALOG="/w/$c.json" -e GOTOOLCHAIN=local -v mpc-compat-gomod:/go/pkg/mod golang:1.26 \
      go test -v -count=1 -run TestCompatDump . 2>&1 | grep -v "^ok\|^PASS\|^---\|^=== \|^FAIL\s\|^go: downloading" > "$work/$t.$c.out" || true
  done
  if ! head -1 "$work/$t.base.out" | grep -qE "^[0-9]+ plugins$"; then
    echo "ERROR $t did not run:"; head -5 "$work/$t.base.out"; fail=1; continue
  fi
  if grep -q "^FAIL" "$work/$t.cand.out" || ! diff -q "$work/$t.base.out" "$work/$t.cand.out" >/dev/null; then
    echo "DIFF  $t"; diff "$work/$t.base.out" "$work/$t.cand.out" | head -40; fail=1
  else echo "same  $t ($(head -1 "$work/$t.base.out"))"; fi
done

# mpc-store.sh and old helpers read catalog.tsv by column: the header and column count must not change.
if [ -f "$work/base.tsv" ]; then
  if [ "$(head -1 "$work/base.tsv")" != "$(head -1 "$work/cand.tsv")" ] || [ "$(awk -F'\t' '{print NF}' "$work/base.tsv" | sort -u)" != "$(awk -F'\t' '{print NF}' "$work/cand.tsv" | sort -u)" ]; then
    echo "DIFF  catalog.tsv columns"; head -1 "$work/base.tsv"; head -1 "$work/cand.tsv"; fail=1
  else echo "same  catalog.tsv columns"; fi
fi
exit $fail
