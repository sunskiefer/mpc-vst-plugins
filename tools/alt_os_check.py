#!/usr/bin/env python3
"""Validate catalog/alt_os.json: alternative OSes (standalone apps that run on the stock system and take the stock MPC app's place at boot;
they do not replace the firmware) for the MPC Sample
(`device` "mpc-sample") or the standalone MPCs and Force (`device` "mpc", with the `models` it runs on), shown on the catalog page's
Alt OS tab, which follows the device switch (MPC Sample OS / Alt OS). Not plugins and never installed by the catalog: each card
links to a pinned release zip. The rules are the entry requirements agreed in issue #958 (docs/CATALOG.md, "Alternative OS entries"):
a device report covering first boot, normal use, returning to stock and uninstall; pinned stock firmware; no root filesystem
writes; a documented way to skip it at boot; stock user data left alone; a pinned release; cleared content and full source;
bundled catalog plugins pinned; and risky behaviour (root, kernel modules, USB gadget changes, network services) declared and
reviewed by a maintainer. Usage: alt_os_check.py [alt_os.json] [--plugins catalog/plugins]. Returns (errors, systems) from check()."""
import argparse
import os
import re
import sys

ID_RE = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")
HASH = re.compile(r"^([0-9a-f]{32}|[0-9a-f]{64})$")   # md5 (like patches.json mpc_md5) or sha256 of the stock binary
DATE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
REPO = re.compile(r"^[\w.-]+/[\w.-]+$")
LOGIN = re.compile(r"^[A-Za-z0-9](-?[A-Za-z0-9])*$")
REQUIRED = ("id", "title", "summary", "author", "license", "repo", "release", "version", "boot_skip")
COVERED = ("first-boot", "normal-use", "return-to-stock", "uninstall")   # what a device report must have covered
FLAGS = ("root", "kernel_modules", "usb_gadget")
DEVICES = ("mpc-sample", "mpc")
MODELS = ("MPC Live", "MPC Live II", "MPC Live III", "MPC One", "MPC One+", "MPC One G2", "MPC X", "MPC X SE", "MPC Key 37", "MPC Key 61",
          "MPC Key G2", "MPC XL", "Force")   # "mpc" entries: the standalone models (as the site's report form names them)


def flagged(s):
    """The risky things this system declares: a maintainer must review each (`flags_reviewed`) and the card shows them."""
    f = s.get("flags") or {}
    return [k for k in FLAGS if f.get(k)] + (["network"] if f.get("network") else [])


