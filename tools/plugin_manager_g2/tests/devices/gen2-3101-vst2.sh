set -e
# Live III on MPC OS 3.10.1 with VST2 Support on (device, 2026-10-31): the browser lists only "<drive>/VST2 Plugins/<folder>/" with a
# plugin.json; plugins registered in MPC.settings are no longer shown. One plugin installed the old way in /nvme/Synths (to move), one
# already in VST2 Plugins, and a user-files-only folder (no .so: not a plugin).
S=/data/Settings/MPC; mkdir -p $S "/nvme/Synths/acme - VST - Oldie/roms" "/nvme/VST2 Plugins/acme - VST - Newbie" "/nvme/Synths/acme - VST - RomsOnly"
printf '<?xml version="1.0" encoding="UTF-8"?>\r\n<PROPERTIES>\r\n  <VALUE name="Enhancements.Enabled" val="1"/>\r\n  <VALUE name="Enhancements.VST2Support" val="1"/>\r\n  <VALUE name="SynthContentLocations">\r\n    <SynthContentLocations>\r\n      <Location>/nvme/Synths</Location>\r\n      <Location>/synths/Expansions</Location>\r\n    </SynthContentLocations>\r\n  </VALUE>\r\n  <VALUE name="pluginList-arm-64bit">\r\n    <KNOWNPLUGINS>\r\n      <PLUGIN name="Oldie" descriptiveName="Oldie" format="VST" category="Synth"\r\n              manufacturer="acme" version="1.0" file="/nvme/Synths/acme - VST - Oldie/oldie.so"\r\n              uid="4f6c6469" isInstrument="1" fileTime="0" infoUpdateTime="0"\r\n              numInputs="0" numOutputs="2" isShell="0"/>\r\n    </KNOWNPLUGINS>\r\n  </VALUE>\r\n</PROPERTIES>\r\n' > $S/MPC.settings
echo so > "/nvme/Synths/acme - VST - Oldie/oldie.so"; echo rom > "/nvme/Synths/acme - VST - Oldie/roms/user.bin"
echo so > "/nvme/VST2 Plugins/acme - VST - Newbie/newbie.so"; echo '{}' > "/nvme/VST2 Plugins/acme - VST - Newbie/plugin.json"
echo rom > "/nvme/Synths/acme - VST - RomsOnly/rom.bin"
mkdir -p /media/STICK /media/acvs-synths /media/az01-internal   # a USB stick (vfat, noexec: MPC remounts it for VST2 plugins) and Akai's own mounts
printf 'x /nvme exfat rw,relatime 0 0\nx /media/STICK vfat rw,noexec 0 0\nx /media/acvs-synths exfat ro 0 0\nx /media/az01-internal ext4 rw 0 0\n' > /tmp/pm-mounts
printf "#!/bin/sh\nexit 0\n" > /usr/bin/systemctl; chmod +x /usr/bin/systemctl
