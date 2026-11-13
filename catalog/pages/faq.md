---
title: Questions and answers
nav: FAQ
order: 15
summary: Installing and trying plugins, plugins after updating to MPC OS 3.10.1, Gen1 or Gen2, how plugins are checked, Works for me and reporting issues, the Plugin Manager's web page and more.
---

Start with [Install the Plugin Manager](install.html). Still stuck? Ask in the community with your MPC model, MPC OS version and what you see.

## Getting started

::: details How do I install and try a plugin?
Put the **Plugin Manager** on your MPC once: the [Install page](install.html) gives your steps (about five minutes; on MPC OS 3.10.1 nothing needs SSH or root). Then, on the MPC's screen or its web page on your phone, pick plugins and press **Apply**. MPC restarts once and they're under **External VST Plugins**, grouped by maker. Don't like one? **Remove** it the same way; your projects and your own files are kept.

Prefer copying by hand? See "Can I just copy plugin folders from GitHub onto the MPC?" below.
:::

::: details Gen1 or Gen2 build?
Gen1 (32-bit): Live, Live II, One, X, Key 61/37, and the Force. Gen2 (64-bit): Live III, One G2, Key G2, XL. A stick made for one won't load on the other.
:::

::: details What about the Force?
On MPC OS 3.10.1 the Force is set up like a Gen1 MPC: pick Akai Force on the [Install page](install.html) and use the Gen1 Plugin Manager zip. On 3.9.1, use the [3.9.1 steps](install.html#mpc-os-3-9-1-or-earlier): the Plugin Manager stick or the installer app.
:::

::: details Does it work on MPC OS 2.x?
The Plugin Manager and its stick don't: they need MPC OS 3.x. On 2.x, install plugins whose catalog card says "MPC OS: 2.x and 3.x" over SSH, with the desktop installer app or by hand: see [MPC OS 2.x](install.html#mpc-os-2-x) on the Install page.
:::

::: details Is Developer mode needed?
No. The web page works without it. It's only for the installer app's SSH features. It turns on SSH as root with no password: switch it off afterwards.
:::

::: details Is it safe to install a plugin?
Installers run with full access to your MPC, so the catalog is careful, but it can't promise a plugin is bug-free. Each release is scanned for code that uses the network, starts other programs, loads code at runtime or changes the system, and a maintainer looks at every hit. The scan catches careless cases, not someone hiding something on purpose. Every download has a SHA-256 checksum that the Plugin Manager checks before installing, older versions are kept, and a bad release can be pulled. Save your project before installing, and prefer **Verified** plugins if you want the safest choice.
:::

## Already had plugins before 3.10.1?

::: details I updated to 3.10.1 and my plugins are gone from the browser.
**Nothing is deleted.** 3.10.1 only shows plugins in a new folder, `VST2 Plugins`. Yours are still in the old place, and your saved projects still open them. To get them back in the browser:

1. **Turn on External Plugin Support:** Menu → Preferences → Enhancements. Wait a few seconds, then restart the MPC.
2. **Put the Plugin Manager on from the [Install page](install.html)** (pick your MPC and 3.10.1; it can write it to a USB stick for you). Do this even if you had it before: your old copy is in the old place too, so it's hidden as well.
3. **Open it and press Move**, then **APPLY**. Add it to a Plugin track (External VST Plugins → OpenMPC), or use its web page. MPC restarts once and your plugins are back, with their ROMs, presets and settings.
:::

::: details Do I need to reinstall or re-add my ROMs?
No. The move copies each plugin folder with everything in it (ROMs, cartridges, presets).
:::

::: details Will my saved projects still open?
Yes. Projects load the plugin from its old place, and from the `VST2 Plugins` copy once the old one is gone (tested on a Live III).
:::

::: details I turned off External Plugin Support and my plugins didn't come back.
On 3.10.1 the old list isn't shown with External Plugin Support off either. Keep it on and move your plugins.
:::

::: details I switched External Plugin Support on/off and it didn't stick.
Wait a few seconds after changing it before restarting MPC: the setting is saved a moment later.
:::

## Copying plugins yourself

::: details Can I just copy plugin folders from GitHub onto the MPC?
Yes, on MPC OS 3.10.1, with three things right:
- **Into `VST2 Plugins`, not `Synths`.** MPC lists only a folder named exactly `VST2 Plugins` at the root of a drive (internal, SD card or USB), with External Plugin Support on.
- **Copy the plugin folder, not the whole download.** In a release zip it is the `<Manufacturer> - VST - <Name>` folder inside `portable/`. A card's **Download (3.10.1 format)** zip is just that folder.
- **The folder needs a `plugin.json`.** Releases made with the current release workflow have one; many older ones don't, and MPC won't list them when copied by hand.

The Plugin Manager 1.0 avoids all three: it installs any plugin in the catalog (it writes the `plugin.json` itself) and puts ROMs where the plugin looks for them.
:::

::: details My new plugin doesn't show after I copied it.
On a USB stick or SD card it shows as soon as you plug it in. Copied to the MPC's internal drive, it shows after you restart MPC. Also check the folder is named `<Manufacturer> - VST - <Name>` and sits directly in `VST2 Plugins`, not in a subfolder.
:::

::: details Can I keep plugins on a USB stick or SD card?
Yes, any drive's `VST2 Plugins` folder works, and a plugged-in stick shows straight away. Leave it in: if you unplug it while a track uses one of its plugins, that plugin stops and its screen goes black until you plug the stick back in. The Plugin Manager's drive menu (tap the storage readout) chooses where new ones go.
:::

## Using plugins and the Plugin Manager

::: details Where is the Plugin Manager's web page?
Add Plugin Manager to a track: its status line at the bottom shows `On your phone or computer: http://…:8790`. Open that on any device on the same Wi-Fi. It works while the Plugin Manager is on a track; after APPLY restarts MPC it comes back when the project with the Plugin Manager reopens.
:::

::: details Is the web page safe?
It has no password: anyone on your network can use it while the Plugin Manager is open. Other web sites can't control it. On a network you don't trust, close the Plugin Manager when you're done.
:::

::: details Do sequencer plugins still drive other tracks?
Yes, through their own MIDI port: pick it as the other track's MIDI input (enable it in Preferences → MIDI first).
:::

## The catalog and the community

::: details What is Open MPC, and how is it different from Akai's SDK?
They do different jobs. **Akai's MPC Plugin SDK** is how a plugin is *built*: the code framework, the build tools and the screen kit. **Open MPC** is how plugins *reach your MPC and stay looked after* once they are built, whoever built them and with whatever tools: this catalog with its checks, CPU ratings and Works-for-me reports, the Plugin Manager, the installer app and USB sticks, support for MPC OS 3.9.1 and earlier (Akai's format needs 3.10.1), browser tiles and switchable skin themes. A growing number of catalog plugins are built with Akai's SDK; Open MPC packages, lists, installs and updates them.
:::

