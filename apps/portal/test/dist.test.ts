// SPDX-License-Identifier: GPL-3.0-only
// The published build (run after the build; skipped without dist/): the app shell only (Ruling 8: the deploy adds the
// site's data), a page per synth and 404.html on the same bundle, no simulated keyboard, no "choose a file".
import { existsSync, readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";
import { describe, expect, it } from "vitest";

const app = join(import.meta.dirname, "..");
const dist = join(app, "dist");
const files = (d: string): string[] =>
  readdirSync(d, { withFileTypes: true }).flatMap((e) => (e.isDirectory() ? files(join(d, e.name)) : [join(d, e.name)]));
const scripts = (html: string) => [...html.matchAll(/<script[^>]*src="([^"]+)"/g)].map((m) => m[1]);

describe.skipIf(!existsSync(dist))("dist/", () => {
  it("has the home page, a page per synth and 404.html, all on the same bundle", () => {
    const home = readFileSync(join(dist, "index.html"), "utf8");
    expect(scripts(home).length).toBe(1);
    expect(scripts(home)[0]).toMatch(/^\/.*assets\/.*\.js$/);   // absolute: works at any depth (Ruling 7)
    for (const p of ["knobi-six/index.html", "404.html"]) {
      expect(existsSync(join(dist, p)), p).toBe(true);
      expect(scripts(readFileSync(join(dist, p), "utf8")), p).toEqual(scripts(home));
    }
    expect(readFileSync(join(dist, "knobi-six/index.html"), "utf8")).toContain("<title>KNOBI-SIX · midi·chlorian</title>");
  });

  it("has a <base href> at the site's base, so the installer panel's site-relative links work at /<synth>/", () => {
    const home = readFileSync(join(dist, "index.html"), "utf8");
    const base = /<base href="([^"]+)"/.exec(home)?.[1];
    expect(base).toMatch(/^\/(.+\/)?$/);
    expect(scripts(home)[0]!.startsWith(`${base}assets/`)).toBe(true);
  });

  it("is the app shell only: no content.json, catalog.json, firmware/ or synths/", () => {
    for (const p of ["content.json", "catalog.json", "firmware", "synths"]) expect(existsSync(join(dist, p)), p).toBe(false);
    expect(files(dist).filter((f) => /\.(fwsc|tar\.gz)$/.test(f))).toEqual([]);
  });

  it.each(["fakeDevice", "FakeKeyboard", "makePackage", "fixtureFetch", "Beta for testers", "(fixture)", "Felucca Update", 'type="file"', "type:\"file\""])(
    "contains no %s", (needle) => {
      for (const f of files(dist)) expect(readFileSync(f, "utf8").includes(needle), f).toBe(false);
    });

  it("never says knobi-flash", () => {
    for (const f of files(dist).filter((x) => !x.endsWith(".map"))) {
      expect(readFileSync(f, "utf8").toLowerCase().includes("knobi-flash"), f).toBe(false);
    }
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

describe.skipIf(!existsSync(dist))("dist/ styles", () => {
  const css = () => files(dist).filter((f) => f.endsWith(".css")).map((f) => readFileSync(f, "utf8")).join("\n");
  it("has the shared components' Tailwind classes (Tailwind must scan @knobi/ui and @knobi/installer-ui)", () => {
    expect(css()).toContain("ring-\\[3px\\]");   // focus-visible:ring-[3px], used only in @knobi/ui's components
    expect(css()).toContain("border-\\[6px\\]"); // the installer panel's frame, used only in @knobi/installer-ui
  });
  it("has the KNOBI panel theme", () => expect(css().toLowerCase()).toContain("#2f6be0"));
});

describe("the portal's own sources", () => {
  const src = [join(app, "index.html"), ...files(join(app, "src")), ...files(join(app, "scripts"))];
  it("never say knobi-flash", () => {
    for (const f of src) expect(readFileSync(f, "utf8").toLowerCase().includes("knobi-flash"), f).toBe(false);
  });
});
