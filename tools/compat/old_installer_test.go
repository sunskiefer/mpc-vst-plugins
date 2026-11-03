package main

// Copied next to a released desktop installer's sources (tools/compat/run.sh) and run with `go test -run TestCompatDump`:
// parses CATALOG with that release's own parseCatalog and ForArch, and prints what it would offer for each build.

import (
	"fmt"
	"os"
	"sort"
	"testing"
)

func TestCompatDump(t *testing.T) {
	data, err := os.ReadFile(os.Getenv("CATALOG"))
	if err != nil {
		t.Fatal(err)
	}
	ps, err := parseCatalog(data)
	if err != nil {
		fmt.Println("FAIL: parseCatalog:", err)
		t.Fatal(err)
	}
	var lines []string
	for _, p := range ps {
		for _, arch := range []string{"armv7", "aarch64"} {
			q, err := p.ForArch(arch)
			if err != nil {
				lines = append(lines, fmt.Sprintf("%s %s %s: none", p.ID, p.Version, arch))
				continue
			}
			lines = append(lines, fmt.Sprintf("%s %s %s: %s %s", q.ID, q.Version, arch, q.URL, q.SHA256))
		}
	}
	sort.Strings(lines)
	fmt.Printf("%d plugins\n", len(ps))
	for _, l := range lines {
		fmt.Println(l)
	}
}
