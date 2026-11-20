// SPDX-License-Identifier: GPL-3.0-only
// The published build: no simulated keyboard, no "choose a file" (run after npm run build; skipped without dist/).
import { existsSync, readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";

const dist = join(import.meta.dirname, "../dist");
const files = (d: string): string[] =>
  readdirSync(d, { withFileTypes: true }).flatMap((e) => (e.isDirectory() ? files(join(d, e.name)) : [join(d, e.name)]));

describe.skipIf(!existsSync(dist))("dist/", () => {
  it("has the page and its script", () => {
    expect(files(dist).some((f) => f.endsWith("index.html"))).toBe(true);
    expect(files(dist).some((f) => f.endsWith(".js"))).toBe(true);
  });
  it.each(["fakeDevice", "FakeKeyboard", "makePackage", "Felucca Update", 'type="file"', "type:\"file\""])(
    "contains no %s", (needle) => {
      for (const f of files(dist)) expect(readFileSync(f, "utf8").includes(needle), f).toBe(false);
    });
});
