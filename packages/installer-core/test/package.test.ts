// SPDX-License-Identifier: GPL-3.0-only
import { existsSync, readFileSync } from "node:fs";
import { createHash } from "node:crypto";
import { describe, expect, it } from "vitest";
import { deinterleave, hasLoaderMark, loadPackage, packageDamage, productOf } from "../src/package.ts";
import { InstallError, isOwnLoader, modelOf, sameKeyboard } from "../src/identity.ts";
import { fixtures, unb64 } from "./helpers/fixtures.ts";

const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");
const SMK = /^(?:SMK-37[ +].*|MKE-P37|STARRYKEY-37.*)$/i;
const OWN = /^(?:ota-(FM-1|SMK-37 Pro|SMK-37 Elite|MKE-P37)_9\d\d)$/i;

describe.each(fixtures.packages)("package $params.product ($params.blocks blocks, damage $params.damage, marker $params.marker)", (f) => {
  const raw = unb64(f.raw);
  it("productOf equals Python's product_of", () => expect(productOf(raw)).toBe(f.product));
  it("deinterleave: block count and image equal Python's", () => {
    const { image, blocks } = deinterleave(raw);
    expect(blocks).toBe(f.blocks);
    expect(sha(image)).toBe(f.imageSha256);
  });
  it("packageDamage equals Python's package_damage", () => expect(packageDamage(deinterleave(raw).image)).toBe(f.damage));
  it("hasLoaderMark equals Python's", () => expect(hasLoaderMark(raw)).toBe(f.loaderMark));
});

/** a fixture package (made by tests/web_fixtures.py from install_test.py's package()) */
const fixture = (p: { product: string; damage?: boolean; marker?: boolean }) =>
  unb64(fixtures.packages.find((f) => f.params.product === p.product && !!f.params.damage === !!p.damage &&
    (f.params.marker ?? true) === (p.marker ?? true) && (f.params.blocks ?? 20) === 20)!.raw);

function code(fn: () => unknown) {
  try {
    fn();
  } catch (e) {
    return e instanceof InstallError ? [e.code, e.message] : ["other", String(e)];
  }
  return null;
}

describe("loadPackage", () => {
  it("returns the product and the logical image", () => {
    const raw = fixture({ product: "SMK-37 Pro_956" });
    const p = loadPackage(raw);
    expect(p.product).toBe("SMK-37 Pro_956");
    expect(p.image).toEqual(deinterleave(raw).image);
  });
  it("refuses a package without the Felucca loader", () => {
    const [c, m] = code(() => loadPackage(fixture({ product: "SMK-37 Pro_956", marker: false })))!;
    expect(c).toBe("badpkg");
    expect(m).toContain("no Felucca update loader");
  });
  it("refuses a damaged package", () => {
    const [c, m] = code(() => loadPackage(fixture({ product: "SMK-37 Pro_956", damage: true })))!;
    expect(c).toBe("badpkg");
    expect(m).toContain("damaged package");
  });
  it("refuses a short file", () => {
    const [c, m] = code(() => loadPackage(new Uint8Array(100)))!;
    expect(c).toBe("badpkg");
    expect(m).toContain("too short");
  });
  const built = new URL("../../../build/knobi-six.fwsc", import.meta.url);
  it.skipIf(!existsSync(built))("the real build/knobi-six.fwsc is SMK-37 Pro_956", () => {
    expect(loadPackage(new Uint8Array(readFileSync(built))).product).toBe("SMK-37 Pro_956");
  });
});

describe("identity rules", () => {
  it("modelOf drops the version and ota-", () => expect(modelOf("ota-SMK-37 Pro_900")).toBe("SMK-37 Pro"));
  it("sameKeyboard: the SMK family is one keyboard, never an FM-1", () => {
    expect(sameKeyboard("SMK-37 Pro_956", "MKE-P37_956", SMK)).toBe(true);
    expect(sameKeyboard("FM-1_906", "SMK-37 Pro_956", SMK)).toBe(false);
    expect(sameKeyboard("ota-SMK-37 Pro_900", "SMK-37 Elite_956", SMK)).toBe(true);
    expect(sameKeyboard("FM-1_015", "FM-1_906", SMK)).toBe(true);
  });
  it("isOwnLoader: only Felucca-family loaders", () => {
    expect(isOwnLoader("ota-SMK-37 Pro_900", OWN)).toBe(true);
    expect(isOwnLoader("ota-SMK-37 Pro_016", OWN)).toBe(false);
  });
});
