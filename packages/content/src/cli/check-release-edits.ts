// SPDX-License-Identifier: GPL-3.0-only
// Compares two versions of a release file and fails when a CI-owned frontmatter field changed (only the notes and
// `current` may). Used by .github/scripts/check-release-edits.sh:
//   node check-release-edits.ts BEFORE_FILE AFTER_FILE
import { readFileSync } from "node:fs";
import { ciFieldsChanged } from "../release-file.ts";

if (import.meta.main) {
  const [before, after] = process.argv.slice(2);
  if (!before || !after) {
    console.error("usage: node check-release-edits.ts BEFORE_FILE AFTER_FILE");
    process.exit(2);
  }
  try {
    const changed = ciFieldsChanged(readFileSync(before, "utf8"), readFileSync(after, "utf8"));
    if (changed.length) {
      console.error(`CI-owned fields changed: ${changed.join(", ")}`);
      process.exit(1);
    }
  } catch (e) {
    console.error(`check-release-edits: ${(e as Error).message}`);
    process.exit(1);
  }
}
