import { existsSync, readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { splitFrontmatter } from "./frontmatter.ts";
import { renderMarkdown } from "./markdown.ts";
import type { Build, Content, ReleaseDoc, SynthDoc } from "./types.ts";

type Obj = Record<string, unknown>;

class FieldError extends Error {}

function bad(field: string, want: string): never {
  throw new FieldError(`${field}: expected ${want}`);
}

const str = (o: Obj, k: string, path = k): string =>
  typeof o[k] === "string" && o[k] !== "" ? (o[k] as string) : bad(path, "a non-empty string");
const num = (o: Obj, k: string, path = k): number =>
  typeof o[k] === "number" && Number.isFinite(o[k]) ? (o[k] as number) : bad(path, "a number");
const sha = (o: Obj, k: string, path: string): string => {
  const v = str(o, k, path);
  return /^[0-9a-f]{64}$/.test(v) ? v : bad(path, "a 64-character lowercase hex SHA-256");
};
const obj = (v: unknown, path: string): Obj =>
  v !== null && typeof v === "object" && !Array.isArray(v) ? (v as Obj) : bad(path, "a mapping");

function parseBuild(v: unknown, i: number): Build {
  const p = (f: string) => `builds[${i}].${f}`;
  const b = obj(v, `builds[${i}]`);
  const common = { file: str(b, "file", p("file")), sha256: sha(b, "sha256", p("sha256")), size: num(b, "size", p("size")) };
  if (b.kind === "keyboard") {
    return { kind: "keyboard", instrument: str(b, "instrument", p("instrument")), identity: str(b, "identity", p("identity")), ...common };
  }
  if (b.kind === "desktop") {
    const format = b.format === "vst3" || b.format === "au" ? b.format : bad(p("format"), "vst3 or au");
    const os = b.os === "mac" || b.os === "windows" ? b.os : bad(p("os"), "mac or windows");
    return { kind: "desktop", format, os, arch: str(b, "arch", p("arch")), ...common };
  }
  return bad(p("kind"), "keyboard or desktop");
}

function parseSynth(id: string, text: string): SynthDoc | undefined {
  const { data, body } = splitFrontmatter(text);
  const origin = data.origin === "own" || data.origin === "external" ? data.origin : bad("origin", "own or external");
  if (origin === "external") return undefined; // reserved, not shown
  const synth: SynthDoc = {
    id,
    name: str(data, "name"),
    summary: str(data, "summary"),
    order: num(data, "order"),
    origin,
    descriptionHtml: renderMarkdown(body),
  };
  if (data.image !== undefined) synth.image = str(data, "image");
  return synth;
}

function parseRelease(synth: string, fileVersion: string, text: string): ReleaseDoc {
  const { data, body } = splitFrontmatter(text);
  const version = str(data, "version");
  if (version !== fileVersion) bad("version", `"${fileVersion}" (the file name)`);
  const tag = str(data, "tag");
  if (tag !== `${synth}-v${version}`) bad("tag", `"${synth}-v${version}"`);
  const source = obj(data.source, "source");
  if (!Array.isArray(data.builds)) bad("builds", "a list");
  const release: ReleaseDoc = {
    synth,
    version,
    date: str(data, "date"),
    tag,
    source: { file: str(source, "file", "source.file"), sha256: sha(source, "sha256", "source.sha256"), size: num(source, "size", "source.size") },
    builds: (data.builds as unknown[]).map(parseBuild),
    notesHtml: renderMarkdown(body),
  };
  if (data.current !== undefined) release.current = typeof data.current === "boolean" ? data.current : bad("current", "a boolean");
  return release;
}

function guarded<T>(file: string, fn: () => T): T {
  try {
    return fn();
  } catch (e) {
    throw new Error(`${file}: ${(e as Error).message}`);
  }
}

/** Reads every `synths/<id>/synth.md` (origin own, sorted by order) and `synths/<id>/releases/*.md`. Throws naming the file and field. */
export function readContent(repoRoot: string): Content {
  const synthsDir = join(repoRoot, "synths");
  const synths: SynthDoc[] = [];
  const releases: ReleaseDoc[] = [];
  if (!existsSync(synthsDir)) return { synths, releases };
  for (const id of readdirSync(synthsDir, { withFileTypes: true }).filter((d) => d.isDirectory()).map((d) => d.name).sort()) {
    const synthFile = join(synthsDir, id, "synth.md");
    if (!existsSync(synthFile)) continue;
    const rel = `synths/${id}/synth.md`;
    const synth = guarded(rel, () => parseSynth(id, readFileSync(synthFile, "utf8")));
    if (!synth) continue;
    synths.push(synth);
    const relDir = join(synthsDir, id, "releases");
    if (!existsSync(relDir)) continue;
    for (const f of readdirSync(relDir).filter((n) => n.endsWith(".md")).sort()) {
      releases.push(guarded(`synths/${id}/releases/${f}`, () => parseRelease(id, f.slice(0, -3), readFileSync(join(relDir, f), "utf8"))));
    }
  }
  synths.sort((a, b) => a.order - b.order || a.id.localeCompare(b.id));
  return { synths, releases };
}
