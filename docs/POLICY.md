# Catalog policy (DRAFT, updated 2026-11-03)

The rules for the `open-mpc` organization (see `ORG_MIGRATION.md`). It writes down rules the catalog already applies
(`CATALOG.md`, `catalog/README.md`) and adds the governance an organization implies. Items marked **DECIDE** are open, each with a
suggestion; nothing here is in force until the owners adopt it (a PR that removes "DRAFT" from the title).

## What this project is
- A community-run catalog and toolkit for VST2 plugins on Akai MPC OS standalone devices and Force: where people find, install,
  port, skin and publish them. New plugins can be built with Akai's own plugin SDK (MPC OS 3.10.1 and later) or with this
  repo's tools; both list the same way.
- **Not affiliated with, endorsed by or sponsored by Akai Professional, inMusic or Steinberg.** Product and company names are
  used only to say what the software works with. **DECIDE:** keep this wording, or Akai's preferred wording if they ask for one
  (suggested: keep it; it is already in the site footer).
- Listing is not a security review. Nobody reads each plugin's code; install only from developers you trust. Plugins run inside
  MPC and installers run as root on the device, so the checks below exist to limit damage, not to guarantee safety.

## What can be listed
- **Open source only**, under an SPDX open licence. `source_available` is not accepted.
- **No closed binaries and no copyrighted ROMs or firmware.** A plugin that needs the user's own ROMs says so (`user_files`) and
  ships none; a port that needs the user's own firmware to build lists as *build-yourself*: source and a build command, no download.
- **No other company's branding** in skins, screenshots or plugin names: no manufacturer names, logos or wordmarks, no
  logo-bearing panel art. "X-style" is fine; implying the maker's endorsement is not.
- **A release must be catalog-conformant** (`mpc-plugin.json` from `tools/release.py`, `catalog_check.py` OK). Releases made with
  the shared release workflow are; a plugin made with Akai's SDK is listed through the same checks.
- Authors keep their own repos. Listing does not require moving a repo into the organization.

## Review and merging
- A new plugin needs one reviewed pull request adding `catalog/plugins/<id>.json`. Later releases are discovered and
  validated automatically; a version that fails validation is hidden and opens an issue.
- **Who may merge:** maintainers with write access, listed in `MAINTAINERS` (with the owners). A maintainer does not merge their
  own plugin's first entry, or their own change to the tools, without a second maintainer's review.
- Tiers are *Verified* (tested on real hardware and recorded in `tested.json`; the card says which generation and whether on
  MPC OS 3.10.1), *Listed* (passed automatic checks) and *Experimental* (new or unproven; hidden by default).
- Third-party workflow references to this repo's reusable workflows are pinned by commit; changing them is a reviewed PR.
- A change to the catalog's format is checked against the Plugin Managers and installers already in use (`tools/compat/run.sh`)
  before it merges: users who have not updated must see the same plugins.

## Yanking and delisting
- Any maintainer may yank a release by adding `"<id>@<version>"` (or `"<id>@*"`) to `catalog/yanked.json`. It stays listed
  as yanked and is never offered as latest. Reasons: a release that breaks loading on a device, a licence or branding
  problem, or a security problem.
- A plugin is delisted for a branding or licence violation, or an author who doesn't respond, after a public issue and a stated
  deadline. **DECIDE:** how long (suggested: 14 days; a security problem is yanked at once, without waiting). The author is told
  in the issue; the decision and reason are public.
- Authors may ask to be removed at any time; maintainers remove the entry in a PR.

## Security
- Report a vulnerability in the catalog, the Plugin Manager or the installer, or a malicious or unsafe plugin, privately through
  GitHub: **Security → Report a vulnerability** on `open-mpc/mpc-vst-plugins` (private vulnerability reporting; **owner to
  enable**, off as of 2026-11-03). Please don't open a public issue for these.
- A maintainer yanks first, then investigates, then says publicly what happened once users are safe.
- Maintainers: 2FA required on every account with write access (organization setting, on since 2026-10-20).

## Governance
- The organization has at least two owners at all times (one owner as of 2026-11-03: the second is added after launch).
- Owners and maintainers act as volunteers; decisions are made in public issues or PRs where practical.
- Rules change by a PR to this file approved by two maintainers (one of them an owner). **DECIDE:** adopt this (suggested: yes).
