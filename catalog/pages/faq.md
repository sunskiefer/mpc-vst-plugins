---
title: Questions and answers
nav: FAQ
order: 15
summary: MPC OS 3.10 (beta), Gen1 or Gen2, how plugins are checked, the Plugin Manager's web page and more.
---

Start with [Install the Plugin Manager](install.html). Still stuck? Ask in the community with your MPC model, MPC OS version and what you see.

::: details Does it work on MPC OS 3.10?
Yes, in beta. For now, the recommended way is to install the Plugin Manager on MPC OS 3.9.1 with the Plugin Manager stick, then update the MPC to 3.10: the Plugin Manager and your plugins stay installed. See [MPC OS 3.10 (beta)](install.html#mpc-os-3-10-beta).
:::

::: details Does it work on MPC OS 2.x?
The Plugin Manager and its stick don't: they need MPC OS 3.x. On 2.x, install plugins whose catalog card says "MPC OS: 2.x and 3.x" over SSH, with the desktop installer app or by hand: see [MPC OS 2.x](install.html#mpc-os-2-x) on the Install page.
:::

::: details Gen1 or Gen2 build?
Gen1 (32-bit): Live, Live II, One, X, Key 61/37 and the Force. Gen2 (64-bit): Live III, One G2, Key G2, XL. The Plugin Manager picks the right build of each plugin for your MPC.
:::

::: details Can I keep plugins on a USB stick or SD card?
Yes. The Plugin Manager's drive menu (tap the storage readout) chooses where new ones go: the internal drive, an SD card or a USB drive. The internal drive is the safest: a plugin on a stick or card is gone while it is unplugged.
:::

::: details What about the Force?
The Force runs MPC OS 3.9.1. Use the [3.9.1 steps](install.html#mpc-os-3-9-1-or-earlier): the Plugin Manager stick or the installer app.
:::

::: details Where is the Plugin Manager's web page?
Add Plugin Manager to a track: its status line at the bottom shows `On your phone or computer: http://…:8790`. Open that on any device on the same Wi-Fi. It works while the Plugin Manager is on a track; after APPLY restarts MPC it comes back when the project with the Plugin Manager reopens.
:::

::: details Is the web page safe?
It has no password: anyone on your network can use it while the Plugin Manager is open. Other web sites can't control it. On a network you don't trust, close the Plugin Manager when you're done.
:::

::: details Do sequencer plugins still drive other tracks?
Yes, through their own MIDI port: pick it as the other track's MIDI input (enable it in Preferences → MIDI first).
:::

::: details How are plugins checked before they are listed?
Every plugin has public source on GitHub, and a maintainer reviews its first listing. After that, each new release is checked automatically before it appears: the zip's layout, its checksums, that the plugin is built for your MPC's processor and MPC OS version, that its name and ID don't clash with another plugin, and that its `install.sh` and `uninstall.sh` are the catalog's standard copies (any difference goes to a person to review). A release that fails is hidden. Details for authors: [Workflow](workflow.html).
:::

::: details Is it safe to install a plugin?
Installers run with full access to your MPC, so the catalog is careful, but it can't promise a plugin is bug-free. Each release is scanned for code that uses the network, starts other programs, loads code at runtime or changes the system, and a maintainer looks at every hit. The scan catches careless cases, not someone hiding something on purpose. Every download has a SHA-256 checksum that the Plugin Manager checks before installing, older versions are kept, and a bad release can be pulled. Save your project before installing, and prefer **Verified** plugins if you want the safest choice.
:::

::: details What do Verified, Listed and Experimental mean?
The catalog works these out from evidence; authors can't pick them. **Verified** (Gen1, Gen2 or MPC Sample): the newest stable version was tested on that hardware. **Listed**: it passed the automatic checks but has no recorded hardware test yet. **Experimental**: no stable release yet; hidden unless you choose to show it. Reports from people that a plugin works move it up the list. A plugin whose source is public under a limited licence (such as non-commercial) shows a *Restricted use* badge.
:::

::: details What is "To improve this listing" on a plugin card?
A checklist of what that listing still lacks, such as a stable release, a screenshot, a skin, a CPU benchmark or a hardware test of the newest version. It is for information only and doesn't hide or demote the plugin.
:::
