#!/usr/bin/env python3
"""Finish a Plugin Manager release after CI made its draft: folder zips, the 3.9.1 stick, publishing, and the pins PR, in one go.

  tools/pm_release.py VERSION --firmware <Force or MPC 3.9.1 update.img> [--work DIR] [--publish] [--pins]

Steps (each checks before it changes anything, and stops at the first problem):
  1. find the DRAFT release of plugin-manager-g2-vVERSION (made by "Run workflow" on plugin-manager.yml in open-mpc/plugin-manager with dry_run off) and
     download its zips; check the names (Plugin-Manager-G2-VERSION-mpc-armv7/aarch64/sdk.zip) and that all hold one plugin folder
  2. make OpenMPC-Plugin-Manager-VERSION-Gen1.zip / -Gen2.zip (tools/folder_zip.py) and attach them to the draft (skipped if there)
  3. build Plugin-Manager-G2-Stick-VERSION.zip (tools/usb_g2/build_stick.py) and create the g2-stick-vVERSION draft (replaced if a draft)
  --publish  4. publish both drafts at origin/main (neither becomes "Latest": that stays the installer)
  --pins     5. on a new branch from origin/main: catalog/downloads.json, tools/desktop/g2stick-pin.json, the Install page, the site
                guide and the folder_zip.py example move to VERSION; the pinned URLs must answer 200; test_catalog.py; then a PR
Needs gh (signed in, write access) and git. Nothing is merged: the pins PR is for a person to merge (installers read the pin live).
"""
import argparse, glob, hashlib, json, os, re, subprocess, sys, tempfile, urllib.request, zipfile

REPO = "open-mpc/mpc-vst-plugins"      # the 3.9.1 stick releases and the pins live here (released installers read the pin from it)
PM_REPO = "open-mpc/plugin-manager"   # the Plugin Manager's own releases, from 1.2.0 on (1.1.0 and older stay in REPO)
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SEMVER = r"\d+\.\d+\.\d+"


def sh(*cmd, capture=True, cwd=None):
    r = subprocess.run(cmd, cwd=cwd, text=True, capture_output=capture)
    if r.returncode:
        raise SystemExit("failed: %s\n%s" % (" ".join(cmd), (r.stderr or r.stdout or "").strip()[-800:]))
    return r.stdout.strip() if capture else ""


def gh_api(path, *extra):
    return json.loads(sh("gh", "api", *extra, path) or "null")


