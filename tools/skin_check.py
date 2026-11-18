#!/usr/bin/env python3
"""Check a built skin (what MPC loads) for mistakes the layout checks can't see:
    skin_check.py "<skin folder>/Plugin Skins"      (or the folder holding TUI.json; exit 1 on any finding)
Reports, per page (tab / Q-Link sub-page):
  TOUCH  two controls whose touch boxes overlap while both are visible: MPC gives the touch to one of them
         (knobs and sliders carry their name and value in the box; narrow them with bw=)
  EDGE   a control's box past the 1280 x 628 plugin area
  QLINK  a Q-Link on a parameter no control on that page is bound to (turning it changes something unseen)
  OPTS   a switch group (option segments, a popup list) missing some of its options on a page
  TEXT   a control's name, measured in Titillium at its size, running into another name or another control's picture
         (MPC draws names centred in their box and doesn't move them apart)
  INK    printed text or lines of the page picture (its background/art images) under a knob's round body: a label the
         knob covers (needs Pillow; skipped without it)
(Q-Link order against the layout is not checked: the built skin doesn't carry the layout's order.)
Controls shown in different modes (when=) or on different sub-pages (banks=) never overlap each other.
gen_vst.py runs it after every skin build and prints the findings as warnings. Idea from
saustin2010/vst_instruments' check_skin.py (docs/COMMUNITY_SKINS.md)."""
import json
import os
import sys

W, H = 1280, 628
MIN_OVERLAP = 6   # px each way: a hairline shared edge is fine


def _rect(b):
    x, y, w, h = (float(v) for v in b.split())
    return x, y, w, h


def _param(c):
    for m in c.get("handle remapping", {}).get("map", []):
        if m.get("key") == "Data" and str(m.get("value", "")).startswith("Parameter "):
            return int(m["value"].split()[1])
    return None


FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "html_art", "fonts", "TitilliumWeb-SemiBold.ttf")
INK_MIN = 25          # px of ink under one knob before it's reported
INK_FRAC = 0.03       # ... and that share of the knob's disc
INK_DIFF = 90         # luminance step from the disc's own median that counts as ink
_fonts = {}


def text_w(text, size):
    """Width in px of `text` in Titillium SemiBold at `size` (rough per-letter estimate without Pillow)."""
    try:
        from PIL import ImageFont
        f = _fonts.get(size) or _fonts.setdefault(size, ImageFont.truetype(FONT, max(1, round(size))))
        return f.getlength(text)
    except (ImportError, OSError):
        return 0.52 * size * len(text)


def _vis(c):
    return tuple(sorted(c["bounds"].get("additionalInvalidatingHandles", [])))


def _together(v1, v2):
    return not (v1 != v2 and v1 and v2)


def _hit(r1, r2, m=2):
    ox = min(r1[0] + r1[2], r2[0] + r2[2]) - max(r1[0], r2[0])
    oy = min(r1[1] + r1[3], r2[1] + r2[3]) - max(r1[1], r2[1])
    return (ox, oy) if ox > m and oy > m else None


def text_rects(kids, defs):
    """[(control name, (x, y, w, h) of its name's ink, visibility, component index)] for every Name label on a page."""
    out = []
    for i, c in enumerate(kids):
        cd = c["componentData"]
        if not cd.get("name", "").strip():
            continue
        x, y, _, _ = _rect(c["bounds"]["bounds"])
        for sub in defs.get(cd["type"], {}).get("componentsData", []):
            d = sub["componentData"]
            if d["type"] != "Label" or d["data"].get("type") != "Name":
                continue
            sx, sy, sw, sh = _rect(sub["bounds"]["bounds"])
            size = float(d["data"].get("textStyle", {}).get("font", {}).get("height", 16))
            tw = min(text_w(cd["name"].strip(), size), sw)   # MPC never draws past the box
            just = d["data"].get("textStyle", {}).get("justification", "")
            tx = x + sx if "left" in just.lower() else x + sx + sw - tw if "right" in just.lower() else x + sx + (sw - tw) / 2
            th = size * 0.75   # cap height + descenders of upper/mixed case
            out.append((cd["name"].strip(), (tx, y + sy + (sh - th) / 2, tw, th), _vis(c), i))
    return out


