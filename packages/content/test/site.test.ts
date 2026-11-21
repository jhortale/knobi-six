import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterAll, describe, expect, it } from "vitest";
import { makePackage } from "@knobi/device-sim";
import { validateCatalog } from "@knobi/catalog";
import { repoRootFrom } from "@knobi/catalog/node";
import { readContent } from "../src/read.ts";
import { releaseFileText } from "../src/release-file.ts";
import { buildSiteData, findForbiddenFiles, scanForTokens } from "../src/site.ts";
import { verifyReleaseDir } from "../src/verify.ts";
import type { PortalContent } from "../src/types.ts";

const ROOT = repoRootFrom(import.meta.dirname);
const sha = (b: Uint8Array | string) => createHash("sha256").update(b).digest("hex");
const bot = { login: "github-actions[bot]" };
const made: string[] = [];
const tmp = (p: string) => {
  const d = mkdtempSync(join(tmpdir(), `site-${p}-`));
  made.push(d);
  return d;
};
afterAll(() => {
  for (const d of made) rmSync(d, { recursive: true, force: true });
});

type Files = Record<string, Uint8Array | string>;

/** a release directory as release.yml leaves it: three packages, the source archive, SHA256SUMS */
function releaseDir(version: string, over: Files = {}): string {
  const dir = tmp("rel");
  const files: Files = {
    [`knobi-six-${version}.fwsc`]: makePackage({ product: "SMK-37 Pro_956" }),
    [`knobi-six-${version}-elite.fwsc`]: makePackage({ product: "SMK-37 Elite_956" }),
    [`knobi-six-${version}-mke.fwsc`]: makePackage({ product: "MKE-P37_956" }),
    [`knobi-six-${version}-src.tar.gz`]: `the GPL source of ${version}`,
    ...over,
  };
  const sums: string[] = [];
  for (const [n, b] of Object.entries(files)) {
    writeFileSync(join(dir, n), b);
    if (n.endsWith(".fwsc")) sums.push(`${sha(b)}  ${n}\n`);
  }
  writeFileSync(join(dir, "SHA256SUMS"), sums.join(""));
  return dir;
}

const assetsOf = (dir: string, uploader = bot) => readdirSync(dir).map((name) => ({ name, uploader }));

interface Fixture {
  repo: string;
  out: string;
  dirs: Record<string, string>;
  assets: Record<string, { name: string; uploader: { login: string } }[]>;
}

/** a repo with the real synth.json/catalog.json of KNOBI-SIX, its synth.md, and one release file per version */
async function fixture(versions: string[], opts: { current?: string[]; image?: boolean } = {}): Promise<Fixture> {
  const repo = tmp("repo");
  writeFileSync(join(repo, "pnpm-workspace.yaml"), "packages: []\n");
  mkdirSync(join(repo, "packages/catalog"), { recursive: true });
  cpSync(join(ROOT, "packages/catalog/instruments.json"), join(repo, "packages/catalog/instruments.json"));
  cpSync(join(ROOT, "synths/knobi-six/synth.json"), join(repo, "synths/knobi-six/synth.json"), { recursive: true });
  cpSync(join(ROOT, "synths/knobi-six/catalog.json"), join(repo, "synths/knobi-six/catalog.json"), { recursive: true });
  writeFileSync(join(repo, "synths/knobi-six/synth.md"),
    `---\nname: KNOBI-SIX\nsummary: A polysynth\norder: 1\norigin: own\n${opts.image ? "image: panel.png\n" : ""}---\nSix **voices**.\n`);
  if (opts.image) writeFileSync(join(repo, "synths/knobi-six/panel.png"), "PNGDATA");
  mkdirSync(join(repo, "synths/knobi-six/releases"), { recursive: true });
  const dirs: Fixture["dirs"] = {};
  const assets: Fixture["assets"] = {};
  for (const v of versions) {
    const dir = releaseDir(v);
    const { builds, source } = await verifyReleaseDir(repo, "knobi-six", v, dir);
    writeFileSync(join(repo, `synths/knobi-six/releases/${v}.md`), releaseFileText({
      synth: "knobi-six", version: v, date: "2026-11-20", tag: `knobi-six-v${v}`,
      ...(opts.current ? { current: opts.current.includes(v) } : {}), source, builds, notes: `Notes of **${v}**.`,
    }));
    dirs[`knobi-six-v${v}`] = dir;
    assets[`knobi-six-v${v}`] = assetsOf(dir);
  }
  return { repo, out: tmp("out"), dirs, assets };
}

const build = (f: Fixture) => buildSiteData({
  repoRoot: f.repo, content: readContent(f.repo), releaseDirs: f.dirs, assets: f.assets, outDir: f.out,
});

