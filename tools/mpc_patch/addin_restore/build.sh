#!/bin/sh
# Build src/addin_restore.c for the device (aarch64 for Gen2, armv7 hard-float for Gen1, both against glibc 2.31) and
# regenerate addin-restore-patch.sh from script.template.sh.
#   ./build.sh          both libraries + the script
#   ./build.sh test     host tests of addin_restore.c (ASan/UBSan; gcc on the host)
#   ./build.sh script   libraries must already exist; rewrite the script only
# The cross compiler is Zig's (`zig cc`), from PyPI: python3 -m pip install ziglang==0.13.0 (or set ZIG to a zig binary).
# No Docker or qemu needed, and the glibc floor is chosen by the target, not by the build machine.
set -eu
cd "$(dirname "$0")"

if [ "${1:-}" = test ]; then
    cd src
    CFLAGS="-O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer" \
        LD_PRELOAD=$(gcc -print-file-name=libasan.so) ASAN_OPTIONS=detect_leaks=0 sh test_addin_restore.sh
    exit 0
fi

if [ "${1:-}" != script ]; then
    if [ -n "${ZIG:-}" ]; then zig() { "$ZIG" "$@"; }; else zig() { python3 -m ziglang "$@"; }; fi
    # no compiler runtime and no unwind tables: the libraries then need only libc (no __aeabi_unwind_* from libgcc_s on armv7)
    lib() { zig build-lib -dynamic -fno-compiler-rt -lc -target "$1" -O ReleaseSmall -fstrip -cflags -std=gnu11 -fvisibility=hidden \
            -fno-unwind-tables -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -- src/addin_restore.c -femit-bin="$2"; }
    lib aarch64-linux-gnu.2.31 src/addin_restore-aarch64.so
    lib arm-linux-gnueabihf.2.31 src/addin_restore-armv7.so   # baseline ARMv7-A hard-float, as the MPC's Gen1 userland
    rm -f src/*.so.o src/*.a
    for s in src/addin_restore-aarch64.so src/addin_restore-armv7.so; do
        echo "$s: $(wc -c < "$s") bytes, needs $(objdump -T "$s" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -n 1)"
        if objdump -T "$s" 2>/dev/null | grep ' \*UND\*' | grep -v GLIBC | grep -v ' w '; then echo "$s needs a symbol libc does not give"; exit 1; fi
    done
fi

python3 build_script.py
