// SPDX-License-Identifier: GPL-3.0-only
// The Node-only side of @knobi/catalog: the repository's synths (synths/<id>/synth.json and catalog.json) and the base
// catalog built from them. Every error names the file it is about.
import { createHash } from "node:crypto";
import { existsSync, readFileSync, readdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { loadPackage, modelOf } from "@knobi/installer-core";
import { mergeCatalog } from "./load.ts";
import type { Catalog } from "./schema.ts";

export interface SynthMeta {
  id: string;
  name: string;
  ident: string;
  /** the release package's file name: {version} and {suffix} are replaced */
  packageName: string;
  /** file-name suffix -> the model (identity prefix) that package is for */
  models: Record<string, string>;
  platforms: Record<string, Record<string, string>>;
}

/** the repository root: the nearest directory upwards with pnpm-workspace.yaml */
export function repoRootFrom(start: string): string {
  for (let d = start; ; d = dirname(d)) {
    if (existsSync(join(d, "pnpm-workspace.yaml"))) return d;
    if (dirname(d) === d) throw new Error(`no pnpm-workspace.yaml above ${start}`);
  }
}

export function listSynths(root: string): string[] {
  const dir = join(root, "synths");
  if (!existsSync(dir)) return [];
  return readdirSync(dir, { withFileTypes: true })
    .filter((e) => e.isDirectory() && existsSync(join(dir, e.name, "catalog.json")))
    .map((e) => e.name).sort();
}

function readJson(root: string, rel: string): unknown {
  const path = join(root, rel);
  if (!existsSync(path)) throw new Error(`${rel}: missing`);
  try {
    return JSON.parse(readFileSync(path, "utf8"));
  } catch (e) {
    throw new Error(`${rel}: not valid JSON (${(e as Error).message})`);
  }
}

const isStr = (v: unknown): v is string => typeof v === "string" && v.length > 0;
const isObj = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);

/** synths/<id>/synth.json, checked */
export function readSynth(root: string, id: string): SynthMeta {
  const rel = `synths/${id}/synth.json`;
  const j = readJson(root, rel);
  if (!isObj(j)) throw new Error(`${rel}: not an object`);
  const errs: string[] = [];
  for (const f of ["id", "name", "ident", "packageName"] as const) if (!isStr(j[f])) errs.push(`${f} missing`);
  if (isStr(j.id) && j.id !== id) errs.push(`id "${j.id}" is not its folder's ("${id}")`);
  if (isStr(j.ident) && !/^\d+$/.test(j.ident)) errs.push(`ident "${j.ident}" is not digits`);
  if (isStr(j.packageName) && !j.packageName.includes("{version}")) errs.push("packageName has no {version}");
  if (!isObj(j.models) || !Object.values(j.models).every(isStr) || !Object.keys(j.models).length) {
    errs.push("models missing (file-name suffix -> model)");
  }
  if (!isObj(j.platforms)) errs.push("platforms missing");
  if (errs.length) throw new Error(`${rel}: ${errs.join("; ")}`);
  return j as unknown as SynthMeta;
}

/** packages/catalog/instruments.json plus every synths/<id>/catalog.json, validated */
export function loadBaseCatalog(root: string): Catalog {
  const instruments = readJson(root, "packages/catalog/instruments.json");
  const firmwares = listSynths(root).map((id) => {
    const rel = `synths/${id}/catalog.json`;
    const fw = readJson(root, rel);
    if (!isObj(fw) || fw.id !== id) throw new Error(`${rel}: its id ${JSON.stringify(isObj(fw) ? fw.id : fw)} is not its folder's ("${id}")`);
    try {
      mergeCatalog(instruments, [fw]);
    } catch (e) {
      throw new Error(`${rel}: ${(e as Error).message}`);
    }
    return fw;
  });
  return mergeCatalog(instruments, firmwares);
}

/** One verified release package: the file in the release directory and what the catalog knows about it. */
export interface CheckedPackage {
  /** the file name inside the release directory */
  name: string;
  /** the instrument id in the base catalog */
  instrument: string;
  /** the package's product, e.g. "SMK-37 Pro_956" */
  identity: string;
  sha256: string;
  size: number;
}

const esc = (s: string) => s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");

/**
 * The release rules for one synth's directory of files: only X.Y versions; every .fwsc named as synth.json says
 * (never -local, cpu1 or a dev build), with a matching SHA256SUMS line, passing loadPackage (identity, loader mark) and
 * carrying the synth's identity. The -src.tar.gz must be named <synth>-<version>-src.tar.gz and match its SHA256SUMS line. Throws a message naming the file.
 */
export function checkReleaseFiles(repoRoot: string, synth: string, version: string, dir: string): CheckedPackage[] {
  const meta = readSynth(repoRoot, synth);
  const cat = loadBaseCatalog(repoRoot);
  if (!/^\d+(\.\d+)*$/.test(version)) {
    throw new Error(`version ${version}: a prerelease or not a version; only X.Y release tags are published`);
  }
  const suffixes = Object.keys(meta.models).sort((a, b) => b.length - a.length).map(esc).join("|");
  const [pre, post] = meta.packageName.replace("{version}", "\u0000").split("\u0000");
  const nameRe = new RegExp(`^${esc(pre).replace("\\{suffix\\}", "")}${esc(version)}` +
    `${esc(post).replace("\\{suffix\\}", `(${suffixes})`)}$`);
  const sums = new Map<string, string>();
  const sumsPath = join(dir, "SHA256SUMS");
  if (existsSync(sumsPath)) {
    for (const line of readFileSync(sumsPath, "utf8").split("\n")) {
      const m = /^([0-9a-f]{64})\s+\*?(.+)$/.exec(line.trim());
      if (m) sums.set(m[2], m[1]);
    }
  }
  const out: CheckedPackage[] = [];
  for (const f of readdirSync(dir).sort()) {
    if (f === "SHA256SUMS") continue;
    if (f.endsWith("-src.tar.gz")) {
      // the GPL source: only `<synth>-<version>-src.tar.gz`, and its hash must be in SHA256SUMS like the packages'
      if (f !== `${meta.id}-${version}-src.tar.gz`) {
        throw new Error(`${f}: not the source archive of ${meta.id} ${version} (${meta.id}-${version}-src.tar.gz; refused)`);
      }
      const h = createHash("sha256").update(readFileSync(join(dir, f))).digest("hex");
      if (sums.get(f) !== h) throw new Error(`${f}: its SHA256SUMS line is missing or does not match`);
      continue;
    }
    const m = nameRe.exec(f);
    if (!m) throw new Error(`${f}: not a release package of ${meta.id} ${version} (refused: never published)`);
    const raw = new Uint8Array(readFileSync(join(dir, f)));
    const h = createHash("sha256").update(raw).digest("hex");
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
    out.push({ name: f, instrument: inst.id, identity: product, sha256: h, size: raw.length });
  }
  if (!out.length) throw new Error(`${dir}: no ${meta.id} ${version} packages`);
  return out;
}
