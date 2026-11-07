set -e
# A drive MPC does not list: /media/STICK is mounted exFAT read-write but its Synths folder is not in SynthContentLocations.
# LOCATIONS offers REGISTER for it; APPLY adds the entry to MPC.settings (CRLF line ends kept).
S=/media/az01-internal/Settings/MPC; mkdir -p $S /sdcard/Synths /media/STICK
printf '<?xml version="1.0" encoding="UTF-8"?>\r\n<PROPERTIES>\r\n  <VALUE name="SynthContentLocations">\r\n    <SynthContentLocations>\r\n      <Location>/sdcard/Synths</Location>\r\n    </SynthContentLocations>\r\n  </VALUE>\r\n  <VALUE name="pluginList-arm">\r\n    <KNOWNPLUGINS/>\r\n  </VALUE>\r\n</PROPERTIES>\r\n' > $S/MPC.settings
printf 'x /sdcard exfat rw,relatime 0 0\nx /media/STICK exfat rw,relatime 0 0\n' > /tmp/pm-mounts
printf "#!/bin/sh\nexit 0\n" > /usr/bin/systemctl; chmod +x /usr/bin/systemctl
