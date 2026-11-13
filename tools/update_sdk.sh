#!/bin/sh
# Move the vendored Akai MPC Plugin SDK (vendor/mpc-plugin-builder-sdk, a git submodule) to its latest commit, or to COMMIT.
#   tools/update_sdk.sh [COMMIT]     then review the SDK's changes (printed) and commit the new pin in a PR
set -eu
cd "$(dirname "$0")/.."
D=vendor/mpc-plugin-builder-sdk
git submodule update --init "$D"
old=$(git -C "$D" rev-parse HEAD)
git -C "$D" fetch -q origin
git -C "$D" checkout -q "${1:-origin/main}"
new=$(git -C "$D" rev-parse HEAD)
[ "$old" = "$new" ] && { echo "already at $new"; exit 0; }
echo "Akai SDK: $old -> $new"
git -C "$D" log --oneline "$old..$new"
echo; echo "Changed files:"; git -C "$D" diff --stat "$old" "$new" | tail -n 20
echo; echo "Now: git add $D && git commit -m 'Vendored Akai SDK: ${new%"${new#???????}"}'   (and check docs/SDK-PORTING.md against the changes)"
