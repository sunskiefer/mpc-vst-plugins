#!/usr/bin/env python3
"""Tests for tools/sdk_zip.py: per-build release zips made here, joined into one SDK-format folder."""
import json, os, sys, tempfile, unittest, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sdk_zip  # noqa: E402

F = "Acme - VST - Wob"


def release(d, arch, extra=None, so=b"ELF", pj_extra=None):
    key = {"armv7": "file32", "aarch64": "file64"}[arch]
    pj = dict({"name": "Wob", "manufacturer": "Acme", "uid": "AcWb", key: "wob.so"}, **(pj_extra or {}))
    files = {"plugin.json": json.dumps(pj), "wob.so": so + arch.encode(), "Plugin Skins/TUI.json": "[]", "version.xml": "<v/>",
             "mpc-plugin.json": json.dumps({"id": "wob", "version": "1.0", "arch": arch, "max_glibc": "2.%d" % len(arch)})}
    files.update(extra or {})
    p = os.path.join(d, "Wob-1.0-mpc-%s.zip" % arch)
    with zipfile.ZipFile(p, "w") as z:
        for n, b in files.items():
            zi = zipfile.ZipInfo("Wob-1.0/portable/%s/%s" % (F, n)); zi.external_attr = (0o755 if n.endswith(".so") else 0o644) << 16
            z.writestr(zi, b)
        z.writestr("Wob-1.0/install.sh", "#!/bin/sh")
    return p


class T(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp(); self.out = os.path.join(self.d, "out.zip")

    def test_both_builds(self):
        sdk_zip.sdk_zip(self.out, [release(self.d, "aarch64"), release(self.d, "armv7")])
        z = zipfile.ZipFile(self.out)
        self.assertEqual({n.split("/")[0] for n in z.namelist()}, {F})
        pj = json.loads(z.read(F + "/plugin.json"))
        self.assertEqual((pj["file32"], pj["file64"], pj["uid"]), ("wob-arm32.so", "wob-arm64.so", "AcWb"))
        self.assertEqual(z.read(F + "/wob-arm32.so"), b"ELFarmv7"); self.assertEqual(z.read(F + "/wob-arm64.so"), b"ELFaarch64")
        self.assertNotIn(F + "/wob.so", z.namelist()); self.assertIn(F + "/Plugin Skins/TUI.json", z.namelist())
        self.assertEqual(z.getinfo(F + "/wob-arm64.so").external_attr >> 16, 0o755)
        m = json.loads(z.read(F + "/mpc-plugin.json"))
        self.assertEqual((m["arch"], m["builds"]), ("universal", {"aarch64": {"max_glibc": "2.7"}, "armv7": {"max_glibc": "2.5"}}))

    def test_one_build(self):
        sdk_zip.sdk_zip(self.out, [release(self.d, "aarch64")])
        pj = json.loads(zipfile.ZipFile(self.out).read(F + "/plugin.json"))
        self.assertEqual(pj["file64"], "wob-arm64.so"); self.assertNotIn("file32", pj)

    def test_refuses_per_build_files(self):
        with self.assertRaisesRegex(SystemExit, "Presets/x"):
            sdk_zip.sdk_zip(self.out, [release(self.d, "armv7", {"Presets/x": "a"}), release(self.d, "aarch64", {"Presets/x": "b"})])
        with self.assertRaisesRegex(SystemExit, "bin/helper"):
            sdk_zip.sdk_zip(self.out, [release(self.d, "armv7", {"bin/helper": "a"}), release(self.d, "aarch64")])

    def test_refuses_mismatched_plugin_json(self):
        with self.assertRaisesRegex(SystemExit, "plugin.json differs"):
            sdk_zip.sdk_zip(self.out, [release(self.d, "armv7"), release(self.d, "aarch64", pj_extra={"version": "2"})])

    def test_refuses_two_of_one_build(self):
        a = release(self.d, "armv7"); b = os.path.join(self.d, "b.zip"); os.rename(a, b)
        with self.assertRaisesRegex(SystemExit, "two armv7"):
            sdk_zip.sdk_zip(self.out, [b, release(self.d, "armv7")])


if __name__ == "__main__":
    unittest.main()
