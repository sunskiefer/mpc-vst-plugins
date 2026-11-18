#!/usr/bin/env python3
"""Install and uninstall a release zip against a scratch Synths folder, using the package's own scripts.

  tools/install_smoke.py dist/My-Plugin-1.0.0-mpc-armv7.zip [--keep]

Unpacks the zip, writes a minimal MPC.settings (one SynthContentLocations entry, as on a device) and runs
`sh install.sh -y -n -t <scratch>/Synths` with MPC_INSTALL_TEST=1: the mode the test suite uses, where the
installer skips the root/arch/systemctl checks and logs the MPC stop/start calls instead of touching MPC.
It then checks that the plugin folder arrived whole (skin, .so, plugin-meta.xml), that exactly one plugin-list
entry was added, that a second run is a clean upgrade and not a duplicate, and that uninstall.sh removes both
while keeping its settings backup. Needs sh, awk, sed and sha256sum: Git Bash on Windows, or any POSIX shell.

This is the last check before a zip is uploaded; it covers what a catalog check cannot see (the scripts
actually running, their paths, plugin_list.awk editing the XML), but it is not a substitute for a device.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

SETTINGS = """<?xml version="1.0" encoding="UTF-8"?>
<PROPERTIES>
  <VALUE name="SynthContentLocations">
    <Location>%(synths)s</Location>
  </VALUE>
</PROPERTIES>
"""


def shell_path(path):
    """The path as the shell sees it: /c/Users/... for a Windows path (Git Bash), unchanged elsewhere."""
    p = os.path.abspath(path).replace("\\", "/")
    m = re.match(r"^([A-Za-z]):(/.*)$", p)
    return "/" + m.group(1).lower() + m.group(2) if m else p


def find_sh():
    """The POSIX shell to run the scripts with: on PATH, or where Git for Windows keeps it (a Windows python
    cannot see Git Bash's /usr/bin)."""
    for c in (shutil.which("sh"), shutil.which("bash"), shutil.which("sh", path="/usr/bin:/bin"),
              r"C:\Program Files\Git\usr\bin\sh.exe", r"C:\Program Files\Git\bin\sh.exe",
              r"C:\Program Files (x86)\Git\usr\bin\sh.exe", "/bin/sh", "/usr/bin/sh"):
        if c and os.path.exists(c):
            return c
    return None


def shell_env(scratch):
    """The environment the scripts get: a test run, a POSIX PATH (a Windows python3 is not a device python3), and
    a no-op `sync` when the shell has none (Windows; the real one only flushes the device's disk)."""
    env = dict(os.environ, MPC_INSTALL_TEST="1",                      # the scripts get shell paths, never a Windows one (awk's
               MPC_SETTINGS=shell_path(os.path.join(scratch, "MPC.settings")),   # -v drops the backslashes in a C:\... value)
               MPC_TEST_LOG=shell_path(os.path.join(scratch, "mpc-calls.log")), MPC_STORE_TMP=shell_path(scratch))
    if os.name == "nt":
        env["PATH"] = "/usr/bin:/bin"
        if not shutil.which("sync", path=env["PATH"]):
            shim = os.path.join(scratch, "shim")
            os.makedirs(shim, exist_ok=True)
            with open(os.path.join(shim, "sync"), "w", newline="\n") as f:
                f.write("#!/bin/sh\nexit 0\n")
            env["PATH"] = shell_path(shim) + ":" + env["PATH"]
    return env


def run(cmd, cwd, env):
    r = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("failed (%d): %s\n--- stdout ---\n%s\n--- stderr ---\n%s" % (r.returncode, " ".join(cmd), r.stdout, r.stderr))
    return r


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("zip")
    ap.add_argument("--keep", action="store_true", help="leave the scratch folder in place (it is printed)")
    a = ap.parse_args()
    sh = find_sh()
    if not sh:
        sys.exit("no sh found: run this in Git Bash (Windows) or on a POSIX system")
    scratch = tempfile.mkdtemp(prefix="mpc-install-smoke-")
    try:
        pkg = os.path.join(scratch, "pkg")
        with zipfile.ZipFile(a.zip) as z:
            z.extractall(pkg)
        tops = {n.split("/")[0] for n in os.listdir(pkg)}
        if len(tops) != 1:
            sys.exit("the zip must unpack to one folder, found %s" % sorted(tops))
        top = tops.pop()
        m = json.load(open(os.path.join(pkg, top, "mpc-plugin.json")))
        if m.get("layout") != "portable":
            sys.exit("%s has layout %r: this smoke test covers the portable layout" % (a.zip, m.get("layout")))
        skin, so, = m["skin"], m["so"]
        synths = os.path.join(scratch, "Synths")
        os.makedirs(synths)
        settings = os.path.join(scratch, "MPC.settings")
        with open(settings, "w", newline="\n") as f:
            f.write(SETTINGS % {"synths": shell_path(synths)})
        env = shell_env(scratch)
        here = os.path.join(pkg, top)          # cwd for the process: a real path, the shell gets shell_path()
        script = shell_path(here)
        target = shell_path(synths)
        plugin = os.path.join(synths, skin)

        def entries():
            text = open(settings, encoding="utf-8").read()
            return (text.count('file="%s/%s/%s"' % (target, skin, so)), text.count(' uid="%s"' % m["uid"]))

        def check(step):
            if not os.path.isdir(plugin):
                sys.exit("%s: %s is missing" % (step, skin))
            for need in (so, "plugin-meta.xml", "version.xml", "mpc-plugin.json", "Plugin Skins/TUI.json"):
                if not os.path.isfile(os.path.join(plugin, need)):
                    sys.exit("%s: %s/%s is missing" % (step, skin, need))
            n = entries()
            if n != (1, 1):
                sys.exit("%s: expected one plugin-list entry, found %d by file and %d by uid" % (step, n[0], n[1]))

        r = run([sh, script + "/install.sh", "-y", "-n", "-t", target], here, env)
        if "not one of MPC's content locations" in r.stderr:
            sys.exit("install: the installer did not find %s in SynthContentLocations" % target)
        check("install")
        backups = [f for f in os.listdir(scratch) if f.startswith("MPC.settings.bak-")]
        if len(backups) != 1:
            sys.exit("install: expected one MPC.settings backup, found %d" % len(backups))
        run([sh, script + "/install.sh", "-y", "-n", "-t", target], here, env)
        check("install again (upgrade)")
        if os.path.isdir(os.path.join(synths, "." + skin + ".old")):
            sys.exit("install again: the staging folder %s.old was left behind" % skin)
        run([sh, script + "/uninstall.sh", "-y", "-n", "-t", target], here, env)
        if os.path.isdir(plugin):
            sys.exit("uninstall: %s is still there" % skin)
        if entries() != (0, 0):
            sys.exit("uninstall: the plugin-list entry is still in MPC.settings")
        print("OK: %s %s (%s): install, upgrade and uninstall%s"
              % (m["id"], m["version"], m["arch"], " in " + scratch if a.keep else " OK"))
    finally:
        if not a.keep:
            shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    main()