def check(doc, plugins_dir=None):
    errors, systems = [], doc.get("systems") if isinstance(doc, dict) else None
    if not isinstance(doc, dict) or doc.get("schema") != 1 or not isinstance(systems, list):
        return ["alt_os.json must be an object with schema 1 and a 'systems' list"], []
    seen = set()
    for i, s in enumerate(systems):
        w = "systems[%d]" % i
        err = lambda m, w=w: errors.append("%s: %s" % (w, m))
        if not isinstance(s, dict):
            err("must be an object"); continue
        for k in REQUIRED:
            if not s.get(k):
                err("missing '%s'" % k)
        if s.get("id") and not ID_RE.match(s["id"]):
            err("id must be lowercase words joined by hyphens")
        if s.get("id") in seen:
            err("duplicate id %r" % s["id"])
        seen.add(s.get("id"))
        if s.get("device") not in DEVICES:
            err("device must be one of %s" % ", ".join(DEVICES))
        models = s.get("models") or []
        if s.get("device") == "mpc" and (not models or not set(models) <= set(MODELS)):
            err("models must list the standalone models it runs on, from: %s" % ", ".join(MODELS))
        if s.get("device") == "mpc-sample" and models:
            err("models is only for device \"mpc\"")
        if s.get("repo") and not REPO.match(s["repo"]):
            err("repo must be owner/name")
        for k in ("release", "screenshot", "docs"):
            if s.get(k) and not str(s[k]).startswith("https://"):
                err("%s must be an https URL" % k)
        # pinned release
        dl = s.get("download") or {}
        if not str(dl.get("url", "")).startswith("https://") or not SHA256.match(str(dl.get("sha256", ""))):
            err("download needs an https url and a sha256 (64 lowercase hex characters)")
        # pinned stock firmware
        fw = s.get("stock_firmware") or []
        if not fw:
            err("stock_firmware must list the stock versions it starts on, each with the stock binary's hash it checks")
        for j, f in enumerate(fw):
            if not f.get("version") or not HASH.match(str(f.get("binary_hash", ""))):
                err("stock_firmware[%d]: needs a version and binary_hash (md5 or sha256, lowercase hex)" % j)
        versions = {f.get("version") for f in fw}
        # device report
        reports = s.get("tested") or []
        if not reports:
            err("tested needs at least one device report from a real device")
        current = False
        for j, t in enumerate(reports):
            tw = "tested[%d]" % j
            if not t.get("by") or not DATE.match(str(t.get("date", ""))) or not t.get("version"):
                err("%s: needs by, date (YYYY-MM-DD) and the version tested" % tw)
            if t.get("firmware") not in versions:
                err("%s: firmware must be one of stock_firmware's versions" % tw)
            if s.get("device") == "mpc" and t.get("device") not in models:
                err("%s: device must be one of the entry's models" % tw)
            missing = [c for c in COVERED if c not in (t.get("covered") or [])]
            if missing:
                err("%s: covered is missing %s" % (tw, ", ".join(missing)))
            current = current or (t.get("version") == s.get("version") and not missing)
        if reports and not current:
            err("no complete device report for version %r (the one listed)" % s.get("version"))
        # leaves the system alone
        fp = s.get("footprint") or {}
        if fp.get("rootfs_writes") is not False:
            err("footprint.rootfs_writes must be false: it runs from the card and RAM and writes nothing to the root filesystem")
        if fp.get("without_media") != "stock":
            err("footprint.without_media must be \"stock\": booting without its card or drive gives the stock device")
        if not fp.get("uninstall"):
            err("footprint.uninstall must say how to remove it (deleting one folder)")
        # user data
        ud = s.get("user_data") or {}
        if ud.get("changes_stock_data") is not False:
            err("user_data.changes_stock_data must be false: it leaves the stock app's projects, samples and settings alone")
        if not isinstance(ud.get("writes_outside", []), list):
            err("user_data.writes_outside must be a list of paths outside its own folder")
        # licence and source
        c = s.get("content") or {}
        for k, m in (("source_complete", "source for everything it runs, kernel modules included"),
                     ("third_party_cleared", "no third-party banks, samples, ROMs or firmware without permission"),
                     ("no_manufacturer_branding", "no manufacturer branding (CATALOG.md principle 6)")):
            if c.get(k) is not True:
                err("content.%s must be true: %s" % (k, m))
        # bundled plugins
        for j, b in enumerate(s.get("bundled_plugins") or []):
            if not b.get("id") or not b.get("version") or not SHA256.match(str(b.get("sha256", ""))):
                err("bundled_plugins[%d]: needs the catalog id, version and the upstream release zip's sha256" % j)
            elif plugins_dir and not os.path.isfile(os.path.join(plugins_dir, b["id"] + ".json")):
                err("bundled_plugins[%d]: %r is not a catalog plugin" % (j, b["id"]))
        # flags
        f = s.get("flags")
        if not isinstance(f, dict) or any(not isinstance(f.get(k), bool) for k in FLAGS) or not isinstance(f.get("network"), list):
            err("flags must say root, kernel_modules and usb_gadget (true/false) and list network services ([] for none)")
            f = {}
        for j, n in enumerate(f.get("network") or []):
            if not isinstance(n.get("port"), int) or not isinstance(n.get("auth"), bool) or not n.get("purpose"):
                err("flags.network[%d]: needs port (number), auth (true/false) and purpose" % j)
        need = flagged(s)
        rev = s.get("flags_reviewed") or {}
        if need and (not LOGIN.match(str(rev.get("by", ""))) or not DATE.match(str(rev.get("date", ""))) or set(rev.get("flags") or []) != set(need)):
            err("flags_reviewed needs a maintainer's login (by), the date and exactly the flags reviewed: %s" % ", ".join(need))
    return errors, systems


if __name__ == "__main__":
    import json
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", nargs="?", default="catalog/alt_os.json")
    ap.add_argument("--plugins", default="catalog/plugins", help="registry folder, to check bundled_plugins ids")
    a = ap.parse_args()
    errs, systems = check(json.load(open(a.path, encoding="utf-8")), a.plugins if os.path.isdir(a.plugins) else None)
    print("\n".join(errs) or "OK (%d systems)" % len(systems))
    sys.exit(1 if errs else 0)
