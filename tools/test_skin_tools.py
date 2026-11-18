#!/usr/bin/env python3
"""Offline tests of the skin tools added with the panel import: skin_check's TEXT and INK findings, studio.py preview's
parameter values, and panel_import's circle finder. Every skin and picture here is drawn by the test (no Akai file).
Needs Pillow. No device: python3 tools/test_skin_tools.py"""
import json
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import panel_import  # noqa: E402
import skin_check  # noqa: E402
import studio  # noqa: E402

from PIL import Image, ImageDraw, ImageFont  # noqa: E402


def bounds(x, y, w, h):
    return {"version": 2, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always",
            "boundsType": "Absolute", "bounds": "%g %g %g %g" % (x, y, w, h), "additionalInvalidatingHandles": []}


def sub(ctype, data, x, y, w, h):
    return {"version": 2, "componentData": {"version": 1, "name": ctype, "type": ctype, "data": data},
            "handle remapping": {"version": 1, "map": []}, "bounds": bounds(x, y, w, h)}


def knob_def(s=70, cw=130):
    """A knob control like shadow_skin's: filmstrip at the top, name under it, value under that."""
    font = lambda h: {"version": 1, "name": "Titillium Web", "style": "SemiBold", "height": h}
    return {"version": 2, "actions": [{"version": 1, "onAction": "Mouse Down", "handler": "Q-Link", "handleName": "Data",
                                       "additionalData": ""}],
            "componentsData": [
                sub("Knob", {"version": 5, "knobType": "FilmStrip", "filmStrip": "strip.png", "numFrames": 3,
                             "handleName": "Data"}, (cw - s) // 2, 0, s, s),
                sub("Label", {"version": 1, "type": "Name", "handleName": "Data",
                              "textStyle": {"version": 1, "font": font(21), "colour": "ffffffff",
                                            "justification": "horizontallyCentred verticallyCentred"}}, 0, s + 2, cw, 28),
                sub("Label", {"version": 1, "type": "Value", "handleName": "Data",
                              "textStyle": {"version": 1, "font": font(22), "colour": "ffaaaaaa",
                                            "justification": "horizontallyCentred verticallyCentred"}}, 0, s + 32, cw, 30)]}


def placed(name, ctype, param, x, y, w, h):
    return {"version": 2, "componentData": {"version": 1, "name": name, "type": ctype, "data": {"version": 1, "handleName": "Data"}},
            "handle remapping": {"version": 1, "map": [{"key": "Data", "value": "Parameter %d" % param}]},
            "bounds": bounds(x, y, w, h)}


def write_skin(folder, knobs, ink_under=None):
    """A one-tab skin: a background picture and one knob per (name, x, y); ink_under = (x, y) prints text there."""
    os.makedirs(folder, exist_ok=True)
    bg = Image.new("RGBA", (1280, 628), (30, 30, 30, 255))
    if ink_under:
        ImageDraw.Draw(bg).text(ink_under, "CUTOFF", fill=(255, 255, 255, 255),
                                font=ImageFont.truetype(skin_check.FONT, 22))
    bg.save(os.path.join(folder, "bg.png"))
    strip = Image.new("RGBA", (70, 210), (0, 0, 0, 0))
    for f in range(3):
        ImageDraw.Draw(strip).ellipse([5, 5 + 70 * f, 65, 65 + 70 * f], fill=(200, 200, 200, 255))
    strip.save(os.path.join(folder, "strip.png"))
    kids = [{"version": 2, "componentData": {"version": 1, "name": "Background", "type": "Image",
                                             "data": {"version": 2, "imageType": "Regular", "colour": "0", "image": "bg.png"}},
             "handle remapping": {"version": 1, "map": []}, "bounds": bounds(0, 0, 1280, 628)}]
    for i, (name, x, y) in enumerate(knobs):
        kids.append(placed(name, "kn", i, x, y, 130, 130))
    tui = {"pageData": {"version": 3, "componentDefinitions": {"localComponentDefinitions": [{"key": "kn", "value": knob_def()},
                                                                                             {"key": "P", "value": {"componentsData": kids}}]},
                        "tabs": [{"tabName": "T", "componentName": "P", "fnKeyIndex": 0, "fnKeySubIndex": 0,
                                  "qlinkBoundsData": ["0 0 0 0"] * 4}]}}
    json.dump(tui, open(os.path.join(folder, "TUI.json"), "w"))
    json.dump({"Screen Mode Q-Links": {"map": []}}, open(os.path.join(folder, "Q-Links.json"), "w"))
    return folder


class SkinCheckText(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.d)

    def findings(self, kind, knobs, ink_under=None):
        return [f for f in skin_check.check(write_skin(self.d, knobs, ink_under)) if (" %s " % kind) in f]

    def test_apart(self):
        self.assertEqual(self.findings("TEXT", [("CUTOFF", 100, 100), ("RESONANCE", 300, 100)]), [])

    def test_names_collide(self):   # boxes side by side, but the long names meet in the middle
        f = self.findings("TEXT", [("FILTER ENVELOPE", 100, 100), ("RESONANCE AMOUNT", 200, 300)])
        self.assertEqual(f, [])
        f = self.findings("TEXT", [("FILTER ENVELOPE", 100, 100), ("RESONANCE AMOUNT", 225, 100)])
        self.assertTrue(any("run into each other" in x for x in f), f)

    def test_name_under_knob(self):   # the second knob sits on the first one's name
        f = self.findings("TEXT", [("CUTOFF", 100, 100), ("RES", 100, 150)])
        self.assertTrue(any("runs into the knob of 'RES'" in x for x in f), f)

    def test_ink(self):
        self.assertEqual(self.findings("INK", [("CUTOFF", 100, 100)]), [])
        f = self.findings("INK", [("CUTOFF", 100, 100)], ink_under=(135, 125))
        self.assertTrue(f and "CUTOFF" in f[0], f)


class Preview(unittest.TestCase):
    PARAMS = [{"key": "cut", "name": "Cutoff", "min": 0, "max": 100, "default": 25, "unit": "%"},
              {"key": "wave", "name": "Wave", "options": ["SAW", "SQR", "TRI"], "default": 1},
              {"key": "patch", "name": "Patch", "display": "string"}]

    def test_values(self):
        v = studio.param_values(self.PARAMS)
        self.assertEqual(v[0], (0.25, None, "25 %"))
        self.assertEqual(v[1], (0.5, 1, "SQR"))
        self.assertIsNone(v[2][2])   # text from the engine: unknown offline
        v = studio.param_values(self.PARAMS, {"cut": "100", "wave": "TRI"})
        self.assertEqual((v[0][0], v[1][1]), (1.0, 2))
        self.assertEqual(studio.param_values(self.PARAMS, {"1": "0"})[1][1], 0)
        with self.assertRaises(SystemExit):
            studio.param_values(self.PARAMS, {"wave": "SINE"})
        with self.assertRaises(SystemExit):
            studio.param_values(self.PARAMS, {"nope": 1})

    def test_render(self):
        d = tempfile.mkdtemp()
        try:
            write_skin(d, [("CUTOFF", 100, 100)])
            outs = studio.preview(d, os.path.join(d, "p_%d.png"), plist=self.PARAMS[:1], sets={"cut": 100}, touch=True)
            im = Image.open(outs[0][0])
            self.assertEqual(im.getpixel((100, 100)), (230, 60, 60))    # touch box outline
            self.assertEqual(im.getpixel((165, 135)), (200, 200, 200))  # knob (last frame) drawn
        finally:
            shutil.rmtree(d)


class PanelImport(unittest.TestCase):
    def test_detect(self):
        """Four knobs and three jacks drawn on a noisy plate: all found, sized apart, nothing else."""
        im = Image.new("RGB", (1280, 628), (40, 36, 30))
        dr = ImageDraw.Draw(im)
        dr.text((20, 20), "PANEL ART", fill=(220, 200, 150), font=ImageFont.truetype(skin_check.FONT, 40))
        knobs = [(200, 200, 45), (420, 200, 45), (640, 200, 45), (860, 200, 45)]
        jacks = [(200, 480, 18), (300, 480, 18), (400, 480, 18)]
        for x, y, r in knobs:
            dr.ellipse([x - r, y - r, x + r, y + r], fill=(15, 15, 15), outline=(170, 160, 140), width=3)
        for x, y, r in jacks:
            dr.ellipse([x - r, y - r, x + r, y + r], fill=(120, 120, 120))
            dr.ellipse([x - 8, y - 8, x + 8, y + 8], fill=(0, 0, 0))
        page, _, _, _ = panel_import.fit(im)
        k = panel_import.WORK / panel_import.W
        work = page.convert("L").resize((panel_import.WORK, round(628 * k)))
        found = panel_import.detect(work, 7, 35)
        kinds = panel_import.classify(found)
        got = sorted((round(x / k), round(y / k), kd) for (x, y, _, _), kd in zip(found, kinds))
        want = sorted([(x, y, "knob") for x, y, _ in knobs] + [(x, y, "small") for x, y, _ in jacks])
        self.assertEqual(len(got), len(want), got)
        for (gx, gy, gk), (wx, wy, wk) in zip(got, want):
            self.assertLessEqual(abs(gx - wx) + abs(gy - wy), 6, (gx, gy, wx, wy))
            self.assertEqual(gk, wk)

    def test_layout(self):
        conf = panel_import.layout_conf([{"cx": 100, "cy": 50, "r": 30}, {"cx": 200, "cy": 50, "r": 30}],
                                        [{"key": "wave", "options": ["A", "B"]}, {"key": "cut", "name": "Cutoff"}], "P")
        self.assertIn('knob cx=100 cy=136 r=30 label="CUTOFF" key=cut ns=0 vs=0', conf)   # 50 + Y_OFF; options skipped
        self.assertIn("key=knob_2", conf)
        self.assertIn('art file="panel.png"', conf)

    def test_fit_letterbox(self):
        page, s, ox, oy = panel_import.fit(Image.new("RGB", (640, 628), (255, 255, 255)))
        self.assertEqual((page.size, s, ox, oy), ((1280, 628), 1.0, 320, 0))
        self.assertEqual(page.getpixel((10, 10)), (0, 0, 0))


if __name__ == "__main__":
    unittest.main()
