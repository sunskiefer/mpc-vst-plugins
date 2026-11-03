set -e
# A Live III on MPC OS 3.10.1 with a USB stick labelled "My Stick": /proc/mounts has /media/My\040Stick. The stick must be in the
# VST2 Plugins drive menu (undecoded, it was left out; a user's report on 0.2.5, 2026-11-03).
S=/data/Settings/MPC; mkdir -p $S /nvme/Synths "/media/My Stick"
printf '<?xml version="1.0" encoding="UTF-8"?>\r\n<PROPERTIES>\r\n  <VALUE name="Enhancements.VST2Support" val="1"/>\r\n  <VALUE name="SynthContentLocations">\r\n    <SynthContentLocations>\r\n      <Location>/nvme/Synths</Location>\r\n    </SynthContentLocations>\r\n  </VALUE>\r\n  <VALUE name="pluginList-arm-64bit">\r\n    <KNOWNPLUGINS/>\r\n  </VALUE>\r\n</PROPERTIES>\r\n' > $S/MPC.settings
printf 'x /nvme exfat rw,relatime 0 0\n/dev/sda1 /media/My\\040Stick exfat rw,relatime 0 0\n' > /tmp/pm-mounts
printf "#!/bin/sh\nexit 0\n" > /usr/bin/systemctl; chmod +x /usr/bin/systemctl
