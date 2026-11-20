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
import { pathToFileURL } from "node:url";
import { loadPackage, modelOf } from "@knobi/installer-core";
import { validateCatalog } from "./load.ts";
import { loadBaseCatalog, readSynth, repoRootFrom } from "./node.ts";
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
const esc = (s: string) => s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");

/** where another synth's committed release package lives in the repository */
export const committedPath = (repoRoot: string, synth: string, version: string, file: string) =>
  join(repoRoot, "synths", synth, "releases", version, basename(file));

export async function makeCatalog(o: MakeOpts): Promise<Catalog> {
  const meta = readSynth(o.repoRoot, o.synth);
  const cat = loadBaseCatalog(o.repoRoot);
  if (!/^\d+(\.\d+)*$/.test(o.version)) {
    throw new Error(`version ${o.version}: a prerelease or not a version; only X.Y release tags are published`);
  }
  const suffixes = Object.keys(meta.models).sort((a, b) => b.length - a.length).map(esc).join("|");
  const [pre, post] = meta.packageName.replace("{version}", "\u0000").split("\u0000");
  const nameRe = new RegExp(`^${esc(pre).replace("\\{suffix\\}", "")}${esc(o.version)}` +
    `${esc(post).replace("\\{suffix\\}", `(${suffixes})`)}$`);
  const sums = new Map<string, string>();
  const sumsPath = join(o.dir, "SHA256SUMS");
  if (existsSync(sumsPath)) {
    for (const line of readFileSync(sumsPath, "utf8").split("\n")) {
      const m = /^([0-9a-f]{64})\s+\*?(.+)$/.exec(line.trim());
      if (m) sums.set(m[2], m[1]);
    }
  }
  const packages: Package[] = [];
  for (const f of readdirSync(o.dir).sort()) {
    if (f === "SHA256SUMS" || f.endsWith("-src.tar.gz")) continue;
    const m = nameRe.exec(f);
    if (!m) throw new Error(`${f}: not a release package of ${meta.id} ${o.version} (refused: never published)`);
    const raw = new Uint8Array(readFileSync(join(o.dir, f)));
    const h = sha256(raw);
    if (sums.get(f) !== h) throw new Error(`${f}: its SHA256SUMS line is missing or does not match`);
    let product: string;
    try {
      product = loadPackage(raw).product;
    } catch (e) {
      throw new Error(`${f}: ${(e as Error).message}`);
    }
    const model = modelOf(product);
    const inst = cat.instruments.find((i) => i.model === model);
    if (!inst) throw new Error(`${f}: ${product}: no instrument in the catalog for ${model}`);
    const want = `${meta.models[m[1] ?? ""]}_${meta.ident}`;
    if (product !== want) throw new Error(`${f}: identity ${product}, expected ${want}`);
    packages.push({ instrument: inst.id, file: `${o.filesPrefix}/${f}`, identity: product, sha256: h, size: raw.length });
  }
  if (!packages.length) throw new Error(`${o.dir}: no ${meta.id} ${o.version} packages`);
  for (const fw of cat.firmwares) {
    if (fw.id === meta.id) continue;
    for (const r of fw.releases) {
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
      for (const p of r.packages) {
        mkdirSync(dirname(join(site, p.file)), { recursive: true });
        copyFileSync(committedPath(repoRoot, fw.id, r.version, p.file), join(site, p.file));
      }
    }
  }
  writeFileSync(a.out, JSON.stringify(cat, null, 2) + "\n");
  console.log(`${a.out}: ${a.synth} ${a.version}, ${mine.packages.length} packages`);
  return 0;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? "").href) {
  main(process.argv.slice(2)).then((c) => process.exit(c), (e) => {
    console.error(`make-catalog: ${(e as Error).message}`);
    process.exit(1);
  });
}
