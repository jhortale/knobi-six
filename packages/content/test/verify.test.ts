import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { makePackage } from "@knobi/device-sim";
import { repoRootFrom } from "@knobi/catalog/node";
import { parseTag, verifyAssets, verifyReleaseDir } from "../src/verify.ts";
import { ciFieldsChanged } from "../src/node.ts";

const ROOT = repoRootFrom(import.meta.dirname);
const sha = (b: Uint8Array | string) => createHash("sha256").update(b).digest("hex");

function tempRepo(): string {
  const d = mkdtempSync(join(tmpdir(), "verify-repo-"));
  writeFileSync(join(d, "pnpm-workspace.yaml"), "packages: []\n");
  mkdirSync(join(d, "packages/catalog"), { recursive: true });
  cpSync(join(ROOT, "packages/catalog/instruments.json"), join(d, "packages/catalog/instruments.json"));
  cpSync(join(ROOT, "synths/knobi-six"), join(d, "synths/knobi-six"), { recursive: true });
  return d;
}

type Files = Record<string, Uint8Array | string>;
/** a release directory: three packages, the source archive and a SHA256SUMS over the packages (as release.yml writes it) */
function releaseDir(over: Files = {}, drop: string[] = [], sums?: (s: Record<string, string>) => void): string {
  const dir = mkdtempSync(join(tmpdir(), "verify-rel-"));
  const files: Files = {
    "knobi-six-1.0.fwsc": makePackage({ product: "SMK-37 Pro_956" }),
    "knobi-six-1.0-elite.fwsc": makePackage({ product: "SMK-37 Elite_956" }),
    "knobi-six-1.0-mke.fwsc": makePackage({ product: "MKE-P37_956" }),
    "knobi-six-1.0-src.tar.gz": "the GPL source",
    ...over,
  };
  for (const d of drop) delete files[d];
  const s: Record<string, string> = {};
  for (const [n, b] of Object.entries(files)) {
    writeFileSync(join(dir, n), b);
    if (n.endsWith(".fwsc") || n.endsWith("-src.tar.gz")) s[n] = sha(b);
  }
  sums?.(s);
  writeFileSync(join(dir, "SHA256SUMS"), Object.entries(s).map(([n, h]) => `${h}  ${n}\n`).join(""));
  return dir;
}

describe("parseTag", () => {
  it("splits <synth>-v<version>", () => {
    expect(parseTag("knobi-six-v1.0")).toEqual({ synth: "knobi-six", version: "1.0" });
    expect(parseTag("other-synth-v2.10.3")).toEqual({ synth: "other-synth", version: "2.10.3" });
  });
  it.each(["knobi-six-v1.0-rc1", "v1.0", "knobi-six-1.0", "knobi-six-v1", "-v1.0", "knobi-six-v1.0-", "knobi-six-v1.x"])(
    "refuses %s", (tag) => {
      expect(() => parseTag(tag)).toThrow(tag);
    });
});

describe("verifyAssets", () => {
  const bot = { login: "github-actions[bot]" };
  it("accepts assets uploaded by github-actions[bot]", () => {
    expect(() => verifyAssets([{ name: "a.fwsc", uploader: bot }, { name: "SHA256SUMS", uploader: bot }])).not.toThrow();
  });
  it("refuses another uploader, naming the asset", () => {
    expect(() => verifyAssets([{ name: "a.fwsc", uploader: bot }, { name: "x.fwsc", uploader: { login: "jhortale" } }]))
      .toThrow(/x\.fwsc.*jhortale/);
  });
  it("refuses an asset with no uploader", () => {
    expect(() => verifyAssets([{ name: "y.fwsc" } as never])).toThrow(/y\.fwsc/);
  });
});

