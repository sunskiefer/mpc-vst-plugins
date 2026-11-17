# Reviewing catalog PRs

How to review a pull request that adds or changes a file in `catalog/plugins/` or `catalog/collections/`. Written for
catalog reviewers (the `catalog-reviewers` team), code reviewers (`code-reviewers`) and maintainers. The rules themselves live in
`catalog/README.md` (what authors are told) and `docs/CATALOG.md` (design, trust tiers, distribution types); this page is
the order to check them in, and what to do with the result. `.claude/skills/catalog-review` runs the same checklist with
Claude Code; you don't need it.

## Who decides what

| Tier | Typical PR | Who reviews and approves | Who merges |
|---|---|---|---|
| 1. Entry update | A new screenshot, summary, tags or `tip` on an entry that is already listed | A catalog reviewer, once CI is green | A maintainer |
| 2. New entry | A first listing (`catalog/plugins/<id>.json` added) | A catalog reviewer, after the whole checklist below, with a recommendation comment | A maintainer |
| 3. Maintainer call | Anything under "Hand it to a maintainer" | A maintainer | A maintainer |
| 4. Code and pipeline | `wrapper/`, `tools/`, `.github/workflows/`, docs | A code reviewer | A maintainer |

Only maintainers merge (the ruleset below enforces it). The catalog's trust files (`catalog/downloads.json`, `yanked.json`,
`confirmed.json`, `added.json`, `patches.json`), `.github/CODEOWNERS`, `.claude/` and this page are reviewed and approved by
maintainers only. A reviewer never approves their own PR, or an entry from a repo they maintain. If unsure which tier a PR is,
treat it as the next one up.

## Checklist

Run it in order: the first items are mechanical and catch most problems.

### A. Mechanics (tier 1 and 2)
1. **CI is green.** The `build` check runs `tools/test_catalog.py` and the registry check. A red check is the author's to
   fix; quote the failing line from the log in your comment. Locally: `python3 tools/catalog_build.py --check-registry`.
   If the failure is in files the PR doesn't touch, say so: the branch was cut before a registry-wide change, it isn't
   the author's error, and merging `main` in fixes it (#890, #892).
