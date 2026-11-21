// SPDX-License-Identifier: GPL-3.0-only
// The made-up KNOBI-SIX releases behind the fixtures (content.json, catalog.json: scripts/make-fixtures.ts), the
// tests and the dev server's ?mock page. Never part of a build: the site's real data comes from the site builder.
import { makePackage } from "@knobi/device-sim";

export const FIXTURE_RELEASES = [
  { version: "0.9", date: "2026-11-14", seed: 9, notes: "Beta for testers." },
  { version: "1.0", date: "2026-11-20", seed: 7, current: true, notes:
    "- First public release: six voices, arpeggiator, 64-step sequencer.\n- Programs survive reinstalls of the same version.\n" },
] as const;

const MODELS = { "": "SMK-37 Pro", "-elite": "SMK-37 Elite", "-mke": "MKE-P37" } as const;

/** the files of one release as release.yml publishes them (SHA256SUMS aside), by file name */
export function fixtureFiles(version: string): Record<string, Uint8Array | string> {
  const r = FIXTURE_RELEASES.find((x) => x.version === version);
  if (!r) throw new Error(`no fixture release ${version}`);
  const files: Record<string, Uint8Array | string> = {};
  for (const [suffix, model] of Object.entries(MODELS)) {
    files[`knobi-six-${version}${suffix}.fwsc`] = makePackage({ product: `${model}_956`, seed: r.seed });
  }
  files[`knobi-six-${version}-src.tar.gz`] = `the GPL source of KNOBI-SIX ${version} (fixture)`;
  return files;
}

/** a site path (firmware/knobi-six/<version>/<file>) -> the fixture file's bytes, or undefined */
export function fixtureFile(path: string): Uint8Array | string | undefined {
  const m = /^firmware\/knobi-six\/([^/]+)\/([^/]+)$/.exec(path);
  if (!m || !FIXTURE_RELEASES.some((r) => r.version === m[1])) return undefined;
  return fixtureFiles(m[1]!)[m[2]!];
}