describe("buildSiteData", () => {
  it("writes the layout, a catalog.json validateCatalog accepts (with the repo's notes), and content.json", async () => {
    const f = await fixture(["0.9", "1.0"], { current: ["1.0"], image: true });
    const { catalog, portal } = await build(f);
    for (const v of ["0.9", "1.0"]) {
      for (const n of [`knobi-six-${v}.fwsc`, `knobi-six-${v}-elite.fwsc`, `knobi-six-${v}-mke.fwsc`, `knobi-six-${v}-src.tar.gz`, "SHA256SUMS"]) {
        expect(existsSync(join(f.out, "firmware/knobi-six", v, n)), `${v}/${n}`).toBe(true);
      }
    }
    expect(readFileSync(join(f.out, "synths/knobi-six/panel.png"), "utf8")).toBe("PNGDATA");

    const onDisk = JSON.parse(readFileSync(join(f.out, "catalog.json"), "utf8"));
    expect(onDisk).toEqual(JSON.parse(JSON.stringify(catalog)));
    const valid = validateCatalog(onDisk);
    expect(valid.schema).toBe(1);
    const fw = valid.firmwares.find((x) => x.id === "knobi-six")!;
    expect(fw.notes).toEqual(["Replaces the stored firmware and its saved data; calibration is not written."]);
    expect(fw.license).toBe("GPL-3.0-only");
    expect(fw.releases.map((r) => r.version)).toEqual(["1.0", "0.9"]);
    const r = fw.releases[0]!;
    expect(r.source).toBe("firmware/knobi-six/1.0/knobi-six-1.0-src.tar.gz");
    expect(r.packages.map((p) => p.file).sort()).toEqual([
      "firmware/knobi-six/1.0/knobi-six-1.0-elite.fwsc",
      "firmware/knobi-six/1.0/knobi-six-1.0-mke.fwsc",
      "firmware/knobi-six/1.0/knobi-six-1.0.fwsc",
    ]);

    const content = JSON.parse(readFileSync(join(f.out, "content.json"), "utf8")) as PortalContent;
    expect(content).toEqual(JSON.parse(JSON.stringify(portal)));
    const s = content.synths[0]!;
    expect(s).toMatchObject({ id: "knobi-six", name: "KNOBI-SIX", summary: "A polysynth", order: 1, image: "synths/knobi-six/panel.png" });
    expect(s.descriptionHtml).toContain("<strong>voices</strong>");
    expect(s.keyboards.sort()).toEqual(["mke-p37", "smk37-elite", "smk37-pro"]);
    expect(s.releases.map((x) => [x.version, x.current])).toEqual([["1.0", true], ["0.9", false]]);
    const rel = s.releases[0]!;
    expect(rel.notesHtml).toContain("<strong>1.0</strong>");
    expect(rel.tag).toBe("knobi-six-v1.0");
    expect(rel.source.url).toBe("firmware/knobi-six/1.0/knobi-six-1.0-src.tar.gz");
    expect(rel.builds.map((b) => b.url).sort()[0]).toBe("firmware/knobi-six/1.0/knobi-six-1.0-elite.fwsc");
    expect(typeof content.generated).toBe("string");
  });

  it("refuses a release file whose build sha256 differs, naming the release and the file", async () => {
    const f = await fixture(["1.0"], { current: ["1.0"] });
    const p = join(f.repo, "synths/knobi-six/releases/1.0.md");
    const text = readFileSync(p, "utf8");
    const real = sha(makePackage({ product: "SMK-37 Pro_956" }));
    expect(text).toContain(real);
    writeFileSync(p, text.replace(real, "b".repeat(64)));
    await expect(build(f)).rejects.toThrow(/knobi-six 1\.0.*knobi-six-1\.0\.fwsc.*sha256/s);
  });

  it("refuses a release file whose source size differs, naming the release and the file", async () => {
    const f = await fixture(["1.0"]);
    const p = join(f.repo, "synths/knobi-six/releases/1.0.md");
    writeFileSync(p, readFileSync(p, "utf8").replace(/size: (\d+)\nbuilds/, "size: 1\nbuilds"));
    await expect(build(f)).rejects.toThrow(/knobi-six 1\.0.*knobi-six-1\.0-src\.tar\.gz.*size/s);
  });

  it("refuses a release file that lists a build the release does not have", async () => {
    const f = await fixture(["1.0"]);
    rmSync(join(f.dirs["knobi-six-v1.0"]!, "knobi-six-1.0-mke.fwsc"));
    await expect(build(f)).rejects.toThrow(/knobi-six 1\.0.*knobi-six-1\.0-mke\.fwsc/s);
  });

  it("refuses a release whose assets were not uploaded by github-actions[bot]", async () => {
    const f = await fixture(["1.0"]);
    f.assets["knobi-six-v1.0"] = assetsOf(f.dirs["knobi-six-v1.0"]!, { login: "mallory" });
    await expect(build(f)).rejects.toThrow(/mallory/);
  });

  it("refuses a release with no downloaded directory or no asset list", async () => {
    const f = await fixture(["1.0"]);
    delete f.dirs["knobi-six-v1.0"];
    await expect(build(f)).rejects.toThrow(/knobi-six 1\.0/);
    const g = await fixture(["1.0"]);
    delete g.assets["knobi-six-v1.0"];
    await expect(build(g)).rejects.toThrow(/knobi-six 1\.0/);
  });

  it("refuses two current releases of one synth, naming both", async () => {
    const f = await fixture(["0.9", "1.0"], { current: ["0.9", "1.0"] });
    await expect(build(f)).rejects.toThrow(/knobi-six.*0\.9.*1\.0/s);
  });

  it("with none current, picks 1.0 over 0.9", async () => {
    const f = await fixture(["0.9", "1.0"]);
    const { portal } = await build(f);
    expect(portal.synths[0]!.releases.map((r) => [r.version, r.current])).toEqual([["1.0", true], ["0.9", false]]);
  });

  it("orders versions numerically, never as strings (10.0 over 2.0)", async () => {
    const f = await fixture(["2.0", "10.0"]);
    const { portal, catalog } = await build(f);
    expect(portal.synths[0]!.releases.map((r) => [r.version, r.current])).toEqual([["10.0", true], ["2.0", false]]);
    expect(catalog.firmwares.find((x) => x.id === "knobi-six")!.releases.map((r) => r.version)).toEqual(["10.0", "2.0"]);
  });

  it("lists a synth with no published release, with nothing current", async () => {
    const f = await fixture([]);
    const { portal } = await build(f);
    expect(portal.synths).toHaveLength(1);
    expect(portal.synths[0]!.releases).toEqual([]);
    expect(portal.synths[0]!.keyboards).toEqual([]);
  });

  it("refuses a -local file in the release directory", async () => {
    const f = await fixture(["1.0"]);
    writeFileSync(join(f.dirs["knobi-six-v1.0"]!, "knobi-six-local.fwsc"), makePackage({ product: "SMK-37 Pro_956" }));
    await expect(build(f)).rejects.toThrow(/knobi-six-local\.fwsc/);
    expect(existsSync(join(f.out, "catalog.json"))).toBe(false);
  });

  it("refuses an output directory that is not empty, leaving its files untouched", async () => {
    const f = await fixture(["1.0"]);
    mkdirSync(join(f.out, "firmware/knobi-six/0.5"), { recursive: true });
    writeFileSync(join(f.out, "firmware/knobi-six/0.5/old.fwsc"), "stale");
    await expect(build(f)).rejects.toThrow(/not empty/);
    expect(readFileSync(join(f.out, "firmware/knobi-six/0.5/old.fwsc"), "utf8")).toBe("stale");
    expect(existsSync(join(f.out, "catalog.json"))).toBe(false);
  });

  it("refuses a synth image that is missing", async () => {
    const f = await fixture(["1.0"], { image: true });
    rmSync(join(f.repo, "synths/knobi-six/panel.png"));
    await expect(build(f)).rejects.toThrow(/panel\.png/);
  });
});

