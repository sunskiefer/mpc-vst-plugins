#!/usr/bin/env python3
"""Regenerates src/vst2_migrate_sh.h from tools/vst2_migrate.sh (MPC_VST: the mpc-vst-plugins checkout)."""
import os
H = os.path.dirname(os.path.abspath(__file__))
mv = os.environ.get("MPC_VST", os.path.join(H, "..", "..", ".."))
sh = open(os.path.join(mv, "tools", "vst2_migrate.sh")).read()
body = "\n".join('    "%s\\n"' % l.replace("\\", "\\\\").replace('"', '\\"').replace("??", "?\\?") for l in sh.rstrip("\n").split("\n"))
open(os.path.join(H, "..", "src", "vst2_migrate_sh.h"), "w").write(
    "/* tools/vst2_migrate.sh (copy plugin folders into VST2 Plugins, write plugin.json), run by apply.sh on MPC OS 3.10.1+; "
    "regenerate with vst/gen_vst2.py */\n"
    "static const char VST2_MIGRATE_SH[] =\n%s;\n" % body)
