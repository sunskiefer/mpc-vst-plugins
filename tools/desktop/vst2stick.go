package main

// A plugin stick for MPC OS 3.10.1 and later: the MPC lists plugins from "<drive>/VST2 Plugins/<Manufacturer> - VST - <Name>/" when
// the folder has a plugin.json (Akai's plugin folder format) and VST2 Support is on in Preferences > Enhancements. No root, no SSH,
// no MPC.settings: the user copies plugins onto a USB drive or SD card here, plugs it in and restarts MPC (a rescan finds new folders,
// the plugin browser lists them after a restart; Live III, 2026-10-31).
//
// Each release zip (tools/release.py) holds the plugin as one folder, portable/<skin>/, which works there as it is: newer releases
// carry plugin.json, older ones get one written from the folder's plugin-meta.xml (the .so stays at the folder root, so engines still
// find their data next to it). A plugin already on the stick is replaced, keeping its user_data folders (the user's ROMs).

import (
	"archive/zip"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
)

const vst2Dir = "VST2 Plugins"

var metaAttrRe = regexp.MustCompile(`(\w+)="([^"]*)"`)

type vst2JSON struct {
	Name         string `json:"name"`
	Manufacturer string `json:"manufacturer"`
	Version      string `json:"version,omitempty"`
	Kind         string `json:"kind"`
	UID          string `json:"uid"`
	Category     string `json:"category,omitempty"`
	NumInputs    int    `json:"numInputs"`
	NumOutputs   int    `json:"numOutputs"`
	File32       string `json:"file32,omitempty"`
	File64       string `json:"file64,omitempty"`
}

// pluginJSONFromMeta builds plugin.json from a plugin-meta.xml (one <PLUGIN .../> as release.py writes it) for arch armv7 or aarch64.
func pluginJSONFromMeta(meta []byte, arch, version string) ([]byte, string, error) {
	at := map[string]string{}
	for _, m := range metaAttrRe.FindAllStringSubmatch(string(meta), -1) {
		if _, ok := at[m[1]]; !ok {
			at[m[1]] = m[2]
		}
	}
	name, mfr, uid := at["name"], at["manufacturer"], at["uid"]
	if name == "" || mfr == "" {
		return nil, "", errors.New("plugin-meta.xml has no name or manufacturer")
	}
	n, err := strconv.ParseUint(uid, 16, 32)
	if err != nil {
		return nil, "", fmt.Errorf("plugin-meta.xml uid %q is not hex", uid)
	}
	so := path.Base(at["file"])
	if !strings.HasSuffix(so, ".so") {
		return nil, "", fmt.Errorf("plugin-meta.xml file= %q is not a .so", at["file"])
	}
	j := vst2JSON{Name: name, Manufacturer: mfr, Version: version, Kind: "instrument", UID: fmt.Sprintf("0x%08x", n), Category: at["category"]}
	if at["isInstrument"] == "0" {
		j.Kind = "effect"
	}
	j.NumInputs, _ = strconv.Atoi(at["numInputs"])
	if j.NumOutputs, err = strconv.Atoi(at["numOutputs"]); err != nil {
		j.NumOutputs = 2
	}
	if j.NumOutputs <= 0 {
		return nil, "", errors.New("the plugin has no audio outputs, so MPC would not list it")
	}
	switch arch {
	case "aarch64":
		j.File64 = so
	case "armv7":
		j.File32 = so
	default:
		return nil, "", fmt.Errorf("unknown architecture %q", arch)
	}
	b, _ := json.MarshalIndent(j, "", "  ")
	return append(b, '\n'), mfr + " - VST - " + name, nil
}

