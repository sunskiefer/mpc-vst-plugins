#!/bin/sh
# Copy installed plugin folders into "VST2 Plugins" so MPC OS 3.10.1+ lists them (PRIVATE PROTOTYPE, local branch only).
#
# On 3.10.1 the plugin browser no longer lists plugins registered in MPC.settings; it lists folders in
# "<drive>/VST2 Plugins/<Manufacturer> - VST - <Name>/" that hold a plugin.json. Saved projects still load the old copy while
# it exists and fall back to the VST2 copy when it is gone (device-tested 2026-10-31).
#
# For every plugin folder in a Synths location this copies the whole folder (user ROMs, carts, presets included) to
# "VST2 Plugins" on the same drive, writes plugin.json, and copies sibling folders the skin points to with "../../<dir>/".
# It never edits MPC.settings and never deletes the old folder. MPC must be restarted to see the new plugins.
#
# Usage: vst2_migrate.sh [--apply] [--force-off]       default: dry run (prints the plan, changes nothing)
#   --apply      do it
#   --force-off  run even when VST2 Support is off (the copies are then unused until it is switched on)
#        vst2_migrate.sh --json DIR                    write DIR/plugin.json for one plugin folder (after an install into VST2 Plugins)
# Env (tests): ROOT=<prefix for every absolute path>, ARCH=<uname -m>.
set -u
ROOT=${ROOT:-}
APPLY=0 FORCE=0 JSON=
while [ $# -gt 0 ]; do
    case $1 in
        --apply) APPLY=1 ;;
        --force-off) FORCE=1 ;;
        --json) [ -n "${2:-}" ] || { echo "--json needs a folder" >&2; exit 2; }; JSON=$2; shift ;;
        *) echo "usage: $0 [--apply] [--force-off] | --json DIR" >&2; exit 2 ;;
    esac
    shift
done
ARCH=${ARCH:-$(uname -m)}
case $ARCH in aarch64) KEY=file64 ;; *) KEY=file32 ;; esac

SETTINGS=
for s in "$ROOT"/data/Settings/MPC/MPC.settings "$ROOT"/media/az01-internal/Settings/MPC/MPC.settings; do
    [ -f "$s" ] && { SETTINGS=$s; break; }
done
[ -n "$SETTINGS" ] || [ -z "$JSON" ] || SETTINGS=/dev/null   # --json reads the folder's plugin-meta.xml; MPC.settings is only a fallback
[ -n "$SETTINGS" ] || { echo "MPC.settings not found" >&2; exit 1; }
if [ -n "$JSON" ]; then :
elif grep -q 'Enhancements.VST2Support" val="1"' "$SETTINGS"; then
    echo "VST2 Support: on"
elif grep -q 'Enhancements.VST2Support' "$SETTINGS"; then
    echo "VST2 Support: off (Preferences > Enhancements)"; [ $FORCE = 1 ] || exit 3
else
    echo "This MPC OS has no VST2 Support setting (needs 3.10.1 or later): nothing to do."; exit 3
fi

