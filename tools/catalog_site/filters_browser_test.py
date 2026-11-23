"""Browser test of the catalog page's tag filter, the removed License and Distribution fields, the Recommended ranking and the popup's video button
(tools/catalog_site/index.template.html). Offline, headless Chromium. Run like browser_test.py:

  python3 tools/catalog_site.py --catalog tools/catalog_site/fixture-catalog.json --out /tmp/site
  docker run --rm -v /tmp:/w -v "$PWD/tools/catalog_site":/t -w /w mpc-vst-html-art python3 /t/filters_browser_test.py /w/site/index.html"""
import sys
from playwright.sync_api import sync_playwright
URL = "file://" + sys.argv[1]
fails = []
def check(name, cond, extra=""):
    print(("PASS " if cond else "FAIL ") + name + (" " + str(extra) if extra and not cond else ""))
    if not cond: fails.append(name)
def names(pg): return pg.locator("#grid article.card h2").all_inner_texts()

def open_more(pg):
    """Developer, MPC OS, Device and Tier sit under "More filters" (2026-11-23): open it before using them."""
    if pg.locator("#morebox").is_hidden(): pg.click("#more-btn")

with sync_playwright() as p:
    b = p.chromium.launch(); ctx = b.new_context(viewport={"width": 1280, "height": 900}); pg = ctx.new_page(); errs = []
    pg.route("https://*.workers.dev/**", lambda r: r.abort())   # the community service is down: cards show only downloads, no buttons
    pg.on("pageerror", lambda e: errs.append(str(e))); pg.route("https://shots.invalid/**", lambda r: r.fulfill(status=404))
    pg.goto(URL); pg.wait_for_timeout(300)
    # 1. License and Distribution are gone
    check("no License or Distribution filter", pg.locator("#f-lic").count() == 0 and pg.locator("#f-dist").count() == 0 and "License" not in " ".join(pg.locator("#panel-plugins .tools").all_inner_texts()) and "Distribution" not in " ".join(pg.locator("#panel-plugins .tools").all_inner_texts()))
    pg.goto(URL + "#lic=open&dist=release&q=synth"); pg.wait_for_timeout(300)
    check("old links that carry lic= and dist= still load, and are ignored", not errs and "TEST SYNTH" in names(pg), (errs, names(pg)))
    pg.goto(URL); pg.wait_for_timeout(300)
    # 2. tags
    tags = lambda: pg.locator("#tagchips [data-tagchip]").all_inner_texts()
    pressed = lambda: pg.locator('#tagchips [data-tagchip][aria-pressed="true"]').count()
    check("tag chips list tags used by 2+ plugins in view with counts, most used first", tags()[:1] == ["synth 2"] and not any(t.startswith(("bass", "fm", "reverb")) for t in tags()), tags())
    total = len(names(pg))
    others = [t.split(" ")[0] for t in tags() if not t.startswith("synth ")]
    pg.click('#tagchips [data-tagchip="synth"]'); pg.wait_for_timeout(100)
    check("the other chips stay offered after picking one (counted without the tag filter), so you can move between tags",
          [t.split(" ")[0] for t in pg.locator('#tagchips [data-tagchip][aria-pressed="false"]').all_inner_texts()] == others, (others, tags()))
    check("picking a tag chip filters, and the chip shows pressed", set(names(pg)) == {"TESTED TWO", "TEST SYNTH"} and len(names(pg)) == 2 and pressed() == 1, names(pg))
    check("the choice is in the URL", "tags=synth" in pg.evaluate("location.hash"), pg.evaluate("location.hash"))
    pg.locator('#grid [data-tag="bass"]').first.click(); pg.wait_for_timeout(100)
    check("clicking a tag on a card adds it (all tags must match, even one used by a single plugin), and it shows as a pressed chip", names(pg) == ["TEST SYNTH"] and pressed() == 2, names(pg))
    pg.click('#tagchips [data-tagchip="bass"]'); pg.wait_for_timeout(100)
    check("pressing a chosen chip again removes it and widens the list", set(names(pg)) == {"TESTED TWO", "TEST SYNTH"} and len(names(pg)) == 2 and pressed() == 1)
    pg.click('#tagchips [data-untag="*"]'); pg.wait_for_timeout(100)
    check("Clear tags removes them all", len(names(pg)) == total and pressed() == 0 and "tags=" not in pg.evaluate("location.hash"))
    pg.goto(URL + "#tags=synth,fm,nonsense,synth"); pg.wait_for_timeout(300)
    check("tags in a shared link are applied; unknown and repeated ones are dropped", names(pg) == ["TESTED TWO"] and pressed() == 2, (names(pg), tags()))
    pg.locator("#clear").click(); pg.wait_for_timeout(100)
    check("Clear filters also clears the tags", len(names(pg)) == total and pressed() == 0)
    # 2a. style chips: only inside a kind, the styles that kind has; one-offs fold only when enough stay in view; in step with the dropdown
    pg.goto(URL); pg.wait_for_timeout(300)
    check("no style chips under All", pg.locator("#stylechips").is_hidden())
    pg.click("#tab-instrument"); pg.wait_for_timeout(100)
    chips = lambda: pg.locator("#stylechips [data-style]").all_inner_texts()
    check("Instruments: All styles, then the kind's styles by count", chips()[0].upper() == "ALL STYLES" and chips()[1].upper().startswith("SYNTH") and not any(c.upper().startswith(("REVERB", "SEQUENCER")) for c in chips()), chips())
    pg.click('#stylechips [data-style="synth"]'); pg.wait_for_timeout(100)
    check("picking a style chip filters, sets the dropdown and the link", set(names(pg)) <= {"TEST SYNTH", "TESTED TWO", "THREE ONLY"} and len(names(pg)) >= 1
          and pg.locator("#f-style").input_value() == "synth" and "style=synth" in pg.evaluate("location.hash")
          and pg.locator('#stylechips [data-style="synth"]').get_attribute("aria-pressed") == "true", names(pg))
    check("a style picked on the chips is not counted on More filters", pg.locator("#more-n").inner_text() == "")
    pg.click("#tab-effect"); pg.wait_for_timeout(100)
    check("changing to a kind without that style drops it", pg.locator("#f-style").input_value() == "" and "style=" not in pg.evaluate("location.hash"))
    check("Effects: its own styles, none folded (all are one-offs)", sorted(c.split(" ")[0].upper() for c in chips()[1:]) == ["DYNAMICS", "REVERB", "SATURATION"] and pg.locator("#stylechips .more").count() == 0, chips())
    pg.click("#tab-plugins"); pg.click("#tab-all"); pg.wait_for_timeout(100)
    pg.fill("#q", "reverb"); pg.wait_for_timeout(100)
    check("typing in Search still finds a tag (a collection holding such plugins matches too)", "OLD CATALOG" in names(pg) and "TEST SYNTH" not in names(pg), names(pg))
    pg.fill("#q", "")
    # 2b. the details fold and the downloads bar (community service down: just the downloads)
    card = pg.locator("article.card", has=pg.locator('a[data-open="test-synth"]')).first
    check("a card's Details are folded by default, with who tested it as a hint", card.locator("details.more").count() == 1 and card.locator("details.more").get_attribute("open") is None and not card.locator("dl.meta").is_visible() and "Not yet tested" in card.locator("details.more summary").inner_text(), card.locator("details.more summary").inner_text())
    card.locator("details.more summary").click(); pg.wait_for_timeout(100)
    check("pressing it opens them", card.locator("dl.meta").is_visible() and "updated" in card.locator("dl.meta").inner_text().lower())
    bar = card.locator(".sig.stat")
    check("the downloads count is in a bar under the title, even with the details folded, and there are no community buttons while the service is down", bar.count() == 1 and "6" in bar.inner_text() and "downloads" in bar.inner_text() and card.locator(".sig-btn").count() == 0, bar.inner_text() if bar.count() else "")
    order = card.evaluate("el => [...el.children].map(c => c.className.split(' ')[0] || c.tagName.toLowerCase())")
    check("the bar sits between the title bar and its badge row (#941) and the tags", order.index("sig") == (order.index("metarow") if "metarow" in order else order.index("header")) + 1 and order[order.index("sig") + 1] == "tags", order)
    coll = pg.locator("article.card.coll").first
    check("a collection card shows its most downloaded member's downloads in the same place", "40" in coll.locator(".sig.stat").inner_text(), coll.locator(".sig.stat").inner_text() if coll.locator(".sig.stat").count() else "none")
    # 2c. the device switch (MPC / Force | MPC Sample) at the top of the filters
    pg.goto(URL); pg.wait_for_timeout(300)
    mpc, smp = pg.locator('[data-plat=""]'), pg.locator('[data-plat="sample"]')
    check("default: MPC / Force is pressed, the Sample-only addin is hidden, and each button has a count",
          mpc.get_attribute("aria-pressed") == "true" and "SAMPLE ONLY" not in names(pg) and pg.locator("#n-plat").inner_text().isdigit() and pg.locator("#n-plat-sample").inner_text().isdigit())
    open_more(pg); pg.select_option("#f-gen", "gen2"); pg.wait_for_timeout(100)
    check("the Gen2 filter does not show Sample-only entries", "SAMPLE ONLY" not in names(pg))
    smp.click(); pg.wait_for_timeout(100)
    sv = names(pg)
    check("MPC Sample: only entries with evidence for it (a Gen2 build alone is not enough: no plugin in the fixture has any); the Gen1/Gen2 filter is hidden; it is in the URL",
          sv == [] and smp.get_attribute("aria-pressed") == "true" and not pg.locator("#f-gen").is_visible() and "plat=sample" in pg.evaluate("location.hash"), sv)
    check("the tab counts and the count line follow the switch", pg.locator("#n-all").inner_text() == pg.locator("#n-plat-sample").inner_text() and (" of %s " % pg.locator("#n-plat-sample").inner_text()) in pg.locator("#count").inner_text(), (pg.locator("#n-all").inner_text(), pg.locator("#count").inner_text()))
    pg.click("#tab-addin"); pg.wait_for_timeout(100)
    check("the Sample view leaves out addins not made for the Sample", names(pg) == ["SAMPLE ONLY"], names(pg))
    check("the switch's counts are for the open section (Addins here)", pg.locator("#n-plat-sample").inner_text() == "1" and (" of 1 " in pg.locator("#count").inner_text()), (pg.locator("#n-plat-sample").inner_text(), pg.locator("#count").inner_text()))
    check("Addins is its own section: no kind row, and the switch sits inside it", not pg.locator("#kinds").is_visible() and pg.locator("#panel-plugins #plat").is_visible())
    pg.locator("#clear").click(); pg.wait_for_timeout(100)
    check("Clear filters keeps the device choice", "SAMPLE ONLY" in names(pg))
    mpc.click(); pg.wait_for_timeout(100)
    check("MPC / Force goes back", "SAMPLE ONLY" not in names(pg) and pg.locator("#f-gen").is_visible())
    pg.goto(URL + "#tab=addin&plat=sample"); pg.wait_for_timeout(300)
    check("a shared link opens the Sample view", "SAMPLE ONLY" in names(pg))
    pg.goto(URL + "#tab=addin&plat=nonsense"); pg.wait_for_timeout(300)
    check("an unknown device in a link falls back to MPC / Force", "SAMPLE ONLY" not in names(pg))
    # 2d. sections (Plugins, Addins, Alt OS, Device patches, Companion apps) and the remembered device choice
    pg.goto(URL); pg.wait_for_timeout(300)
    check("All under Plugins leaves addins out", "SAMPLE ONLY" not in names(pg) and pg.locator("#tab-plugins").get_attribute("aria-selected") == "true" and pg.locator("#kinds").is_visible())
    check("a section with nothing in it is hidden (no alternative OS listed in the repo file)", pg.locator("#tab-altos").is_hidden())
    pg.click('[data-plat="sample"]'); pg.wait_for_timeout(100)
    pg.goto("about:blank"); pg.goto(URL); pg.wait_for_timeout(300)
    check("the device choice is remembered in this browser", pg.locator('[data-plat="sample"]').get_attribute("aria-pressed") == "true")
    pg.goto(URL + "#plat="); pg.wait_for_timeout(300)
    check("a link's own device choice wins over the remembered one", pg.locator('[data-plat=""]').get_attribute("aria-pressed") == "true")
    pg.click('[data-plat=""]')
    if pg.locator("#tab-apps").is_visible():
        pg.click("#tab-apps"); pg.wait_for_timeout(100)
        check("no device switch on Companion apps", pg.locator("#plat").is_hidden())
        pg.click("#tab-plugins"); pg.wait_for_timeout(100)
        check("back on Plugins the switch is shown again, inside the panel", pg.locator("#panel-plugins #plat").is_visible())
    pg.goto(URL); pg.wait_for_timeout(300)
    # 3. the popup: tags are plain text there, and the video button
    pg.locator('a[data-open="test-synth"]').first.click(); pg.wait_for_timeout(300)
    check("the popup has its Details open", pg.locator("#modal-body details.more").get_attribute("open") is not None and pg.locator("#modal-body dl.meta").is_visible())
    check("popup tags are not buttons", pg.locator("#modal-body .tags button.tag").count() == 0 and pg.locator("#modal-body .tags span.tag", has_text="bass").count() == 1)
    v = pg.locator("#modal-video")
    check("popup shows a Video button next to Copy link, opening the catalog's youtube link in a new tab", v.is_visible() and v.get_attribute("href") == "https://www.youtube.com/watch?v=abc123" and v.get_attribute("target") == "_blank" and "noopener" in v.get_attribute("rel") and pg.locator("#modal-share").is_visible())
    bv, bs = v.bounding_box(), pg.locator("#modal-share").bounding_box()
    check("it sits right of Copy link on the same line", abs(bv["y"] - bs["y"]) < 3 and bv["x"] > bs["x"] + bs["width"] - 1, (bv, bs))
    f = pg.locator("#modal-follow")
    check("popup has Follow updates, linking this plugin's own feed", f.is_visible() and f.get_attribute("href") == "feed/test-synth.xml" and f.get_attribute("type") == "application/atom+xml", f.get_attribute("href"))
    bf = f.bounding_box(); check("it sits right of the Video button on the same line", abs(bf["y"] - bv["y"]) < 3 and bf["x"] > bv["x"] + bv["width"] - 1, (bf, bv))
    pg.keyboard.press("Escape"); pg.wait_for_timeout(200)
    pg.locator('a[data-open="tested-two"]').first.click(); pg.wait_for_timeout(300)
    check("a plugin without a youtube line has no Video button, but can still be followed", not pg.locator("#modal-video").is_visible() and pg.locator("#modal-follow").is_visible() and pg.locator("#modal-follow").get_attribute("href") == "feed/tested-two.xml")
    pg.keyboard.press("Escape")
    for bad in ("http://youtu.be/abc", "javascript:alert(1)", "https://evil.example/youtube.com/x"):
        pg.evaluate("u => { BYID['tested-two'].youtube = u; }", bad); pg.locator('a[data-open="tested-two"]').first.click(); pg.wait_for_timeout(200)
        check("a bad youtube value is never shown as a link: %s" % bad, not pg.locator("#modal-video").is_visible()); pg.keyboard.press("Escape"); pg.wait_for_timeout(100)
    pg.keyboard.press("Escape"); pg.wait_for_timeout(100)
    pg.goto(URL + "#c=acme-pack"); pg.wait_for_timeout(400)
    check("a collection popup has Follow updates for the whole collection's feed, and no Video button", pg.locator("#modal").is_visible() and pg.locator("#modal-follow").is_visible() and pg.locator("#modal-follow").get_attribute("href") == "feed/c/acme-pack.xml" and not pg.locator("#modal-video").is_visible(), pg.locator("#modal-follow").get_attribute("href"))
    pg.goto(URL); pg.wait_for_timeout(300)
    # 4. Recommended: evidence points = verification + community
    def cmp(a, b):
        return pg.evaluate("""([a, b]) => { const mk = (o) => ({ id: o.id, name: 'x', downloads: 0, tier: o.tier, verified_on: o.on, featured: !!o.feat,
            versions: [{ version: '1.0.0', channel: 'stable', yanked: false, gen2: !!o.gen2, gen2_only: !!o.only, tested: [] }] });
            return SORTS.recommended.cmp(mk(a), mk(b)); }""", [a, b])
    gen1_only = dict(id="a", tier="verified", on=["gen1"])
    dual_both = dict(id="b", tier="verified", on=["gen1", "gen2"], gen2=True)
    dual_one = dict(id="c", tier="verified", on=["gen1"], gen2=True)
    gen2_only = dict(id="d", tier="verified", on=["gen2"], gen2=True, only=True)
    listed = dict(id="l", tier="listed", on=[])
    exp = dict(id="e", tier="experimental", on=[])
    check("without counts: a Gen1-only build verified on Gen1 equals a dual build verified on both", cmp(gen1_only, dual_both) == 0 and cmp(dual_both, gen1_only) == 0)
    check("without counts: a Gen2-only build verified on Gen2 is complete too", cmp(gen2_only, dual_both) == 0 and cmp(gen2_only, dual_one) < 0)
    check("without counts: a dual build verified on one generation ranks below the complete ones, above Listed", cmp(dual_both, dual_one) < 0 and cmp(gen1_only, dual_one) < 0 and cmp(dual_one, listed) < 0)
    check("experimental is always last, featured always first", cmp(listed, exp) < 0 and cmp(dict(exp, feat=True), dual_both) > 0 or cmp(dict(listed, feat=True), dual_both) < 0)
    check("featured is first even over a verified plugin", cmp(dict(listed, id="l", feat=True), dual_both) < 0)
    pg.evaluate("() => { window.communityScore = p => ({ a: 0, b: 0, c: 0, l: 4, l2: 2, l3: 1.5, e: 100 })[p.id] || 0; }")
    check("with reports: 4 people confirming a Listed plugin beat a complete Verified badge with none", cmp(dict(listed, id="l"), gen1_only) < 0 and cmp(gen1_only, dict(listed, id="l")) > 0)
    check("but 2 people do not", cmp(dict(listed, id="l2"), gen1_only) > 0)
    check("a few reports lift a Listed plugin past a partly verified one (1.5 points)", cmp(dict(listed, id="l2"), dual_one) < 0 and cmp(dict(listed, id="l3"), dual_one) == 0)
    check("a verified plugin with reports beats a Listed one with the same reports", cmp(dict(gen1_only, id="l"), dict(listed, id="l")) < 0)
    check("reports never rescue an experimental plugin", cmp(dict(exp, id="e"), listed) > 0)
    check("between two Listed plugins, more community evidence first", cmp(dict(listed, id="l"), dict(listed, id="l2")) < 0 and cmp(dict(listed, id="l2"), dict(listed, id="l")) > 0)
    # 5. phone width
    ctx.close(); ctx = b.new_context(viewport={"width": 375, "height": 800}); pg = ctx.new_page(); pg.route("https://shots.invalid/**", lambda r: r.fulfill(status=404)); pg.goto(URL + "#tags=synth,fm"); pg.wait_for_timeout(300)
    check("phone: no horizontal scroll with tag chips shown", pg.evaluate("document.documentElement.scrollWidth <= innerWidth + 1"), pg.evaluate("[document.documentElement.scrollWidth, innerWidth]"))
    check("no page errors", not errs, errs)
    ctx.close(); b.close()
print("%d failed" % len(fails) if fails else "all passed"); sys.exit(1 if fails else 0)