// WriteVST2Folders puts the plugin folder of each release zip into <out>/VST2 Plugins/. arch is the target MPC's (armv7 for Gen1,
// aarch64 for Gen2); a zip built for the other one is refused. It returns the folders written.
func WriteVST2Folders(out string, zips []string, arch string, log func(string, ...any)) ([]string, error) {
	if fi, err := os.Stat(out); err != nil || !fi.IsDir() {
		return nil, fmt.Errorf("%s is not a folder or drive", out)
	}
	dest := filepath.Join(out, vst2Dir)
	if err := os.MkdirAll(dest, 0o755); err != nil {
		return nil, err
	}
	var done []string
	for _, zp := range zips {
		pkg, err := OpenPackage(zp, "catalog") // the same checks as an install: names, size, manifest
		if err != nil {
			return done, fmt.Errorf("%s: %v", filepath.Base(zp), err)
		}
		if pkg.Addin {
			log("%s is an addin, not a plugin: skipped (addins are installed with the Plugin Manager)", pkg.Title)
			continue
		}
		if pkg.Symlinks > 0 {
			return done, fmt.Errorf("%s needs symbolic links, which a USB stick or SD card cannot hold: install it with the Plugin Manager instead", pkg.Title)
		}
		for _, m := range pkg.Plugins {
			if m.Arch != "" && m.Arch != arch && (m.Arch == "armv7" || m.Arch == "aarch64") {
				return done, fmt.Errorf("%s is the %s build, and this stick is for %s", pkg.Title, m.Arch, genName(arch))
			}
		}
		folders, err := writeVST2FromZip(zp, dest, arch, pkg, log)
		if err != nil {
			return done, fmt.Errorf("%s: %v", pkg.Title, err)
		}
		done = append(done, folders...)
	}
	return done, nil
}

func writeVST2FromZip(zp, dest, arch string, pkg *Package, log func(string, ...any)) ([]string, error) {
	z, err := zip.OpenReader(zp)
	if err != nil {
		return nil, err
	}
	defer z.Close()
	// every ".../portable/<skin>/" that holds a plugin-meta.xml: one folder for a release, one per plugin in a bundle
	roots := map[string]string{} // prefix in the zip -> skin folder name
	for _, f := range z.File {
		n, err := cleanName(f.Name)
		if err != nil {
			return nil, err
		}
		if path.Base(n) == "plugin-meta.xml" && path.Base(path.Dir(path.Dir(n))) == "portable" {
			roots[path.Dir(n)+"/"] = path.Base(path.Dir(n))
		}
	}
	if len(roots) == 0 {
		return nil, errors.New("no plugin folder (portable/<name>/) in this release: it is an older layout, install it with the Plugin Manager")
	}
	var written []string
	for prefix, skin := range roots {
		if !skinRe.MatchString(skin) {
			return nil, fmt.Errorf("bad folder name %q", skin)
		}
		var meta []byte
		hasJSON := false
		for _, f := range z.File {
			if f.Name == prefix+"plugin-meta.xml" {
				if meta, err = readZipFile(f, 1<<16); err != nil {
					return nil, err
				}
			}
			hasJSON = hasJSON || f.Name == prefix+"plugin.json"
		}
		pj, want, err := pluginJSONFromMeta(meta, arch, pkg.Version)
		if err != nil {
			return nil, err
		}
		if want != skin {
			return nil, fmt.Errorf("folder %q is not \"<manufacturer> - VST - <name>\" (%q), so MPC would not match it", skin, want)
		}
		final := filepath.Join(dest, skin)
		stage, old := final+".new", final+".old"
		os.RemoveAll(stage)
		os.RemoveAll(old)
		for _, f := range z.File {
			if !strings.HasPrefix(f.Name, prefix) || f.Name == prefix {
				continue
			}
			rel, err := cleanName(strings.TrimPrefix(f.Name, prefix))
			if err != nil {
				return nil, err
			}
			p := filepath.Join(stage, filepath.FromSlash(rel))
			if f.FileInfo().IsDir() {
				if err := os.MkdirAll(p, 0o755); err != nil {
					return nil, err
				}
				continue
			}
			if err := extractFile(f, p); err != nil {
				os.RemoveAll(stage)
				return nil, err
			}
		}
		if !hasJSON {
			if err := os.WriteFile(filepath.Join(stage, "plugin.json"), pj, 0o644); err != nil {
				os.RemoveAll(stage)
				return nil, err
			}
		}
		if _, err := os.Stat(final); err == nil { // an update: keep the user's own files (ROMs, carts) from the copy on the stick
			for _, m := range pkg.Plugins {
				if m.Skin != skin {
					continue
				}
				for _, u := range m.UserData {
					if u == "" || strings.Contains(u, "..") {
						continue
					}
					src := filepath.Join(final, filepath.FromSlash(u))
					if _, err := os.Stat(src); err == nil {
						os.RemoveAll(filepath.Join(stage, filepath.FromSlash(u)))
						if err := os.MkdirAll(filepath.Dir(filepath.Join(stage, filepath.FromSlash(u))), 0o755); err != nil {
							return nil, err
						}
						if err := os.Rename(src, filepath.Join(stage, filepath.FromSlash(u))); err != nil {
							return nil, fmt.Errorf("could not keep your files in %s: %v", u, err)
						}
						log("kept your files in %s/%s", skin, u)
					}
				}
			}
			if err := os.Rename(final, old); err != nil {
				return nil, err
			}
		}
		if err := os.Rename(stage, final); err != nil {
			os.Rename(old, final)
			return nil, err
		}
		os.RemoveAll(old)
		log("%s/%s", vst2Dir, skin)
		written = append(written, skin)
	}
	return written, nil
}

