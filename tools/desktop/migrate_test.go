package main

import (
	"os"
	"strings"
	"testing"
)

// The installer carries a copy of tools/vst2_migrate.sh (go:embed cannot reach outside this folder): it must stay the same file.
func TestEmbeddedMigrateScriptMatchesTheTool(t *testing.T) {
	b, err := os.ReadFile("../vst2_migrate.sh")
	if err != nil {
		t.Fatal(err)
	}
	if string(b) != vst2MigrateSh {
		t.Fatal("tools/desktop/vst2_migrate.sh differs from tools/vst2_migrate.sh: copy it again (cp ../vst2_migrate.sh .)")
	}
}

func TestMigrateCommand(t *testing.T) {
	if c := migrateCommand(false); c != "sh -s" || strings.Contains(c, "systemctl") {
		t.Fatalf("a check must not stop MPC: %q", c)
	}
	c := migrateCommand(true)
	stop, run, start := strings.Index(c, "systemctl stop"), strings.Index(c, "--apply"), strings.Index(c, "systemctl start")
	if stop < 0 || run < stop || start < run {
		t.Fatalf("a move stops MPC, copies, then starts MPC: %q", c)
	}
	if !strings.Contains(c, "inmusic-mpc") {
		t.Fatal("the service is picked as the installer does (acvs, else inmusic-mpc)")
	}
}
