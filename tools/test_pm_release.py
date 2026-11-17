#!/usr/bin/env python3
"""tools/pm_release.py update_pins(): every pin and link moves to the new version, nothing else changes."""
import json, os, shutil, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pm_release  # noqa: E402

FILES = ("catalog/downloads.json", "tools/desktop/g2stick-pin.json", "catalog/pages/install.md", "tools/catalog_site/guide.js", "tools/folder_zip.py")


class UpdatePins(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        for f in FILES:
            os.makedirs(os.path.join(self.root, os.path.dirname(f)), exist_ok=True)
            shutil.copy(os.path.join(HERE, "..", f), os.path.join(self.root, f))
        self.old = json.load(open(os.path.join(self.root, "tools/desktop/g2stick-pin.json")))["version"]

    def tearDown(self):
        shutil.rmtree(self.root)

    def test_everything_moves_to_the_new_version(self):
        sums = {"pm-gen1": "a" * 64, "pm-gen2": "b" * 64, "pm-sdk": "d" * 64, "stick": "c" * 64}
        pm_release.update_pins(self.root, "9.8.7", sums)
        d = json.load(open(os.path.join(self.root, "catalog/downloads.json")))
        pm = {f["id"]: f for f in d["files"] if f.get("id") in ("pm-gen1", "pm-gen2", "pm-sdk")}
        self.assertEqual(pm["pm-gen1"]["file"], "OpenMPC-Plugin-Manager-9.8.7-Gen1.zip")
        self.assertEqual(pm["pm-gen2"]["sha256"], "b" * 64)
        self.assertIn("open-mpc/plugin-manager/releases/download/plugin-manager-g2-v9.8.7/OpenMPC-Plugin-Manager-9.8.7-Gen2.zip", pm["pm-gen2"]["url"])
        self.assertEqual((pm["pm-sdk"]["file"], pm["pm-sdk"]["version"], pm["pm-sdk"]["sha256"]), ("Plugin-Manager-G2-9.8.7-mpc-sdk.zip", "9.8.7", "d" * 64))
        self.assertTrue(pm["pm-sdk"]["url"].endswith("/plugin-manager-g2-v9.8.7/Plugin-Manager-G2-9.8.7-mpc-sdk.zip"), pm["pm-sdk"]["url"])
        st = json.load(open(os.path.join(self.root, "tools/desktop/g2stick-pin.json")))
        self.assertEqual((st["version"], st["sha256"]), ("9.8.7", "c" * 64))
        self.assertTrue(st["url"].endswith("/g2-stick-v9.8.7/Plugin-Manager-G2-Stick-9.8.7.zip"), st["url"])
        for f in FILES[2:]:
            s = open(os.path.join(self.root, f)).read()
            self.assertNotIn("plugin-manager-g2-v" + self.old, s, f)
            self.assertNotIn("OpenMPC-Plugin-Manager-%s-" % self.old, s, f)
        self.assertIn("plugin-manager-g2-v9.8.7", open(os.path.join(self.root, "catalog/pages/install.md")).read())
        g = open(os.path.join(self.root, "tools/catalog_site/guide.js")).read()
        self.assertIn('var PM = "https://github.com/open-mpc/plugin-manager/releases/download/plugin-manager-g2-v9.8.7/OpenMPC-Plugin-Manager-9.8.7-', g)
        self.assertIn("Plugin-Manager-G2-9.8.7-mpc-sdk.zip", g)   # guide.js: the SDK zip link
        self.assertNotIn("Plugin-Manager-G2-%s-mpc-sdk" % self.old, g)
        self.assertIn("github.com/open-mpc/plugin-manager/releases/tag/plugin-manager-g2-v9.8.7", open(os.path.join(self.root, "catalog/pages/install.md")).read())


if __name__ == "__main__":
    unittest.main()
