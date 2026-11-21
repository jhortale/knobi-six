// SPDX-License-Identifier: GPL-3.0-only
// The installer's catalog.json: the base catalog (packages/catalog/instruments.json + every synths/<id>/catalog.json)
// plus one synth's release, read from its .fwsc files and SHA256SUMS. The synth's package names, models and IDENT come
// from synths/<id>/synth.json. Release files only: anything else in the directory (-local, cpu1probe, dev builds)
// stops it. Every other synth's entry is kept, with its committed releases (synths/<id>/releases/<version>/) checked.
// Runs under plain node 24 (built-in TypeScript type stripping).
//   node packages/catalog/src/make-catalog.ts --synth knobi-six --version 1.0 --dir build --out dist/catalog.json \
//        --files dist/firmware/knobi-six/1.0 [--source SITE_PATH] [--date YYYY-MM-DD] [--repo-root DIR]
import { createHash } from "node:crypto";
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { basename, dirname, join } from "node:path";
import { validateCatalog } from "./load.ts";
import { checkReleaseFiles, loadBaseCatalog, readSynth, repoRootFrom } from "./node.ts";
import type { Catalog, Package } from "./schema.ts";

export interface MakeOpts {
  /** the repository root (with pnpm-workspace.yaml, packages/catalog/, synths/) */
  repoRoot: string;
  /** the synth being released: synths/<synth>/ */
  synth: string;
  version: string;
  /** the release's .fwsc files and SHA256SUMS */
  dir: string;
  /** where the packages are served, relative to the site: firmware/<synth>/<version> */
  filesPrefix: string;
  /** the GPL source archive, relative to the site */
  source?: string;
  date?: string;
}

const sha256 = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");

/** where another synth's committed release package lives in the repository */
export const committedPath = (repoRoot: string, synth: string, version: string, file: string) =>
  join(repoRoot, "synths", synth, "releases", version, basename(file));

export async function makeCatalog(o: MakeOpts): Promise<Catalog> {
  const meta = readSynth(o.repoRoot, o.synth);
  const cat = loadBaseCatalog(o.repoRoot);
  const packages: Package[] = checkReleaseFiles(o.repoRoot, o.synth, o.version, o.dir).map((p) => ({
    instrument: p.instrument, file: `${o.filesPrefix}/${p.name}`, identity: p.identity, sha256: p.sha256, size: p.size,
  }));
  for (const fw of cat.firmwares) {
    if (fw.id === meta.id) continue;
    for (const r of fw.releases) {
      // another synth's committed release: its files stay in its own folder on the site, and its GPL source is
      // published with it (a deploy replaces the whole site)
      const own = `firmware/${fw.id}/${r.version}/`;
      for (const f of [...r.packages.map((p) => p.file), ...(r.source ? [r.source] : [])]) {
        if (!f.startsWith(own) || f.slice(own.length).includes("/")) {
          throw new Error(`${fw.id} ${r.version}: ${f} must be directly under ${own}`);
        }
      }
      if (r.source && !existsSync(committedPath(o.repoRoot, fw.id, r.version, r.source))) {
        throw new Error(`${fw.id} ${r.version}: its source ${r.source} is missing from synths/${fw.id}/releases/${r.version}/`);
      }
      for (const p of r.packages) {
        const path = committedPath(o.repoRoot, fw.id, r.version, p.file);
        if (!existsSync(path) || sha256(new Uint8Array(readFileSync(path))) !== p.sha256) {
          throw new Error(`${fw.id} ${r.version}: ${p.file} is missing from synths/${fw.id}/releases/${r.version}/ ` +
            "or does not match its sha256");
        }
      }
    }
  }
  const mine = cat.firmwares.find((f) => f.id === meta.id);
  if (!mine) throw new Error(`synths/${meta.id}/catalog.json: missing`);
  mine.releases = [{
    version: o.version, date: o.date ?? new Date().toISOString().slice(0, 10),
    ...(o.source ? { source: o.source } : {}), packages,
  }];
  cat.generated = new Date().toISOString();
  return validateCatalog(cat);
}

function args(argv: string[]): Record<string, string> {
  const a: Record<string, string> = {};
  for (let i = 0; i < argv.length; i++) {
    if (argv[i].startsWith("--")) a[argv[i].slice(2)] = argv[i + 1]?.startsWith("--") || i + 1 >= argv.length ? "" : argv[++i];
  }
  return a;
}

async function main(argv: string[]) {
  const a = args(argv);
  if ("help" in a || !a.synth || !a.version || !a.dir || !a.out || !a.files) {
    console.log("usage: node packages/catalog/src/make-catalog.ts --synth ID --version X.Y --dir RELEASE_DIR " +
      "--out SITE/catalog.json --files SITE/firmware/ID/X.Y [--source SITE_PATH] [--date YYYY-MM-DD] [--repo-root DIR]");
    return "help" in a ? 0 : 2;
  }
  const repoRoot = a["repo-root"] || repoRootFrom(process.cwd());
  const cat = await makeCatalog({
    repoRoot, synth: a.synth, version: a.version, dir: a.dir, filesPrefix: `firmware/${a.synth}/${a.version}`,
    source: a.source || undefined, date: a.date || undefined,
  });
  const site = dirname(a.out);
  mkdirSync(a.files, { recursive: true });
  const mine = cat.firmwares.find((f) => f.id === a.synth)!.releases[0];
  for (const p of mine.packages) copyFileSync(join(a.dir, basename(p.file)), join(a.files, basename(p.file)));
  copyFileSync(join(a.dir, "SHA256SUMS"), join(a.files, "SHA256SUMS"));
  for (const fw of cat.firmwares) {
    if (fw.id === a.synth) continue;
    for (const r of fw.releases) {
      for (const f of [...r.packages.map((p) => p.file), ...(r.source ? [r.source] : [])]) {
        mkdirSync(dirname(join(site, f)), { recursive: true });
        copyFileSync(committedPath(repoRoot, fw.id, r.version, f), join(site, f));
      }
    }
  }
  writeFileSync(a.out, JSON.stringify(cat, null, 2) + "\n");
  console.log(`${a.out}: ${a.synth} ${a.version}, ${mine.packages.length} packages`);
  return 0;
}

// import.meta.main (Node 24): true when this file is the entry point, also when it was started through a symlink
// (node_modules/@knobi/catalog/...), where comparing import.meta.url with argv[1] silently did nothing
if (import.meta.main) {
  main(process.argv.slice(2)).then((c) => process.exit(c), (e) => {
    console.error(`make-catalog: ${(e as Error).message}`);
    process.exit(1);
  });
}
