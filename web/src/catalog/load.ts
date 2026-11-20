// SPDX-License-Identifier: GPL-3.0-only
// Loading, checking and querying catalog.json.
import { modelOf, sameKeyboard } from "../protocol/identity.ts";
import type { Catalog, Firmware, Instrument, Package, Release } from "./schema.ts";

const isObj = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);
const anchored = (src: string) => new RegExp(`^(?:${src})$`, "i");

/** the catalog, or an Error listing every problem in it */
export function validateCatalog(json: unknown): Catalog {
  if (!isObj(json)) throw new Error("catalog: not an object");
  const errs: string[] = [];
  const c = json as unknown as Catalog;
  if (c.schema !== 1) errs.push(`catalog: schema ${String(c.schema)}, this page reads schema 1`);
  const re = (where: string, src: unknown) => {
    try {
      if (typeof src !== "string") throw new Error();
      new RegExp(src);
    } catch {
      errs.push(`${where}: not a regular expression`);
    }
  };
  if (!isObj(c.protocols) || !isObj(c.families) || !Array.isArray(c.instruments) || !Array.isArray(c.firmwares)) {
    errs.push("catalog: protocols, families, instruments and firmwares are required");
    throw new Error(errs.join("\n"));
  }
  for (const [id, p] of Object.entries(c.protocols)) re(`protocol "${id}" ownLoader`, p?.ownLoader);
  for (const [id, f] of Object.entries(c.families)) {
    re(`family "${id}" match`, f?.match);
    re(`family "${id}" ports`, f?.ports);
    if (!Array.isArray(f?.models)) errs.push(`family "${id}": models missing`);
  }
  const inst = new Map<string, Instrument>();
  for (const i of c.instruments) {
    if (inst.has(i.id)) errs.push(`instrument "${i.id}": duplicate id`);
    inst.set(i.id, i);
    if (!(i.family in c.families)) errs.push(`instrument "${i.id}": unknown family "${i.family}"`);
    if (!(i.protocol in c.protocols)) errs.push(`instrument "${i.id}": unknown protocol "${i.protocol}"`);
  }
  const fw = new Set<string>();
  for (const f of c.firmwares) {
    if (fw.has(f.id)) errs.push(`firmware "${f.id}": duplicate id`);
    fw.add(f.id);
    if (!Array.isArray(f.releases)) {
      errs.push(`firmware "${f.id}": releases missing`);
      continue;
    }
    for (const r of f.releases) {
      for (const p of r.packages ?? []) {
        const where = `firmware "${f.id}" ${r.version} ${p.file}`;
        const i = inst.get(p.instrument);
        if (!i) errs.push(`${where}: unknown instrument "${p.instrument}"`);
        if (!/^[0-9a-f]{64}$/.test(p.sha256 ?? "")) errs.push(`${where}: sha256 is not 64 hex digits`);
        if (!Number.isInteger(p.size) || p.size <= 0) errs.push(`${where}: size`);
        if (i && !new RegExp(`^${i.model.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")}_\\d+$`).test(p.identity ?? "")) {
          errs.push(`${where}: identity "${p.identity}" is not ${i.model}_NNN`);
        }
      }
    }
  }
  if (errs.length) throw new Error(errs.join("\n"));
  return c;
}

export async function fetchCatalog(url = "catalog.json", fetcher: typeof fetch = fetch): Promise<Catalog> {
  const r = await fetcher(url, { cache: "no-cache" });
  if (!r.ok) throw new Error(`catalog: ${url}: HTTP ${r.status}`);
  return validateCatalog(await r.json());
}

export function familyRe(c: Catalog, familyId: string): RegExp {
  return anchored(c.families[familyId].match);
}

export function ownLoaderRe(c: Catalog, protocolId: string): RegExp {
  return anchored(c.protocols[protocolId].ownLoader);
}

/** MIDI ports worth probing: any family's pattern, anywhere in the name */
export function portMatcher(c: Catalog): RegExp {
  return new RegExp(Object.values(c.families).map((f) => `(?:${f.ports})`).join("|"), "i");
}

export interface Offer {
  firmware: Firmware;
  release: Release;
  pkg: Package;
  instrument: Instrument;
  /** the package is for the device's own model */
  exact: boolean;
}

const versionKey = (v: string) => v.split(/[.-]/).map((x) => (/^\d+$/.test(x) ? parseInt(x, 10) : 0));
export function compareVersions(a: string, b: string): number {
  const ka = versionKey(a), kb = versionKey(b);
  for (let i = 0; i < Math.max(ka.length, kb.length); i++) {
    const d = (ka[i] ?? 0) - (kb[i] ?? 0);
    if (d) return d;
  }
  return 0;
}

/** the packages a device with this identity can take: its own model first, newest release first; then the
 *  family's other models (the same hardware under another name) */
export function packagesFor(c: Catalog, identityText: string): Offer[] {
  const model = modelOf(identityText);
  const out: Offer[] = [];
  for (const firmware of c.firmwares) {
    for (const release of firmware.releases) {
      for (const pkg of release.packages) {
        const instrument = c.instruments.find((i) => i.id === pkg.instrument)!;
        const exact = instrument.model === model;
        if (exact || sameKeyboard(model, instrument.model, familyRe(c, instrument.family))) {
          out.push({ firmware, release, pkg, instrument, exact });
        }
      }
    }
  }
  return out.sort((a, b) => Number(b.exact) - Number(a.exact) || compareVersions(b.release.version, a.release.version));
}
