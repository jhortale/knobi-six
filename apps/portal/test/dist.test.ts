// SPDX-License-Identifier: GPL-3.0-only
// The published build (run after the build; skipped without dist/): the app shell only (Ruling 8: the deploy adds the
// site's data and runs scripts/prerender.ts), no simulated keyboard, no "choose a file". prerender() is checked on a
// copy of dist/ with the fixture content.
import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterAll, describe, expect, it } from "vitest";
import { prerender } from "../scripts/prerender.ts";
import content from "./fixtures/content.json" with { type: "json" };

const app = join(import.meta.dirname, "..");
const dist = join(app, "dist");
const files = (d: string): string[] =>
  readdirSync(d, { withFileTypes: true }).flatMap((e) => (e.isDirectory() ? files(join(d, e.name)) : [join(d, e.name)]));
const scripts = (html: string) => [...html.matchAll(/<script[^>]*src="([^"]+)"/g)].map((m) => m[1]);

describe.skipIf(!existsSync(dist))("dist/", () => {
  it("is the app shell only: no synth pages, no 404.html (the deploy prerenders them from the published content)", () => {
    const pages = readdirSync(dist, { withFileTypes: true })
      .filter((e) => e.isDirectory() && existsSync(join(dist, e.name, "index.html"))).map((e) => e.name);
    expect(pages).toEqual([]);
    expect(existsSync(join(dist, "404.html"))).toBe(false);
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

describe.skipIf(!existsSync(dist))("prerender (on a copy of dist/, with the fixture content)", () => {
  const copies: string[] = [];
  afterAll(() => { for (const d of copies) rmSync(d, { recursive: true, force: true }); });
  const copy = () => {
    const d = mkdtempSync(join(tmpdir(), "portal-dist-"));
    copies.push(d);
    cpSync(dist, d, { recursive: true });
    return d;
  };

  it("writes a page per synth and 404.html, all on the home page's bundle, the synth's name in <title>", () => {
    const d = copy();
    expect(prerender(d, content)).toEqual(["knobi-six/index.html", "404.html"]);
    const home = readFileSync(join(d, "index.html"), "utf8");
    for (const p of ["knobi-six/index.html", "404.html"]) {
      expect(scripts(readFileSync(join(d, p), "utf8")), p).toEqual(scripts(home));
    }
    expect(readFileSync(join(d, "knobi-six/index.html"), "utf8")).toContain("<title>KNOBI-SIX · midi·chlorian</title>");
    expect(readFileSync(join(d, "index.html"), "utf8")).toBe(readFileSync(join(dist, "index.html"), "utf8"));
  });

  it("removes the pages of synths no longer in the content, and nothing else", () => {
    const d = copy();
    prerender(d, { synths: [...content.synths, { id: "old-synth", name: "OLD" }] });
    expect(existsSync(join(d, "old-synth/index.html"))).toBe(true);
    // the deploy's data and a hand-made page: not prerendered pages, never touched
    mkdirSync(join(d, "synths/knobi-six"), { recursive: true });
    writeFileSync(join(d, "synths/knobi-six/index.html"), "<p>data</p>");
    mkdirSync(join(d, "firmware/knobi-six/1.0"), { recursive: true });
    writeFileSync(join(d, "firmware/knobi-six/1.0/SHA256SUMS"), "");
    mkdirSync(join(d, "about"));
    writeFileSync(join(d, "about/index.html"), "<p>about</p>");
    // a prerendered page directory that holds something else too: kept
    prerender(d, { synths: [...content.synths, { id: "mixed", name: "MIXED" }] });
    writeFileSync(join(d, "mixed/notes.txt"), "keep");

    expect(prerender(d, content)).toEqual(["knobi-six/index.html", "404.html"]);
    expect(existsSync(join(d, "old-synth"))).toBe(false);
    expect(existsSync(join(d, "knobi-six/index.html"))).toBe(true);
    expect(existsSync(join(d, "mixed/notes.txt"))).toBe(true);
    for (const p of ["synths/knobi-six/index.html", "firmware/knobi-six/1.0/SHA256SUMS", "about/index.html", "backup.txt"]) {
      expect(existsSync(join(d, p)), p).toBe(true);
    }
    expect(readdirSync(join(d, "assets"))).toEqual(readdirSync(join(dist, "assets")));
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
