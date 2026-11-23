#!/usr/bin/env python3
"""Which registry entries a PR adds or changes in a way that needs its releases re-checked (catalog.yml, "New or changed listings
must release the portable layout"). An entry counts when it is new, or when anything but its site-only metadata changed. A bulk
edit of `style`, `tags` or `summary` across many entries (#1112 moved all 319 to fixed styles) then does not rebuild every listed
plugin, which is slow and can fail on old releases the edit did not touch.

  python3 tools/changed_entries.py BASE [HEAD]     # prints one id per line; HEAD defaults to the working tree
"""
import json
import os
import subprocess
import sys

SITE_ONLY = {"style", "tags", "summary"}   # fields that change only what the site shows, never what a release is checked against


def needs_check(old, new):
    """True for a new entry (old None) or one whose fields beyond SITE_ONLY differ; a file that is not valid JSON counts too."""
    if old is None or not isinstance(new, dict) or not isinstance(old, dict):
        return True
    strip = lambda e: {k: v for k, v in e.items() if k not in SITE_ONLY}
    return strip(old) != strip(new)


def _show(ref, path):
    r = subprocess.run(["git", "show", "%s:%s" % (ref, path)], capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def changed(base, head=None):
    args = ["git", "diff", "--name-only", "--diff-filter=AM", base] + ([head] if head else []) + ["--", "catalog/plugins"]
    out = []
    for path in subprocess.run(args, capture_output=True, text=True, check=True).stdout.split():
        if not path.endswith(".json"):
            continue
        new_raw = _show(head, path) if head else (open(path).read() if os.path.exists(path) else None)
        old_raw = _show(base, path)
        try:
            new = json.loads(new_raw) if new_raw is not None else None
            old = json.loads(old_raw) if old_raw is not None else None
        except ValueError:
            new, old = None, None
        if new_raw is not None and needs_check(old, new):
            out.append(os.path.splitext(os.path.basename(path))[0])
    return out


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    print("\n".join(changed(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)))
