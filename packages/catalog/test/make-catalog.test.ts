// SPDX-License-Identifier: GPL-3.0-only
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { cpSync, existsSync, mkdtempSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { makePackage } from "@knobi/device-sim";
import { makeCatalog } from "../src/make-catalog.ts";
import { validateCatalog } from "../src/load.ts";
import { repoRootFrom } from "../src/node.ts";

const ROOT = repoRootFrom(import.meta.dirname);
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");

/** a throwaway repo with the real instruments.json and synths/knobi-six */
function tempRepo(): string {
  const d = mkdtempSync(join(tmpdir(), "knobi-repo-"));
  writeFileSync(join(d, "pnpm-workspace.yaml"), "packages: []\n");
  mkdirSync(join(d, "packages/catalog"), { recursive: true });
  cpSync(join(ROOT, "packages/catalog/instruments.json"), join(d, "packages/catalog/instruments.json"));
  cpSync(join(ROOT, "synths/knobi-six"), join(d, "synths/knobi-six"), { recursive: true });
  return d;
}

type Files = Record<string, Uint8Array>;
function release(extra: Files = {}, sums?: (s: Record<string, string>) => void) {
  const dir = mkdtempSync(join(tmpdir(), "knobi-flash-"));
  const files: Files = {
    "knobi-six-1.0.fwsc": makePackage({ product: "SMK-37 Pro_956" }),
    "knobi-six-1.0-elite.fwsc": makePackage({ product: "SMK-37 Elite_956" }),
    "knobi-six-1.0-mke.fwsc": makePackage({ product: "MKE-P37_956" }),
    ...extra,
  };
  const s: Record<string, string> = {};
  for (const [n, b] of Object.entries(files)) {
    writeFileSync(join(dir, n), b);
    s[n] = sha(b);
  }
  sums?.(s);
  writeFileSync(join(dir, "SHA256SUMS"), Object.entries(s).map(([n, h]) => `${h}  ${n}\n`).join(""));
  return dir;
}

const opts = (dir: string, version = "1.0", repoRoot = tempRepo()) => ({
  repoRoot, synth: "knobi-six", version, dir, date: "2026-11-20", filesPrefix: `firmware/knobi-six/${version}`,
  source: `firmware/knobi-six/${version}/knobi-six-${version}-src.tar.gz`,
});

/** a second synth in the repo, with one committed release */
function addOther(repo: string, pkgBytes: Uint8Array, hash = sha(pkgBytes)) {
  const d = join(repo, "synths/other");
  mkdirSync(join(d, "releases/2.0"), { recursive: true });
  writeFileSync(join(d, "releases/2.0/other-2.0.fwsc"), pkgBytes);
  writeFileSync(join(d, "synth.json"), JSON.stringify({ id: "other", name: "Other", ident: "957",
    packageName: "other-{version}{suffix}.fwsc", models: { "": "SMK-37 Pro" }, platforms: {} }));
  const pkg = { instrument: "smk37-pro", file: "firmware/other/2.0/other-2.0.fwsc", identity: "SMK-37 Pro_957",
    sha256: hash, size: pkgBytes.length };
  writeFileSync(join(d, "catalog.json"), JSON.stringify({ id: "other", name: "Other", summary: "s",
    license: "GPL-3.0-only", notes: [], releases: [{ version: "2.0", date: "2026-11-01", packages: [pkg] }] }));
  return pkg;
}

describe("makeCatalog", () => {
  it("lists the three release packages with hash, size, instrument and source", async () => {
    const c = validateCatalog(await makeCatalog(opts(release())));
    const r = c.firmwares.find((f) => f.id === "knobi-six")!.releases;
    expect(r).toHaveLength(1);
    expect(r[0].version).toBe("1.0");
    expect(r[0].source).toBe("firmware/knobi-six/1.0/knobi-six-1.0-src.tar.gz");
    const byInst = Object.fromEntries(r[0].packages.map((p) => [p.instrument, p]));
    expect(byInst["smk37-pro"].file).toBe("firmware/knobi-six/1.0/knobi-six-1.0.fwsc");
    expect(byInst["smk37-pro"].identity).toBe("SMK-37 Pro_956");
    expect(byInst["smk37-elite"].identity).toBe("SMK-37 Elite_956");
    expect(byInst["mke-p37"].sha256).toBe(sha(makePackage({ product: "MKE-P37_956" })));
    expect(byInst["mke-p37"].size).toBe(makePackage({ product: "MKE-P37_956" }).length);
  });

  const refusals: [string, () => ReturnType<typeof opts>, RegExp][] = [
    ["a -local package", () => opts(release({ "knobi-six-local.fwsc": makePackage({ product: "SMK-37 Pro_956" }) })), /knobi-six-local\.fwsc/],
    ["a cpu1 probe package", () => opts(release({ "knobi-six-cpu1probe.fwsc": makePackage({ product: "SMK-37 Pro_956" }) })), /cpu1probe/],
    ["an unexpected name", () => opts(release({ "knobi-six-2.0.fwsc": makePackage({ product: "SMK-37 Pro_956" }) })), /knobi-six-2\.0\.fwsc/],
    ["a wrong hash", () => opts(release({}, (s) => { s["knobi-six-1.0.fwsc"] = "0".repeat(64); })), /SHA256SUMS/],
    ["a missing hash", () => opts(release({}, (s) => { delete s["knobi-six-1.0-mke.fwsc"]; })), /SHA256SUMS/],
    ["another identity", () => opts(release({ "knobi-six-1.0.fwsc": makePackage({ product: "SMK-37 Pro_900" }) })), /SMK-37 Pro_900/],
    ["a damaged package", () => opts(release({ "knobi-six-1.0.fwsc": makePackage({ product: "SMK-37 Pro_956", damage: true }) })), /damaged/],
    ["a model with no instrument", () => opts(release({ "knobi-six-1.0.fwsc": makePackage({ product: "FM-1_956" }) })), /no instrument/],
    ["a prerelease version", () => opts(release(), "1.0-rc1"), /prerelease/],
    ["an unknown synth", () => ({ ...opts(release()), synth: "nope" }), /synths\/nope\/synth\.json/],
  ];
  it.each(refusals)("refuses %s", async (_n, o, msg) => {
    await expect(makeCatalog(o())).rejects.toThrow(msg);
  });

  it("keeps another synth's entry and its committed release when the file matches", async () => {
    const repo = tempRepo();
    const pkg = addOther(repo, makePackage({ product: "SMK-37 Pro_957" }));
    const c = await makeCatalog(opts(release(), "1.0", repo));
    expect(c.firmwares.map((f) => f.id).sort()).toEqual(["knobi-six", "other"]);
    expect(c.firmwares.find((f) => f.id === "other")!.releases[0].packages[0]).toEqual(pkg);
  });

  it("refuses another synth's committed release that does not match its sha256", async () => {
    const repo = tempRepo();
    addOther(repo, makePackage({ product: "SMK-37 Pro_957" }), "1".repeat(64));
    await expect(makeCatalog(opts(release(), "1.0", repo))).rejects.toThrow(/other-2\.0\.fwsc/);
  });

  it("runs as a command under plain node and writes the site: catalog, packages, the other synth's files", () => {
    const repo = tempRepo();
    addOther(repo, makePackage({ product: "SMK-37 Pro_957" }));
    const rel = release();
    const site = mkdtempSync(join(tmpdir(), "site-"));
    const out = execFileSync("node", ["src/make-catalog.ts", "--synth", "knobi-six", "--version", "1.0", "--dir", rel,
      "--out", join(site, "catalog.json"), "--files", join(site, "firmware/knobi-six/1.0"), "--repo-root", repo],
      { cwd: join(import.meta.dirname, ".."), encoding: "utf8" });
    expect(out).toContain("knobi-six 1.0, 3 packages");
    expect(existsSync(join(site, "firmware/knobi-six/1.0/knobi-six-1.0-mke.fwsc"))).toBe(true);
    expect(existsSync(join(site, "firmware/knobi-six/1.0/SHA256SUMS"))).toBe(true);
    expect(existsSync(join(site, "firmware/other/2.0/other-2.0.fwsc"))).toBe(true);
    expect(JSON.parse(readFileSync(join(site, "catalog.json"), "utf8")).firmwares).toHaveLength(2);
  });

  it("prints its usage", () => {
    const out = execFileSync("node", ["src/make-catalog.ts", "--help"], { cwd: join(import.meta.dirname, ".."), encoding: "utf8" });
    expect(out).toContain("--synth");
  });
});
