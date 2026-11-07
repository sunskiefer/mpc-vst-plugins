# Retrolooper Controls for MPC 3.10.0.49

Optional and experimental. Install Retrolooper One by itself first. Touch, Q-Links, MIDI and WAV export are available in the standalone effect. Controls adds track and pad follow, hardware pad colours and actions, selected Audio track MERGE input preparation, and timeline bounce.

With MPC transport stopped, open Retrolooper on the selected Audio track and tap **EXPORT**. Controls 0.2.13 saves a WAV and places the loop at the start of the current sequence if the destination range is empty. Use native MPC Undo to remove the region. The WAV is saved in **RetroLooper Exports** on the internal drive if the timeline edit is declined.

This installer accepts only ARMv7 and the modified MPC One+ 3.10.0.49 host build bc141062ea5ea5be619d98366f423c8102574365. The installer checks the host SHA-256. The library checks the build again before activating. It does not modify the host executable.

The patch adds /data/mpc-addins/retrolooper-buttons/retrolooper_buttons.so to /etc/systemd/system/acvs.service.d/90-mpc-addins.conf and preserves unrelated add-ins. Backups are under /data/retrolooper-controls/backups/retrolooper-buttons. The script restarts the Akai app, checks the library loaded and rolls back if it did not.

Save the project first. Run the patch with **status** for a read-only check, **install** and type PATCH to apply, or **uninstall** and type RESTORE to remove just these Controls. The effect and projects stay in place. Check status again after a power cycle. If the device mounts its add-in overlay after systemd reads it on boot, it may need its existing add-in restore setup; this patch does not add or change boot services.
