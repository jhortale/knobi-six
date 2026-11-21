// SPDX-License-Identifier: GPL-3.0-only
// The site builder: re-checks every release file against its downloaded GitHub release, copies the files to
// firmware/<synth>/<version>/, and writes catalog.json (the installer's, schema 1) and content.json (the portal's).
// Node only.
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, writeFileSync } from "node:fs";
import { basename, dirname, join, relative, resolve, sep } from "node:path";
import { isDeepStrictEqual } from "node:util";
import { compareVersions, validateCatalog } from "@knobi/catalog";
import type { Catalog, Package, Release } from "@knobi/catalog";
import { loadBaseCatalog, readSynth } from "@knobi/catalog/node";
import type { Content, PortalContent, PortalRelease, PortalSynth, ReleaseDoc } from "./types.ts";
import { verifyAssets, verifyReleaseDir } from "./verify.ts";

export interface AssetInfo {
  name: string;
  uploader: { login: string };
}

export interface SiteOpts {
  repoRoot: string;
  content: Content;
  /** release tag -> the directory its assets were downloaded to */
  releaseDirs: Record<string, string>;
  /** release tag -> the release's asset list (name, uploader) */
  assets: Record<string, AssetInfo[]>;
  outDir: string;
}

const fail = (r: ReleaseDoc, msg: string): never => {
  throw new Error(`${r.synth} ${r.version}: ${msg}`);
};

/** Newest first, by numeric version order (never string order: 10.0 is newer than 2.0). */
const newestFirst = (a: ReleaseDoc, b: ReleaseDoc) => compareVersions(b.version, a.version);

/** Verifies one release with the same checks as the release PR, then compares what its file records with what was found. */
async function verifyRelease(o: SiteOpts, r: ReleaseDoc): Promise<string> {
  const dir = o.releaseDirs[r.tag];
  if (!dir) return fail(r, `no downloaded release directory for ${r.tag}`);
  const assets = o.assets[r.tag];
  if (!assets) return fail(r, `no asset list for ${r.tag}`);
  verifyAssets(assets);
  const found = await verifyReleaseDir(o.repoRoot, r.synth, r.version, dir);
  const desktop = r.builds.find((b) => b.kind !== "keyboard");
  if (desktop) fail(r, `${desktop.file}: desktop builds cannot be verified yet (refused)`);
  const recorded = new Map(r.builds.map((b) => [b.file, b]));
  for (const f of found.builds) {
    const b = recorded.get(f.file);
    if (!b) fail(r, `${f.file}: in the release but not in its release file`);
    for (const k of ["kind", "instrument", "identity", "sha256", "size"] as const) {
      if (!isDeepStrictEqual((b as unknown as Record<string, unknown>)[k], f[k])) {
        fail(r, `${f.file}: ${k} is ${JSON.stringify((b as unknown as Record<string, unknown>)[k])} in the release file, ` +
          `${JSON.stringify(f[k])} in the release`);
      }
    }
    recorded.delete(f.file);
  }
  for (const file of recorded.keys()) fail(r, `${file}: in the release file but not in the release`);
  for (const k of ["file", "sha256", "size"] as const) {
    if (r.source[k] !== found.source[k]) {
      fail(r, `${found.source.file}: source ${k} is ${JSON.stringify(r.source[k])} in the release file, ` +
        `${JSON.stringify(found.source[k])} in the release`);
    }
  }
  return dir;
}

/** The release of a synth that is current: the one marked so (more than one is an error), else the newest. */
function currentOf(synth: string, rels: ReleaseDoc[]): ReleaseDoc | undefined {
  const marked = rels.filter((r) => r.current === true);
  if (marked.length > 1) {
    throw new Error(`${synth}: more than one release is current (${marked.map((r) => r.version).sort(compareVersions).join(", ")})`);
  }
  return marked[0] ?? [...rels].sort(newestFirst)[0];
}

const copyInto = (from: string, to: string) => {
  mkdirSync(dirname(to), { recursive: true });
  copyFileSync(from, to);
};

/** Refuses an output directory that holds anything: whatever is in it would be published without being checked. */
export function assertEmptyOut(outDir: string): void {
  if (existsSync(outDir) && readdirSync(outDir).length > 0) {
    throw new Error(`${outDir}: exists and is not empty (refused: files already in it would reach the site unchecked)`);
  }
}