2. **Only catalog files change.** A catalog PR that also touches anything outside `catalog/plugins/` or
   `catalog/collections/` is tier 4. Also look for unrelated hunks, entries carried in from a stacked or stale branch,
   and conflict resolutions that roll `main` back (a delisted entry re-added, `flags_reviewed` dropped, an older entry
   restored: #397, #408, #663). Ask for them to be dropped. Use one PR per entry, unless the entries form a collection
   (#453).
3. **The PR is mergeable and comes from a branch, not the fork's `main`.** A PR from `main` can't be updated cleanly
   (#724); ask for a branch.
4. **Summary:** 350 characters or fewer (CI enforces it), plain description, no marketing, no other company's name used
   as a brand ("M1-style" in the summary or tags is fine; a manufacturer name or logo in the plugin name, skin or
   screenshot is not: `docs/CATALOG.md` principle 6). It describes the plugin itself:
   - No credit lines. The `author` field carries credit (#437).
   - Plain ASCII punctuation (#453).
   - Tags agree with it (#685).
   - Say up front if the plugin restarts MPC, needs root, writes outside its own folder, can't run alongside a legacy mod,
     or works only on some MPC OS versions. The detail goes in the README (#438, #706, #724, #876).
   - Testing claims match what was tested: one device means "tested on", not "supports" (#437, #685).
5. **Screenshot:** a `raw.githubusercontent.com` URL pinned to a tag or commit, never a branch, so the image can't change
   under the card. Open it: it shows the plugin's MPC page, and no manufacturer logo. Optional, but ask for one on a new
   entry (comment template below). Check that the file exists at that tag and that it's in the author's own repo, not
   another entry's (#437, #453). A version bump should move the pin too (#783).
6. **`tip`:** a plain link to the author's own Ko-fi, Patreon, GitHub Sponsors or similar page.
7. **PR description:** the PR template is filled in. That means "What and why", plus the `catalog_check.py --catalog`
   output and SHA-256 for each zip, the testing status, and ticked boxes only where the notes back them up (#520, #535,
   #536, #543). If the description is empty, or free text that skips the template (no checks, no SHA-256), ask for the
   template to be filled in as a smaller point; don't skip it because the entry itself looks fine (#408, #990).

### B. Who and what (tier 2)
8. **`repo` and `author`:** the repo exists and is public, and the PR author owns it, belongs to its organisation, or is
   named in its README. If someone lists another person's plugin, ask the owner on the plugin repo before merging.
   `author` is the name the person wants shown on the card. It's a name, not a description of the port (#448). If it's a
   real name or differs from the repo owner, confirm it with them, because it will be public (#74, #707, #893).
9. **A release exists:** the repo's latest release has an asset matching `asset_pattern` (for example
   `*-mpc-aarch64.zip`), built with `release.py` (it carries `mpc-plugin.json`). The scheduled catalog build validates it
   after merge; a release that fails shows as an issue on this repo, so a missing release just means the card stays empty.
   A build-yourself entry has no release by design (`catalog/README.md`, "If your plugin can't publish a zip").
   For a new entry, also run what CI runs: `python3 tools/catalog_build.py --require-portable <id>`.
   **If it fails on a zip that unpacks to `plugin.json`, `version.xml`, `VST/` and `Plugin Skins/`**, that is Akai's SDK
   one-folder format, not a missing-`release.py` mistake. Offer both fixes: rebuild per-CPU zips with `tools/release.py`
   (or `vst-release.yml` with `sdk_project`), **or** keep the SDK zip, name the asset `*-mpc-sdk.zip` and remove
   `asset_pattern` from the entry, and the catalog converts it itself (`docs/CATALOG.md`, "Releases with only an Akai SDK
   zip"; needs a 32-bit binary in `plugin.json`). Which to suggest: the conversion is the least work for a simple first
   listing, but it takes no `--requires` / `--depends` / `--conflicts` (`tools/sdk_release.py` has none) and refuses a
   Gen2-only plugin, so those need per-CPU zips. For an author who will keep releasing, point them at `vst-release.yml`
   with `sdk_project`, which publishes the per-CPU zips on every run. A literal
   `asset_pattern` (no `*`) is also worth a note: a rename or a new version silently matches nothing.
10. **`id` and `name`:** the `id` is lower case, stable and not already used. It can never change after merge. Neither the
    `id` nor the `name` may take another author's plugin. A second port of the same plugin gets a distinct name and `id`
    (e.g. "OB-Xd by 3dthc"), or the existing author agrees to hand over their entry (#448, #453, #463). The `name` doesn't
    include "OpenMPC", because that reads as published by this project (#845).
11. **`kind`, `style`, `tags`** match what the plugin is. If `min_os` is set it is `"3.10.1"` (CI enforces the value) and
    the plugin really needs MPC OS 3.10.1 or later: its README or `tested.json` says so. It only hides the card on older
    OS versions (`docs/CATALOG.md`), so a reviewer may approve it; ask the author if nothing supports it.

### C. Licence and contents (tier 2)
12. **Open source:** the repo has a licence file and it matches `license` (an SPDX id). For GPL and LGPL plugins the
    complete source of the released binary must be published: in the repo, or as a source archive the repo links,
    with build steps. A binary-only repo is not listed.
13. **Nobody else's firmware, ROMs or samples in the release.** An emulator that needs the user's own ROM must say so in
    the summary and ship none. `docs/CATALOG.md` principle 7 covers firmware-compiled ports (build-yourself, discontinued
    products only).
    - **Look inside the release zip, not just the repo:** list it (`python3 -I -c "import zipfile..."`, never extract and run)
      and check the skin folder for Akai's stock skin JSON or PNGs, and for bundled third-party "framework files" that need
      crediting (item 14).
    - **Other copyrighted content:** also not allowed, e.g. MIDI files of real songs (#549).
    - **No ROM-extraction tools:** the plugin repo must not ship or promote a tool that pulls a manufacturer's ROM from an
      unlicensed source (#938).
    - **Gaps in existing entries:** if an existing entry slipped through, that's a gap to fix, not a precedent (#901).
14. **Third-party code is credited:** a port of someone else's engine names it in the summary or README and keeps its
    licence and notices.
15. **Device run for installers that touch the system:** a custom installer, or anything that runs as root or hooks MPC
    (`LD_PRELOAD`, fixed addresses), needs one real install, update and uninstall on a device. The output goes in the PR
    before you recommend merge (#571, #834). An ordinary plugin entry can still be listed untested (Listed tier).

## Hand it to a maintainer

Don't approve, and don't reject either. Comment that you have asked a maintainer, add the `needs-maintainer` label, and
mention `@open-mpc/maintainers` with one line on why. Hand over when:
- **Licence doubt:** a non-OSI or non-commercial licence (PolyForm, CC-NC, "free for personal use"), source said to be
  available but not found, or a port of freeware whose redistribution terms are unclear (#901).
- **Someone else's product:** an emulation of hardware still on sale, anything that runs a manufacturer's firmware, or a
  name, skin or screenshot that may carry a brand.
- **Risky calls:** the plugin opens network sockets or runs programs, or the entry adds or changes `flags_reviewed`.
  Only a maintainer sets `flags_reviewed`.
- **Trust fields:** `featured`, `tier`, `depends`, `conflicts`, `devices`, `retire`, or a new collection. (`min_os` is not one: it is an author opt-out, checked in item 11.)
- **Ownership doubt:** the PR author isn't clearly the repo owner, or an entry changes `repo` or `author`.
- **A yank or a delisting request**, a security report, or a complaint from another author or company.

## After the review
- **All good:** approve, and leave a recommendation comment for the maintainer who merges: the tier, "recommend merge",
  what you checked and anything you couldn't (template below). For a tier 1 update one line is enough. The maintainer merges
  with a merge commit, not a squash, so the author's commits are kept.
- **Small fix needed:** request changes with the exact edit, for example a shorter summary that you counted. For an
  entry update on a branch in this repo you may push the fix yourself; never push to someone else's fork.
- **No reply for 30 days:** close it with a note that it can be reopened at any time.
- **The branch can't be updated:** a maintainer may reapply the change on current `main` and close the original with a link
  and the reason. The author keeps credit: either their commits are kept through a merge commit, or the new commit has a
  `Co-authored-by` line (#201, #586, #616, #672).
- **One of your points was wrong:** correct it in a new comment rather than editing silently (#893).
- Never edit `downloads.json`, `yanked.json` or `confirmed.json` in a catalog PR.

## Writing the comment
- **Start positive:** thank the author and name something specific before listing problems.
- **Group the points:** use **Blocking**, **Smaller points** and **Optional** sections, and end with one verdict line
  (e.g. "Requesting changes for the name only.").
- **Ask before guessing:** when the intent is unclear, ask (#408, #565).
- **Shared failures:** if `main` fails the same way, say so, so the author isn't blamed for it (#539, #834).
- **Rights disputes:** state the principle and how other entries met it, then give the compliant path (the user's own ROM,
  stand-in data). Don't argue about other entries (#901, #938).
- **First-time contributors:** when the PR doesn't follow the catalog flow, give a numbered guide from release to entry
  (#402, #549).

## Comment templates

Screenshot:
> Thanks for listing <name>! Could you add a `screenshot`? The catalog card and the Plugin Manager show it. Use a raw
> GitHub URL pinned to a tag or commit, not a branch: `https://raw.githubusercontent.com/<owner>/<repo>/<tag>/<path>.png`.

Summary too long:
> CI stops at 350 characters for the summary (yours is <n>). Here is a version at <m>: "<text>". Detail belongs in your
> README, which the card links to.

PR from `main`:
> Could you open this from a branch instead of your fork's `main`? Your `main` is <n> commits behind ours, so this PR
> conflicts, and it can't be updated without rewriting your `main`.

Recommendation (after approving):
> Reviewed against docs/CATALOG_REVIEW.md: tier <n>, **recommend merge**. Checked: CI, registry check, summary (<n> chars),
> pinned screenshot, licence <id> with published source, release asset, no ROMs or firmware. Not checked: <anything>.
> Optional for the author: <nits, or "none">.

Handed to a maintainer:
> Thanks! The entry passes the checklist. I've asked a maintainer to look at <reason> before it goes in.

## Code review (tier 4)
A code reviewer checks the change against the PR template's checklist: the offline tests it names were run (and pass in CI),
device claims are dated and say which device, docs are synced, and the repo stays device-generic (`CLAUDE.md`). Approve with
a recommendation comment (what you ran, what you could not), or request changes. Anything touching install scripts, checksums,
release signing or root-on-device behaviour also gets a maintainer's review before merge.

## GitHub setup (maintainers, once)
1. Teams in the `open-mpc` org: `maintainers` (Maintain or Admin on this repo), `catalog-reviewers` and `code-reviewers`.
   `.github/CODEOWNERS` names all three. A team's approval only counts as a code-owner review if it has Write on the repo,
   so give both reviewer teams Write; the ruleset (next step) still keeps them from merging or pushing to `main`. Until
   someone is trusted with approvals, add them with Triage instead (labels, comments, closing).
2. A branch ruleset on `main`: require a pull request with 1 approval, require review from Code Owners, block force
   pushes and deletion, and **Restrict updates** so that only the bypass list can merge. Bypass list: the `maintainers`
   team only. Don't add required status checks: every workflow here runs only for some paths (`build` in `catalog.yml`
   only for catalog and catalog-tool changes), and a required check that never starts blocks the PR for good. Reviewers and
   the merging maintainer look at the checks that did run. The workflows that push (`catalog.yml`'s mirror jobs) push to
   other repositories, so the ruleset doesn't affect them.
3. Labels: `catalog`, `new-entry`, `needs-maintainer`, `waiting-on-author`.
4. Add people to a reviewer team after a few reviews that a maintainer has looked over.
