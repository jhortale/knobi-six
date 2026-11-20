// SPDX-License-Identifier: GPL-3.0-only
// The Node-only side of @knobi/catalog: the repository's synths (synths/<id>/synth.json and catalog.json) and the base
// catalog built from them. Every error names the file it is about.
import { existsSync, readFileSync, readdirSync } from "node:fs";
import { dirname, join } from "node:path";
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
