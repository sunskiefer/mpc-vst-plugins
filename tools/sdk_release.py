#!/usr/bin/env python3
"""Turn an Akai SDK plugin folder (or its zip) into our catalog release: one zip per build, for MPC OS 3.9.1 and 3.10.1+.

    tools/sdk_release.py SDK_FOLDER_OR_ZIP --version 1.0.0 [--id simple-synth] [--repo owner/name] [--license MIT] [-o dist]
                         [-- extra release.py options]

Akai's SDK (tools/build.py) builds "<Manufacturer> - VST - <Name>/" with plugin.json naming VST/<slug>-arm32.so (file32) and
VST/<slug>-arm64.so (file64). Our catalog, the Plugin Manager, the installers and the sticks install per-build zips with an
install.sh that also registers the plugin on MPC OS 3.9.1 (plugin-meta.xml + MPC.settings). This writes those zips from the SDK
folder with tools/release.py, keeping the SDK layout: each zip holds the whole folder with only its own binary, still at the path
plugin.json gives (the SDK's mpc::pluginFolder() walks up from the .so to plugin.json, so the plugin finds Content/ on 3.9.1 too).
release.py writes plugin.json again for that build (same keys; file32 or file64 only). Then tools/sdk_zip.py can make the SDK zip.

The plugin-list entry (3.9.1) is made from plugin.json: a four-character uid such as "MxSy" becomes its hex value (4d785379), as the
MPC does. A plugin with no skin (no Plugin Skins/TUI.json or version.xml) is refused: release.py needs one, and MPC shows nothing
useful without it.

Opting out of MPC OS 3.9.1 (a min-OS field in the catalog) is not built yet: docs/ROADMAP.md.
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

HERE = os.path.dirname(os.path.abspath(__file__))
ARCH = (("file32", "armv7"), ("file64", "aarch64"))


def uid_hex(uid):
    """plugin.json uid -> 8 hex digits, as the MPC reads it (docs/plugin-folder-format.md in the SDK)."""
    if isinstance(uid, int):
        return "%08x" % (uid & 0xffffffff)
    u = str(uid).strip()
    if re.fullmatch(r"0[xX][0-9a-fA-F]{1,8}", u):
        return "%08x" % int(u, 16)
    if re.fullmatch(r"[0-9]{1,10}", u):
        return "%08x" % (int(u) & 0xffffffff)
    if len(u) == 4 and all(32 <= ord(c) < 127 for c in u):
        return "%02x%02x%02x%02x" % tuple(ord(c) for c in u)
    raise SystemExit("plugin.json uid %r: need a four-character code, a decimal or a 0x hex value" % uid)


def find_folder(src, work):
    """The SDK plugin folder: src itself, or the one folder inside src's zip."""
    if os.path.isdir(src):
        return os.path.normpath(src)
    with zipfile.ZipFile(src) as z:
        tops = {n.split("/")[0] for n in z.namelist() if n.strip("/")}
        for n in z.namelist():
            if n.startswith("/") or ".." in n.split("/"):
                raise SystemExit("%s: unsafe path %r" % (src, n))
        z.extractall(work)
    dirs = [t for t in tops if os.path.isdir(os.path.join(work, t)) and " - VST - " in t]
    if len(dirs) != 1:
        raise SystemExit("%s: expected one '<Manufacturer> - VST - <Name>' folder, found %s" % (src, sorted(tops)[:4]))
    return os.path.join(work, dirs[0])


