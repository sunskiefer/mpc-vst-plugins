/* Install page guide: a few questions, then the steps for this MPC, with the download it needs.
   Runs only on a page with the guide block (catalog_md "::: guide"). Everything it says is also in the page below it. */
(function () {
  var root = document.getElementById("guide");
  if (!root) return;
  var REL = "https://github.com/open-mpc/mpc-vst-plugins/releases";
  var PM = REL + "/download/plugin-manager-g2-v1.0.0/OpenMPC-Plugin-Manager-1.0.0-";
  var MODELS = [
    ["MPC Live", 1], ["MPC Live II", 1], ["MPC One", 1], ["MPC X", 1], ["MPC Key 61", 1], ["MPC Key 37", 1], ["Force", 1, "force"],
    ["MPC Live III", 2], ["MPC One G2", 2], ["MPC Key G2", 2], ["MPC XL", 2]];
  var S = {};

  function el(tag, cls, text) { var e = document.createElement(tag); if (cls) e.className = cls; if (text) e.textContent = text; return e; }
  function link(text, href, cls) { var a = el("a", cls, text); a.href = href; return a; }
  function btn(text, fn, cls) { var b = el("button", cls || "gbtn", text); b.type = "button"; b.onclick = fn; return b; }
  function computer() {
    var u = navigator.userAgent;
    if (/Windows/.test(u)) return ["Windows", "-windows.zip"];
    if (/Mac OS X/.test(u)) return ["Mac", "-mac-apple-silicon.tar.gz (Apple chip) or -mac-intel.tar.gz (Intel)"];
    if (/Linux/.test(u) && !/Android/.test(u)) return ["Linux", "-linux.tar.gz"];
    return null;
  }

  function ask(q, opts) {
    var box = el("div", "gq"); box.appendChild(el("p", "gqt", q));
    var row = el("div", "gopts");
    opts.forEach(function (o) { row.appendChild(btn(o[0], o[1])); });
    box.appendChild(row); return box;
  }
  function steps(list) {
    var ol = el("ol", "gsteps");
    list.forEach(function (s) {
      var li = el("li");
      (typeof s === "string" ? [s] : s).forEach(function (p) { li.appendChild(typeof p === "string" ? document.createTextNode(p) : p); });
      ol.appendChild(li);
    });
    return ol;
  }
  function installerBox(why) {
    var b = el("div", "gdl"), c = computer();
    b.appendChild(el("p", null, why));
    b.appendChild(link("Download the desktop installer app", REL + "/latest", "gbtn primary"));
    b.appendChild(el("p", "gsmall", (c ? "For " + c[0] + ", get the file ending in " + c[1] + ". " : "") + "Unpack it and double-click mpc-installer. "));
    b.lastChild.appendChild(link("How to start it", "#the-desktop-installer-app"));
    return b;
  }

  function draw() {
    root.textContent = "";
    root.appendChild(el("h2", null, "Find your steps"));
    var crumbs = el("p", "gcrumbs");
    if (S.model) crumbs.appendChild(document.createTextNode(S.model + (S.os ? ", MPC OS " + (S.os === "new" ? "3.10.1 or later" : "3.9.1 or earlier") : "") + " "));
    if (S.model) crumbs.appendChild(btn("Start again", function () { S = {}; draw(); }, "linkbtn"));
    root.appendChild(crumbs);

    if (!S.model) {
      var row = el("div", "gq"); row.appendChild(el("p", "gqt", "Which MPC do you have?"));
      [["Gen1", 1], ["Gen2", 2]].forEach(function (g) {
        var grp = el("div", "gopts"); grp.appendChild(el("span", "ggen", g[0]));
        MODELS.filter(function (m) { return m[1] === g[1]; }).forEach(function (m) {
          grp.appendChild(btn(m[0], function () { S.model = m[0]; S.gen = m[1]; S.force = m[2] === "force"; draw(); }));
        });
        row.appendChild(grp);
      });
      root.appendChild(row); return;
    }
    if (!S.os) {
      root.appendChild(ask("Which MPC OS version? (Menu → Preferences → Info)", [
        ["3.10.1 or later", function () { S.os = "new"; draw(); }], ["3.9.1 or earlier", function () { S.os = "old"; draw(); }]]));
      return;
    }
    if (S.os === "new" && S.force) {
      root.appendChild(el("p", null, "Akai hasn't released MPC OS 3.10.1 for the Force, so yours runs 3.9.1 or earlier."));
      root.appendChild(btn("Continue with 3.9.1 or earlier", function () { S.os = "old"; draw(); }));
      return;
    }
    if (S.os === "new") {
      var g = S.gen === 2 ? "Gen2" : "Gen1";
      root.appendChild(el("h3", null, "1. Turn on VST2 Support"));
      root.appendChild(steps(["On the MPC: Menu → Preferences → Enhancements. Tick Enable Enhancements and VST2 Support, wait a few seconds, restart."]));
      root.appendChild(el("h3", null, "2. Download the Plugin Manager"));
      var d = el("div", "gdl");
      d.appendChild(link("Download OpenMPC-Plugin-Manager-1.0.0-" + g + ".zip", PM + g + ".zip", "gbtn primary"));
      d.appendChild(el("p", "gsmall", "Unzip it. You get one folder, OpenMPC - VST - Plugin Manager G2. Copy that whole folder in the next step."));
      root.appendChild(d);
      root.appendChild(el("h3", null, "3. Copy it into a VST2 Plugins folder on the MPC"));
      root.appendChild(el("p", "gsmall", "Pick the way that suits you:"));
      var ways = [
        ["Over Wi-Fi (Developer mode)", ["Turn on Developer mode on the MPC and note its IP address.",
          "Connect with a file app (WinSCP, Cyberduck, FileZilla) as root, no password.",
          ["Copy the folder into ", el("code", null, S.gen === 2 ? "/nvme/VST2 Plugins/" : "/sdcard/VST2 Plugins/"), " and restart the MPC."]], "#a-over-wi-fi-developer-mode"],
        ["With a USB cable (Controller Mode)", ["Put an SD card or USB drive in the MPC and connect the MPC to your computer.",
          "Menu, then the computer icon: Controller Mode. The card or drive appears on your computer.",
          "Copy the folder into VST2 Plugins on it, eject, tap Standalone on the MPC. Leave the card or drive in."], "#b-with-a-usb-cable-controller-mode"],
        ["With a USB stick or SD card", ["On your computer, make a folder called VST2 Plugins on the stick or card.",
          "Copy the folder into it.", "Put it in the MPC and restart. Leave it in."], "#c-with-a-usb-stick-or-sd-card"]];
      ways.forEach(function (w) {
        var det = el("details", "fold"); det.appendChild(el("summary", null, w[0]));
        det.appendChild(steps(w[1]));
        var more = el("p", "gsmall"); more.appendChild(link("More detail", w[2])); det.appendChild(more);
        root.appendChild(det);
      });
      root.appendChild(el("h3", null, "4. Open it"));
      root.appendChild(steps(["Add a track: External VST Plugins → OpenMPC - Plugin Manager G2.",
        "Browse and install on its screen, or open the address on its status line on your phone or computer. Press Apply; the MPC restarts once."]));
      var alt = el("details", "fold"); alt.appendChild(el("summary", null, "Rather use an app? (optional)"));
      alt.appendChild(installerBox("The desktop installer app can write the stick for you, or install over Wi-Fi with Developer mode."));
      root.appendChild(alt);
      return;
    }
    root.appendChild(el("h3", null, "Make a Plugin Manager stick"));
    root.appendChild(installerBox("On 3.9.1 or earlier, the desktop installer app writes the stick. You need an empty USB stick."));
    root.appendChild(steps(["In the app choose 3.9.1 or earlier, follow its questions to Plugin Manager stick (one prompt), and press Write stick….",
      "Save your project. Put the stick in the MPC and choose Open on the prompt. The MPC restarts with the Plugin Manager.",
      "Add a track and choose the OpenMPC Plugin Manager. Browse and install on its screen, or open the address on its status line on your phone or computer."]));
    var adv = el("p", "gsmall"); adv.appendChild(document.createTextNode("With root SSH, the same app also installs over Wi-Fi, removes plugins, manages backups and patches: "));
    adv.appendChild(link("details", "#installing-over-wi-fi-with-the-app-mpc-os-3-9-1-or-earlier")); root.appendChild(adv);
  }
  root.hidden = false;
  draw();
})();
