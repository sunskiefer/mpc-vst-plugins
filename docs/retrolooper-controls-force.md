# Retrolooper Controls for Force 3.10

Optional and experimental. Install Retrolooper One by itself first. Touch, Q-Links and MIDI are available in the standalone effect. Controls adds Track Select follow, lower-row pad colours and actions, selected Audio track MERGE input preparation, and Bounce to Clip. Controls 0.2.10 keeps the looper editor open when you choose an Audio slot with a pad. You can also choose a slot on the Matrix touchscreen. The bounce starts as soon as the slot is selected, including while Force plays.

This installer accepts only ARMv7 and Akai Force 3.10 host build 154ae9752e0f3c6490912a703b0dfe4b668b343d. The installer checks the host SHA-256. The library checks the build again before activating. It does not modify the host executable.

The patch adds /data/mpc-addins/retrolooper-force-controls/retrolooper_force_controls.so to /etc/systemd/system/acvs.service.d/90-mpc-addins.conf and preserves unrelated add-ins. Backups are under /data/retrolooper-controls/backups/retrolooper-force-controls. The script restarts the Akai app, checks the library loaded and rolls back if it did not.

Save the project first. Run the patch with **status** for a read-only check, **install** and type PATCH to apply, or **uninstall** and type RESTORE to remove just these Controls. The effect and projects stay in place. Check status again after a power cycle. If the device mounts its add-in overlay after systemd reads it on boot, it may need its existing add-in restore setup; this patch does not add or change boot services.
