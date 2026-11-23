"""Browser test of the Other sources page (tools/site_listings.py). Offline, headless Chromium. Run:

  mkdir -p /tmp/os && cp tools/catalog_site/fixture-catalog.json /tmp/os/catalog.json && cp tools/catalog_site/fixture-listings.json /tmp/os/listings.json
  python3 tools/catalog_site.py --catalog /tmp/os/catalog.json --out /tmp/os/site --other-sources
  python3 tools/catalog_site/other_sources_browser_test.py /tmp/os/site"""
import os
import sys
from playwright.sync_api import sync_playwright
SITE = sys.argv[1]
fails = []
def check(name, cond, extra=""):
    print(("PASS " if cond else "FAIL ") + name + (" " + str(extra) if extra and not cond else ""))
    if not cond: fails.append(name)
def shown(pg): return [t for t in pg.locator("#others article.card:not([hidden]) h2").all_inner_texts()]

with sync_playwright() as p:
    b = p.chromium.launch(**({"executable_path": os.environ["PW_CHROMIUM"], "args": ["--no-sandbox"]} if os.environ.get("PW_CHROMIUM") else {})); pg = b.new_context(viewport={"width": 1280, "height": 900}).new_page(); errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.goto("file://" + os.path.join(SITE, "other-sources.html")); pg.wait_for_timeout(200)
    check("no script errors", not errs, errs)
    check("lists every listing", sorted(shown(pg)) == ["DRUM BOX", "FREE FX", "PRO SYNTH"], shown(pg))
    check("the banner says OpenMPC does not host, sell or review", "does not host, sell, install or review" in pg.locator(".note").inner_text())
    check("no Download, Install or checksum control on any card", pg.locator("#others").get_by_text("Download").count() == 0 and pg.locator("#others").get_by_text("Install", exact=False).count() == 0 and pg.locator("#others").get_by_text("sha256", exact=False).count() == 0)
    check("every card links out with rel=sponsored", pg.locator('#others a[rel~="sponsored"]').count() == 3)
    check("the demo link is labelled as the developer's", "Demo (developer" in pg.locator("#others").text_content())
    check("the Sample claim is worded as the developer's", "Developer says it works on" in pg.locator("#others").text_content())
    pg.select_option("#o-price", "free"); check("Free filter", shown(pg) == ["FREE FX"], shown(pg))
    pg.select_option("#o-price", "paid"); pg.select_option("#o-kind", "instrument"); check("Paid + Instruments", sorted(shown(pg)) == ["DRUM BOX", "PRO SYNTH"], shown(pg))
    pg.select_option("#o-price", ""); pg.select_option("#o-kind", ""); pg.select_option("#o-dev", "mpc-sample"); check("MPC Sample shows only listings that claim it", shown(pg) == ["PRO SYNTH"], shown(pg))
    pg.select_option("#o-dev", ""); pg.fill("#o-q", "wavetable"); check("search matches tags", shown(pg) == ["PRO SYNTH"], shown(pg))
    pg.fill("#o-q", "zzz"); check("an empty result says so", pg.locator("#o-empty").is_visible() and shown(pg) == [])
    pg.goto("about:blank"); pg.goto("file://" + os.path.join(SITE, "other-sources.html") + "#plat=sample"); pg.wait_for_timeout(200)
    check("#plat=sample opens the Sample view", shown(pg) == ["PRO SYNTH"], shown(pg))
    pg.goto("file://" + os.path.join(SITE, "index.html")); pg.wait_for_timeout(300)
    check("the Catalog page has the Other sources nav item and none of the listings", pg.locator("nav a", has_text="Other sources").count() >= 1 and "Pro Synth" not in pg.content())
    pg.set_viewport_size({"width": 390, "height": 800}); pg.goto("file://" + os.path.join(SITE, "other-sources.html")); pg.wait_for_timeout(200)
    check("no horizontal scroll at phone width", pg.evaluate("document.documentElement.scrollWidth") <= 392)
    pg.screenshot(path=os.path.join(SITE, "other-sources-phone.png"))
    b.close()
print("%d failed" % len(fails)); sys.exit(1 if fails else 0)