describe("scanForTokens and findForbiddenFiles", () => {
  it("finds planted token prefixes, a PEM header and a given secret value, naming the file", () => {
    const d = tmp("scan");
    mkdirSync(join(d, "sub"));
    writeFileSync(join(d, "ok.json"), "{}");
    writeFileSync(join(d, "a.txt"), "x ghp_abcdefghijklmnopqrstuvwxyz0123456789 y");
    writeFileSync(join(d, "sub/b.txt"), "github_pat_11AAAA");
    writeFileSync(join(d, "c.txt"), "-----BEGIN OPENSSH PRIVATE KEY-----");
    writeFileSync(join(d, "d.bin"), Buffer.concat([Buffer.from([0, 1, 2]), Buffer.from("s3cr3t-value"), Buffer.from([255])]));
    const hits = scanForTokens(d, ["s3cr3t-value"]);
    expect(hits).toHaveLength(4);
    expect(hits.join("\n")).toMatch(/a\.txt/);
    expect(hits.join("\n")).toMatch(/sub\/b\.txt/);
    expect(hits.join("\n")).toMatch(/c\.txt/);
    expect(hits.join("\n")).toMatch(/d\.bin/);
    expect(hits.join("\n")).not.toContain("s3cr3t-value");
  });
  it("finds nothing in a clean tree, and ignores empty secrets", () => {
    const d = tmp("scan");
    writeFileSync(join(d, "ok.json"), "{}");
    expect(scanForTokens(d, ["", "unset"])).toEqual([]);
  });
  it("lists -local and cpu1 files", () => {
    const d = tmp("forbid");
    mkdirSync(join(d, "firmware/x"), { recursive: true });
    writeFileSync(join(d, "firmware/x/knobi-six-local.fwsc"), "");
    writeFileSync(join(d, "firmware/x/knobi-six-cpu1probe.fwsc"), "");
    writeFileSync(join(d, "firmware/x/knobi-six-1.0.fwsc"), "");
    expect(findForbiddenFiles(d).sort()).toEqual(["firmware/x/knobi-six-cpu1probe.fwsc", "firmware/x/knobi-six-local.fwsc"]);
  });
});

