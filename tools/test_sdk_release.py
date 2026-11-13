#!/usr/bin/env python3
"""Tests for tools/sdk_release.py: an Akai SDK plugin folder -> our per-build release zips (3.9.1 + 3.10.1), and back to an SDK zip."""
import json, os, subprocess, warnings, sys, tempfile, unittest, zipfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sdk_release, sdk_zip  # noqa: E402

F = "Acme - VST - Wob Synth"


def elf(machine, bits32):   # just enough of an ELF header for release.py / catalog_check.py to tell the CPU
    h = bytearray(64)
    h[:4] = b"\x7fELF"; h[4] = 1 if bits32 else 2; h[5] = 1; h[6] = 1
    h[18:20] = machine.to_bytes(2, "little")
    return bytes(h)


def sdk_folder(d, pj_extra=None, skin=True, drop=()):
    f = os.path.join(d, F)
    os.makedirs(os.path.join(f, "VST")); os.makedirs(os.path.join(f, "Content", "Waves"))
    pj = {"name": "Wob Synth", "manufacturer": "Acme", "version": "1.2.0", "format": "vst2", "kind": "instrument", "uid": "AcWb",
          "numInputs": 0, "numOutputs": 2, "category": "Synth", "file32": "VST/wob-synth-arm32.so", "file64": "VST/wob-synth-arm64.so",
          "description": "A test synth.", "license": "MIT"}
    pj.update(pj_extra or {})
    for k in drop:
        pj.pop(k, None)
    json.dump(pj, open(os.path.join(f, "plugin.json"), "w"))
    open(os.path.join(f, "VST", "wob-synth-arm32.so"), "wb").write(elf(40, True))
    open(os.path.join(f, "VST", "wob-synth-arm64.so"), "wb").write(elf(183, False))
    open(os.path.join(f, "Content", "Waves", "saw.wav"), "wb").write(b"RIFF")
    if skin:
        os.makedirs(os.path.join(f, "Plugin Skins"))
        open(os.path.join(f, "Plugin Skins", "TUI.json"), "w").write("{}")
        open(os.path.join(f, "version.xml"), "w").write("<v/>")
    return f


def run(*args):
    return subprocess.run([sys.executable, os.path.join(HERE, "sdk_release.py")] + list(args), capture_output=True, text=True)


