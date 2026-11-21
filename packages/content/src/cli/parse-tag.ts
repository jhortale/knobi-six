// SPDX-License-Identifier: GPL-3.0-only
// Prints `synth=`, `version=` and `ok=` lines for $GITHUB_OUTPUT (release.yml's meta job); ok=false, never an error, for
// a tag that is not a release (a prerelease such as knobi-six-v1.0-rc1). Plain node, no install needed.
//   node packages/content/src/cli/parse-tag.ts knobi-six-v1.0 >> "$GITHUB_OUTPUT"
import { parseTag } from "../tag.ts";

if (import.meta.main) {
  try {
    const { synth, version } = parseTag(process.argv[2] ?? "");
    console.log(`synth=${synth}\nversion=${version}\nok=true`);
  } catch (e) {
    console.error(`not published: ${(e as Error).message}`);
    console.log("ok=false");
  }
}
