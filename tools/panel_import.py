#!/usr/bin/env python3
"""Start a skin from a picture of a hardware panel: find its round controls, cut each one out as a tile and write a
first layout.conf with the picture as page art and a knob on every control found.

    panel_import.py PANEL.png -o OUTDIR [--params PARAMS] [--title NAME] [--labels] [--rmin 14] [--rmax 70] [--min-score 0.6]

OUTDIR gets:
  panel.png          the picture fitted into the 1280x628 plugin area (letterboxed, never stretched)
  controls.json      every circle found: cx, cy, r (plugin pixels), score (0..1, how round), kind (knob | small)
  tiles/NN_<kind>.png each control cut out with room under it for its printed label
  layout.conf        `art file="panel.png"` + one `knob` per knob found (no name/value text unless --labels), top to bottom, left to right; with --params
                     the knobs take the port's ranged parameters in order (names as labels), else keys knob_1, knob_2 ...
  overlay.png        the picture with every find circled (green knob, orange small) and numbered, to check by eye

"small" circles (jacks, LEDs, screws: below the widest jump, at least 1.4x, in the sizes found) are listed but not placed.
Detection: Sobel edges, a gradient-direction circle Hough transform to propose, then a rim check at a working width of 640 px, pure Pillow (no numpy).
It is a first pass: open layout.conf in the studio (tools/studio.py serve) to fix what it missed.
Idea: sunskiefer/BUSTEDXXL's knob/jack detection from panel photos (reimplemented, not copied)."""
import argparse
import json
import math
import os
import sys

W, H = 1280, 628
WORK = 640   # working width for detection


def fit(img):
    """The picture scaled to fit 1280x628 (aspect kept) and centred on black. -> (page image, scale, ox, oy)"""
    from PIL import Image
    s = min(W / img.width, H / img.height)
    w, h = round(img.width * s), round(img.height * s)
    page = Image.new("RGB", (W, H), (0, 0, 0))
    ox, oy = (W - w) // 2, (H - h) // 2
    page.paste(img.convert("RGB").resize((w, h), Image.LANCZOS), (ox, oy))
    return page, s, ox, oy


