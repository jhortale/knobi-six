// SPDX-License-Identifier: GPL-3.0-only
// The package builder makes the same bytes as tests/install_test.py's package() (the fixtures of @knobi/installer-core,
// written by tests/web_fixtures.py).
import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { makePackage, type PackageOpts } from "../src/index.ts";

const fixtures: { packages: { params: PackageOpts; raw: string }[] } = JSON.parse(
  readFileSync(new URL("../../installer-core/test/fixtures/python.json", import.meta.url), "utf8"));

describe.each(fixtures.packages)("package $params.product ($params.blocks blocks)", (f) => {
  it("makePackage builds the same bytes as install_test.py's package()", () => {
    expect(makePackage(f.params)).toEqual(new Uint8Array(Buffer.from(f.raw, "base64")));
  });
});