::: details How are plugins checked before they are listed?
Every plugin has public source on GitHub, and a maintainer reviews its first listing. After that, each new release is checked automatically before it appears: the zip's layout, its checksums, that the plugin is built for your MPC's processor and MPC OS version, that its name and ID don't clash with another plugin, and that its `install.sh` and `uninstall.sh` are the catalog's standard copies (any difference goes to a person to review). A release that fails is hidden. Details for authors: [Workflow](workflow.html).
:::

::: details What do Verified, Listed and Experimental mean?
The catalog works these out from evidence; authors can't pick them. **Verified** (Gen1, Gen2 or MPC Sample): the newest stable version was tested on that hardware. **Listed**: it passed the automatic checks but has no recorded hardware test yet. **Experimental**: no stable release yet; hidden unless you choose to show it. Reports from people that a plugin works move it up the list. A plugin whose source is public under a limited licence (such as non-commercial) shows a *Restricted use* badge.
:::

::: details What is "To improve this listing" on a plugin card?
A checklist of what that listing still lacks, such as a stable release, a screenshot, a skin, a CPU benchmark or a hardware test of the newest version. It is for information only and doesn't hide or demote the plugin.
:::

::: details How do I say a plugin works for me, or like it?
Press **Works for me** on a plugin's card or in its popup, then pick your device, MPC OS version and plugin version. Press the heart to like it. Both ask you to sign in first, using the **Sign in** button at the top of the catalog page; you don't need an account just to browse. Each person counts once per plugin, version and device. You can take a report back with **Withdraw** in the plugin's popup, and remove your account and everything it posted with **Delete my account** in the menu under your name. These reports and likes help other people choose, and they move a plugin up the **Recommended** order. They come from the community and are not hardware verification, so they never change a plugin's Verified badge.
:::

::: details Where can I see who it works for?
The card shows "✓ N working" and its likes. Open the plugin's popup for more detail: how many people reported it working on every version and on the newest one, a **Reported on** table of devices and MPC OS versions, the latest reports, and the downloads and reports for each version in the version history. Reports are shown without names. The **Most reported working** and **Most liked** sorts list the most popular plugins first. A collection card shows the figures of its best plugin, not a total; open the collection to see each one.
:::

::: details Something is wrong with a plugin. Where do I report it?
Open the plugin and use **Report an issue** in its Community section. It opens a new issue on the plugin's own GitHub repo, already filled in with the plugin and version, so you just add your MPC model, MPC OS version and what happens. Tick **It does not work** only if the plugin doesn't load, crashes or makes no sound; for anything smaller, leave it unticked. One report never changes a listing: a plugin loses Verified only when its author confirms the problem, a catalog maintainer reproduces it, or several people report the same thing on the same device and MPC OS with no answer from the author. Reporting needs a free GitHub account. The same section lists the repo's open and closed issues, with each one under the version it is about in the version history, so check whether someone has already reported your problem. **All issues** and **Discussions** (if the repo has them) link to the repo. If the repo has Issues turned off, the popup says so; ask in the community instead. For a problem with the catalog itself (a wrong listing, a broken download), open an issue on the [catalog repo](https://github.com/open-mpc/mpc-vst-plugins/issues).
:::