func readZipFile(f *zip.File, max int64) ([]byte, error) {
	rc, err := f.Open()
	if err != nil {
		return nil, err
	}
	defer rc.Close()
	return io.ReadAll(io.LimitReader(rc, max))
}

func extractFile(f *zip.File, p string) error {
	if f.Mode()&os.ModeSymlink != 0 {
		return errors.New("symbolic link in the plugin folder")
	}
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		return err
	}
	rc, err := f.Open()
	if err != nil {
		return err
	}
	defer rc.Close()
	w, err := os.OpenFile(p, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0o644)
	if err != nil {
		return err
	}
	if _, err := io.Copy(w, io.LimitReader(rc, maxUncompressed)); err != nil {
		w.Close()
		return err
	}
	return w.Close()
}

// POST /api/vst2stick {out, arch, ids}: download the chosen catalog plugins for arch and write them to <out>/VST2 Plugins/ (a job).
func (a *App) vst2Stick(w http.ResponseWriter, r *http.Request) {
	if !postOnly(w, r) {
		return
	}
	var in struct {
		Out  string   `json:"out"`
		Arch string   `json:"arch"`
		IDs  []string `json:"ids"`
	}
	if err := json.NewDecoder(io.LimitReader(r.Body, 1<<16)).Decode(&in); err != nil || in.Out == "" || len(in.IDs) == 0 {
		fail(w, 400, "choose a drive and at least one plugin")
		return
	}
	if in.Arch != "armv7" && in.Arch != "aarch64" {
		fail(w, 400, "choose the MPC: Gen1 (32-bit) or Gen2 (64-bit)")
		return
	}
	out := filepath.Clean(in.Out)
	a.mu.Lock()
	if a.stickJob != nil {
		if st, _, _, _ := a.stickJob.snapshot(0); st == "running" {
			a.mu.Unlock()
			fail(w, 409, "a stick is already being written")
			return
		}
	}
	cat := a.cat
	a.mu.Unlock()
	if cat == nil {
		c, err := FetchCatalog(a.catalogURL)
		if err != nil {
			fail(w, 502, "cannot read the catalog: "+err.Error())
			return
		}
		cat = c
	}
	var picks []CatPlugin
	for _, id := range in.IDs {
		found := false
		for _, c := range cat {
			if c.ID == id {
				cp, err := c.ForArch(in.Arch)
				if err != nil {
					fail(w, 400, err.Error())
					return
				}
				picks, found = append(picks, cp), true
			}
		}
		if !found {
			fail(w, 400, "not in the catalog: "+id)
			return
		}
	}
	j := &Job{ID: randHex(4), State: "running"}
	a.mu.Lock()
	a.stickJob = j
	a.mu.Unlock()
	go func() {
		tmp, err := tempWorkDir()
		if err != nil {
			j.finish(err)
			return
		}
		defer os.RemoveAll(tmp)
		var zips []string
		for i, c := range picks {
			j.log("downloading %s %s (%d of %d)", c.Name, c.Version, i+1, len(picks))
			zp := filepath.Join(tmp, fmt.Sprintf("%d.zip", i))
			if err := Download(c, zp, nil); err != nil {
				j.finish(fmt.Errorf("%s: %v", c.Name, err))
				return
			}
			zips = append(zips, zp)
		}
		folders, err := WriteVST2Folders(out, zips, in.Arch, j.log)
		if err != nil {
			j.finish(err)
			return
		}
		j.finishWithResult(fmt.Sprintf("%d plugin folder(s) in %s", len(folders), filepath.Join(out, vst2Dir)))
	}()
	writeJSON(w, 200, map[string]string{"job": j.ID})
}