# Every PLUGIN element of the arch's list, one per line (they span several lines in MPC.settings).
case $ARCH in aarch64) LIST=pluginList-arm-64bit ;; *) LIST=pluginList-arm ;; esac
ENTRIES=$(awk -v list="$LIST" '
    index($0, "<VALUE name=\"" list "\"") { on = 1 }
    on && /<\/VALUE>/ { on = 0 }
    on { gsub(/\r/, ""); buf = buf " " $0; if (index(buf, "/>")) { if (index(buf, "<PLUGIN ")) print substr(buf, index(buf, "<PLUGIN ")); buf = "" } }
' "$SETTINGS")

attr() {   # attr NAME "<PLUGIN ... />"
    printf '%s\n' "$2" | sed -n "s/.* $1=\"\([^\"]*\)\".*/\1/p" | head -n 1
}
jstr() {   # JSON string body: escape \ and "
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}
unxml() {
    printf '%s' "$1" | sed 's/&quot;/"/g; s/&apos;/'"'"'/g; s/&lt;/</g; s/&gt;/>/g; s/&amp;/\&/g'
}

element() {   # element DIR LOC: the folder's plugin-meta.xml, else the MPC.settings entry whose file= is inside it
    [ -f "$1/plugin-meta.xml" ] && { tr '\r\n' '  ' < "$1/plugin-meta.xml"; return; }
    e=$(printf '%s\n' "$ENTRIES" | grep -F "file=\"$2/${1##*/}/" | head -n 1)
    [ -n "$e" ] || e=$(printf '%s\n' "$ENTRIES" | grep -F "/${1##*/}/" | head -n 1)   # registered from another location (the old Synths copy)
    printf '%s\n' "$e"
}
write_json() {   # write_json OUT ELEMENT
    el=$2
    name=$(unxml "$(attr name "$el")"); mfr=$(unxml "$(attr manufacturer "$el")"); uid=$(attr uid "$el"); file=$(attr file "$el")
    ins=$(attr numInputs "$el"); outs=$(attr numOutputs "$el")
    isi=$(attr isInstrument "$el"); kind=instrument; [ "$isi" = 0 ] && kind=effect
    cat=$(unxml "$(attr category "$el")"); ver=$(attr version "$el")
    {
        printf '{\n  "name": "%s",\n  "manufacturer": "%s",\n' "$(jstr "$name")" "$(jstr "$mfr")"
        printf '  "uid": "0x%s",\n  "kind": "%s",\n' "$uid" "$kind"
        [ -n "$ver" ] && printf '  "version": "%s",\n' "$(jstr "$ver")"
        [ -n "$cat" ] && printf '  "category": "%s",\n' "$(jstr "$cat")"
        printf '  "numInputs": %s,\n  "numOutputs": %s,\n' "${ins:-0}" "${outs:-2}"
        printf '  "%s": "%s"\n}\n' "$KEY" "$(jstr "${file##*/}")"
    } > "$1.new" && mv "$1.new" "$1"
}

# Synths locations: the content locations MPC lists, plus the usual ones; only writable, existing ones.
LOCS=$(sed -n 's:.*<Location>\(.*\)</Location>.*:\1:p' "$SETTINGS")
LOCS=$(printf '%s\n' "$LOCS" /nvme/Synths /sdcard/Synths /media/az01-internal/Synths | sed 's:/*$::' | awk 'NF && !seen[$0]++')

# MPC OS 3.10.1 draws a plugin's skin from an old copy of the same folder in a Synths location when there is one, even when it
# runs the plugin from VST2 Plugins (Live III, 2026-11-02): set that old "Plugin Skins" aside (renamed, not deleted) once the
# VST2 copy has its own. retire_old_skins FOLDER VST2_DIR
retire_old_skins() {
    [ -d "$ROOT$2/$1/Plugin Skins" ] || return 0
    printf '%s\n' "$LOCS" | while IFS= read -r l; do
        o="$ROOT$l/$1"
        [ -d "$o/Plugin Skins" ] || continue
        if [ $APPLY = 1 ] || [ -n "$JSON" ]; then
            rm -rf "$o/Plugin Skins.vst2-moved" && mv "$o/Plugin Skins" "$o/Plugin Skins.vst2-moved" && echo "SKIN  $1: old skin in $l set aside (MPC would draw it instead of the new one)"
        else
            echo "SKIN  $1: old skin in $l would be set aside"
        fi
    done
}

if [ -n "$JSON" ]; then
    d=${JSON%/}
    el=$(element "$ROOT$d" "${d%/*}")
    [ -n "$el" ] || { echo "$d: no plugin-meta.xml and no MPC.settings entry" >&2; exit 1; }
    uid=$(attr uid "$el"); case $uid in *[!0-9a-fA-F]*|'') echo "$d: uid \"$uid\" is not hex" >&2; exit 1 ;; esac
    write_json "$ROOT$d/plugin.json" "$el" && echo "wrote $d/plugin.json" || exit 1
    retire_old_skins "${d##*/}" "${d%/*}"
    exit 0
fi


n_ok=0 n_skip=0 n_have=0
printf '%s\n' "$LOCS" | while IFS= read -r loc; do
    [ -d "$ROOT$loc" ] || continue
    case $loc in /usr/*|/synths/*) continue ;; esac   # Akai's own read-only content
    drive=${loc%/*}; [ -n "$drive" ] || continue
    vdir="$drive/VST2 Plugins"
    for d in "$ROOT$loc"/*/; do
        [ -d "$d" ] || continue
        d=${d%/}; folder=${d##*/}
        case $folder in *" - VST - "*) ;; *) continue ;; esac   # shared asset folders (e.g. "AW Skin") are copied with their users
        el=$(element "$d" "$loc")
        if [ -z "$el" ]; then echo "SKIP  $folder: no plugin-meta.xml and no MPC.settings entry (no plugin here?)"; continue; fi
        name=$(unxml "$(attr name "$el")"); mfr=$(unxml "$(attr manufacturer "$el")"); uid=$(attr uid "$el")
        file=$(attr file "$el"); so=${file##*/}
        if [ "$folder" != "$mfr - VST - $name" ]; then
            echo "SKIP  $folder: folder name is not \"<manufacturer> - VST - <name>\" ($mfr / $name), MPC would not match it"; continue
        fi
        if [ ! -f "$d/$so" ]; then echo "SKIP  $folder: binary $so not in the folder"; continue; fi
        case $uid in *[!0-9a-fA-F]*|'') echo "SKIP  $folder: uid \"$uid\" is not hex"; continue ;; esac
        dest="$ROOT$vdir/$folder"
        if [ -f "$dest/plugin.json" ]; then echo "HAVE  $folder (already in $vdir)"; retire_old_skins "$folder" "$vdir"; continue; fi
        # sibling folders the skin uses ("../../AW Skin/x.png")
        sibs=$(cat "$d/Plugin Skins/"*.json 2>/dev/null | grep -o '\.\./\.\./[^/"]*/' | sed 's:^\.\./\.\./::; s:/$::' | sort -u)
        need=$(du -sk "$d" | cut -f1)
        free=$(df -k "$ROOT$drive" | awk 'NR==2 {print $4}')
        if [ "$need" -gt "$(( free * 9 / 10 ))" ]; then echo "SKIP  $folder: needs ${need} KB, $free KB free on $drive"; continue; fi
        echo "COPY  $folder -> $vdir/ ($need KB${sibs:+, with sibling: $(echo $sibs)})"
        [ $APPLY = 1 ] || continue
        mkdir -p "$ROOT$vdir" || exit 1
        rm -rf "$dest.new"
        cp -a "$d" "$dest.new" || { echo "FAIL  $folder: copy failed" >&2; rm -rf "$dest.new"; continue; }
        write_json "$dest.new/plugin.json" "$el" || { echo "FAIL  $folder: plugin.json" >&2; rm -rf "$dest.new"; continue; }
        mv "$dest.new" "$dest" || { echo "FAIL  $folder: rename failed" >&2; continue; }
        retire_old_skins "$folder" "$vdir"
        printf '%s\n' "$sibs" | while IFS= read -r s; do   # names can hold spaces ("AW Skin")
            [ -n "$s" ] && [ -d "$ROOT$loc/$s" ] && [ ! -e "$ROOT$vdir/$s" ] && cp -a "$ROOT$loc/$s" "$ROOT$vdir/$s.new" && mv "$ROOT$vdir/$s.new" "$ROOT$vdir/$s"
        done
    done
done
[ $APPLY = 1 ] && echo "Done. Restart MPC to see the plugins under External VST Plugins." || echo "Dry run: nothing changed. Run with --apply."
