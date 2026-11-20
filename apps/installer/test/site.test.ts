// SPDX-License-Identifier: GPL-3.0-only
// site/: the README published on knobi-flash with every release must say where the installer's source is.
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";

describe("site/README.md (published on knobi-flash)", () => {
  const readme = readFileSync(join(import.meta.dirname, "../site/README.md"), "utf8");
  it("points at the installer's source where it is now (apps/, packages/), not the old web/", () => {
    expect(readme).not.toMatch(/`web\/`/);
    expect(readme).toContain("`apps/installer/`");
    expect(readme).toContain("`packages/`");
  });
});
