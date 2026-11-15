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
2. **Only catalog files change.** A catalog PR that also touches anything outside `catalog/plugins/` or
   `catalog/collections/` is tier 4.
3. **The PR is mergeable and comes from a branch, not the fork's `main`.** A PR from `main` can't be updated cleanly
   (#724); ask for a branch.
4. **Summary:** 350 characters or fewer (CI enforces it), plain description, no marketing, no other company's name used
   as a brand ("M1-style" in the summary or tags is fine; a manufacturer name or logo in the plugin name, skin or
   screenshot is not: `docs/CATALOG.md` principle 6).
5. **Screenshot:** a `raw.githubusercontent.com` URL pinned to a tag or commit, never a branch, so the image can't change
   under the card. Open it: it shows the plugin's MPC page, and no manufacturer logo. Optional, but ask for one on a new
   entry (comment template below).
6. **`tip`:** a plain link to the author's own Ko-fi, Patreon, GitHub Sponsors or similar page.

### B. Who and what (tier 2)
7. **`repo` and `author`:** the repo exists and is public, and the PR author owns it, belongs to its organisation, or is
   named in its README. If someone lists another person's plugin, ask the owner on the plugin repo before merging.
8. **A release exists:** the repo's latest release has an asset matching `asset_pattern` (for example
   `*-mpc-aarch64.zip`), built with `release.py` (it carries `mpc-plugin.json`). The scheduled catalog build validates it
   after merge; a release that fails shows as an issue on this repo, so a missing release just means the card stays empty.
   A build-yourself entry has no release by design (`catalog/README.md`, "If your plugin can't publish a zip").
9. **`id`:** lower case, stable, not already used. It can never change after merge.
10. **`kind`, `style`, `tags`** match what the plugin is.

### C. Licence and contents (tier 2)
11. **Open source:** the repo has a licence file and it matches `license` (an SPDX id). For GPL and LGPL plugins the
    complete source of the released binary must be published: in the repo, or as a source archive the repo links,
    with build steps. A binary-only repo is not listed.
12. **Nobody else's firmware, ROMs or samples in the release.** An emulator that needs the user's own ROM must say so in
    the summary and ship none. `docs/CATALOG.md` principle 6 covers firmware-compiled ports (build-yourself, discontinued
    products only).
13. **Third-party code is credited:** a port of someone else's engine names it in the summary or README and keeps its
    licence and notices.

## Hand it to a maintainer

Don't approve, and don't reject either. Comment that you have asked a maintainer, add the `needs-maintainer` label, and
mention `@open-mpc/maintainers` with one line on why. Hand over when:
- **Licence doubt:** a non-OSI or non-commercial licence (PolyForm, CC-NC, "free for personal use"), source said to be
  available but not found, or a port of freeware whose redistribution terms are unclear (#901).
- **Someone else's product:** an emulation of hardware still on sale, anything that runs a manufacturer's firmware, or a
  name, skin or screenshot that may carry a brand.
- **Risky calls:** the plugin opens network sockets or runs programs, or the entry adds or changes `flags_reviewed`.
  Only a maintainer sets `flags_reviewed`.
- **Trust fields:** `featured`, `tier`, `depends`, `conflicts`, `devices`, `min_os`, `retire`, or a new collection.
- **Ownership doubt:** the PR author isn't clearly the repo owner, or an entry changes `repo` or `author`.
- **A yank or a delisting request**, a security report, or a complaint from another author or company.

## After the review
- **All good:** approve, and leave a recommendation comment for the maintainer who merges: the tier, "recommend merge",
  what you checked and anything you couldn't (template below). For a tier 1 update one line is enough. The maintainer merges
  with a merge commit, not a squash, so the author's commits are kept.
- **Small fix needed:** request changes with the exact edit, for example a shorter summary that you counted. For an
  entry update on a branch in this repo you may push the fix yourself; never push to someone else's fork.
- **No reply for 30 days:** close it with a note that it can be reopened at any time.
- Never edit `downloads.json`, `yanked.json` or `confirmed.json` in a catalog PR.

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