def detect(gray, rmin, rmax, min_score=0.6):
    """Circles in a Pillow 'L' image: [(cx, cy, r, score)] in its pixels, best first, overlapping finds suppressed."""
    w, h = gray.size
    px = list(gray.get_flattened_data() if hasattr(gray, "get_flattened_data") else gray.getdata())
    mag = [0.0] * (w * h)
    gxs = [0.0] * (w * h)
    gys = [0.0] * (w * h)
    for y in range(1, h - 1):
        r0, r1, r2 = (y - 1) * w, y * w, (y + 1) * w
        for x in range(1, w - 1):
            gx = (px[r0 + x + 1] + 2 * px[r1 + x + 1] + px[r2 + x + 1]) - (px[r0 + x - 1] + 2 * px[r1 + x - 1] + px[r2 + x - 1])
            gy = (px[r2 + x - 1] + 2 * px[r2 + x] + px[r2 + x + 1]) - (px[r0 + x - 1] + 2 * px[r0 + x] + px[r0 + x + 1])
            i = r1 + x
            mag[i] = math.hypot(gx, gy)
            gxs[i], gys[i] = gx, gy
    nz = sorted(m for m in mag if m > 0)
    if not nz:
        return []
    # the strongest 15% of edges, but never above a third of the very strongest: soft rims (jacks) next to hard ones count
    thr = max(40.0, min(nz[int(len(nz) * 0.85)], nz[int(len(nz) * 0.99)] / 3))
    nr = rmax - rmin + 1
    acc = [0] * (w * h * nr)
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            i = y * w + x
            m = mag[i]
            if m < thr:
                continue
            ux, uy = gxs[i] / m, gys[i] / m
            for k in range(nr):   # a circle's centre lies along the edge's gradient, either way (dark or light knob)
                r = rmin + k
                for sgn in (1, -1):
                    cx, cy = int(x + sgn * ux * r + 0.5), int(y + sgn * uy * r + 0.5)
                    if 0 <= cx < w and 0 <= cy < h:
                        acc[(cy * w + cx) * nr + k] += 1
    cands = []
    for c in range(w * h):
        base = c * nr
        best, bk = 0.0, 0
        for k in range(nr):
            v = acc[base + k]
            if v:
                sc = v / (2 * math.pi * (rmin + k))   # share of the circumference that voted
                if sc > best:
                    best, bk = sc, k
        if best >= 0.15:   # the Hough step only proposes; ring() below decides
            cands.append((best, c % w, c // w, rmin + bk))
    cands.sort(reverse=True)

    def ring(x, y, r, n=72):
        """Share of n directions round (x, y) with an edge at radius r (+-1) whose gradient points along the radius."""
        hit = 0
        for a in range(n):
            ca, sa = math.cos(2 * math.pi * a / n), math.sin(2 * math.pi * a / n)
            for dr in (0, -1, 1):
                ex, ey = int(x + ca * (r + dr) + 0.5), int(y + sa * (r + dr) + 0.5)
                if 0 <= ex < w and 0 <= ey < h:
                    i = ey * w + ex
                    m = mag[i]
                    if m >= thr * 0.5 and abs(gxs[i] * ca + gys[i] * sa) >= 0.6 * m:
                        hit += 1
                        break
        return hit / n

    scored = []
    for _, x, y, r in cands[:4000]:
        best = max((ring(x, y, rr), rr) for rr in (r - 1, r, r + 1) if rr >= rmin)
        if best[0] >= min_score:
            scored.append((best[0], x, y, best[1]))
    scored.sort(reverse=True)
    found = []
    for sc, x, y, r in scored:
        if all(math.hypot(x - fx, y - fy) > 0.8 * max(r, fr) for fx, fy, fr, _ in found):
            found.append((x, y, r, min(1.0, sc)))
    return found


def classify(circles):
    """kind per circle: 'knob' for the large group, 'small' for anything under 60% of the largest group's radius."""
    if not circles:
        return []
    rs = sorted(c[2] for c in circles)
    cut, gap = 0, 1.0
    for a, b in zip(rs, rs[1:]):   # the widest jump in size splits small parts from knobs
        if b / a > gap:
            cut, gap = b, b / a
    if gap < 1.4:   # one size only: all knobs
        return ["knob"] * len(circles)
    return ["knob" if c[2] >= cut else "small" for c in circles]


def reading_order(items, tol):
    """Rows (by cy within tol) top to bottom, left to right inside a row."""
    rows = []
    for it in sorted(items, key=lambda c: c["cy"]):
        if rows and abs(rows[-1][0]["cy"] - it["cy"]) <= tol:
            rows[-1].append(it)
        else:
            rows.append([it])
    return [it for row in rows for it in sorted(row, key=lambda c: c["cx"])]


Y_OFF = 86   # layout.conf y is in the shadow screen's coordinates: the plugin area starts 86 px down (shadow_skin.Y_OFF)


def layout_conf(knobs, plist, title, labels=False):
    lines = ["# First layout from tools/panel_import.py: the panel picture as page art, a knob on every round control found.",
             "# Check overlay.png, then move/rename/rebind in the studio (tools/studio.py serve layout.conf).",
             "[tab %s]" % title,
             'art file="panel.png"']
    ranged = [p for p in (plist or []) if not p.get("options") and p.get("display") != "string" and not p.get("momentary")]
    keys = []
    for n, k in enumerate(knobs):
        if plist is not None and n < len(ranged):
            key, label = ranged[n]["key"], ranged[n].get("name", ranged[n]["key"]).upper()
        else:
            key, label = "knob_%d" % (n + 1), "KNOB %d" % (n + 1)
        keys.append(key)
        lines.append('knob cx=%d cy=%d r=%d label="%s" key=%s%s' % (k["cx"], k["cy"] + Y_OFF, k["r"], label, key,
                                                                    "" if labels else " ns=0 vs=0"))
    if keys:
        lines.insert(2, "qlinks_track = " + ",".join(keys[:16]))
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("panel")
    ap.add_argument("-o", required=True, help="output folder")
    ap.add_argument("--params", help="the port's parameter file: knobs take its ranged parameters in order")
    ap.add_argument("--title", default="PANEL")
    ap.add_argument("--labels", action="store_true",
                    help="give the knobs name and value text (off by default: the panel picture has its own labels)")
    ap.add_argument("--rmin", type=int, default=14, help="smallest control radius, plugin px (default 14; lower finds letters too)")
    ap.add_argument("--rmax", type=int, default=70, help="largest control radius, plugin px (default 70)")
    ap.add_argument("--min-score", type=float, default=0.6, help="share of its rim a find must show, 0..1 (default 0.6)")
    a = ap.parse_args()
    try:
        from PIL import Image, ImageDraw, ImageFilter
    except ImportError:
        sys.exit("panel_import.py needs Pillow (pip install Pillow)")
    os.makedirs(os.path.join(a.o, "tiles"), exist_ok=True)
    page, _, _, _ = fit(Image.open(a.panel))
    page.save(os.path.join(a.o, "panel.png"))
    k = WORK / W   # plugin px -> working px
    work = page.convert("L").resize((WORK, round(H * k)), Image.LANCZOS).filter(ImageFilter.GaussianBlur(1))
    found = detect(work, max(3, round(a.rmin * k)), max(4, round(a.rmax * k)), a.min_score)
    kinds = classify(found)
    items = [{"cx": round(x / k), "cy": round(y / k), "r": round(r / k), "score": round(sc, 3), "kind": kd}
             for (x, y, r, sc), kd in zip(found, kinds)]
    items = reading_order(items, tol=max([c["r"] for c in items] or [20]))
    for n, c in enumerate(items):
        c["n"] = n + 1
        x0, y0, x1, y1 = c["cx"] - 1.6 * c["r"], c["cy"] - 1.6 * c["r"], c["cx"] + 1.6 * c["r"], c["cy"] + 2.4 * c["r"]
        page.crop(tuple(round(v) for v in (max(0, x0), max(0, y0), min(W, x1), min(H, y1)))).save(
            os.path.join(a.o, "tiles", "%02d_%s.png" % (n + 1, c["kind"])))
    json.dump(items, open(os.path.join(a.o, "controls.json"), "w"), indent=1)
    plist = None
    if a.params:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import params
        plist = params.load(a.params)[0]
    knobs = [c for c in items if c["kind"] == "knob"]
    open(os.path.join(a.o, "layout.conf"), "w").write(layout_conf(knobs, plist, a.title, a.labels))
    ov = page.copy()
    dr = ImageDraw.Draw(ov)
    for c in items:
        col = (60, 220, 90) if c["kind"] == "knob" else (240, 150, 40)
        dr.ellipse([c["cx"] - c["r"], c["cy"] - c["r"], c["cx"] + c["r"], c["cy"] + c["r"]], outline=col, width=3)
        dr.text((c["cx"] + c["r"] * 0.7, c["cy"] - c["r"]), str(c["n"]), fill=col)
    ov.save(os.path.join(a.o, "overlay.png"))
    print("%d knob(s), %d small circle(s) -> %s" % (len(knobs), len(items) - len(knobs), a.o))


if __name__ == "__main__":
    main()