def sha256(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def find_release(name_re, draft, repo=REPO):
    for r in gh_api("repos/%s/releases?per_page=50" % repo):
        if r["draft"] == draft and re.search(name_re, r["name"] or ""):
            return r
    return None


def zip_folder(path):
    """The one plugin folder a release zip carries (portable/<folder>/ or, in an SDK zip, the top folder)."""
    names = zipfile.ZipFile(path).namelist()
    port = {n.split("/")[2] for n in names if "/portable/" in n and n.count("/") >= 3}
    return port if port else {n.split("/")[0] for n in names}


def update_pins(root, version, sums):
    """Move the pins and links to VERSION; sums = {"pm-gen1": sha, "pm-gen2": sha, "stick": sha}. Returns the files changed."""
    changed = []
    p = os.path.join(root, "catalog", "downloads.json")
    d = json.load(open(p))
    for f in d["files"]:
        if f.get("id") in ("pm-gen1", "pm-gen2"):
            for k in ("file", "url"):
                f[k] = re.sub(r"(OpenMPC-Plugin-Manager-)%s" % SEMVER, r"\g<1>" + version, f[k])
                f[k] = re.sub(r"(plugin-manager-g2-v)%s" % SEMVER, r"\g<1>" + version, f[k])
                f[k] = f[k].replace("/%s/releases/download/plugin-manager-g2-v" % REPO, "/%s/releases/download/plugin-manager-g2-v" % PM_REPO)
            f["version"], f["sha256"] = version, sums[f["id"]]
    open(p, "w").write(json.dumps(d, indent=2) + "\n"); changed.append(p)
    p = os.path.join(root, "tools", "desktop", "g2stick-pin.json")
    d = json.load(open(p))
    d["version"] = version
    d["url"] = re.sub(r"(g2-stick-v)%s" % SEMVER, r"\g<1>" + version, re.sub(r"(Stick-)%s" % SEMVER, r"\g<1>" + version, d["url"]))
    d["sha256"] = sums["stick"]
    open(p, "w").write(json.dumps(d, indent=2) + "\n"); changed.append(p)
    for rel in ("catalog/pages/install.md", "tools/catalog_site/guide.js", "tools/folder_zip.py"):
        p = os.path.join(root, rel)
        s = open(p).read()
        n = re.sub(r"(plugin-manager-g2-v)%s" % SEMVER, r"\g<1>" + version, s)
        n = n.replace("github.com/%s/releases/tag/plugin-manager-g2-v" % REPO, "github.com/%s/releases/tag/plugin-manager-g2-v" % PM_REPO)
        n = n.replace("github.com/%s/releases/download/plugin-manager-g2-v" % REPO, "github.com/%s/releases/download/plugin-manager-g2-v" % PM_REPO)
        n = n.replace('var PM = REL + "/download/plugin-manager-g2-v', 'var PM = "https://github.com/%s/releases/download/plugin-manager-g2-v' % PM_REPO)   # guide.js
        n = re.sub(r"(OpenMPC-Plugin-Manager-)%s(-)" % SEMVER, r"\g<1>" + version + r"\2", n)
        n = re.sub(r"(Plugin-Manager-G2-)%s(-mpc-)" % SEMVER, r"\g<1>" + version + r"\2", n)
        if n != s:
            open(p, "w").write(n); changed.append(p)
    return changed


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("version")
    ap.add_argument("--firmware", required=True, help="a 3.9.1 update image (or its usr/bin/MPC) for the stick's autorun check")
    ap.add_argument("--work", help="folder for the downloads and builds (default: a temporary one)")
    ap.add_argument("--publish", action="store_true")
    ap.add_argument("--pins", action="store_true")
    a = ap.parse_args()
    v = a.version
    if not re.fullmatch(SEMVER, v):
        raise SystemExit("VERSION must look like 1.2.3")
    work = a.work or tempfile.mkdtemp(prefix="pm-release-")
    os.makedirs(work, exist_ok=True)
    tag, stag = "plugin-manager-g2-v" + v, "g2-stick-v" + v

    # 1. the draft and its zips
    rel = find_release(r"v%s$" % re.escape(v), draft=True, repo=PM_REPO) or sys.exit("no draft release ending in v%s in %s: run its workflow (dry_run off)" % (v, PM_REPO))
    want = {"Plugin-Manager-G2-%s-mpc-%s.zip" % (v, x) for x in ("armv7", "aarch64", "sdk")}
    have = {x["name"]: x for x in rel["assets"]}
    if not want <= set(have):
        raise SystemExit("the draft lacks %s (has %s)" % (sorted(want - set(have)), sorted(have)))
    for n in sorted(want):
        out = os.path.join(work, n)
        if not os.path.exists(out) or os.path.getsize(out) != have[n]["size"]:
            with open(out, "wb") as f:
                f.write(subprocess.run(["gh", "api", "-H", "Accept: application/octet-stream", "repos/%s/releases/assets/%d" % (PM_REPO, have[n]["id"])],
                                       capture_output=True, check=True).stdout)
    folders = set().union(*(zip_folder(os.path.join(work, n)) for n in want))
    if len(folders) != 1:
        raise SystemExit("the zips hold different folders: %s" % sorted(folders))
    print("1. draft %s: %s, folder %s" % (rel["name"], ", ".join(sorted(want)), folders.pop()))

    # 2. folder zips
    fz = {"pm-gen1": "OpenMPC-Plugin-Manager-%s-Gen1.zip" % v, "pm-gen2": "OpenMPC-Plugin-Manager-%s-Gen2.zip" % v}
    for key, arch in (("pm-gen1", "armv7"), ("pm-gen2", "aarch64")):
        out = os.path.join(work, fz[key])
        sh(sys.executable, os.path.join(HERE, "folder_zip.py"), os.path.join(work, "Plugin-Manager-G2-%s-mpc-%s.zip" % (v, arch)), out)
        if fz[key] not in have:
            sh("curl", "-sf", "-o", "/dev/null", "-X", "POST", "-H", "Authorization: token " + sh("gh", "auth", "token"),
               "-H", "Content-Type: application/zip", "--data-binary", "@" + out,
               "https://uploads.github.com/repos/%s/releases/%d/assets?name=%s" % (PM_REPO, rel["id"], fz[key]))
    print("2. folder zips attached: %s" % ", ".join(fz.values()))

    # 3. stick + its draft
    sdir = os.path.join(work, "stick")
    stick = os.path.join(work, "Plugin-Manager-G2-Stick-%s.zip" % v)
    gdir = os.path.join(work, "g2"); os.makedirs(gdir, exist_ok=True)
    for arch in ("armv7", "aarch64"):
        n = "Plugin-Manager-G2-%s-mpc-%s.zip" % (v, arch)
        dst = os.path.join(gdir, n)
        if not os.path.exists(dst):
            os.link(os.path.join(work, n), dst)
    sh(sys.executable, os.path.join(HERE, "usb_g2", "build_stick.py"), "--firmware", a.firmware, "--out", sdir, "--g2-dir", gdir, "--zip", stick)
    names = zipfile.ZipFile(stick).namelist()
    if not all(re.fullmatch(r"g2/Plugin-Manager-G2-[0-9][0-9A-Za-z.]*-mpc-(armv7|aarch64)\.zip", n) for n in names if n.startswith("g2/") and n.endswith(".zip")):
        raise SystemExit("the stick's g2/ files don't match what installer 2.0.x accepts")
    old = find_release(r"stick .* v%s$" % re.escape(v), draft=True)
    if old:
        sh("gh", "api", "-X", "DELETE", "repos/%s/releases/%d" % (REPO, old["id"]))
    prev = find_release(r"stick", draft=False)
    notes = os.path.join(work, "stick-notes.md")
    open(notes, "w").write(re.sub(SEMVER, v, prev["body"]) if prev and prev.get("body") else "The Plugin Manager stick for MPC OS 3.9.1, v%s.\n" % v)
    sh("gh", "release", "create", stag, "-R", REPO, "--draft", "--title", "Plugin Manager stick for MPC OS 3.9.1 v%s" % v, "--notes-file", notes, stick)
    print("3. stick %s (sha256 %s), draft %s" % (os.path.basename(stick), sha256(stick)[:12], stag))

    sums = {k: sha256(os.path.join(work, n)) for k, n in fz.items()}
    sums["stick"] = sha256(stick)

    # 4. publish
    if a.publish:
        srel = find_release(r"stick .* v%s$" % re.escape(v), draft=True)
        for r, t, repo in ((rel, tag, PM_REPO), (srel, stag, REPO)):
            main_sha = sh("git", "-C", ROOT, "ls-remote", "https://github.com/%s.git" % repo, "refs/heads/main").split()[0]
            sh("gh", "api", "-X", "PATCH", "repos/%s/releases/%d" % (repo, r["id"]), "-F", "draft=false", "-f", "make_latest=false",
               "-f", "tag_name=" + t, "-f", "target_commitish=" + main_sha)
        print("4. published %s and %s at %s" % (tag, stag, main_sha[:7]))

    # 5. pins PR
    if a.pins:
        wt = os.path.join(work, "pins")
        branch = "pm-%s-pins" % v
        sh("git", "-C", ROOT, "fetch", "-q", "origin")
        sh("git", "-C", ROOT, "worktree", "add", "-q", "-b", branch, wt, "origin/main")
        changed = update_pins(wt, v, sums)
        d = json.load(open(os.path.join(wt, "catalog", "downloads.json")))
        urls = [f["url"] for f in d["files"] if "url" in f] + [json.load(open(os.path.join(wt, "tools", "desktop", "g2stick-pin.json")))["url"]]
        for u in urls:
            try:
                code = urllib.request.urlopen(urllib.request.Request(u, method="HEAD"), timeout=30).status
            except Exception as e:
                code = getattr(e, "code", str(e))
            if code != 200:
                raise SystemExit("%s answers %s: publish first (--publish), then --pins" % (u, code))
        sh(sys.executable, os.path.join(wt, "tools", "test_catalog.py"))
        sh("git", "-C", wt, "commit", "-qam", "Plugin Manager %s: re-pin the folder zips and the 3.9.1 stick; Install page and site guide link %s\n\n"
           "Made with tools/pm_release.py." % (v, v))
        sh("git", "-C", wt, "push", "-q", "origin", "HEAD:refs/heads/" + branch)
        body = os.path.join(work, "pins-pr.md")
        open(body, "w").write("Plugin Manager %s and stick %s are published; this points the site and the installer at them "
                              "(tools/pm_release.py). Files: %s. All pinned URLs answer 200; test_catalog.py passes.\n"
                              % (v, v, ", ".join(os.path.relpath(c, wt) for c in changed)))
        print("5. pins PR:", sh("gh", "pr", "create", "-R", REPO, "--base", "main", "--head", branch,
                                "--title", "Plugin Manager %s: re-pin folder zips and stick, update links" % v, "--body-file", body))
    print("work folder:", work)


if __name__ == "__main__":
    main()
