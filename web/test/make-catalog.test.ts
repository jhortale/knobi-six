// SPDX-License-Identifier: GPL-3.0-only
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { makeCatalog } from "../scripts/make-catalog.ts";
import { validateCatalog } from "../src/catalog/load.ts";
import { makePackage } from "@knobi/device-sim";

const base = JSON.parse(readFileSync(new URL("../catalog/base.json", import.meta.url), "utf8"));
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");

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

const opts = (dir: string, version = "1.0") => ({
  base, version, dir, date: "2026-11-20", filesPrefix: `firmware/knobi-six/${version}`,
  source: `firmware/knobi-six/${version}/knobi-six-${version}-src.tar.gz`, publicDir: mkdtempSync(join(tmpdir(), "pub-")),
});

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
  ];
  it.each(refusals)("refuses %s", async (_n, o, msg) => {
    await expect(makeCatalog(o())).rejects.toThrow(msg);
  });

  it("keeps another firmware's complete entry when its committed file matches", async () => {
    const o = opts(release());
    const other = makePackage({ product: "SMK-37 Pro_957" });
    mkdirSync(join(o.publicDir, "firmware/other/2.0"), { recursive: true });
    writeFileSync(join(o.publicDir, "firmware/other/2.0/other.fwsc"), other);
    const pkg = { instrument: "smk37-pro", file: "firmware/other/2.0/other.fwsc", identity: "SMK-37 Pro_957", sha256: sha(other), size: other.length };
    const b = structuredClone(base);
    b.firmwares.push({ id: "other", name: "Other", summary: "s", license: "GPL-3.0-only", notes: [],
      releases: [{ version: "2.0", date: "2026-11-01", packages: [pkg] }] });
    const c = await makeCatalog({ ...o, base: b });
    expect(c.firmwares.find((f) => f.id === "other")!.releases[0].packages[0]).toEqual(pkg);
    pkg.sha256 = "1".repeat(64);
    await expect(makeCatalog({ ...o, base: b })).rejects.toThrow(/other\.fwsc/);
  });

  it("runs as a CLI under plain node", () => {
    const out = execFileSync("node", ["scripts/make-catalog.ts", "--help"], { cwd: new URL("..", import.meta.url), encoding: "utf8" });
    expect(out).toContain("--version");
  });
});