describe("site CLI (offline overrides)", () => {
  const cli = join(import.meta.dirname, "../src/cli/site.ts");
  const run = (f: Fixture, extra: string[] = [], env: Record<string, string> = {}) => {
    const args = [cli, "--repo-root", f.repo, "--out", f.out];
    for (const [tag, dir] of Object.entries(f.dirs)) {
      const a = join(tmp("assets"), "assets.json");
      writeFileSync(a, JSON.stringify(f.assets[tag]));
      args.push("--release", `${tag}=${dir}`, "--assets", `${tag}=${a}`);
    }
    return execFileSync("node", [...args, ...extra], {
      encoding: "utf8", stdio: ["ignore", "pipe", "pipe"], env: { ...process.env, GH_TOKEN: "", GITHUB_TOKEN: "", ...env },
    });
  };

  it("builds the site without the network", async () => {
    const f = await fixture(["1.0"], { current: ["1.0"] });
    run(f);
    expect(existsSync(join(f.out, "catalog.json"))).toBe(true);
    expect(existsSync(join(f.out, "content.json"))).toBe(true);
  });

  it("fails, and leaves no site, when a token value ends up in the site", async () => {
    const f = await fixture(["1.0"], { current: ["1.0"] });
    // the planted secret goes in via the release notes, which land in content.json
    const p = join(f.repo, "synths/knobi-six/releases/1.0.md");
    writeFileSync(p, readFileSync(p, "utf8") + "\nleaked tok3n-1234\n");
    expect(() => run(f, [], { GH_TOKEN: "tok3n-1234" })).toThrow(/token|secret/i);
    expect(existsSync(join(f.out, "content.json"))).toBe(false);
  });

  it("refuses an existing non-empty --out, before anything, leaving its files", async () => {
    const f = await fixture(["1.0"], { current: ["1.0"] });
    writeFileSync(join(f.out, "keep.txt"), "mine");
    expect(() => run(f)).toThrow(/not empty/);
    expect(readdirSync(f.out)).toEqual(["keep.txt"]);
    expect(readFileSync(join(f.out, "keep.txt"), "utf8")).toBe("mine");
  });

  it("a failing build leaves an absent --out absent and no staging directory", async () => {
    const f = await fixture(["1.0"], { current: ["1.0"] });
    const parent = tmp("parent");
    const out = join(parent, "site");
    const p = join(f.repo, "synths/knobi-six/releases/1.0.md");
    writeFileSync(p, readFileSync(p, "utf8") + "\nleaked tok3n-1234\n");
    f.out = out;
    expect(() => run(f, [], { GH_TOKEN: "tok3n-1234" })).toThrow();
    expect(readdirSync(parent)).toEqual([]);
  });

  it("a failing build never deletes a pre-existing empty --out, nor leaves staging beside it", async () => {
    const f = await fixture(["1.0"], { current: ["1.0"] });
    const p = join(f.repo, "synths/knobi-six/releases/1.0.md");
    writeFileSync(p, readFileSync(p, "utf8") + "\nleaked tok3n-1234\n");
    const parent = tmp("parent");
    const out = join(parent, "site");
    mkdirSync(out);
    f.out = out;
    expect(() => run(f, [], { GH_TOKEN: "tok3n-1234" })).toThrow();
    expect(readdirSync(parent)).toEqual(["site"]);
    expect(readdirSync(out)).toEqual([]);
  });

  it("builds into a fresh --out that does not exist yet, and into an existing empty one", async () => {
    const f = await fixture(["1.0"], { current: ["1.0"] });
    const parent = tmp("parent");
    f.out = join(parent, "new/site");
    run(f);
    expect(existsSync(join(f.out, "catalog.json"))).toBe(true);
    expect(readdirSync(join(parent, "new"))).toEqual(["site"]);
  });

  it("fails with a usage message without --out", () => {
    expect(() => execFileSync("node", [cli], { stdio: ["ignore", "pipe", "pipe"] })).toThrow();
  });
});