def pictures(kids, defs, types=("Knob",)):
    """[(control name, (x, y, w, h), visibility, component index, sub data)] of every knob filmstrip on a page."""
    out = []
    for i, c in enumerate(kids):
        cd = c["componentData"]
        x, y, _, _ = _rect(c["bounds"]["bounds"])
        for sub in defs.get(cd["type"], {}).get("componentsData", []):
            if sub["componentData"]["type"] in types:
                sx, sy, sw, sh = _rect(sub["bounds"]["bounds"])
                out.append((cd.get("name", "").strip() or cd["type"], (x + sx, y + sy, sw, sh), _vis(c), i, sub["componentData"]["data"]))
    return out


def text_findings(where, kids, defs):
    out = []
    texts = text_rects(kids, defs)
    for k, (n1, r1, v1, i1) in enumerate(texts):
        for n2, r2, v2, i2 in texts[k + 1:]:
            if i1 != i2 and _together(v1, v2) and _hit(r1, r2):
                out.append("%s: TEXT  names %r and %r run into each other" % (where, n1, n2))
    for n1, r1, v1, i1 in texts:
        for n2, r2, v2, i2, _ in pictures(kids, defs):
            if i1 != i2 and _together(v1, v2) and _hit(r1, r2, 4):
                out.append("%s: TEXT  name %r runs into the knob of %r" % (where, n1, n2))
    return out


