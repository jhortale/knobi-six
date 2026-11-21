// SPDX-License-Identifier: GPL-3.0-only
// Writes synths/<synth>/releases/<version>.md for a tag, from the release's downloaded files and its asset list
// (release.yml's release-pr job). Refuses when the file exists. Runs under plain node 24 (type stripping):
//   node packages/content/src/cli/release-pr.ts --tag knobi-six-v1.0 --dir release --assets assets.json \
//        --out synths/knobi-six/releases/1.0.md [--repo-root DIR] [--date YYYY-MM-DD]
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname } from "node:path";
import { repoRootFrom } from "@knobi/catalog/node";
import { releaseFileText } from "../release-file.ts";
import { parseTag, verifyAssets, verifyReleaseDir } from "../verify.ts";

function args(argv: string[]): Record<string, string> {
  const a: Record<string, string> = {};
  for (let i = 0; i < argv.length; i++) {
    if (argv[i]!.startsWith("--")) a[argv[i]!.slice(2)] = argv[i + 1]?.startsWith("--") || i + 1 >= argv.length ? "" : argv[++i]!;
  }
  return a;
}

async function main(argv: string[]): Promise<number> {
  const a = args(argv);
  if ("help" in a || !a.tag || !a.dir || !a.assets || !a.out) {
    console.log("usage: node release-pr.ts --tag SYNTH-vX.Y --dir RELEASE_DIR --assets assets.json " +
      "--out synths/SYNTH/releases/X.Y.md [--repo-root DIR] [--date YYYY-MM-DD]");
    return "help" in a ? 0 : 2;
  }
  const { synth, version } = parseTag(a.tag);
  if (existsSync(a.out)) throw new Error(`${a.out}: already exists (a published release never changes)`);
  verifyAssets(JSON.parse(readFileSync(a.assets, "utf8")));
  const repoRoot = a["repo-root"] || repoRootFrom(process.cwd());
  const { builds, source } = await verifyReleaseDir(repoRoot, synth, version, a.dir);
  mkdirSync(dirname(a.out), { recursive: true });
  writeFileSync(a.out, releaseFileText({
    synth, version, date: a.date || new Date().toISOString().slice(0, 10), tag: a.tag, source, builds,
    notes: `Release notes for ${synth} ${version}.`,
  }), { flag: "wx" });
  console.log(`${a.out}: ${synth} ${version}, ${builds.length} packages`);
  return 0;
}

// import.meta.main (Node 24): true also through a symlink, where comparing argv[1] with import.meta.url does nothing
if (import.meta.main) {
  main(process.argv.slice(2)).then((c) => process.exit(c), (e) => {
    console.error(`release-pr: ${(e as Error).message}`);
    process.exit(1);
  });
}
