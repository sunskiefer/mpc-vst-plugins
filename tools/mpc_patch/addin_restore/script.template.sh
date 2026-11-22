#!/bin/sh
# addin-restore-patch.sh: restore MPC add-ins (LD_PRELOAD addins in /data/mpc-addins) after a reboot on MPC OS 3.10 (Gen1 and Gen2).
#
# On MPC OS 3.10, addins load through a systemd drop-in in /run (tmpfs). After every reboot that drop-in is gone, so
# the addins do not load until someone writes it again. This patch installs a Lua device script that MPC runs at every
# start. If addins are installed but not loaded, the script writes the /run drop-in and restarts MPC once. The user
# sees one automatic restart after a reboot; after that, all addins work. Since 1.2.0 it also brings back the 16-pad drum
# layout patch on Gen2 (its patched copy of MPC runs through a /run drop-in too), with the same single restart.
#
# The device script lives on persistent storage (the content drive: /nvme, /sdcard or /storage), so it survives reboots and
# firmware updates. It does not modify the system image.
#
#   sh addin-restore-patch.sh status                  what state the device is in
#   sh addin-restore-patch.sh install [--confirmed]   install (asks you to type PATCH first)
#   sh addin-restore-patch.sh uninstall [--confirmed] remove it (asks you to type REMOVE first)
#   sh addin-restore-patch.sh help
#
# Run it ON the device as root (ssh root@<device-ip>).
# Gen2 (aarch64) and Gen1 (armv7: device-untested). Has an effect when addins are in /data/mpc-addins or the 16-pad patch is on (Gen2).
#
# Not affiliated with Akai Professional / inMusic.
# Source: tools/mpc_patch/addin_restore (src/addin_restore.c; build.sh regenerates this script from script.template.sh).
set -u
VERSION=1.2.0
ADDINS_DIR=/data/mpc-addins
die() { echo "ERROR: $*" >&2; exit 1; }
usage() { sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

find_content_root() {
    # /storage last: the loopback content drive of a Gen2 unit without NVMe (an MPC One, a Key 37), so a device that resolves
    # /nvme, /sdcard or /data/UserData today resolves to the same one
    for r in /nvme /sdcard /data/UserData /storage; do
        [ -d "$r/MPC Documents" ] && echo "$r" && return 0
    done
    return 1
}

# The installed files are this version's: the same library for this CPU and the same device script.
is_current() {
    t=$(mktemp) || return 1
    h() { md5sum < "$1" | cut -d' ' -f1; }   # md5sum: the MPC's BusyBox has it (the drum-pad patch relies on it too)
    write_so "$t"; same=1; [ "$(h "$t")" = "$(h "$1/addin_restore.so")" ] || same=0
    write_lua "$t"; [ "$(h "$t")" = "$(h "$1/addin_restore.lua")" ] || same=0
    rm -f "$t"; [ $same = 1 ]
}

install_dir() {
    root=$(find_content_root) || return 1
    echo "$root/MPC Documents/Devices/Addin Restore"
}

# --- arguments
CMD=${1:-help}; [ $# -gt 0 ] && shift
CONFIRMED=0
while [ $# -gt 0 ]; do
    case "$1" in --confirmed) CONFIRMED=1; shift ;; *) usage 1 ;; esac
done

need_device() {
    [ "$(id -u)" = 0 ] || die "run as root on the device"
    case "$(uname -m)" in aarch64|armv7*) ;; *) die "this patch is for MPC devices (aarch64 or armv7); this one is $(uname -m)" ;; esac
    find_content_root >/dev/null || die "cannot find content root (no MPC Documents folder in /nvme, /sdcard, /data/UserData or /storage)"
}

