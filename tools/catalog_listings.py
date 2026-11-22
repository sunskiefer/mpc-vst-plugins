#!/usr/bin/env python3
"""Other sources: listings for closed-source or paid plugins that their developers host and sell themselves.

Advertisement only. A listing is a card that links to the developer's own site; the catalog hosts no file, checks no
checksum and installs nothing. Listings live in their own directory (catalog/listings/<id>.json) and their own output
(listings.json), never in catalog/plugins/ or catalog.json, so no installer or Plugin Manager can be offered one.

  python3 tools/catalog_listings.py [catalog/listings] [--plugins catalog/plugins] [--out catalog/dist]

Checks every file and writes listings.json (only when there is at least one listing). Exit 1 on any problem."""
import argparse
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

ID = re.compile(r"[a-z0-9]+(-[a-z0-9]+)*")
KINDS = ("instrument", "effect")             # no addins: they change how MPC loads plugins
SOURCES = ("closed", "source-available")     # an open-source plugin belongs in the catalog proper (catalog/plugins/)
DEVICES = {"mpc-sample": "MPC Sample"}       # same device ids as the plugin registry (catalog_build.DEVICES)
MAX_SUMMARY = 350
MAX_TAGS = 8
MAX_PRICE = 40
MAX_RESPONSE_DAYS = 14                       # the longest answer time a listing may promise (policy, 2026-11-10)
URL = re.compile(r"https://[^\s/]+\.[^\s/]+(?:/[^\s]*)?")
MAX_URL = 300
REQUIRED = ("id", "name", "author", "kind", "source", "price", "shop_url", "summary")
OPTIONAL = ("demo_url", "homepage", "screenshot", "style", "tags", "devices", "support")
FIELDS = REQUIRED + OPTIONAL


def is_free(price):
    return isinstance(price, str) and price.strip().lower() == "free"


def check_listing(e, fname=None):
    """Problems with one listing (a list of strings)."""
    if not isinstance(e, dict):
        return ["a listing must be a JSON object"]
    p = ["unknown field %r (allowed: %s)" % (k, ", ".join(FIELDS)) for k in sorted(e) if k not in FIELDS]
    for k in REQUIRED:
        if not isinstance(e.get(k), str) or not e[k].strip():
            p.append("missing " + k)
    if p:
        return p
    if not ID.fullmatch(e["id"]):
        p.append("bad id %r" % e["id"])
    if fname and os.path.splitext(os.path.basename(fname))[0] != e["id"]:
        p.append("file name must be <id>.json")
    if len(e["summary"]) > MAX_SUMMARY:
        p.append("summary is %d characters, the limit is %d" % (len(e["summary"]), MAX_SUMMARY))
    if e["kind"] not in KINDS:
        p.append("kind must be one of %s" % ", ".join(KINDS))
    if e["source"] == "open":
        p.append("an open-source plugin belongs in the catalog proper: add it to catalog/plugins/ instead")
    elif e["source"] not in SOURCES:
        p.append("source must be one of %s" % ", ".join(SOURCES))
    if len(e["price"]) > MAX_PRICE:
        p.append("price is %d characters, the limit is %d (for example \"Free\" or \"from $29\")" % (len(e["price"]), MAX_PRICE))
    for k in ("shop_url", "demo_url", "homepage", "screenshot"):
        if k in e and not (isinstance(e[k], str) and len(e[k]) <= MAX_URL and URL.fullmatch(e[k])):
            p.append("%s must be an https link (no spaces, at most %d characters)" % (k, MAX_URL))
    if "style" in e and not (isinstance(e["style"], str) and ID.fullmatch(e["style"])):
        p.append("style must be a lowercase slug, e.g. sampler, synth, reverb")
    if "tags" in e and not (isinstance(e["tags"], list) and all(isinstance(t, str) and ID.fullmatch(t) for t in e["tags"])):
        p.append("tags must be a list of lowercase slugs")
    elif len(e.get("tags", [])) > MAX_TAGS:
        p.append("%d tags, the limit is %d" % (len(e["tags"]), MAX_TAGS))
    if "devices" in e and not (isinstance(e["devices"], list) and e["devices"] and all(d in DEVICES for d in e["devices"])
                               and len(set(e["devices"])) == len(e["devices"])):
        p.append("devices must list the devices the developer says it works on, from: %s (leave it out for every MPC)"
                 % ", ".join(DEVICES))
    sup = e.get("support")
    if sup is None:
        if not is_free(e["price"]):
            p.append("a paid listing needs support: {\"contact\": <https link>, \"response_days\": <1-%d>}" % MAX_RESPONSE_DAYS)
    elif not isinstance(sup, dict) or set(sup) - {"contact", "response_days"}:
        p.append("support must be an object with contact and response_days only")
    else:
        if not (isinstance(sup.get("contact"), str) and len(sup["contact"]) <= MAX_URL and URL.fullmatch(sup["contact"])):
            p.append("support.contact must be an https link")
        d = sup.get("response_days")
        if not (isinstance(d, int) and not isinstance(d, bool) and 1 <= d <= MAX_RESPONSE_DAYS):
            p.append("support.response_days must be a whole number from 1 to %d" % MAX_RESPONSE_DAYS)
    return p


def load_listings(path, plugin_ids=()):
    """-> (listings, problems [(file, message)]) for every *.json in path. An id may not repeat or match a plugin in the registry."""
    listings, problems, seen = [], [], {}
    for f in sorted(glob.glob(os.path.join(path, "*.json"))):
        try:
            with open(f) as fh:
                e = json.load(fh)
        except ValueError as ex:
            problems.append((f, "invalid JSON: %s" % ex))
            continue
        errs = check_listing(e, f)
        if not errs and e["id"] in seen:
            errs.append("duplicate id, also in " + seen[e["id"]])
        if not errs and e["id"] in plugin_ids:
            errs.append("id is already a plugin in the catalog (catalog/plugins/)")
        problems += [(f, x) for x in errs]
        if not errs:
            seen[e["id"]] = f
            listings.append(e)
    return listings, problems


def build_listings(listings):
    """The listings.json document: the entries as written, sorted by id. Nothing is resolved, fetched or computed."""
    return {"listings_version": 1, "listings": sorted(listings, key=lambda e: e["id"])}


def write_listings(listings, out):
    """Write listings.json in out, but only when there is something to list; returns its path or None."""
    if not listings:
        return None
    os.makedirs(out, exist_ok=True)
    dest = os.path.join(out, "listings.json")
    with open(dest, "w") as fh:
        json.dump(build_listings(listings), fh, indent=1)
    return dest


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", nargs="?", default="catalog/listings")
    ap.add_argument("--plugins", default="catalog/plugins", help="the plugin registry, to catch an id used in both")
    ap.add_argument("--out", default="catalog/dist")
    a = ap.parse_args()
    ids = set()
    for f in glob.glob(os.path.join(a.plugins, "*.json")):
        try:
            with open(f) as fh:
                ids.add(json.load(fh).get("id"))
        except (ValueError, AttributeError):
            pass
    listings, problems = load_listings(a.path, ids)
    for f, m in problems:
        print("error: %s: %s" % (f, m), file=sys.stderr)
    if not problems:
        dest = write_listings(listings, a.out)
        print("%d listings%s" % (len(listings), ", wrote " + dest if dest else ""))
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
