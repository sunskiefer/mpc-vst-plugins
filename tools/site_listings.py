#!/usr/bin/env python3
"""The "Other sources" page: cards for closed-source or paid plugins that their developers host and sell themselves
(tools/catalog_listings.py). Advertisement only: every card links out; there is no Download, no Install and no checksum.
A section of the Catalog page (index.html), built only with tools/catalog_site.py --other-sources."""
from html import escape as html_escape

import catalog_listings

SLUG = "other-sources"
TITLE = "Other sources"
SUMMARY = "Closed-source and paid plugins, hosted and sold by their developers."
BANNER = ("These listings link to the developer's own site. OpenMPC does not host, sell, install or review these files, and "
          "Plugin Manager and the installer never offer them. Purchases and support are between you and the developer.")
SOURCE_LABELS = {"closed": "Closed source", "source-available": "Source available"}
DEVICE_NAMES = catalog_listings.DEVICES


def _e(x):
    return html_escape(str(x), quote=True)


def card(l):
    free = catalog_listings.is_free(l["price"])
    tags = ['<span class="tag">%s</span>' % _e(SOURCE_LABELS[l["source"]]),
            '<span class="tag">%s</span>' % _e(l["price"]),
            '<span class="tag">%s</span>' % _e(l["kind"])]
    tags += ['<span class="tag">%s</span>' % _e(t) for t in ([l["style"]] if l.get("style") else []) + l.get("tags", [])]
    rows = []
    if l.get("devices"):
        rows.append(("Developer says it works on", _e(", ".join(DEVICE_NAMES[d] for d in l["devices"]))))
    else:
        rows.append(("Devices", "not stated"))
    sup = l.get("support")
    if sup:
        rows.append(("Support", '<a href="%s" rel="noopener noreferrer">contact</a>, answers within %d days' % (_e(sup["contact"]), sup["response_days"])))
    rows.append(("Versions and updates", "see the developer's site"))
    dl = "".join("<dt>%s</dt><dd>%s</dd>" % r for r in rows)
    shot = ('<a class="shot" href="%s" rel="noopener noreferrer"><img src="%s" alt="%s screenshot" loading="lazy" referrerpolicy="no-referrer"></a>'
            % (_e(l["shop_url"]), _e(l["screenshot"]), _e(l["name"]))) if l.get("screenshot") else ""
    # grey, not red: red stays for things the catalog installs
    btns = '<a class="btn-l" href="%s" rel="sponsored noopener noreferrer">Visit developer\'s site &#8599;</a>' % _e(l["shop_url"])
    if l.get("demo_url"):
        btns += '<a class="btn-l" href="%s" rel="noopener noreferrer">Demo (developer\'s site)</a>' % _e(l["demo_url"])
    text = " ".join([l["name"], l["author"], l["summary"], l.get("style", "")] + l.get("tags", [])).lower()
    return ('<article class="card" data-kind="%s" data-price="%s" data-devices="%s" data-text="%s">%s<header><div><h2>%s</h2>'
            '<div class="by">by %s</div></div></header><div class="tags">%s</div><p>%s</p><dl class="meta">%s</dl>'
            '<div class="actions">%s</div></article>'
            % (_e(l["kind"]), "free" if free else "paid", _e(" ".join(l.get("devices", []))), _e(text), shot, _e(l["name"]), _e(l["author"]),
               "".join(tags), _e(l["summary"]), dl, btns))


def tab_html():
    """The section tab, at the right end of the Catalog page's section row (only when the build has listings)."""
    return ('<button type="button" role="tab" id="tab-other" class="tab-ext" aria-controls="panel-other" aria-selected="false" '
            'data-tab="other" tabindex="-1">%s</button>' % TITLE)


def panel_html(doc):
    """The section panel: intro, kinds and the device switch (moved in by the page script), search, grid. No install controls."""
    cards = "".join(card(l) for l in doc["listings"])
    kinds = "".join('<button type="button" role="tab" aria-selected="%s" data-okind="%s">%s <span class="n" id="n-o-%s"></span></button>'
                    % ("true" if k == "" else "false", k, label, k or "all")
                    for k, label in (("", "All"), ("instrument", "Instruments"), ("effect", "Effects")))
    return ('<div role="tabpanel" id="panel-other" aria-labelledby="tab-other" hidden>'
            '<p class="tabintro"><strong>%s</strong> %s</p>'
            '<div class="secbar"><div class="kinds" role="tablist" aria-label="Kinds" id="o-kinds">%s</div></div>'
            '<div class="tools main"><label class="search">Search<input id="o-q" type="search" placeholder="Name, developer or description"></label>'
            '<label>Price<select id="o-price"><option value="">Free and paid</option><option value="free">Free</option><option value="paid">Paid</option></select></label></div>'
            '<div class="status"><span id="o-count" aria-live="polite"></span></div>'
            '<div class="grid" id="others">%s</div><p class="empty" id="o-empty" hidden>No listings match.</p></div>'
            % (_e(SUMMARY), _e(BANNER), kinds, cards))


def data_json(doc):
    """What the Catalog page's script needs for the "N more matches in Other sources" line: no URLs, just what search and filters match."""
    import json
    rows = [{"id": l["id"], "kind": l["kind"], "style": l.get("style", ""), "tags": l.get("tags", []), "devices": l.get("devices", []),
             "text": " ".join([l["name"], l["author"], l["summary"], l.get("style", "")] + l.get("tags", [])).lower()} for l in doc["listings"]]
    return json.dumps(rows, separators=(",", ":")).replace("<", "\\u003c")
