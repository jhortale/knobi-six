// SPDX-License-Identifier: GPL-3.0-only
// knobi-flash's catalog.json from catalog/base.json and one KNOBI-SIX release (its .fwsc files and SHA256SUMS).
// Release files only: anything else in the directory (-local, cpu1probe, dev builds) stops it. Runs under plain
// node 24 (built-in TypeScript type stripping).
//   node scripts/make-catalog.ts --version 1.0 --dir ../build --out dist/catalog.json \
//        --files dist/firmware/knobi-six/1.0 [--base catalog/base.json] [--source PATH] [--date YYYY-MM-DD]
import { createHash } from "node:crypto";
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import { validateCatalog } from "../src/catalog/load.ts";
import type { Catalog, Package } from "../src/catalog/schema.ts";
import { modelOf } from "../src/protocol/identity.ts";
import { loadPackage } from "../src/protocol/package.ts";

export const FIRMWARE = "knobi-six";
export const IDENT = "956";
const SUFFIX: Record<string, string> = { "": "SMK-37 Pro", "-elite": "SMK-37 Elite", "-mke": "MKE-P37" };

export interface MakeOpts {
  base: unknown;
  version: string;
  /** the release's .fwsc files and SHA256SUMS */
  dir: string;
  /** where the packages are served, relative to the site: firmware/knobi-six/<version> */
  filesPrefix: string;
  /** the GPL source archive, relative to the site */
  source?: string;
  date?: string;
  /** committed files of other firmwares (web/public), checked against their catalog entries */
  publicDir: string;
}

const sha256 = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");
const esc = (s: string) => s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");

export async function makeCatalog(o: MakeOpts): Promise<Catalog> {
  const cat = validateCatalog(structuredClone(o.base));
  if (!/^\d+(\.\d+)*$/.test(o.version)) {
    throw new Error(`version ${o.version}: a prerelease or not a version; only X.Y release tags are published`);
  }
  const nameRe = new RegExp(`^${FIRMWARE}-${esc(o.version)}(-elite|-mke)?\\.fwsc$`);
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
    if (!nameRe.test(f)) throw new Error(`${f}: not a release package of ${FIRMWARE} ${o.version} (refused: never published)`);
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
    const want = `${SUFFIX[nameRe.exec(f)![1] ?? ""]}_${IDENT}`;
    if (product !== want) throw new Error(`${f}: identity ${product}, expected ${want}`);
    packages.push({ instrument: inst.id, file: `${o.filesPrefix}/${f}`, identity: product, sha256: h, size: raw.length });
  }
  if (!packages.length) throw new Error(`${o.dir}: no ${FIRMWARE} ${o.version} packages`);
  for (const fw of cat.firmwares) {
    if (fw.id === FIRMWARE) continue;
    for (const r of fw.releases) {
      for (const p of r.packages) {
        const path = join(o.publicDir, p.file);
        if (!existsSync(path) || sha256(new Uint8Array(readFileSync(path))) !== p.sha256) {
          throw new Error(`${fw.id} ${r.version}: ${p.file} is missing from ${o.publicDir} or does not match its sha256`);
        }
      }
    }
  }
  const knobi = cat.firmwares.find((f) => f.id === FIRMWARE);
  if (!knobi) throw new Error(`base catalog: no firmware "${FIRMWARE}"`);
  knobi.releases = [{
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
  if ("help" in a || !a.version || !a.dir || !a.out || !a.files) {
    console.log("usage: node scripts/make-catalog.ts --version X.Y --dir RELEASE_DIR --out dist/catalog.json " +
      "--files dist/firmware/knobi-six/X.Y [--base catalog/base.json] [--source SITE_PATH] [--date YYYY-MM-DD] " +
      "[--public public]");
    return "help" in a ? 0 : 2;
  }
  const filesPrefix = `firmware/${FIRMWARE}/${a.version}`;
  const cat = await makeCatalog({
    base: JSON.parse(readFileSync(a.base || "catalog/base.json", "utf8")), version: a.version, dir: a.dir,
    filesPrefix, source: a.source || undefined, date: a.date || undefined, publicDir: a.public || "public",
  });
  mkdirSync(a.files, { recursive: true });
  for (const p of cat.firmwares.find((f) => f.id === FIRMWARE)!.releases[0].packages) {
    const name = p.file.split("/").pop()!;
    copyFileSync(join(a.dir, name), join(a.files, name));
  }
  copyFileSync(join(a.dir, "SHA256SUMS"), join(a.files, "SHA256SUMS"));
  writeFileSync(a.out, JSON.stringify(cat, null, 2) + "\n");
  console.log(`${a.out}: ${FIRMWARE} ${a.version}, ${cat.firmwares.find((f) => f.id === FIRMWARE)!.releases[0].packages.length} packages`);
  return 0;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? "").href) {
  main(process.argv.slice(2)).then((c) => process.exit(c), (e) => {
    console.error(`make-catalog: ${(e as Error).message}`);
    process.exit(1);
  });
}
