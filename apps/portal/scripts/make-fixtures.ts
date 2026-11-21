// SPDX-License-Identifier: GPL-3.0-only
// Writes test/fixtures/{content,catalog}.json: the site builder (@knobi/content) run over made-up KNOBI-SIX releases
// (test/fixtures/packages.ts) and the repo's own synths/knobi-six (synth.md, synth.json, catalog.json). With --site DIR
// it also leaves the whole built site data there (firmware/ included), for serving the built portal locally.
//   node scripts/make-fixtures.ts [--site DIR]
import { createHash } from "node:crypto";
import { cpSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { repoRootFrom } from "@knobi/catalog/node";
import { buildSiteData, readContent, releaseFileText, verifyReleaseDir } from "@knobi/content/node";
import { FIXTURE_RELEASES, fixtureFiles } from "../test/fixtures/packages.ts";

const GENERATED = "2026-11-21T00:00:00.000Z";
const ROOT = repoRootFrom(import.meta.dirname);
const FIXTURES = join(import.meta.dirname, "../test/fixtures");
const sha = (b: Uint8Array | string) => createHash("sha256").update(b).digest("hex");

const siteArg = process.argv.indexOf("--site");
const site = siteArg > 0 ? resolve(process.argv[siteArg + 1]!) : undefined;
const tmp = mkdtempSync(join(tmpdir(), "portal-fixtures-"));
try {
  const repo = join(tmp, "repo");
  mkdirSync(join(repo, "packages/catalog"), { recursive: true });
  writeFileSync(join(repo, "pnpm-workspace.yaml"), "packages: []\n");
  cpSync(join(ROOT, "packages/catalog/instruments.json"), join(repo, "packages/catalog/instruments.json"));
  for (const f of ["synth.json", "catalog.json", "synth.md"]) {
    cpSync(join(ROOT, "synths/knobi-six", f), join(repo, "synths/knobi-six", f));
  }
  mkdirSync(join(repo, "synths/knobi-six/releases"), { recursive: true });
  const releaseDirs: Record<string, string> = {};
  const assets: Record<string, { name: string; uploader: { login: string } }[]> = {};
  for (const r of FIXTURE_RELEASES) {
    const dir = join(tmp, `release-${r.version}`);
    mkdirSync(dir);
    const files = fixtureFiles(r.version);
    const sums: string[] = [];
    for (const [name, body] of Object.entries(files)) {
      writeFileSync(join(dir, name), body);
      if (name.endsWith(".fwsc")) sums.push(`${sha(body)}  ${name}\n`);
    }
    writeFileSync(join(dir, "SHA256SUMS"), sums.join(""));
    const { builds, source } = await verifyReleaseDir(repo, "knobi-six", r.version, dir);
    const tag = `knobi-six-v${r.version}`;
    writeFileSync(join(repo, `synths/knobi-six/releases/${r.version}.md`), releaseFileText({
      synth: "knobi-six", version: r.version, date: r.date, tag, current: "current" in r ? r.current : undefined,
      source, builds, notes: r.notes,
    }));
    releaseDirs[tag] = dir;
    assets[tag] = [...Object.keys(files), "SHA256SUMS"].map((name) => ({ name, uploader: { login: "github-actions[bot]" } }));
  }
  const out = site ?? join(tmp, "site");
  await buildSiteData({ repoRoot: repo, content: readContent(repo), releaseDirs, assets, outDir: out });
  for (const f of ["content.json", "catalog.json"]) {
    const json = { ...JSON.parse(readFileSync(join(out, f), "utf8")), generated: GENERATED };
    writeFileSync(join(out, f), `${JSON.stringify(json, null, 2)}\n`);
    writeFileSync(join(FIXTURES, f), `${JSON.stringify(json, null, 2)}\n`);
  }
  console.log(`wrote ${FIXTURES}/{content,catalog}.json${site ? ` and the site data in ${site}` : ""}`);
} finally {
  rmSync(tmp, { recursive: true, force: true });
}
