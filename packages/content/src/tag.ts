// SPDX-License-Identifier: GPL-3.0-only
// No imports: release.yml's meta job runs this before any install.

/** `<synth>-v<version>` (a release only: no `-` after the version, so never a prerelease). */
export function parseTag(tag: string): { synth: string; version: string } {
  const m = /^([a-z0-9][a-z0-9-]*)-v(\d+(?:\.\d+)+)$/.exec(tag);
  if (!m) throw new Error(`${tag}: not a release tag (<synth>-v<version>, e.g. knobi-six-v1.0; prereleases are never published)`);
  return { synth: m[1]!, version: m[2]! };
}