addins_installed() {
    [ -d "$ADDINS_DIR" ] || return 1
    for d in "$ADDINS_DIR"/*/; do
        [ -d "$d" ] || continue
        for f in "$d"*.so; do [ -f "$f" ] && return 0; done
    done
    return 1
}

addins_loaded() {
    pid=$(pidof MPC 2>/dev/null | awk '{print $1}') || return 1
    [ -n "$pid" ] || return 1
    tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -q '^LD_PRELOAD=.*mpc-addins' 2>/dev/null
}

state_line() { echo "STATE state=$1 supported=$2${3:+ reason=$3}"; }

cmd_status() {
    if [ "$(id -u)" != 0 ]; then echo "Not root: run this on the device as root."; state_line unsupported 0 not-root; return; fi
    case "$(uname -m)" in aarch64|armv7*) ;; *) echo "This patch is for MPC devices (aarch64 or armv7); this device is $(uname -m)."; state_line unsupported 0 arch; return ;; esac
    root=$(find_content_root) || { echo "Cannot find content root."; state_line unsupported 0 no-root; return; }
    dir="$root/MPC Documents/Devices/Addin Restore"
    if [ -f "$dir/addin_restore.lua" ] && [ -f "$dir/addin_restore.so" ] && [ -f "$dir/addin_restore.ini" ]; then
        echo "Installed at: $dir"
        if ! is_current "$dir"; then
            echo "An older version is installed. Install updates it to $VERSION (it also brings back the 16-pad drum layout patch on Gen2)."
            state_line old-patch 1
            return
        fi
        if addins_installed; then
            n=$(find "$ADDINS_DIR" -maxdepth 2 -name '*.so' 2>/dev/null | wc -l)
            echo "Addins in $ADDINS_DIR: $n"
            if addins_loaded; then
                echo "Addins are currently loaded in MPC (LD_PRELOAD)."
                state_line patched 1
            else
                echo "Addins are NOT currently loaded. They will be restored at the next MPC start."
                state_line patched 1
            fi
        else
            echo "No addins found in $ADDINS_DIR. The patch is installed but has nothing to restore."
            state_line patched 1
        fi
        return
    fi
    if ! addins_installed; then
        echo "No addins installed in $ADDINS_DIR. Install an addin first (the addin installer does this)."
        echo "This patch is only useful when you have addins that need restoring after a reboot."
        state_line stock 1
    else
        n=$(find "$ADDINS_DIR" -maxdepth 2 -name '*.so' 2>/dev/null | wc -l)
        echo "Not installed. $n addin(s) in $ADDINS_DIR."
        echo "Content root: $root"
        if addins_loaded; then echo "Addins are currently loaded."; else echo "Addins are NOT currently loaded."; fi
        state_line stock 1
    fi
}

cmd_install() {
    need_device
    dir=$(install_dir) || die "cannot determine install directory"
    update=0
    if [ -f "$dir/addin_restore.lua" ] && [ -f "$dir/addin_restore.so" ] && [ -f "$dir/addin_restore.ini" ]; then
        if is_current "$dir"; then echo "Already installed ($VERSION) at $dir. Nothing to do."; state_line patched 1; exit 0; fi
        update=1
    fi
    [ $update = 1 ] && echo "An older version is installed at $dir: it is replaced by $VERSION."
    echo "Installing the addin restore patch ($VERSION):"
    echo "  destination: $dir"
    echo "  files: addin_restore.ini, addin_restore.lua, addin_restore.so"
    echo "  effect: at every MPC start, if addins in $ADDINS_DIR are not loaded,"
    echo "          writes a /run systemd drop-in and restarts MPC once. The same for the"
    echo "          16-pad drum layout patch on Gen2, if it is installed (one restart for both)."
    echo "  MPC is NOT restarted by this installer. The restore runs at the next MPC start."
    echo
    if ! addins_installed && [ ! -f /data/mpc-drumpad/mpc-drum-pad-patch.sh ]; then
        echo "  NOTE: no addins in $ADDINS_DIR and no 16-pad drum layout patch right now."
        echo "  The patch has no effect until one of them is installed."
        echo
    fi
    if [ "$CONFIRMED" = 0 ]; then
        printf "Type PATCH to continue: "
        read -r a < /dev/tty 2>/dev/null || read -r a
        [ "$a" = PATCH ] || die "cancelled; nothing was changed"
    fi
    mkdir -p "$dir" || die "cannot create $dir"
    # Write the three files
    write_lua "$dir/addin_restore.lua.new"
    write_so  "$dir/addin_restore.so.new"
    write_ini "$dir/addin_restore.ini.new"
    [ "$(dd if="$dir/addin_restore.so.new" bs=1 skip=1 count=3 2>/dev/null)" = "ELF" ] || { rm -f "$dir"/addin_restore.*.new; die "the .so came out empty or broken (is openssl on this device?); nothing was changed"; }
    chmod 644 "$dir/addin_restore.lua.new" "$dir/addin_restore.ini.new"
    chmod 755 "$dir/addin_restore.so.new"
    for f in lua so ini; do mv "$dir/addin_restore.$f.new" "$dir/addin_restore.$f" || die "could not put addin_restore.$f in place"; done
    # Verify
    [ -f "$dir/addin_restore.lua" ] || die "lua not written"
    [ -f "$dir/addin_restore.so" ]  || die "so not written"
    [ -s "$dir/addin_restore.so" ] && [ "$(dd if="$dir/addin_restore.so" bs=1 skip=1 count=3 2>/dev/null)" = "ELF" ] || { rm -f "$dir/addin_restore.lua" "$dir/addin_restore.so" "$dir/addin_restore.ini"; die "the .so came out empty or broken (is openssl on this device?); nothing was installed"; }
    [ -f "$dir/addin_restore.ini" ] || die "ini not written"
    so_size=$(wc -c < "$dir/addin_restore.so")
    echo "Installed ($so_size bytes .so). MPC will pick it up at the next start."
    echo "After a reboot: MPC starts, the script detects missing addins, writes the"
    echo "drop-in, and restarts MPC. One restart, then everything works."
    state_line patched 1
}

cmd_uninstall() {
    need_device
    dir=$(install_dir) || die "cannot determine install directory"
    if [ ! -d "$dir" ]; then echo "Not installed. Nothing to do."; exit 0; fi
    if [ ! -f "$dir/addin_restore.lua" ] && [ ! -f "$dir/addin_restore.so" ] && [ ! -f "$dir/addin_restore.ini" ]; then
        echo "Directory exists but files are missing. Removing directory."
        rmdir "$dir" 2>/dev/null || rm -rf "$dir"
        exit 0
    fi
    if [ "$CONFIRMED" = 0 ]; then
        echo "This removes the addin restore device script from: $dir"
        echo "Addins already loaded in the current MPC session are not affected."
        echo "After the next reboot, addins will NOT be restored automatically."
        printf "Type REMOVE to continue: "
        read -r a < /dev/tty 2>/dev/null || read -r a
        [ "$a" = REMOVE ] || die "cancelled; nothing was changed"
    fi
    rm -f "$dir/addin_restore.lua" "$dir/addin_restore.so" "$dir/addin_restore.ini"
    rmdir "$dir" 2>/dev/null || true
    echo "Removed. Addins will not be restored after the next reboot."
    state_line stock 1
}

write_lua() {
    cat > "$1" <<'LUA_PAYLOAD_EOF'
-- addin_restore: Lua device script that restores LD_PRELOAD addins after a reboot.
-- Runs inside MPC's Lua VM (io=nil, os=nil; package.loadlib works).

local function find_module()
    if debug and debug.getinfo then
        local src = debug.getinfo(1, "S").source
        if src and src:sub(1,1) == "@" then
            local dir = src:sub(2):match("(.*/)")
            if dir then return dir .. "addin_restore.so" end
        end
    end
    for _, root in ipairs({"/nvme", "/sdcard", "/data/UserData"}) do
        local path = root .. "/MPC Documents/Devices/Addin Restore/addin_restore.so"
        local f = package.loadlib(path, "*")
        if f then return path end
    end
    return nil
