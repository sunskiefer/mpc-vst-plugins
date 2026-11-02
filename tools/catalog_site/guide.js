/* Install page guide: the desktop installer's questions (model, MPC OS), then the steps for this MPC with the download it needs.
   Runs only on a page with the guide block (catalog_md "::: guide"). Everything it says is also in the page below it. */
(function () {
  var root = document.getElementById("guide");
  if (!root) return;
  var REL = "https://github.com/open-mpc/mpc-vst-plugins/releases";
  var PM = REL + "/download/plugin-manager-g2-v1.0.0/OpenMPC-Plugin-Manager-1.0.0-";
  var HERE = "on your computer", MPC = "on the MPC";
  var GROUPS = [["Gen1", 1, ["MPC Live / Live II", "MPC One / One+", "MPC X / X SE", "MPC Key 61 / Key 37", "Akai Force"]],
                ["Gen2", 2, ["MPC Live III", "MPC One G2", "MPC Key G2", "MPC XL"]]];
  var S = {}, hist = [];

  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text) e.textContent = text; return e; }
  function link(text, href, cls) { var a = el("a", cls, text); a.href = href; return a; }
  function opt(title, sub, fn) {
    var b = el("button", "gopt"); b.type = "button";
    var s = el("span"); s.appendChild(el("b", null, title)); if (sub) s.appendChild(el("small", null, sub));
    b.appendChild(s); b.onclick = function () { hist.push(JSON.stringify(S)); fn(); draw(); }; return b;
  }
  function computer() {
    var u = navigator.userAgent;
    if (/Windows/.test(u)) return ["Windows", "-windows.zip"];
    if (/Mac OS X/.test(u)) return ["Mac", "-mac-apple-silicon.tar.gz (Apple chip) or -mac-intel.tar.gz (Intel)"];
    if (/Linux/.test(u) && !/Android/.test(u)) return ["Linux", "-linux.tar.gz"];
    return null;
  }
  function parts(li, p) { (typeof p === "string" ? [p] : p).forEach(function (x) { li.appendChild(typeof x === "string" ? document.createTextNode(x) : x); }); }
  function plan(list) {   /* [[text or parts, where], ...] as the installer's numbered steps */
    var ol = el("ol", "gplan");
    list.forEach(function (s) { var li = el("li"), sp = el("span"); parts(sp, s[0]); li.appendChild(sp); if (s[1]) li.appendChild(el("small", null, s[1])); ol.appendChild(li); });
    return ol;
  }
  function card(step, of, q, sub) {
    var c = el("section", "gcard"), dots = el("div", "gdots");
    for (var i = 1; i <= of; i++) dots.appendChild(el("i", i <= step ? "on" : null));
    c.appendChild(dots); c.appendChild(el("h2", "gq", q)); if (sub) c.appendChild(el("p", "gsub", sub));
    return c;
  }
  function nav(c) {
    var n = el("div", "gnav");
    if (hist.length) { var b = el("button", "gback", "Back"); b.type = "button"; b.onclick = function () { S = JSON.parse(hist.pop()); draw(); }; n.appendChild(b); }
    if (S.model) n.appendChild(el("span", "gfor", S.model + (S.os ? " · MPC OS " + (S.os === "new" ? "3.10.1 or later" : "3.9.1 or earlier") : "")));
    c.appendChild(n);
  }
  function installer(c, why) {
    var b = el("div", "gdl"), k = computer();
    b.appendChild(link("Download the desktop installer app", REL + "/latest", "gbtn"));
    var p = el("p", "gsub", why + " " + (k ? "For " + k[0] + ", get the file ending in " + k[1] + ". " : "") + "Unpack it and double-click mpc-installer. ");
    p.appendChild(link("How to start it", "#the-desktop-installer-app")); b.appendChild(p); c.appendChild(b);
  }

  function draw() {
    root.textContent = "";
    var c;
    if (!S.model) {
      c = card(1, 3, "Which MPC do you have?", "A couple of questions, then the steps for your MPC.");
      GROUPS.forEach(function (g) {
        c.appendChild(el("div", "ggrp", g[0]));
        var m = el("div", "gmodels");
        g[2].forEach(function (name) { m.appendChild(opt(name, "", function () { S.model = name; S.gen = g[1]; S.force = /Force/.test(name); })); });
        c.appendChild(m);
      });
    } else if (!S.os) {
      c = card(2, 3, "Which MPC OS is on it?", "On the MPC: Menu > Preferences > Info shows the version.");
      var o = el("div", "gopts");
      o.appendChild(opt("3.10.1 or later", "Preferences has an Enhancements page", function () { S.os = "new"; }));
      o.appendChild(opt("3.9.1 or earlier", "", function () { S.os = "old"; }));
      c.appendChild(o);
    } else if (S.os === "new" && S.force) {
      c = card(3, 3, "No MPC OS 3.10.1 for the Force yet", "Akai hasn't released 3.10.1 for the Force, so yours runs 3.9.1 or earlier.");
      var f = el("div", "gopts"); f.appendChild(opt("Continue with 3.9.1 or earlier", "", function () { S.os = "old"; })); c.appendChild(f);
    } else if (S.os === "new") {
      var g = S.gen === 2 ? "Gen2" : "Gen1";
      c = card(3, 3, "Put the Plugin Manager on your MPC", "Once it is on a track, you install everything else from its screen, or from your phone or computer.");
      c.appendChild(plan([["Menu > Preferences > Enhancements: tick Enable Enhancements and VST2 Support, wait a few seconds, restart", MPC],
        [["Download ", link("OpenMPC-Plugin-Manager-1.0.0-" + g + ".zip", PM + g + ".zip"), " and unzip it: one folder, OpenMPC - VST - Plugin Manager G2"], HERE],
        ["Copy that whole folder into a VST2 Plugins folder on the MPC. Pick a way below", HERE],
        ["Add a track: External VST Plugins > OpenMPC - Plugin Manager G2. Install from its screen, or open the address it shows on your phone", MPC]]));
      [["Over Wi-Fi", "Developer mode on", [["Turn on Developer mode and note the MPC's IP address", MPC],
          ["Connect with a file app (WinSCP, Cyberduck, FileZilla) as root, no password", HERE],
          [["Copy the folder into ", el("code", null, S.gen === 2 ? "/nvme/VST2 Plugins/" : "/sdcard/VST2 Plugins/"), ", then restart the MPC"], HERE]], "#a-over-wi-fi-developer-mode"],
       ["With a USB cable", "Controller Mode", [["Put an SD card or USB drive in the MPC and connect the MPC to your computer", MPC],
          ["Menu, the computer icon: Controller Mode. The card or drive appears on your computer", MPC],
          ["Copy the folder into VST2 Plugins on it, eject, tap Standalone on the MPC. Leave the card or drive in", HERE]], "#b-with-a-usb-cable-controller-mode"],
       ["With a USB stick or SD card", "", [["Make a folder called VST2 Plugins on the stick or card and copy the folder into it", HERE],
          ["Put it in the MPC and restart. Leave it in", MPC]], "#c-with-a-usb-stick-or-sd-card"]].forEach(function (w) {
        var d = el("details", "gway"), s = el("summary"); s.appendChild(el("b", null, w[0])); if (w[1]) s.appendChild(el("small", null, " (" + w[1] + ")"));
        d.appendChild(s); d.appendChild(plan(w[2]));
        var m = el("p", "gsub"); m.appendChild(link("More detail", w[3])); d.appendChild(m); c.appendChild(d);
      });
      var a = el("details", "gothers"); a.appendChild(el("summary", null, "Rather use the desktop app?"));
      installer(a, "It can write the stick for you, or install over Wi-Fi with Developer mode."); c.appendChild(a);
    } else {
      c = card(3, 3, "Plugin Manager stick", "A stick that installs the Plugin Manager: you confirm one prompt and MPC restarts with it.");
      installer(c, "On 3.9.1 or earlier the desktop installer app makes the stick.");
      c.appendChild(plan([["Start the app, choose 3.9.1 or earlier and follow it to Plugin Manager stick. Write it to an empty USB stick", HERE],
        ["Save your project, plug the stick in and choose Open on the prompt", MPC],
        ["Add the Plugin Manager to a track and install plugins from its screen, or from your phone", MPC]]));
      var adv = el("p", "gsub", "With root SSH the same app also installs over Wi-Fi, removes plugins, and manages backups and patches: ");
      adv.appendChild(link("details", "#installing-over-wi-fi-with-the-app-mpc-os-3-9-1-or-earlier")); c.appendChild(adv);
    }
    nav(c); root.appendChild(c);
  }
  root.hidden = false;
  draw();
})();
