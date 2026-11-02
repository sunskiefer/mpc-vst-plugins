#!/usr/bin/env python3
"""Tests for folder_zip.py (offline, standard library)."""
import os
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import folder_zip  # noqa: E402


def make(path, entries):
    with zipfile.ZipFile(path, "w") as z:
        for name, data, mode in entries:
            i = zipfile.ZipInfo(name)
            i.external_attr = mode << 16
            z.writestr(i, data)


class FolderZip(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.src, self.dst = os.path.join(self.d, "r.zip"), os.path.join(self.d, "o.zip")

    def test_keeps_only_the_plugin_folder_with_modes(self):
        f = "X-1.0/portable/V - VST - X/"
        make(self.src, [("X-1.0/install.sh", b"#!", 0o100755), (f + "plugin.json", b"{}", 0o100644),
                        (f + "x.so", b"ELF", 0o100755), (f + "Plugin Skins/a.png", b"p", 0o100644)])
        self.assertEqual(folder_zip.folder_zip(self.src, self.dst), "V - VST - X")
        z = zipfile.ZipFile(self.dst)
        self.assertEqual(sorted(z.namelist()), ["V - VST - X/Plugin Skins/a.png", "V - VST - X/plugin.json", "V - VST - X/x.so"])
        self.assertEqual(z.getinfo("V - VST - X/x.so").external_attr >> 16, 0o100755)
        self.assertEqual(z.read("V - VST - X/x.so"), b"ELF")

    def test_refuses_without_plugin_json(self):
        make(self.src, [("X-1.0/portable/V - VST - X/x.so", b"ELF", 0o100755)])
        with self.assertRaises(SystemExit):
            folder_zip.folder_zip(self.src, self.dst)

    def test_refuses_two_folders(self):
        make(self.src, [("X/portable/A/plugin.json", b"{}", 0o100644), ("X/portable/B/plugin.json", b"{}", 0o100644)])
        with self.assertRaises(SystemExit):
            folder_zip.folder_zip(self.src, self.dst)


if __name__ == "__main__":
    unittest.main()
