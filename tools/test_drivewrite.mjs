// Host test for tools/catalog_site/drivewrite.js (the Install page's browser drive writer): node tools/test_drivewrite.mjs
// Zips are made by Python's zipfile; the drive is an in-memory stand-in for a FileSystemDirectoryHandle.
import { createRequire } from "module"; import { execFileSync } from "child_process"; import assert from "assert";
const DW = createRequire(import.meta.url)("./catalog_site/drivewrite.js");
function zip(files, method = "ZIP_DEFLATED") {
  const py = `import io,zipfile,sys,json\nb=io.BytesIO()\nz=zipfile.ZipFile(b,"w",zipfile.${method})\nfor n,d in json.loads(sys.argv[1]).items():\n  z.writestr(n,d)\nz.close()\nsys.stdout.buffer.write(b.getvalue())`;
  const out = execFileSync("python3", ["-c", py, JSON.stringify(files)]);
  return { arrayBuffer: async () => out.buffer.slice(out.byteOffset, out.byteOffset + out.length) };
}
class Dir { constructor(name) { this.name = name; this.k = new Map(); }
  async getDirectoryHandle(n, o) { if (!this.k.has(n)) { if (!o?.create) throw new Error("nf"); this.k.set(n, new Dir(n)); } return this.k.get(n); }
  async getFileHandle(n) { const d = this; return { createWritable: async () => { let b; return { write: async x => { b = Buffer.from(x); }, close: async () => d.k.set(n, b) }; } }; }
  async removeEntry(n) { if (!this.k.delete(n)) throw new Error("nf"); }
  values() { const it = [...this.k.keys()].map(name => ({ name }))[Symbol.iterator](); return { next: async () => it.next() }; }
}
const F = "OpenMPC - VST - Plugin Manager";
let t = 0; async function test(n, fn) { await fn(); t++; console.log("ok", n); }
await test("folder zip goes into VST2 Plugins, replacing an old copy", async () => {
  const d = new Dir("STICK"); (await (await d.getDirectoryHandle("VST2 Plugins", { create: true })).getDirectoryHandle(F, { create: true })).k.set("old.txt", Buffer.from("x"));
  const big = "a".repeat(100000);
  assert.equal(await DW.folderZip(zip({ [F + "/plugin.json"]: "{}", [F + "/x.so"]: big, [F + "/skin/TUI.json"]: "[]" }), d), F);
  const f = d.k.get("VST2 Plugins").k.get(F);
  assert.equal(f.k.get("x.so").toString(), big); assert.equal(f.k.get("skin").k.get("TUI.json").toString(), "[]"); assert(!f.k.has("old.txt"));
});
await test("stored (not deflated) entries", async () => { const d = new Dir("S"); await DW.folderZip(zip({ [F + "/plugin.json"]: "{}" }, "ZIP_STORED"), d); });
for (const [n, files, re] of [["two folders", { "a/plugin.json": "", "b/x": "" }, /one plugin folder/], ["no plugin.json", { [F + "/x.so"]: "" }, /no plugin.json/],
                               ["path escape", { [F + "/plugin.json"]: "", [F + "/../../evil"]: "" }, /unsafe/], ["absolute", { "/etc/x": "" }, /unsafe/]])
  await test("refuses " + n, async () => { await assert.rejects(DW.folderZip(zip(files), new Dir("S")), re); });
await test("not a zip", async () => { await assert.rejects(DW.folderZip({ arrayBuffer: async () => new ArrayBuffer(100) }, new Dir("S")), /not a zip/); });
const stick = zip({ "autorun/autorun.json": "{}", "autorun/open.sh": "#!/bin/sh", ".mpc-manager-id": "x" });
const pin = { sha256: await DW.sha256(await stick.arrayBuffer()), need: ["autorun/autorun.json", ".mpc-manager-id"] };
await test("stick zip onto an empty drive (housekeeping ignored)", async () => {
  const d = new Dir("S"); d.k.set("System Volume Information", new Dir("x")); d.k.set(".Trashes", new Dir("x"));
  assert.equal(await DW.stickZip(stick, d, pin), 3); assert.equal(d.k.get("autorun").k.get("open.sh").toString(), "#!/bin/sh");
});
await test("stick refuses a drive with files", async () => { const d = new Dir("S"); d.k.set("Samples", new Dir("x")); await assert.rejects(DW.stickZip(stick, d, pin), /isn.t empty \(it has Samples\)/); assert.equal(d.k.size, 1); });
await test("stick refuses a different zip", async () => { await assert.rejects(DW.stickZip(stick, new Dir("S"), { ...pin, sha256: "0".repeat(64) }), /checksum/); });
await test("stick refuses a zip missing a needed file", async () => { await assert.rejects(DW.stickZip(stick, new Dir("S"), { ...pin, need: ["autorun/autorun.check"] }), /missing/); });
console.log(t, "passed");
await test("folder zip with a pinned checksum", async () => {
  const z = zip({ [F + "/plugin.json"]: "{}" }), sha = await DW.sha256(await z.arrayBuffer());
  await DW.folderZip(z, new Dir("S"), null, sha);
  await assert.rejects(DW.folderZip(z, new Dir("S"), null, "0".repeat(64)), /checksum/);
});
console.log(t, "passed");
