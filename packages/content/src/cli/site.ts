// SPDX-License-Identifier: GPL-3.0-only
// Builds the site's data into DIR from the GitHub releases of every release file in the repository: downloads each tag
// (gh release download) with its asset list (gh api), re-checks every file, writes firmware/, catalog.json and
// content.json, then scans for tokens and dev builds. Ruling 1: --release TAG=DIR and --assets TAG=FILE use a local
// directory / asset list instead of the network for that tag.
//   node packages/content/src/cli/site.ts --out DIR [--repo-root DIR] [--release TAG=DIR]... [--assets TAG=FILE]...
import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, renameSync, rmSync, rmdirSync } from "node:fs";
import { tmpdir } from "node:os";
import { basename, dirname, join, resolve } from "node:path";
import { repoRootFrom } from "@knobi/catalog/node";
import { readContent } from "../read.ts";
import { assertEmptyOut, buildSiteData, findForbiddenFiles, scanForTokens } from "../site.ts";
import type { AssetInfo } from "../site.ts";

const USAGE = "usage: node site.ts --out DIR [--repo-root DIR] [--release TAG=DIR]... [--assets TAG=FILE]...";

function parse(argv: string[]) {
  const a: { out?: string; repoRoot?: string; release: Record<string, string>; assets: Record<string, string>; help: boolean } =
    { release: {}, assets: {}, help: false };
  const pair = (v: string | undefined, flag: string): [string, string] => {
    const i = (v ?? "").indexOf("=");
    if (i < 1 || i === v!.length - 1) throw new Error(`${flag}: expected TAG=PATH, got ${JSON.stringify(v)}`);
    return [v!.slice(0, i), v!.slice(i + 1)];
  };
  for (let i = 0; i < argv.length; i++) {
    const f = argv[i]!;
    if (f === "--help") a.help = true;
    else if (f === "--out") a.out = argv[++i];
    else if (f === "--repo-root") a.repoRoot = argv[++i];
    else if (f === "--release") { const [t, p] = pair(argv[++i], f); a.release[t] = p; }
    else if (f === "--assets") { const [t, p] = pair(argv[++i], f); a.assets[t] = p; }
    else throw new Error(`unknown argument ${f}`);
  }
  return a;
}

async function main(argv: string[]): Promise<number> {
  const a = parse(argv);
  if (a.help || !a.out) {
    console.log(USAGE);
    return a.help ? 0 : 2;
  }
  const out = resolve(a.out);
  assertEmptyOut(out); // before any download: --out is never a directory that already holds files
  const repoRoot = a.repoRoot || repoRootFrom(process.cwd());
  const repo = process.env.GITHUB_REPOSITORY || "jhortale/midi-chlorian";
  const content = readContent(repoRoot);
  const scratch = mkdtempSync(join(tmpdir(), "site-releases-"));
  // built in a fresh sibling of --out and renamed into place only after every check: the only directory this tool
  // ever deletes is one it created itself
  mkdirSync(dirname(out), { recursive: true });
  const staging = mkdtempSync(join(dirname(out), `.${basename(out)}-staging-`));
  const releaseDirs: Record<string, string> = {};
  const assets: Record<string, AssetInfo[]> = {};
  try {
    for (const r of content.releases) {
      const tag = r.tag;
      if (a.release[tag]) releaseDirs[tag] = a.release[tag]!;
      else {
        const dir = join(scratch, tag);
        execFileSync("gh", ["release", "download", tag, "-R", repo, "-D", dir], { stdio: ["ignore", "inherit", "inherit"] });
        releaseDirs[tag] = dir;
      }
      assets[tag] = JSON.parse(a.assets[tag]
        ? readFileSync(a.assets[tag]!, "utf8")
        : execFileSync("gh", ["api", `repos/${repo}/releases/tags/${tag}`, "--jq", ".assets"], { encoding: "utf8" }));
    }
    await buildSiteData({ repoRoot, content, releaseDirs, assets, outDir: staging });
    const bad = [
      ...scanForTokens(staging, [process.env.GH_TOKEN, process.env.GITHUB_TOKEN].filter((s): s is string => !!s)),
      ...findForbiddenFiles(staging).map((f) => `${f}: a -local or cpu1 file`),
    ];
    if (bad.length) throw new Error(`refused, the site must not contain:\n  ${bad.join("\n  ")}`);
    if (existsSync(out)) rmdirSync(out); // empty: checked at the start (rmdir refuses anything else)
    renameSync(staging, out);
  } catch (e) {
    rmSync(staging, { recursive: true, force: true }); // only ever the directory created above
    throw e;
  } finally {
    rmSync(scratch, { recursive: true, force: true });
  }
  console.log(`${a.out}: ${content.synths.length} synths, ${content.releases.length} releases`);
  return 0;
}

// import.meta.main (Node 24): true also through a symlink, where comparing argv[1] with import.meta.url does nothing
if (import.meta.main) {
  main(process.argv.slice(2)).then((c) => process.exit(c), (e) => {
    console.error(`site: ${(e as Error).message}`);
    process.exit(1);
  });
}
