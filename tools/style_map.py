#!/usr/bin/env python3
"""Move registry entries onto the fixed styles (catalog/styles.json): from the old free-form style, the tags, or a reviewed override.
Used once on 2026-11-23 to move all entries over (docs/CATALOG.md, "Fixed styles"); kept for entries that arrive with an old style.

  python3 tools/style_map.py              # markdown table: entry, kind, old style, proposed style, why
  python3 tools/style_map.py --summary    # counts per proposed style
  python3 tools/style_map.py --apply      # write the proposed style into each catalog/plugins/<id>.json
"""
import argparse
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Reviewed by hand where the old style and the tags don't decide it (each line: id -> style). Keep it short: tags should do the work.
OVERRIDES = {
    # instruments filed as plain "synth"
    "8bit-schmitter": "experimental", "dronexxl": "experimental", "op1fx-cluster": "experimental", "meowsynth": "experimental",
    "dub-force-siren": "experimental", "piamb": "experimental", "monksynth": "experimental", "grainology": "experimental",
    "denis": "mono-bass", "twenty": "mono-bass", "maze-voice": "mono-bass", "ms20xxl": "mono-bass", "aphex": "mono-bass",
    "braids": "macro-hybrid", "mpc-plaits": "macro-hybrid", "mr-hyde": "macro-hybrid", "mono-voice": "macro-hybrid",
    "monomodule": "macro-hybrid", "monomodule-one-gen2": "macro-hybrid", "tinyfreaky": "macro-hybrid", "vader": "macro-hybrid",
    "surgexxl": "macro-hybrid", "mutable-vibe": "macro-hybrid", "op1fx-digital": "macro-hybrid", "op1fx-phase": "macro-hybrid",
    "op1fx-pulse": "macro-hybrid", "op1fx-voltage": "macro-hybrid", "interzonexxl": "macro-hybrid", "odin2": "macro-hybrid",
    "lxrxxl-zynth": "groovebox", "lxrxxl-bazzline": "groovebox",
    "elements": "physical-modeling", "fizzik": "physical-modeling", "rings": "physical-modeling", "kp-sim-prophetic": "physical-modeling",
    "op1fx-string": "physical-modeling",
    "oxzygen": "sampler-rompler", "sonix": "sampler-rompler", "fretty": "sampler-rompler",
    "roads": "keys-organ", "chordism": "fm", "downrush": "polysynth", "hexachord-73": "polysynth", "donut-theory": "polysynth",
    "osirus": "polysynth", "ostirusmpc": "polysynth", "je8086": "polysynth", "nodalred2x": "polysynth", "ms2kr": "polysynth",
    "dexed-dx7": "fm", "hank": "fm", "op3000": "fm", "op1fx-fm": "fm", "op1fx-dsynth": "fm",
    "op1fx-dna": "wavetable", "op1fx-drwave": "wavetable", "sq8lmpc": "wavetable",
    "sunflower": "experimental", "meta4mpcxxl": "experimental", "evil-tuner-xxl": "experimental",
    "midi-transport": "utility", "mpc-midi-converter": "utility", "osc-to-midi": "utility", "sysex-bandit": "utility",
    "mpcairlink": "utility", "plugin-manager-g2": "utility", "potentialnoise": "utility",
    # MIDI plugins
    "acid": "generative", "cadenza": "generative", "daemon": "generative", "eucalypso": "generative", "euclidier": "generative",
    "glass-spheres": "generative", "grids": "generative", "maze-sequencer": "generative", "piclidean-mpc": "generative",
    "pixel-walkers": "generative", "chordsmith": "arp-chord", "groove-bank": "arp-chord", "super-arp": "arp-chord",
    "midi-player": "utility",
    # effects whose old style was a catch-all
    "airwindows-tovinyl4": "lofi-tape", "aw-effects": "multi-fx", "aw-utility": "utility", "binaural-echo": "delay",
    "clif-tape": "lofi-tape", "ctrl-mixbus": "mixing", "keyscope": "utility", "mpc-radio": "lofi-tape", "tuney": "pitch-vocal",
    "voxman": "pitch-vocal", "voxboy": "pitch-vocal", "twenty-fx": "filter-eq", "tuner": "utility", "alpha": "pitch-vocal",
    "potentialbleed": "lofi-tape", "potentialgrit": "lofi-tape", "vulcan-rotary": "glitch-performance", "z950": "lofi-tape",
    "mantra": "glitch-performance", "effectwerk": "glitch-performance", "mpc-dj-fx-2": "glitch-performance", "rmxxxl": "glitch-performance",
    "studio424": "mixing", "rings-fx": "granular",
    # addins
    "commander": "control", "mjx-mpc-twister": "control", "piremote": "control", "remote": "control", "gateway": "utility",
    "nova-audio": "audio", "skipback": "audio",
}
TAG_RULES = {   # kind -> [(tags, style)], first match wins, for entries without an override or a 'from' match
    "instrument": [({"wavetable"}, "wavetable"), ({"fm"}, "fm"), ({"mono", "monophonic", "monosynth", "acid", "303", "bass", "tb-303"}, "mono-bass"),
                   ({"polyphonic", "poly", "polysynth", "paraphonic", "supersaw", "virtual-analog"}, "polysynth"),
                   ({"physical-modelling", "physical-modeling"}, "physical-modeling")],
    "midi": [({"step-sequencer"}, "step-sequencer"), ({"generative", "euclidean"}, "generative"), ({"arpeggiator", "chords"}, "arp-chord")],
    "effect": [], "addin": [],
}


