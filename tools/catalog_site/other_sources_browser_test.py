"""Browser test of the Other sources section of the catalog page (tools/site_listings.py, index.template.html). Offline, headless Chromium:

  mkdir -p /tmp/os && cp tools/catalog_site/fixture-catalog.json /tmp/os/catalog.json && cp tools/catalog_site/fixture-listings.json /tmp/os/listings.json
  python3 tools/catalog_site.py --catalog /tmp/os/catalog.json --out /tmp/os/site --other-sources
  python3 tools/catalog_site/other_sources_browser_test.py /tmp/os/site [/tmp/os/plain-site]

The optional second site is one built without --other-sources: it must have no Other sources tab."""
import os
import sys
from playwright.sync_api import sync_playwright
SITE = sys.argv[1]
fails = []
def check(name, cond, extra=""):
    print(("PASS " if cond else "FAIL ") + name + (" " + str(extra) if extra and not cond else ""))
    if not cond: fails.append(name)
def shown(pg): return pg.locator("#others article.card:not([hidden]) h2").all_inner_texts()
URL = "file://" + os.path.join(SITE, "index.html")

with sync_playwright() as p:
    b = p.chromium.launch(**({"executable_path": os.environ["PW_CHROMIUM"], "args": ["--no-sandbox"]} if os.environ.get("PW_CHROMIUM") else {}))
    pg = b.new_context(viewport={"width": 1280, "height": 900}).new_page(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.route("https://**", lambda r: r.abort())
    pg.goto(URL); pg.wait_for_timeout(300)
    check("no script errors", not errs, errs)
    check("the Other sources tab is in the section row, and none of its listings are on Plugins",
          pg.locator("#tab-other").is_visible() and "Pro Synth" not in pg.locator("#grid").inner_text())
    pg.fill("#q", "drums"); pg.wait_for_timeout(100)
    check("filtering Plugins says how many listings match elsewhere", pg.locator("#elsewhere").is_visible() and pg.locator("#elsewhere").inner_text().startswith("1 more match"), pg.locator("#elsewhere").inner_text())
    pg.click("#elsewhere a"); pg.wait_for_timeout(100)
    check("its link opens Other sources with the same search", pg.locator("#panel-other").is_visible() and shown(pg) == ["DRUM BOX"] and "tab=other" in pg.evaluate("location.hash"), shown(pg))
    pg.fill("#o-q", ""); pg.wait_for_timeout(100)
    check("under MPC / Force, all but the listing its developer says is for the MPC Sample only", sorted(shown(pg)) == ["DRUM BOX", "FREE FX"], shown(pg))
    check("the intro says OpenMPC does not host, sell or review them", "does not host, sell, install or review" in pg.locator("#panel-other .tabintro").inner_text())
    check("no Download, Install or checksum on a listing", pg.locator("#others").get_by_text("Download").count() == 0 and pg.locator("#others .pick").count() == 0 and pg.locator("#others").get_by_text("sha256", exact=False).count() == 0)
    check("every listing links out with rel=sponsored, as a grey button", pg.locator('#others a[rel~="sponsored"]').count() == 3 and pg.locator('#others a.btn-l.primary').count() == 0)
    check("the device switch sits in the section's first row", pg.locator("#panel-other .secbar #plat").is_visible())
    pg.select_option("#o-price", "free"); pg.wait_for_timeout(50)
    check("Free", shown(pg) == ["FREE FX"], shown(pg))
    pg.select_option("#o-price", ""); pg.click('[data-okind="effect"]'); pg.wait_for_timeout(50)
    check("Effects", shown(pg) == ["FREE FX"] and pg.locator("#n-o-effect").inner_text() == "1", shown(pg))
    pg.click('[data-okind=""]'); pg.click('[data-plat="sample"]'); pg.wait_for_timeout(50)
    check("MPC Sample: only listings whose developer says they work on it", shown(pg) == ["PRO SYNTH"] and pg.locator("#n-plat-sample").inner_text() == "1", shown(pg))
    pg.click('[data-plat=""]')
    pg.goto("about:blank"); pg.goto(URL + "#tab=other"); pg.wait_for_timeout(300)
    check("#tab=other opens the section", pg.locator("#panel-other").is_visible() and pg.locator("#tab-other").get_attribute("aria-selected") == "true")
    pg.set_viewport_size({"width": 390, "height": 900}); pg.wait_for_timeout(100)
    check("no horizontal scroll at phone width", pg.evaluate("document.documentElement.scrollWidth") <= 392)
    if len(sys.argv) > 2:
        pg.goto("file://" + os.path.join(sys.argv[2], "index.html") + "#tab=other"); pg.wait_for_timeout(300)
        check("a build without --other-sources has no Other sources tab, and #tab=other falls back to Plugins",
              pg.locator("#tab-other").count() == 0 and pg.locator("#panel-other").count() == 0 and pg.locator("#tab-plugins").get_attribute("aria-selected") == "true")
    b.close()
print("%d failed" % len(fails)); sys.exit(1 if fails else 0)
