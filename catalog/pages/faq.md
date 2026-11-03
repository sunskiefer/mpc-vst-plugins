---
title: Questions and answers
nav: FAQ
order: 15
summary: Plugins after updating to MPC OS 3.10.1, Gen1 or Gen2, the Plugin Manager's web page and more.
---

Start with [Install the Plugin Manager](install.html). Still stuck? Ask in the community with your MPC model, MPC OS version and what you see.

::: details I updated to 3.10.1 and my plugins are gone from the browser.
They're still on the drive and your projects still load them. 3.10.1 only lists plugins in `VST2 Plugins`. Turn on VST2 Support, then open Plugin Manager 1.0 and press Move (on its screen or its web page), then APPLY. MPC restarts once.
:::

::: details Do I need to reinstall or re-add my ROMs?
No. The move copies each plugin folder with everything in it (ROMs, cartridges, presets).
:::

::: details Will my saved projects still open?
Yes. Projects load the plugin from its old place, and from the `VST2 Plugins` copy once the old one is gone (tested on a Live III).
:::

::: details I turned off VST2 Support and my plugins didn't come back.
On 3.10.1 the old list isn't shown with VST2 Support off either. Keep it on and move your plugins.
:::

::: details I switched VST2 Support on/off and it didn't stick.
Wait a few seconds after changing it before restarting MPC: the setting is saved a moment later.
:::

::: details My new plugin doesn't show after I copied it.
On a USB stick or SD card it shows as soon as you plug it in. Copied to the MPC's internal drive, it shows after you restart MPC. Also check the folder is named `<Manufacturer> - VST - <Name>` and sits directly in `VST2 Plugins`, not in a subfolder.
:::

::: details Does it work on MPC OS 2.x?
No. The Plugin Manager, its stick and the installer app need MPC OS 3.x. On 2.x, install plugins whose catalog card says "MPC OS: 2.x and 3.x" by hand: see [MPC OS 2.x](install.html#mpc-os-2-x) on the Install page.
:::

::: details Gen1 or Gen2 build?
Gen1 (32-bit): Live, Live II, One, X, Key 61/37. Gen2 (64-bit): Live III, One G2, Key G2, XL. A stick made for one won't load on the other.
:::

::: details Can I keep plugins on a USB stick or SD card?
Yes, any drive's `VST2 Plugins` folder works, and a plugged-in stick shows straight away. Leave it in: if you unplug it while a track uses one of its plugins, that plugin stops and its screen goes black until you plug the stick back in. The Plugin Manager's drive menu (tap the storage readout) chooses where new ones go.
:::

::: details What about the Force?
The Force runs MPC OS 3.9.1 for now. Use the [3.9.1 steps](install.html#mpc-os-3-9-1-or-earlier): the Plugin Manager stick or the installer app.
:::

::: details Where is the Plugin Manager's web page?
Add Plugin Manager to a track: its status line at the bottom shows `On your phone or computer: http://…:8790`. Open that on any device on the same Wi-Fi. It works while the Plugin Manager is on a track; after APPLY restarts MPC it comes back when the project with the Plugin Manager reopens.
:::

::: details Is the web page safe?
It has no password: anyone on your network can use it while the Plugin Manager is open. Other web sites can't control it. On a network you don't trust, close the Plugin Manager when you're done.
:::

::: details Is Developer mode needed?
No. The web page works without it. It's only for the installer app's SSH features. It turns on SSH as root with no password: switch it off afterwards.
:::

::: details Do sequencer plugins still drive other tracks?
Yes, through their own MIDI port: pick it as the other track's MIDI input (enable it in Preferences → MIDI first).
:::
