# Getting started (MPC OS 3.10.1 and later)

MPC OS 3.10.1 loads third-party VST2 plugins from a folder named **`VST2 Plugins`** at the root of a drive: the internal drive,
an SD card or a USB drive. Each plugin is one folder inside it, `<Manufacturer> - VST - <Name>/`, with a `plugin.json`. Every
plugin in the [Open MPC catalog](https://sd88me.github.io/mpc-vst-plugins/) comes as such a folder.

## 1. Switch it on

1. Update your MPC to **MPC OS 3.10.1** or later (Preferences, Info shows the version).
2. **Preferences, Enhancements**: turn on **Enable Enhancements**, then **VST2 Support**, and read the warning.
3. Wait a few seconds before restarting, so the setting is saved.

Works on Gen1 MPCs (Live, Live II, One, X, Key) and Gen2 (Live III, One G2, Key G2, XL). There is no 3.10.1 for the Force yet.

## 2. Get plugins onto the MPC

**Easiest: the Plugin Manager and its web page.** In the [installer app](https://github.com/sd88me/mpc-vst-plugins/releases/latest),
pick **MPC OS 3.10.1 or later**, choose Gen1 or Gen2 and your USB stick, and press **Just the Plugin Manager**. Plug the stick into
the MPC, restart, and add **Plugin Manager** to a track (External VST Plugins). Its status line shows an address like
`http://192.168.1.40:8790`: open it on a phone or computer on the same Wi-Fi. The page is the catalog (pictures, badges, details)
with **Install**, **Update**, **Remove**, **Your files** (ROMs, checked in the browser before they are copied) and **Apply**, which
restarts MPC. Save your project with the Plugin Manager on a track: after the restart the page comes back by itself. Developer
mode is not needed.

**A stick with plugins on it (no Wi-Fi).** In the [installer app](https://github.com/sd88me/mpc-vst-plugins/releases/latest), open the
**USB stick** tab, choose Gen1 or Gen2, tick the plugins you want (the **OpenMPC Plugin Manager VST** is ticked for you), pick
your USB stick or SD card and press **Download and write**. Plugins that need your own ROM files ask for them there; each file is
checked before it goes on the stick. Plug the stick into the MPC and **restart MPC**.

**By hand.** Download a plugin's zip from the catalog (the Gen1 or Gen2 one), unzip it, and copy the folder inside
`portable/` (for example `sd88me - VST - Acid`) into `VST2 Plugins` on a drive. Restart MPC.

**On the MPC.** The **OpenMPC Plugin Manager VST** also works on the MPC's own screen: it browses the catalog over Wi-Fi,
installs, updates and removes plugins, and lets you choose the drive (tap the storage readout at the top right; the web page has
the same choice).

New plugins show in the plugin browser under **External VST Plugins** after MPC restarts (a rescan finds them, but the browser
updates at the next start).

## Already have plugins?

If you installed plugins on an earlier MPC OS (with the installer app, the Plugin Manager or a stick), they are registered in
`MPC.settings`, which 3.10.1 no longer lists in the plugin browser. Nothing is lost: the plugin folders and your ROMs are still on
the drive, and saved projects still load those plugins. To see them in the browser again, copy them into `VST2 Plugins`:

- **Plugin Manager 1.0:** open it on 3.10.1 with VST2 Support on. It offers **Move N plugins to VST2 Plugins** (on its screen
  and on its web page); press **APPLY** (MPC restarts once).
- **Installer app:** connect over SSH (Developer mode starts it), then **MPC OS 3.10.1: move your plugins to VST2 Plugins**,
  **Check**, then **Move**.

Both copy each plugin folder with your files (ROMs, cartridges, presets) and never delete the old one or edit `MPC.settings`.
After the move, saved projects load the copy in `VST2 Plugins`.

## Good to know

- **MIDI generators and sequencers** (Maze Sequencer, Daemon, Acid…) still drive other tracks: pick the plugin's MIDI port as
  the other track's input (enable it in Preferences, MIDI, Enable Track first).
- **The web page** has no password: anyone on the same network can use it while the Plugin Manager is open on a track. Close
  the Plugin Manager (or the project) on a network you do not control.
- **Developer mode** (on 3.10.1) is not needed for any of this. It turns on SSH as root with no password. Switch it off when you are done on a network you do
  not control.
- Plugins are community software and run inside MPC: save often, and report problems on
  [Discord](https://discord.gg/sRRysZSgu3) or GitHub.
