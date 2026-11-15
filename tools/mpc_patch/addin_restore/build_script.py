#!/usr/bin/env python3
"""Generate addin-restore-patch.sh from script.template.sh and the two libraries in src/ (build.sh builds them first).

The libraries are embedded as base64 (the script decodes them with `openssl base64 -d`, as 1.1.0 did), so the one script the
device runs is the whole patch (docs/PATCHES.md). Output is LF. A path argument writes there instead (tests).
"""
import base64
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def lib(name, machine):
    p = os.path.join(HERE, "src", name)
    if not os.path.isfile(p):
        sys.exit("missing %s: run tools/mpc_patch/addin_restore/build.sh first" % p)
    b = open(p, "rb").read()
    if b[:4] != b"\x7fELF" or int.from_bytes(b[18:20], "little") != machine:
        sys.exit("%s is not the expected ELF (machine %d)" % (name, machine))
    s = base64.b64encode(b).decode()
    return "\n".join(s[i:i + 64] for i in range(0, len(s), 64))


tpl = open(os.path.join(HERE, "script.template.sh"), encoding="utf-8").read()
for m in ("@@SO_AARCH64@@", "@@SO_ARMV7@@"):
    if tpl.count(m) != 1:
        sys.exit("script.template.sh must have %s once" % m)
out = tpl.replace("@@SO_AARCH64@@", lib("addin_restore-aarch64.so", 183)).replace("@@SO_ARMV7@@", lib("addin_restore-armv7.so", 40))
dst = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "addin-restore-patch.sh")
with open(dst, "w", encoding="utf-8", newline="\n") as f:
    f.write(out)
print("wrote", dst)