class T(unittest.TestCase):
    def setUp(self):
        warnings.simplefilter("ignore", ResourceWarning)
        self.d = tempfile.mkdtemp(); self.out = os.path.join(self.d, "dist")

    def test_uid(self):
        self.assertEqual(sdk_release.uid_hex("MxSy"), "4d785379")
        self.assertEqual(sdk_release.uid_hex("0x4d785379"), "4d785379")
        self.assertEqual(sdk_release.uid_hex(1299731321), "4d785379")
        self.assertEqual(sdk_release.uid_hex("1299731321"), "4d785379")
        with self.assertRaises(SystemExit):
            sdk_release.uid_hex("toolong")

    def test_both_builds_keep_the_sdk_layout(self):
        r = run(sdk_folder(self.d), "--version", "1.2.0", "--id", "wob-synth", "--repo", "acme/wob", "-o", self.out)
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        z32 = zipfile.ZipFile(os.path.join(self.out, "Wob-Synth-1.2.0-mpc-armv7.zip"))
        z64 = zipfile.ZipFile(os.path.join(self.out, "Wob-Synth-1.2.0-mpc-aarch64.zip"))
        base = "Wob-Synth-1.2.0/portable/%s/" % F
        n32 = z32.namelist()
        self.assertIn(base + "VST/wob-synth-arm32.so", n32)
        self.assertNotIn(base + "VST/wob-synth-arm64.so", n32)   # only this build's binary
        self.assertNotIn(base + "wob-synth-arm32.so", n32)        # not copied to the folder root
        self.assertIn(base + "Content/Waves/saw.wav", n32)
        self.assertIn("Wob-Synth-1.2.0/install.sh", n32)
        meta = z32.read(base + "plugin-meta.xml").decode()
        self.assertIn('file="%%payload-path%%/%s/VST/wob-synth-arm32.so"' % F, meta)
        self.assertIn('uid="41635762"', meta)   # "AcWb"
        pj = json.loads(z32.read(base + "plugin.json"))
        self.assertEqual((pj.get("file32"), pj.get("file64"), pj["uid"], pj["format"]), ("VST/wob-synth-arm32.so", None, "0x41635762", "vst2"))
        m = json.loads(z64.read("Wob-Synth-1.2.0/mpc-plugin.json"))
        self.assertEqual((m["id"], m["arch"], m["so"], m["source_repo"]), ("wob-synth", "aarch64", "VST/wob-synth-arm64.so", "acme/wob"))
        inst = z32.read("Wob-Synth-1.2.0/install.sh").decode()
        self.assertIn("SO='VST/wob-synth-arm32.so'", inst)
        # back to one SDK folder: the paths stay as Akai's tools wrote them
        out = os.path.join(self.d, "sdk.zip")
        sdk_zip.sdk_zip(out, [z32.filename, z64.filename])
        zs = zipfile.ZipFile(out)
        spj = json.loads(zs.read(F + "/plugin.json"))
        self.assertEqual((spj["file32"], spj["file64"]), ("VST/wob-synth-arm32.so", "VST/wob-synth-arm64.so"))
        self.assertIn(F + "/VST/wob-synth-arm64.so", zs.namelist())

    def test_catalog_check_accepts_them(self):
        r = run(sdk_folder(self.d), "--version", "1.2.0", "--id", "wob-synth", "--repo", "acme/wob", "--license", "MIT", "-o", self.out)
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        for z in sorted(os.listdir(self.out)):
            c = subprocess.run([sys.executable, os.path.join(HERE, "catalog_check.py"), os.path.join(self.out, z)], capture_output=True, text=True)
            self.assertEqual(c.returncode, 0, c.stdout + c.stderr)
            warns = [l for l in (c.stdout + c.stderr).splitlines() if l.startswith("warning:")]
            self.assertEqual(warns, ["warning: max_glibc not recorded"])   # the fake binaries have no symbols; real builds record it

    def test_one_build_only(self):
        r = run(sdk_folder(self.d, drop=("file32",)), "--version", "1.2.0", "-o", self.out)
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        self.assertEqual(sorted(os.listdir(self.out)), ["Wob-Synth-1.2.0-mpc-aarch64.zip"])

    def test_from_a_zip(self):
        f = sdk_folder(self.d)
        zp = os.path.join(self.d, "wob.zip")
        with zipfile.ZipFile(zp, "w") as z:
            for root, _, files in os.walk(f):
                for n in files:
                    p = os.path.join(root, n); z.write(p, os.path.relpath(p, self.d))
        r = run(zp, "--version", "1.2.0", "-o", self.out)
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)
        self.assertEqual(len(os.listdir(self.out)), 2)

    def test_refusals(self):
        self.assertNotEqual(run(sdk_folder(os.path.join(self.d, "a"), skin=False), "--version", "1", "-o", self.out).returncode, 0)
        self.assertNotEqual(run(sdk_folder(os.path.join(self.d, "b"), {"name": "Other"}), "--version", "1", "-o", self.out).returncode, 0)
        self.assertNotEqual(run(sdk_folder(os.path.join(self.d, "c"), {"file32": "../x.so"}), "--version", "1", "-o", self.out).returncode, 0)
        self.assertNotEqual(run(sdk_folder(os.path.join(self.d, "d"), {"file": "VST/w.so"}, drop=("file32", "file64")), "--version", "1", "-o", self.out).returncode, 0)


if __name__ == "__main__":
    unittest.main()