def kind_of(e):
    return "midi" if e.get("role") == "midi" else e["kind"]


def propose(e, styles):
    k, old, tags = kind_of(e), e.get("style", ""), set(e.get("tags", []))
    ids = {s["id"] for s in styles[k]}
    if e["id"] in OVERRIDES:
        return OVERRIDES[e["id"]], "reviewed"
    for s in styles[k]:
        if old and old in s["from"]:
            return s["id"], "from %r" % old
    for want, s in TAG_RULES[k]:
        if tags & want:
            return s, "tags %s" % ",".join(sorted(tags & want))
    if old in ids:
        return old, "already"
    if k == "midi" and old == "sequencer":
        return "step-sequencer", "from 'sequencer'"
    return None, "needs a decision (old %r)" % old


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--registry", default=os.path.join(ROOT, "catalog", "plugins"))
    ap.add_argument("--styles", default=os.path.join(ROOT, "catalog", "styles.json"))
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--apply", action="store_true")
    a = ap.parse_args()
    styles = json.load(open(a.styles))["styles"]
    rows, bad = [], 0
    for f in sorted(glob.glob(os.path.join(a.registry, "*.json"))):
        e = json.load(open(f))
        new, why = propose(e, styles)
        if new and new not in {s["id"] for s in styles[kind_of(e)]}:
            new, why = None, "override %r is not a %s style" % (new, kind_of(e))
        bad += new is None
        rows.append((e["id"], kind_of(e), e.get("style", ""), new, why, f))
        if a.apply and new and new != e.get("style"):
            raw = open(f).read()
            if '"style"' in raw:
                raw = re.sub(r'("style"\s*:\s*)"[^"]*"', lambda m: m.group(1) + json.dumps(new), raw, count=1)
            else:
                raw = re.sub(r'("kind"\s*:\s*"[^"]*")', lambda m: m.group(1) + ', "style": ' + json.dumps(new), raw, count=1)
            json.loads(raw)
            open(f, "w").write(raw)
    if a.summary:
        lab = {(k, s["id"]): s["label"] for k, v in styles.items() for s in v}
        for k in styles:
            n = {}
            for r in rows:
                if r[1] == k:
                    n[r[3]] = n.get(r[3], 0) + 1
            print("%s (%d): %s" % (k, sum(n.values()), ", ".join("%s %d" % (lab.get((k, s), s), c) for s, c in sorted(n.items(), key=lambda x: -x[1]))))
    elif not a.apply:
        print("| entry | kind | old style | proposed | why |\n|---|---|---|---|---|")
        for r in rows:
            print("| %s | %s | %s | %s | %s |" % (r[0], r[1], r[2] or "", r[3] or "**?**", r[4]))
    print("%d entries, %d need a decision" % (len(rows), bad), file=sys.stderr)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
