// SPDX-License-Identifier: GPL-3.0-only
// site/README.md: published with every deploy; it must say the site is unofficial and GPL-3.0-only, where the source is, and the credits.
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";

describe("site/README.md (published on jhortale/midi-chlorian)", () => {
  const readme = readFileSync(join(import.meta.dirname, "../site/README.md"), "utf8");
  it("says unofficial, GPL-3.0 only, where the source is and where the credits are", () => {
    expect(readme).toContain("Unofficial");
    expect(readme).toContain("version 3 only");
    expect(readme).toContain("source/midi-chlorian-src-<commit>.tar.gz");
    expect(readme).toContain("LICENSING.md");
    expect(readme).not.toContain("knobi-flash");
  });
});