def convert(src, version, out, plugin_id=None, repo=None, license=None, rest=()):
    """Write the per-build zips for the SDK folder or zip src into out; returns {arch: zip path}."""
    work = tempfile.mkdtemp()
    os.makedirs(out, exist_ok=True)
    try:
        folder = find_folder(src, os.path.join(work, "in"))
        fname = os.path.basename(folder)
        pj = json.load(open(os.path.join(folder, "plugin.json")))
        name, mfr = pj.get("name", ""), pj.get("manufacturer", "")
        if fname != "%s - VST - %s" % (mfr, name):
            raise SystemExit("folder %r does not match plugin.json (%r - VST - %r)" % (fname, mfr, name))
        if str(pj.get("format", "vst2")).lower() != "vst2":
            raise SystemExit("plugin.json format %r: only vst2" % pj.get("format"))
        if pj.get("version") and pj["version"] != version:
            print("note: plugin.json says version %s, releasing %s" % (pj["version"], version), file=sys.stderr)
        kind = "effect" if pj.get("isInstrument") is False or str(pj.get("kind", "")).lower() == "effect" else "instrument"
        builds = [(key, arch, pj[key]) for key, arch in ARCH if pj.get(key)]
        if not builds and pj.get("file"):
            raise SystemExit("plugin.json has only 'file' (one binary for both CPUs): name file32 and/or file64")
        if not builds:
            raise SystemExit("plugin.json names no binary (file32 / file64)")
        binaries = {rel for _, _, rel in builds}
        made = {}
        for key, arch, rel in builds:
            if rel.startswith("/") or ".." in rel.split("/") or not os.path.isfile(os.path.join(folder, rel)):
                raise SystemExit("plugin.json %s %r is not a file inside the folder" % (key, rel))
            stage = os.path.join(work, arch)
            skin = os.path.join(stage, fname)
            shutil.copytree(folder, skin, symlinks=True)
            for b in binaries:   # the zip carries only this build's binary, passed to release.py separately
                os.remove(os.path.join(skin, b))
            os.remove(os.path.join(skin, "plugin.json"))   # release.py writes it for this build
            so = os.path.join(stage, os.path.basename(rel))
            shutil.copy2(os.path.join(folder, rel), so)
            entry = os.path.join(stage, "entry.xml")
            outs = int(pj.get("numOutputs", 2))
            ins = int(pj.get("numInputs", 2 if kind == "effect" else 0))
            esc = lambda t: str(t).replace("&", "&amp;").replace('"', "&quot;").replace("<", "&lt;").replace(">", "&gt;")
            open(entry, "w").write(
                '<PLUGIN name="%s" descriptiveName="%s" format="VST" category="%s" manufacturer="%s" version="%s" file="%s" '
                'uid="%s" isInstrument="%d" fileTime="0" infoUpdateTime="0" numInputs="%d" numOutputs="%d" isShell="0"/>\n'
                % (esc(name), esc(name), esc(pj.get("category") or ("Effect" if kind == "effect" else "Synth")), esc(mfr),
                   esc(version), esc(os.path.basename(rel)), uid_hex(pj.get("uid")), kind == "instrument", ins, outs))
            before = set(os.listdir(out))
            cmd = [sys.executable, os.path.join(HERE, "release.py"), "--so", so, "--so-path", rel, "--skin", skin, "--entry", entry,
                   "--version", version, "-o", out, "--about", pj.get("description", "")]
            for opt, val in (("--id", plugin_id), ("--repo", repo), ("--license", license or pj.get("license"))):
                if val:
                    cmd += [opt, val]
            r = subprocess.run(cmd + list(rest))
            if r.returncode:
                raise SystemExit("release.py failed for %s" % arch)
            new = [n for n in set(os.listdir(out)) - before if n.endswith("-mpc-%s.zip" % arch)]
            made[arch] = os.path.join(out, new[0]) if len(new) == 1 else os.path.join(out, sorted(n for n in os.listdir(out) if n.endswith("-mpc-%s.zip" % arch))[-1])
        return made
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src")
    ap.add_argument("--version", required=True)
    ap.add_argument("--id")
    ap.add_argument("--repo")
    ap.add_argument("--license")
    ap.add_argument("-o", "--out", default="dist")
    a, rest = ap.parse_known_args()   # anything else goes to tools/release.py (--bench, --user-data ...)
    made = convert(a.src, a.version, a.out, a.id, a.repo, a.license, [x for x in rest if x != "--"])
    print("made %s zips for %s in %s" % ("+".join(made), a.version, a.out))


if __name__ == "__main__":
    main()
