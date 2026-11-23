#!/usr/bin/env python3
"""The "Other sources" page: cards for closed-source or paid plugins that their developers host and sell themselves
(tools/catalog_listings.py). Advertisement only: every card links out; there is no Download, no Install and no checksum.
Static HTML plus a few lines of filter script, built into other-sources.html by tools/catalog_site.py --other-sources."""
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
    btns = '<a class="btn-l primary" href="%s" rel="sponsored noopener noreferrer">Visit developer\'s site</a>' % _e(l["shop_url"])
    if l.get("demo_url"):
        btns += '<a class="btn-l" href="%s" rel="noopener noreferrer">Demo (developer\'s site)</a>' % _e(l["demo_url"])
    text = " ".join([l["name"], l["author"], l["summary"], l.get("style", "")] + l.get("tags", [])).lower()
    return ('<article class="card" data-kind="%s" data-price="%s" data-devices="%s" data-text="%s">%s<header><div><h2>%s</h2>'
            '<div class="by">by %s</div></div></header><div class="tags">%s</div><p>%s</p><dl class="meta">%s</dl>'
            '<div class="actions">%s</div></article>'
            % (_e(l["kind"]), "free" if free else "paid", _e(" ".join(l.get("devices", []))), _e(text), shot, _e(l["name"]), _e(l["author"]),
               "".join(tags), _e(l["summary"]), dl, btns))


SCRIPT = """<script>
(function () {
  var q = document.getElementById("o-q"), kind = document.getElementById("o-kind"), price = document.getElementById("o-price"),
      dev = document.getElementById("o-dev"), cards = [].slice.call(document.querySelectorAll("#others .card")),
      empty = document.getElementById("o-empty");
  function fromHash() { if (/(^|[#&])plat=sample(&|$)/.test(location.hash)) dev.value = "mpc-sample"; }
  fromHash();
  function apply() {
    var t = q.value.trim().toLowerCase(), n = 0;
    cards.forEach(function (c) {
      var ok = (!t || c.dataset.text.indexOf(t) >= 0) && (!kind.value || c.dataset.kind === kind.value) &&
               (!price.value || c.dataset.price === price.value) &&
               (!dev.value || (" " + c.dataset.devices + " ").indexOf(" " + dev.value + " ") >= 0);
      c.hidden = !ok; if (ok) n++;
    });
    empty.hidden = n > 0;
  }
  [q, kind, price, dev].forEach(function (el) { el.addEventListener("input", apply); });
  addEventListener("hashchange", function () { fromHash(); apply(); });
  apply();
})();
</script>"""


def page_body(doc):
    """HTML for the page body (inside the guide-page template, which supplies the h1 and the lede)."""
    cards = "".join(card(l) for l in doc["listings"])
    dev_opts = "".join('<option value="%s">%s</option>' % (_e(k), _e(v)) for k, v in sorted(DEVICE_NAMES.items()))
    return ('<style>.ofilters{display:flex;flex-wrap:wrap;gap:12px;margin:16px 0}.ofilters label{display:flex;flex-direction:column;gap:4px;'
            'font-size:13px;color:var(--muted)}.ofilters input,.ofilters select{font:inherit;padding:6px 8px;min-width:150px}.ofilters .search{flex:1 1 220px}</style>'
            '<p class="note"><strong>%s</strong></p>'
            '<div class="ofilters"><label class="search">Search<input id="o-q" type="search" placeholder="Name, developer or description"></label>'
            '<label>Kind<select id="o-kind"><option value="">All</option><option value="instrument">Instruments</option><option value="effect">Effects</option></select></label>'
            '<label>Price<select id="o-price"><option value="">Free and paid</option><option value="free">Free</option><option value="paid">Paid</option></select></label>'
            '<label>Device<select id="o-dev"><option value="">MPC / Force</option>%s</select></label></div>'
            '<div class="grid" id="others">%s</div><p class="empty" id="o-empty" hidden>No listings match.</p>%s'
            % (_e(BANNER), dev_opts, cards, SCRIPT))


def page(doc):
    """The guide-page dict catalog_site.render_page takes; order 45 puts it after Device patches (40)."""
    return {"slug": SLUG, "title": TITLE, "nav": TITLE, "order": 45, "summary": SUMMARY, "html": page_body(doc)}
