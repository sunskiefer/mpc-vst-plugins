package main

// MPC OS 3.10.1 lists plugins only from "<drive>/VST2 Plugins/<folder>/" with a plugin.json; those registered in MPC.settings (every
// install this app made before) are no longer offered in the plugin browser, though saved projects still load them and fall back to the
// VST2 copy (Live III, 2026-10-31). This runs tools/vst2_migrate.sh on the connected device: "check" prints what it would copy, "move"
// stops MPC, copies every plugin folder (user files included) into VST2 Plugins with a plugin.json, and starts MPC again. It never
// edits MPC.settings or removes the old folders. The script is embedded (vst2_migrate.sh here is a copy; a test keeps it the same).

import (
	_ "embed"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
)

//go:embed vst2_migrate.sh
var vst2MigrateSh string

// migrateCommand is what runs on the device; the script comes on stdin. MPC is stopped only for a move.
func migrateCommand(apply bool) string {
	if !apply {
		return "sh -s"
	}
	return `SVC=acvs; systemctl cat acvs >/dev/null 2>&1 || ! systemctl cat inmusic-mpc >/dev/null 2>&1 || SVC=inmusic-mpc
T=$(mktemp -d) && cat > "$T/m.sh" || exit 1
systemctl stop $SVC; i=0; while pidof MPC >/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done
sh "$T/m.sh" --apply; rc=$?
rm -rf "$T"
systemctl start $SVC
exit $rc`
}

// POST /api/vst2migrate {apply}: a device job.
func (a *App) vst2Migrate(w http.ResponseWriter, r *http.Request) {
	if !postOnly(w, r) {
		return
	}
	var in struct {
		Apply bool `json:"apply"`
	}
	if err := json.NewDecoder(io.LimitReader(r.Body, 1<<10)).Decode(&in); err != nil {
		fail(w, 400, "bad request")
		return
	}
	a.mu.Lock()
	defer a.mu.Unlock()
	if a.dev == nil {
		fail(w, 400, "connect to the device first")
		return
	}
	if a.job != nil {
		if st, _, _, _ := a.job.snapshot(0); st == "running" {
			fail(w, 409, "another job is running")
			return
		}
	}
	j := &Job{ID: randHex(4), State: "running"}
	a.job = j
	dev := a.dev
	go func() {
		if in.Apply {
			j.log("stopping MPC, copying, starting MPC (save your project first)")
		}
		code, err := dev.Run(migrateCommand(in.Apply), strings.NewReader(vst2MigrateSh), func(l string) { j.log("%s", l) })
		switch {
		case err != nil:
			j.finish(err)
		case code == 3:
			j.finishWithResult("nothing to do on this MPC (needs MPC OS 3.10.1 or later with VST2 Support on)")
		case code != 0:
			j.finish(fmt.Errorf("the move stopped with exit code %d: see the log", code))
		case in.Apply:
			j.finishWithResult("moved: your plugins are under External VST Plugins (MPC was restarted)")
		default:
			j.finishWithResult("checked: nothing was changed")
		}
	}()
	writeJSON(w, 200, map[string]string{"job": j.ID})
}
