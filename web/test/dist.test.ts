// SPDX-License-Identifier: GPL-3.0-only
// The published build: no simulated keyboard, no "choose a file" (run after npm run build; skipped without dist/).
import { existsSync, readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";
import { execFileSync } from "node:child_process";
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
  it("publishes the backup instructions (docs/BACKUP.md) as backup.txt", () => {
    expect(readFileSync(join(dist, "backup.txt"), "utf8")).toContain("Backing up the SMK-37's flash");
  });
  it("links nothing in the private repo", () => {
    for (const f of files(dist).filter((x) => !x.endsWith(".map"))) {
      expect(readFileSync(f, "utf8").includes("github.com/jhortale/knobi-six"), f).toBe(false);
    }
  });
});

describe("local test files are never committed", () => {
  it.each(["public/catalog.json", "public/firmware/knobi-six/1.0/knobi-six-1.0.fwsc"])("%s is ignored", (p) => {
    expect(() => execFileSync("git", ["check-ignore", "-q", p], { cwd: join(import.meta.dirname, "..") })).not.toThrow();
  });
});