describe("verifyReleaseDir", () => {
  it("accepts three packages, the source archive and SHA256SUMS", async () => {
    const dir = releaseDir();
    const r = await verifyReleaseDir(tempRepo(), "knobi-six", "1.0", dir);
    expect(r.builds).toHaveLength(3);
    expect(r.builds.map((b) => b.instrument).sort()).toEqual(["mke-p37", "smk37-elite", "smk37-pro"]);
    const pro = r.builds.find((b) => b.instrument === "smk37-pro")!;
    expect(pro).toEqual({
      kind: "keyboard", instrument: "smk37-pro", identity: "SMK-37 Pro_956", file: "knobi-six-1.0.fwsc",
      sha256: sha(makePackage({ product: "SMK-37 Pro_956" })), size: makePackage({ product: "SMK-37 Pro_956" }).length,
    });
    expect(r.source).toEqual({ file: "knobi-six-1.0-src.tar.gz", sha256: sha("the GPL source"), size: 14 });
  });

  const refusals: [string, () => string, RegExp][] = [
    ["a -local file", () => releaseDir({ "knobi-six-local.fwsc": makePackage({ product: "SMK-37 Pro_956" }) }), /knobi-six-local\.fwsc/],
    ["a cpu1 file", () => releaseDir({ "knobi-six-cpu1probe.fwsc": makePackage({ product: "SMK-37 Pro_956" }) }), /cpu1probe/],
    ["a wrong hash", () => releaseDir({}, [], (s) => { s["knobi-six-1.0.fwsc"] = "0".repeat(64); }), /SHA256SUMS/],
    ["a wrong identity", () => releaseDir({ "knobi-six-1.0.fwsc": makePackage({ product: "SMK-37 Pro_900" }) }), /SMK-37 Pro_900/],
    ["a source archive without a SHA256SUMS line", () => releaseDir({}, [], (s) => { delete s["knobi-six-1.0-src.tar.gz"]; }), /knobi-six-1\.0-src\.tar\.gz.*SHA256SUMS/],
    ["a source archive whose SHA256SUMS line is wrong", () => releaseDir({}, [], (s) => { s["knobi-six-1.0-src.tar.gz"] = "0".repeat(64); }), /knobi-six-1\.0-src\.tar\.gz.*SHA256SUMS/],
    ["another source archive", () => releaseDir({ "evil-9.9-src.tar.gz": "x" }), /evil-9\.9-src\.tar\.gz/],
    ["a missing source archive", () => releaseDir({}, ["knobi-six-1.0-src.tar.gz"]), /knobi-six-1\.0-src\.tar\.gz/],
  ];
  it.each(refusals)("refuses %s", async (_n, dir, msg) => {
    await expect(verifyReleaseDir(tempRepo(), "knobi-six", "1.0", dir())).rejects.toThrow(msg);
  });
});

describe("release-pr CLI", () => {
  const cli = join(import.meta.dirname, "../src/cli/release-pr.ts");
  const bot = { login: "github-actions[bot]" };
  function run(repo: string, dir: string, assets: unknown, out: string, tag = "knobi-six-v1.0") {
    const a = join(mkdtempSync(join(tmpdir(), "assets-")), "assets.json");
    writeFileSync(a, JSON.stringify(assets));
    return execFileSync("node", [cli, "--tag", tag, "--dir", dir, "--assets", a, "--out", out, "--repo-root", repo],
      { encoding: "utf8", stdio: ["ignore", "pipe", "pipe"] });
  }
  const names = ["knobi-six-1.0.fwsc", "knobi-six-1.0-elite.fwsc", "knobi-six-1.0-mke.fwsc", "knobi-six-1.0-src.tar.gz", "SHA256SUMS"];

  it("writes the release file: CI fields filled, notes for João to edit, and it round-trips", () => {
    const repo = tempRepo();
    const out = join(repo, "synths/knobi-six/releases/1.0.md");
    run(repo, releaseDir(), names.map((name) => ({ name, uploader: bot })), out);
    const text = readFileSync(out, "utf8");
    expect(text).toContain("tag: knobi-six-v1.0");
    expect(text).toContain("Release notes for knobi-six 1.0.");
    expect(text).toMatch(/^date: \d{4}-\d{2}-\d{2}$/m);
    expect(ciFieldsChanged(text, text)).toEqual([]);
    expect(text).toContain("instrument: smk37-pro");
    expect(text).toContain("sha256: " + sha("the GPL source"));
  });

  it("refuses when the release file already exists, leaving it alone", () => {
    const repo = tempRepo();
    const out = join(repo, "synths/knobi-six/releases/1.0.md");
    mkdirSync(join(repo, "synths/knobi-six/releases"), { recursive: true });
    writeFileSync(out, "published\n");
    expect(() => run(repo, releaseDir(), names.map((name) => ({ name, uploader: bot })), out)).toThrow(/already exists/);
    expect(readFileSync(out, "utf8")).toBe("published\n");
  });

  it("refuses an asset another user uploaded, writing nothing", () => {
    const repo = tempRepo();
    const out = join(repo, "synths/knobi-six/releases/1.0.md");
    const assets = names.map((name) => ({ name, uploader: name === "SHA256SUMS" ? { login: "mallory" } : bot }));
    expect(() => run(repo, releaseDir(), assets, out)).toThrow(/SHA256SUMS/);
    expect(existsSync(out)).toBe(false);
  });

  it("refuses a prerelease tag", () => {
    const repo = tempRepo();
    expect(() => run(repo, releaseDir(), [], join(repo, "x.md"), "knobi-six-v1.0-rc1")).toThrow(/rc1/);
  });
});

describe("parse-tag CLI (release.yml's meta job)", () => {
  const cli = join(import.meta.dirname, "../src/cli/parse-tag.ts");
  const run = (tag: string) => execFileSync("node", [cli, tag], { encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] });
  it("prints the outputs for a release tag", () => {
    expect(run("knobi-six-v1.0")).toBe("synth=knobi-six\nversion=1.0\nok=true\n");
  });
  it("prints ok=false, and exits 0, for a prerelease", () => {
    expect(run("knobi-six-v1.0-rc1")).toBe("ok=false\n");
  });
});
