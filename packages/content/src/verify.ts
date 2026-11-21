// SPDX-License-Identifier: GPL-3.0-only
// What a release must be before its file reaches the repository: the tag, who uploaded the assets, and the files
// themselves (the rules are @knobi/catalog's, shared with the site build). Node only.
import { createHash } from "node:crypto";
import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { checkReleaseFiles } from "@knobi/catalog/node";
import { parseTag } from "./tag.ts";
import type { KeyboardBuild, ReleaseSource } from "./types.ts";

export { parseTag };

const BOT = "github-actions[bot]";

/** Every release asset must have been uploaded by the workflow's own token. */
export function verifyAssets(assets: { name: string; uploader: { login: string } }[]): void {
  for (const a of assets) {
    const login = a.uploader?.login;
    if (login !== BOT) throw new Error(`${a.name}: uploaded by ${login ?? "unknown"}, not ${BOT} (refused)`);
  }
}

/** The packages and the GPL source in a downloaded release directory, checked; returns what the release file records. */
export async function verifyReleaseDir(
  repoRoot: string, synth: string, version: string, dir: string,
): Promise<{ builds: KeyboardBuild[]; source: ReleaseSource }> {
  const builds: KeyboardBuild[] = checkReleaseFiles(repoRoot, synth, version, dir).map((p) => ({
    kind: "keyboard", instrument: p.instrument, identity: p.identity, file: p.name, sha256: p.sha256, size: p.size,
  }));
  const file = `${synth}-${version}-src.tar.gz`;
  const path = join(dir, file);
  if (!existsSync(path)) throw new Error(`${file}: the source archive is missing from ${dir}`);
  const raw = readFileSync(path);
  return { builds, source: { file, sha256: createHash("sha256").update(raw).digest("hex"), size: raw.length } };
}
