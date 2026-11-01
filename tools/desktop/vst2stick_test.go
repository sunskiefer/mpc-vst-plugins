package main

import (
	"encoding/json"
	"io/fs"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

const acidMeta = `<PLUGIN name="Acid" descriptiveName="Acid" format="VST" category="Synth" manufacturer="me" version="1.0" ` +
	`file="%payload-path%/me - VST - Acid/acid.so" uid="41634964" isInstrument="1" fileTime="0" infoUpdateTime="0" numInputs="0" numOutputs="2" isShell="0"/>`

// a release zip whose plugin folder has a real plugin-meta.xml (pluginEntries' is a stub)
func stickPkg(t *testing.T, mod func(*Manifest), extra ...zent) string {
	t.Helper()
	ents := pluginEntries("Acid-1.2.3", "", "acid", "me - VST - Acid", newInstall, mod)
	for i := range ents {
		if strings.HasSuffix(ents[i].name, "/plugin-meta.xml") {
			ents[i].body = acidMeta
		}
	}
	return writeZip(t, t.TempDir(), append(ents, extra...))
}

func readPJ(t *testing.T, p string) map[string]any {
	t.Helper()
	b, err := os.ReadFile(p)
	if err != nil {
		t.Fatal(err)
	}
	var m map[string]any
	if err := json.Unmarshal(b, &m); err != nil {
		t.Fatal(err)
	}
	return m
}

func quiet(string, ...any) {}

func TestVST2StickWritesFolderAndPluginJSON(t *testing.T) {
	out := t.TempDir()
	got, err := WriteVST2Folders(out, []string{stickPkg(t, nil)}, "armv7", quiet)
	if err != nil || len(got) != 1 || got[0] != "me - VST - Acid" {
		t.Fatalf("got %v, %v", got, err)
	}
	dir := filepath.Join(out, "VST2 Plugins", "me - VST - Acid")
	if _, err := os.Stat(filepath.Join(dir, "acid.so")); err != nil {
		t.Fatal("the .so must stay at the folder root:", err)
	}
	pj := readPJ(t, filepath.Join(dir, "plugin.json"))
	if pj["name"] != "Acid" || pj["manufacturer"] != "me" || pj["uid"] != "0x41634964" || pj["file32"] != "acid.so" || pj["kind"] != "instrument" || pj["numOutputs"] != 2.0 {
		t.Fatalf("plugin.json: %v", pj)
	}
	if _, ok := pj["file64"]; ok {
		t.Fatal("a Gen1 stick must not name a 64-bit file")
	}
	if _, err := os.Stat(dir + ".new"); err == nil {
		t.Fatal("the staging folder was left behind")
	}
}

func TestVST2StickKeepsTheReleasesOwnPluginJSON(t *testing.T) {
	own := `{"name":"Acid","manufacturer":"me","uid":"AcId","file32":"acid.so","numOutputs":2}`
	out := t.TempDir()
	if _, err := WriteVST2Folders(out, []string{stickPkg(t, nil, zent{"Acid-1.2.3/portable/me - VST - Acid/plugin.json", 0, own})}, "armv7", quiet); err != nil {
		t.Fatal(err)
	}
	b, _ := os.ReadFile(filepath.Join(out, "VST2 Plugins", "me - VST - Acid", "plugin.json"))
	if string(b) != own {
		t.Fatalf("a release's plugin.json must be copied as it is, got %s", b)
	}
}

func TestVST2StickRefusesTheOtherArchitecture(t *testing.T) {
	_, err := WriteVST2Folders(t.TempDir(), []string{stickPkg(t, nil)}, "aarch64", quiet)
	if err == nil || !strings.Contains(err.Error(), "armv7 build") {
		t.Fatalf("a Gen1 build on a Gen2 stick must be refused: %v", err)
	}
}

func TestVST2StickUpdateKeepsUserData(t *testing.T) {
	out := t.TempDir()
	userData := func(m *Manifest) { m.UserData = []string{"roms"} }
	dir := filepath.Join(out, "VST2 Plugins", "me - VST - Acid")
	if err := os.MkdirAll(filepath.Join(dir, "roms"), 0o755); err != nil {
		t.Fatal(err)
	}
	os.WriteFile(filepath.Join(dir, "roms", "mine.bin"), []byte("rom"), 0o644)
	os.WriteFile(filepath.Join(dir, "stale.txt"), []byte("old version"), 0o644)
	if _, err := WriteVST2Folders(out, []string{stickPkg(t, userData)}, "armv7", quiet); err != nil {
		t.Fatal(err)
	}
	if b, err := os.ReadFile(filepath.Join(dir, "roms", "mine.bin")); err != nil || string(b) != "rom" {
		t.Fatalf("the user's ROM must survive an update: %v", err)
	}
	if _, err := os.Stat(filepath.Join(dir, "stale.txt")); err == nil {
		t.Fatal("the old version's files must be replaced")
	}
	if _, err := os.Stat(dir + ".old"); err == nil {
		t.Fatal("the old copy was left behind")
	}
}

func TestVST2StickRefusesAFolderNameMPCWouldNotMatch(t *testing.T) {
	ents := pluginEntries("Acid-1.2.3", "", "acid", "me - VST - Acid", newInstall, nil)
	for i := range ents {
		if strings.HasSuffix(ents[i].name, "/plugin-meta.xml") {
			ents[i].body = strings.Replace(acidMeta, `name="Acid"`, `name="Acid 2"`, 1)
		}
	}
	_, err := WriteVST2Folders(t.TempDir(), []string{writeZip(t, t.TempDir(), ents)}, "armv7", quiet)
	if err == nil || !strings.Contains(err.Error(), "would not match") {
		t.Fatalf("got %v", err)
	}
}

func TestVST2StickRefusesSymlinks(t *testing.T) {
	z := stickPkg(t, nil, zent{"Acid-1.2.3/portable/me - VST - Acid/bin/link", fs.ModeSymlink | 0o777, "acid.so"})
	_, err := WriteVST2Folders(t.TempDir(), []string{z}, "armv7", quiet)
	if err == nil || !strings.Contains(err.Error(), "symbolic links") {
		t.Fatalf("got %v", err)
	}
}

func TestVST2StickBundleWritesEveryPlugin(t *testing.T) {
	var ents []zent
	ents = append(ents, zent{"Mono-1.0/install.sh", 0o755, newInstall}, zent{"Mono-1.0/uninstall.sh", 0o755, "#!/bin/sh\n"})
	for _, p := range []struct{ dir, id, skin, name string }{{"one", "one", "me - VST - One", "One"}, {"fx", "fx", "me - VST - FX", "FX"}} {
		e := pluginEntries("Mono-1.0", p.dir, p.id, p.skin, newInstall, nil)
		for i := range e {
			if strings.HasSuffix(e[i].name, "/plugin-meta.xml") {
				e[i].body = `<PLUGIN name="` + p.name + `" manufacturer="me" file="%payload-path%/` + p.skin + `/` + p.id + `.so" uid="4d6e6d4f" isInstrument="1" numInputs="0" numOutputs="2"/>`
			}
		}
		ents = append(ents, e...)
	}
	out := t.TempDir()
	got, err := WriteVST2Folders(out, []string{writeZip(t, t.TempDir(), ents)}, "armv7", quiet)
	if err != nil || len(got) != 2 {
		t.Fatalf("a bundle's plugins must all be written: %v %v", got, err)
	}
	for _, s := range []string{"me - VST - One", "me - VST - FX"} {
		if _, err := os.Stat(filepath.Join(out, "VST2 Plugins", s, "plugin.json")); err != nil {
			t.Fatal(err)
		}
	}
}

func TestPluginJSONFromMetaEffect(t *testing.T) {
	meta := strings.NewReplacer(`isInstrument="1"`, `isInstrument="0"`, `numInputs="0"`, `numInputs="2"`).Replace(acidMeta)
	b, skin, err := pluginJSONFromMeta([]byte(meta), "aarch64", "2.0.0")
	if err != nil || skin != "me - VST - Acid" {
		t.Fatal(skin, err)
	}
	var pj map[string]any
	json.Unmarshal(b, &pj)
	if pj["kind"] != "effect" || pj["numInputs"] != 2.0 || pj["file64"] != "acid.so" || pj["version"] != "2.0.0" {
		t.Fatalf("%v", pj)
	}
	if _, _, err := pluginJSONFromMeta([]byte(strings.Replace(acidMeta, `numOutputs="2"`, `numOutputs="0"`, 1)), "armv7", ""); err == nil {
		t.Fatal("a plugin with no outputs must be refused: MPC would not list it")
	}
}

func TestStickUserFiles(t *testing.T) {
	g := UserFileGroup{Path: "roms", Label: "ROMs", Rules: []UserFileRule{{Label: "Test ROM", Ext: []string{".bin"}, Size: 4}}}
	plugin := filepath.Join(t.TempDir(), "VST2 Plugins", "me - VST - Acid")
	if _, err := writeStickUserFiles(plugin, g, []stickFile{{"a.bin", []byte("abcd")}}); err == nil {
		t.Fatal("a plugin that is not on the stick yet must be refused")
	}
	os.MkdirAll(plugin, 0o755)
	vs, err := writeStickUserFiles(plugin, g, []stickFile{{"good.bin", []byte("abcd")}, {"wrong.txt", []byte("abcd")}, {"../evil.bin", []byte("abcd")}, {"short.bin", []byte("ab")}})
	if err != nil {
		t.Fatal(err)
	}
	st := map[string]string{}
	for _, v := range vs {
		st[v.Name] = v.Status
	}
	if st["good.bin"] != "ok" || st["wrong.txt"] == "ok" || st["../evil.bin"] != "bad" || st["short.bin"] == "ok" {
		t.Fatalf("verdicts: %v", vs)
	}
	if b, err := os.ReadFile(filepath.Join(plugin, "roms", "good.bin")); err != nil || string(b) != "abcd" {
		t.Fatal("a good file must be written into the group's folder:", err)
	}
	ents, _ := os.ReadDir(filepath.Join(plugin, "roms"))
	if len(ents) != 1 {
		t.Fatalf("only the good file may be written (no .part left behind): %v", ents)
	}
	if _, err := os.Stat(filepath.Join(filepath.Dir(plugin), "evil.bin")); err == nil {
		t.Fatal("a name with .. escaped the folder")
	}
}

func TestStickUserFilesSaveAs(t *testing.T) {
	g := UserFileGroup{Path: "roms", Label: "ROMs", Rules: []UserFileRule{{Label: "Test ROM", Ext: []string{".bin"}, Size: 4, SaveAs: "test.bin"}}}
	plugin := filepath.Join(t.TempDir(), "VST2 Plugins", "me - VST - Acid")
	os.MkdirAll(plugin, 0o755)
	vs, err := writeStickUserFiles(plugin, g, []stickFile{{"My Copy.bin", []byte("abcd")}})
	if err != nil || len(vs) != 1 || vs[0].Status != "ok" {
		t.Fatalf("%v %v", vs, err)
	}
	if b, err := os.ReadFile(filepath.Join(plugin, "roms", "test.bin")); err != nil || string(b) != "abcd" {
		t.Fatal("an upload must be saved under the rule's save_as:", err)
	}
	if _, err := os.Stat(filepath.Join(plugin, "roms", "My Copy.bin")); err == nil {
		t.Fatal("the computer's file name must not be kept")
	}
}