def ink_findings(where, kids, defs, skin_dir):
    """Ink of the page's pictures under each knob's disc (pictures drawn before the knob, i.e. under it)."""
    try:
        from PIL import Image
    except ImportError:
        return []
    page = None
    out = []
    order = {}
    for i, c in enumerate(kids):
        cd = c["componentData"]
        if cd["type"] != "Image" or not cd.get("data", {}).get("image"):
            continue
        f = os.path.join(skin_dir, cd["data"]["image"])
        if not os.path.exists(f):
            continue
        if page is None:
            page = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        x, y, w, h = (int(v) for v in _rect(c["bounds"]["bounds"]))
        im = Image.open(f).convert("RGBA")
        if im.size != (w, h) and w > 0 and h > 0:
            im = im.resize((w, h))
        page.alpha_composite(im, (max(0, x), max(0, y)))
        order[i] = _vis(c)
    if page is None:
        return out
    lum = page.convert("L")
    alpha = page.getchannel("A")
    for name, (x, y, w, h), vis, i, _ in pictures(kids, defs):
        if not any(j < i and _together(v, vis) for j, v in order.items()):
            continue
        r = 0.42 * min(w, h)
        cx, cy = x + w / 2, y + h / 2
        px = []
        for yy in range(int(cy - r), int(cy + r) + 1):
            for xx in range(int(cx - r), int(cx + r) + 1):
                if 0 <= xx < W and 0 <= yy < H and (xx - cx) ** 2 + (yy - cy) ** 2 <= r * r and alpha.getpixel((xx, yy)) > 128:
                    px.append(lum.getpixel((xx, yy)))
        if len(px) < 20:
            continue
        med = sorted(px)[len(px) // 2]
        ink = sum(1 for v in px if abs(v - med) > INK_DIFF)
        if ink >= INK_MIN and ink >= INK_FRAC * len(px):
            out.append("%s: INK   %d px of the page picture's text/lines lie under the knob of %r (%.0f%% of its disc)"
                       % (where, ink, name, 100.0 * ink / len(px)))
    return out


def pages(tui):
    """(tab, page definition components) for each page, in either skin shape (MPC OS 3.x or 2.x)."""
    pd = tui["pageData"]
    defs = {d["key"]: d["value"] for d in pd["componentDefinitions"].get("localComponentDefinitions", [])}
    for t in pd["tabs"]:
        page = t.get("componentDefinition") or defs.get(t.get("componentName"), {})
        yield t, page.get("componentsData", []), defs


def check(skin_dir):
    if not os.path.exists(os.path.join(skin_dir, "TUI.json")):   # the plugin folder (write_skin's result)
        skin_dir = os.path.join(skin_dir, "Plugin Skins")
    tui = json.load(open(os.path.join(skin_dir, "TUI.json")))
    ql = json.load(open(os.path.join(skin_dir, "Q-Links.json")))
    qpages = {(q["Tab"], q["SubTab"]): q["Q-Links"] for q in ql.get("Screen Mode Q-Links", {}).get("map", [])}
    out = []
    for t, kids, defs in pages(tui):
        where = t["tabName"]
        controls = []
        for c in kids:
            cd = c["componentData"]
            p = _param(c)
            if p is None or cd["type"] in ("Image", "Label"):
                continue
            acts = defs.get(cd["type"], {}).get("actions", [])
            if not acts:   # display-only (meters, readouts with no tap): no touch
                continue
            x, y, w, h = _rect(c["bounds"]["bounds"])
            vis = tuple(sorted(c["bounds"].get("additionalInvalidatingHandles", [])))
            controls.append((cd["name"], p, (x, y, w, h), vis))
            if x < 0 or y < 0 or x + w > W or y + h > H:
                out.append("%s: EDGE  %s (%g %g %g %g) is past the %dx%d plugin area" % (where, cd["name"], x, y, w, h, W, H))
        for i, (n1, p1, r1, v1) in enumerate(controls):
            for n2, p2, r2, v2 in controls[i + 1:]:
                if v1 != v2 and v1 and v2:   # different modes: never on screen together
                    continue
                ox = min(r1[0] + r1[2], r2[0] + r2[2]) - max(r1[0], r2[0])
                oy = min(r1[1] + r1[3], r2[1] + r2[3]) - max(r1[1], r2[1])
                if ox > MIN_OVERLAP and oy > MIN_OVERLAP and p1 != p2:
                    out.append("%s: TOUCH %s and %s overlap by %gx%g px" % (where, n1, n2, ox, oy))
        groups = {}   # (param, visibility) -> (options in the group, button ids present)
        for c in kids:
            p = _param(c)
            for sub in defs.get(c["componentData"]["type"], {}).get("componentsData", []):
                d = sub["componentData"]
                if p is not None and d["type"] == "Button" and d["data"].get("numButtonsInGroup", 1) > 1:
                    vis = tuple(sorted(c["bounds"].get("additionalInvalidatingHandles", [])))
                    g = groups.setdefault((p, vis), [d["data"]["numButtonsInGroup"], set()])
                    g[1].add(d["data"]["buttonId"])
        for (p, _), (n, ids) in sorted(groups.items(), key=lambda x: (x[0][0], x[0][1])):
            if ids != set(range(n)):
                out.append("%s: OPTS  parameter %d shows options %s of 0..%d" % (where, p, sorted(ids), n - 1))
        out += text_findings(where, kids, defs)
        out += ink_findings(where, kids, defs, skin_dir)
        bound = {p for _, p, _, _ in controls} | {_param(c) for c in kids}
        q = qpages.get((t.get("fnKeyIndex", 0) + 1, t.get("fnKeySubIndex", 0) + 1), {})
        for slot, p in sorted(q.items()):
            if isinstance(p, int) and p >= 0 and p not in bound:
                out.append("%s: QLINK %s is on parameter %d, which no control on this page shows" % (where, slot, p))
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    found = check(sys.argv[1])
    for f in found:
        print(f)
    sys.exit(1 if found else 0)


if __name__ == "__main__":
    main()