export async function buildSiteData(o: SiteOpts): Promise<{ catalog: Catalog; portal: PortalContent }> {
  assertEmptyOut(o.outDir);
  const catalog = loadBaseCatalog(o.repoRoot);
  const generated = new Date().toISOString();
  catalog.generated = generated;
  const synths: PortalSynth[] = [];

  for (const synth of o.content.synths) {
    const rels = o.content.releases.filter((r) => r.synth === synth.id).sort(newestFirst);
    const current = currentOf(synth.id, rels);
    const fw = catalog.firmwares.find((f) => f.id === synth.id);
    if (rels.length && !fw) throw new Error(`synths/${synth.id}/catalog.json: missing (the synth has releases)`);
    if (rels.length) readSynth(o.repoRoot, synth.id); // synth.json must exist and be valid
    const portalReleases: PortalRelease[] = [];
    const catalogReleases: Release[] = [];

    for (const r of rels) {
      const dir = await verifyRelease(o, r);
      const prefix = `firmware/${r.synth}/${r.version}`;
      const dest = join(o.outDir, prefix);
      for (const f of [...r.builds.map((b) => b.file), r.source.file, "SHA256SUMS"]) copyInto(join(dir, f), join(dest, f));
      const packages: Package[] = r.builds.map((b) => {
        if (b.kind !== "keyboard") throw new Error("unreachable");
        return { instrument: b.instrument, file: `${prefix}/${b.file}`, identity: b.identity, sha256: b.sha256, size: b.size };
      });
      catalogReleases.push({ version: r.version, date: r.date, source: `${prefix}/${r.source.file}`, packages });
      portalReleases.push({
        version: r.version, date: r.date, tag: r.tag, current: r === current, notesHtml: r.notesHtml,
        source: { ...r.source, url: `${prefix}/${r.source.file}` },
        builds: r.builds.map((b) => ({ ...b, url: `${prefix}/${b.file}` })),
      });
    }
    if (fw) fw.releases = catalogReleases;

    const out: PortalSynth = {
      id: synth.id, name: synth.name, summary: synth.summary, order: synth.order,
      descriptionHtml: synth.descriptionHtml,
      keyboards: [...new Set(rels.flatMap((r) => r.builds.flatMap((b) => (b.kind === "keyboard" ? [b.instrument] : []))))].sort(),
      releases: portalReleases,
    };
    if (synth.image) {
      const synthDir = resolve(o.repoRoot, "synths", synth.id);
      const from = resolve(synthDir, synth.image);
      if (!from.startsWith(synthDir + sep)) throw new Error(`synths/${synth.id}/synth.md: image ${synth.image} is outside the synth's folder`);
      if (!existsSync(from)) throw new Error(`synths/${synth.id}/synth.md: image ${synth.image} is missing`);
      copyInto(from, join(o.outDir, "synths", synth.id, basename(from)));
      out.image = `synths/${synth.id}/${basename(from)}`;
    }
    synths.push(out);
  }

  validateCatalog(catalog);
  const portal: PortalContent = { generated, synths };
  mkdirSync(o.outDir, { recursive: true });
  writeFileSync(join(o.outDir, "catalog.json"), JSON.stringify(catalog, null, 2) + "\n");
  writeFileSync(join(o.outDir, "content.json"), JSON.stringify(portal, null, 2) + "\n");
  return { catalog, portal };
}

function walk(dir: string, root = dir): string[] {
  return readdirSync(dir, { withFileTypes: true }).flatMap((e) => {
    const p = join(dir, e.name);
    return e.isDirectory() ? walk(p, root) : [relative(root, p).split(sep).join("/")];
  });
}

const TOKEN_MARKS = ["ghp_", "github_pat_", "-----BEGIN"];

/** Files under `dir` that contain a token prefix, a PEM header, or one of `secrets`: "path: what". Never prints a secret. */
export function scanForTokens(dir: string, secrets: string[]): string[] {
  const marks = [...TOKEN_MARKS.map((m) => ({ needle: Buffer.from(m), label: `contains ${m}` })),
    ...secrets.filter((s) => s.length > 0).map((s) => ({ needle: Buffer.from(s), label: "contains a secret value from the environment" }))];
  const hits: string[] = [];
  for (const f of walk(dir)) {
    const raw = readFileSync(join(dir, f));
    for (const m of marks) if (raw.includes(m.needle)) hits.push(`${f}: ${m.label}`);
  }
  return hits;
}

/** `-local` and `cpu1` files (dev builds) under `dir`: never published. */
export function findForbiddenFiles(dir: string): string[] {
  return walk(dir).filter((f) => /-local|cpu1/i.test(basename(f)));
}