end

local function log(msg)
    if print then pcall(print, "[addin-restore] " .. tostring(msg)) end
end

local function try_restore()
    log("try_restore start")

    if not package or type(package.loadlib) ~= "function" then
        log("no package.loadlib")
        return "no-loadlib"
    end

    local module = find_module()
    log("module path: " .. tostring(module))
    if not module then
        log("could not find addin_restore.so")
        return "no-module"
    end

    local fn, err1, err2 = package.loadlib(module, "addin_restore_check")
    log("loadlib: fn=" .. tostring(fn) .. " err1=" .. tostring(err1) .. " err2=" .. tostring(err2))

    if type(fn) ~= "function" then
        log("loadlib failed")
        return "load-failed"
    end

    log("calling addin_restore_check")
    local ok, result = pcall(fn)
    log("result: ok=" .. tostring(ok) .. " val=" .. tostring(result))

    return "done:" .. tostring(result)
end

log("script loaded")
local r = try_restore()
log("direct: " .. tostring(r))

if addin_restore ~= nil then
    log("registering OnLoad/OnUnload")
    function addin_restore.OnLoad()
        log("OnLoad")
        return try_restore()
    end
    function addin_restore.OnUnload()
        log("OnUnload")
    end
end

return "helper-registered"
LUA_PAYLOAD_EOF
}
write_ini() {
    cat > "$1" <<'INI_PAYLOAD_EOF'
[info]
device=Addin Restore
manufacturer=OpenMPC
script=addin_restore.lua
scriptVersion=1.0
checksum=dd21e94f08aa4e5850c626278b83391867736b52226901d1133d6028259d5e65
INI_PAYLOAD_EOF
}
write_so() {
    case "$(uname -m)" in aarch64) write_so_aarch64 "$1" ;; *) write_so_armv7 "$1" ;; esac
}

write_so_aarch64() {
    openssl base64 -d > "$1" <<'SO_PAYLOAD_EOF'
@@SO_AARCH64@@
SO_PAYLOAD_EOF
}

# Gen1 (armv7) build of the same addin_restore.c (arm-linux-gnueabihf-gcc, Debian bullseye; needs GLIBC 2.4)
write_so_armv7() {
    openssl base64 -d > "$1" <<'SO_PAYLOAD_EOF'
@@SO_ARMV7@@
SO_PAYLOAD_EOF
}

case "$CMD" in
    status) cmd_status ;;
    install) cmd_install ;;
    uninstall) cmd_uninstall ;;
    help|-h|--help) usage 0 ;;
    *) usage 1 ;;
esac
