package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func dialInfo(t *testing.T, fd *fakeDevice) DeviceInfo {
	t.Helper()
	d, err := Dial("127.0.0.1", "secret", fd.cfg())
	if err != nil {
		t.Fatal(err)
	}
	defer d.Close()
	return d.Info
}

func TestDetectsOlderMPCOS(t *testing.T) {
	fd := newFakeDevice(t)
	os.MkdirAll(filepath.Join(fd.dir, "Synths", "me - VST - A"), 0o755)
	os.WriteFile(filepath.Join(fd.dir, "Synths", "me - VST - A", "plugin-meta.xml"), []byte(`<PLUGIN name="A" uid="1"/>`), 0o644)
	in := dialInfo(t, fd)
	if in.VST2 != "" || in.LegacyCount != 0 || in.Model != "" || in.MPCOS != "" || in.Manager != "" {
		t.Fatalf("an MPC OS without VST2 Support must read as none: %+v", in)
	}
}

func TestDetectsMPCOS3101(t *testing.T) {
	fd := newFakeDevice(t)
	setUname(fd, "aarch64")
	os.WriteFile(filepath.Join(fd.dir, "Settings", "MPC", "MPC.settings"),
		[]byte("<PROPERTIES>\r\n  <VALUE name=\"Enhancements.VST2Support\" val=\"1\"/>\r\n</PROPERTIES>\r\n"), 0o644)
	os.WriteFile(filepath.Join(fd.dir, "model"), []byte("Akai Professional MPC Live III \x00"), 0o644)
	os.WriteFile(filepath.Join(fd.dir, "MPC"), []byte("\x7fELF\x00junk\x003.10.1.6 (69e2df86f00)\x00more"), 0o755)
	for _, f := range []string{"me - VST - A", "me - VST - B", "OpenMPC - VST - Plugin Manager G2"} {
		os.MkdirAll(filepath.Join(fd.dir, "Synths", f), 0o755)
		os.WriteFile(filepath.Join(fd.dir, "Synths", f, "plugin-meta.xml"), []byte(`<PLUGIN name="x" uid="1"/>`), 0o644)
	}
	v := filepath.Join(fd.dir, "VST2 Plugins")
	os.MkdirAll(filepath.Join(v, "me - VST - A"), 0o755) // A was moved already
	os.MkdirAll(filepath.Join(v, "OpenMPC - VST - Plugin Manager G2"), 0o755)
	os.WriteFile(filepath.Join(v, "OpenMPC - VST - Plugin Manager G2", "mpc-plugin.json"), []byte(`{"schema": 1, "id": "plugin-manager-g2", "version": "1.0.0"}`), 0o644)
	in := dialInfo(t, fd)
	if in.VST2 != "on" || in.Model != "MPC Live III" || in.MPCOS != "3.10.1" || in.Manager != "1.0.0" {
		t.Fatalf("detection: %+v", in)
	}
	if in.LegacyCount != 1 { // B: A and the Plugin Manager have a VST2 copy
		t.Fatalf("legacy count %d, want 1", in.LegacyCount)
	}
	if len(in.VST2Dirs) != 1 || in.VST2Dirs[0] != v {
		t.Fatalf("VST2 folders: %v", in.VST2Dirs)
	}
	os.WriteFile(filepath.Join(fd.dir, "Settings", "MPC", "MPC.settings"), []byte(`<VALUE name="Enhancements.VST2Support" val="0"/>`), 0o644)
	if in := dialInfo(t, fd); in.VST2 != "off" {
		t.Fatalf("VST2 Support off must read as off, got %q", in.VST2)
	}
}

func TestInstallOnMPCOS3101GoesToVST2PluginsWithPluginJSON(t *testing.T) {
	fd := newFakeDevice(t)
	os.WriteFile(filepath.Join(fd.dir, "Settings", "MPC", "MPC.settings"), []byte(`<VALUE name="Enhancements.VST2Support" val="1"/>`), 0o644)
	ents := pluginEntries("Acid-1.2.3", "", "acid", "me - VST - Acid", fakeInstaller("A", true, 0), nil)
	for i := range ents {
		if strings.HasSuffix(ents[i].name, "/plugin-meta.xml") {
			ents[i].body = acidMeta
		}
	}
	p, err := OpenPackage(writeZip(t, t.TempDir(), ents), "upload")
	if err != nil {
		t.Fatal(err)
	}
	if _, err := runJob(t, fd, Item{Pkg: p}); err != nil {
		t.Fatal(err)
	}
	dir := filepath.Join(fd.dir, "VST2 Plugins", "me - VST - Acid")
	if _, err := os.Stat(filepath.Join(dir, "acid.so")); err != nil {
		t.Fatal("the plugin must go to VST2 Plugins on the same drive:", err)
	}
	if _, err := os.Stat(filepath.Join(fd.dir, "Synths", "me - VST - Acid")); err == nil {
		t.Fatal("not into Synths: MPC OS 3.10.1 would not list it")
	}
	if pj := readPJ(t, filepath.Join(dir, "plugin.json")); pj["uid"] != "0x41634964" || pj["file32"] != "acid.so" {
		t.Fatalf("plugin.json: %v", pj)
	}
	if in := dialInfo(t, fd); !strings.Contains(strings.Join(in.Installed, "|"), "me - VST - Acid") || in.LegacyCount != 0 {
		t.Fatalf("a plugin in VST2 Plugins is installed and needs no move: %v legacy %d", in.Installed, in.LegacyCount)
	}
}

func TestInstallWithVST2SupportOffStaysInSynths(t *testing.T) {
	fd := newFakeDevice(t)
	os.WriteFile(filepath.Join(fd.dir, "Settings", "MPC", "MPC.settings"), []byte(`<VALUE name="Enhancements.VST2Support" val="0"/>`), 0o644)
	if _, err := runJob(t, fd, Item{Pkg: installerPkg(t, "A-1", "a-plug", "me - VST - A", fakeInstaller("A", true, 0))}); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(fd.dir, "Synths", "me - VST - A")); err != nil {
		t.Fatal(err)
	}
}
