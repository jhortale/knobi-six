// SPDX-License-Identifier: GPL-3.0-only
import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { familyRe, ownLoaderRe, packagesFor, portMatcher, validateCatalog } from "../src/catalog/load.ts";
import type { Catalog } from "../src/catalog/schema.ts";

const base = JSON.parse(readFileSync(new URL("../catalog/base.json", import.meta.url), "utf8"));
const clone = (): Catalog => structuredClone(base);
const H = "a".repeat(64);

function withPackages(): Catalog {
  const c = clone();
  c.firmwares[0].releases = [
    { version: "0.9", date: "2026-11-01", packages: [
      { instrument: "smk37-pro", file: "firmware/knobi-six/0.9/knobi-six-0.9.fwsc", identity: "SMK-37 Pro_956", sha256: H, size: 1 },
    ] },
    { version: "1.0", date: "2026-11-20", packages: [
      { instrument: "smk37-elite", file: "e.fwsc", identity: "SMK-37 Elite_956", sha256: H, size: 1 },
      { instrument: "smk37-pro", file: "p.fwsc", identity: "SMK-37 Pro_956", sha256: H, size: 1 },
      { instrument: "mke-p37", file: "m.fwsc", identity: "MKE-P37_956", sha256: H, size: 1 },
    ] },
  ];
  return c;
}

describe("validateCatalog", () => {
  it("accepts the base catalog", () => expect(() => validateCatalog(base)).not.toThrow());
  it("accepts a catalog with packages", () => expect(() => validateCatalog(withPackages())).not.toThrow());
  const bad: [string, (c: Catalog) => void, RegExp][] = [
    ["schema 2", (c) => { (c as { schema: number }).schema = 2; }, /schema/],
    ["unknown instrument", (c) => { c.firmwares[0].releases[1].packages[0].instrument = "nope"; }, /unknown instrument "nope"/],
    ["bad sha256", (c) => { c.firmwares[0].releases[1].packages[0].sha256 = "xyz"; }, /sha256/],
    ["identity of another model", (c) => { c.firmwares[0].releases[1].packages[1].identity = "MKE-P37_956"; }, /identity "MKE-P37_956".*SMK-37 Pro/],
    ["bad regex", (c) => { c.families.smk37.ports = "("; }, /regular expression/],
    ["unknown family", (c) => { c.instruments[0].family = "x"; }, /unknown family "x"/],
    ["unknown protocol", (c) => { c.instruments[0].protocol = "x"; }, /unknown protocol "x"/],
    ["duplicate instrument id", (c) => { c.instruments[1].id = "smk37-pro"; }, /duplicate/],
  ];
  it.each(bad)("refuses: %s", (_n, change, msg) => {
    const c = withPackages();
    change(c);
    expect(() => validateCatalog(c)).toThrow(msg);
  });
  it("refuses a non-object", () => expect(() => validateCatalog("x")).toThrow());
});

describe("queries", () => {
  const c = validateCatalog(withPackages());
  it("packagesFor a running Pro: the newest Pro package first, then the other models", () => {
    const r = packagesFor(c, "SMK-37 Pro_956");
    expect(r[0].pkg.file).toBe("p.fwsc");
    expect(r[0].exact).toBe(true);
    expect(r[0].release.version).toBe("1.0");
    expect(r.filter((x) => x.exact).map((x) => x.release.version)).toEqual(["1.0", "0.9"]);
    expect(r.filter((x) => !x.exact).map((x) => x.pkg.identity).sort()).toEqual(["MKE-P37_956", "SMK-37 Elite_956"]);
  });
  it("packagesFor the Pro's update loader: the Pro package first", () => {
    expect(packagesFor(c, "ota-SMK-37 Pro_900")[0].pkg.file).toBe("p.fwsc");
  });
  it("packagesFor an FM-1: nothing", () => expect(packagesFor(c, "FM-1_906")).toEqual([]));
  it("portMatcher matches the keyboard's ports, not other gear", () => {
    const re = portMatcher(c);
    expect(re.test("X0X FM-1")).toBe(true);
    expect(re.test("SMK-37 Pro")).toBe(true);
    expect(re.test("Felucca Update")).toBe(true);
    expect(re.test("IAC Driver Bus 1")).toBe(false);
  });
  it("familyRe / ownLoaderRe are anchored and case-insensitive", () => {
    expect(familyRe(c, "smk37").test("smk-37 pro")).toBe(true);
    expect(familyRe(c, "smk37").test("FM-1")).toBe(false);
    expect(ownLoaderRe(c, "felucca-ota").test("ota-SMK-37 Pro_900")).toBe(true);
    expect(ownLoaderRe(c, "felucca-ota").test("ota-SMK-37 Pro_900x")).toBe(false);
  });
});

describe("drift: the catalog's patterns are tools/fm1_install.py's", () => {
  const py = readFileSync(new URL("../../tools/fm1_install.py", import.meta.url), "utf8");
  const lit = (name: string) => new RegExp(`${name}\\s*=\\s*re\\.compile\\(r"((?:[^"\\\\]|\\\\.)*)"`).exec(py)![1];
  it("ports = PORT_RE", () => expect(base.families.smk37.ports).toBe(lit("PORT_RE")));
  it("match = SMK_FAMILY", () => expect(base.families.smk37.match).toBe(lit("SMK_FAMILY")));
  it("ownLoader = OWN_LOADER_RE", () => expect(base.protocols["felucca-ota"].ownLoader).toBe(lit("OWN_LOADER_RE")));
});
