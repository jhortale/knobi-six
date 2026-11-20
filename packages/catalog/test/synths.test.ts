// SPDX-License-Identifier: GPL-3.0-only
// synths/<id>/synth.json and catalog.json: read, checked against the firmware's own identity, and refused clearly when
// broken.
import { cpSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { listSynths, loadBaseCatalog, readSynth, repoRootFrom } from "../src/node.ts";

const ROOT = repoRootFrom(import.meta.dirname);

/** a throwaway repo with the real instruments.json and synths/knobi-six, plus extra synth files */
function tempRepo(extra: Record<string, string> = {}): string {
  const d = mkdtempSync(join(tmpdir(), "knobi-repo-"));
  writeFileSync(join(d, "pnpm-workspace.yaml"), "packages: []\n");
  mkdirSync(join(d, "packages/catalog"), { recursive: true });
  cpSync(join(ROOT, "packages/catalog/instruments.json"), join(d, "packages/catalog/instruments.json"));
  cpSync(join(ROOT, "synths/knobi-six"), join(d, "synths/knobi-six"), { recursive: true });
  for (const [p, s] of Object.entries(extra)) {
    mkdirSync(join(d, p, ".."), { recursive: true });
    writeFileSync(join(d, p), s);
  }
  return d;
}

function err(fn: () => unknown): Error {
  try {
    fn();
  } catch (e) {
    return e as Error;
  }
  throw new Error("did not throw");
}

describe("readSynth", () => {
  it("reads KNOBI-SIX's synth.json", () => {
    expect(readSynth(ROOT, "knobi-six")).toEqual({
      id: "knobi-six", name: "KNOBI-SIX", ident: "956", packageName: "knobi-six-{version}{suffix}.fwsc",
      models: { "": "SMK-37 Pro", "-elite": "SMK-37 Elite", "-mke": "MKE-P37" },
      platforms: { smk37: { firmware: "firmware/", build: "./build.sh" } },
    });
  });
  it("agrees with the firmware: tools/build.py's IDENT and docs/IDENTITY.md", () => {
    const build = readFileSync(join(ROOT, "tools/build.py"), "utf8");
    expect(/^IDENT = "(\d+)"/m.exec(build)![1]).toBe(readSynth(ROOT, "knobi-six").ident);
    const identity = readFileSync(join(ROOT, "docs/IDENTITY.md"), "utf8");
    for (const model of Object.values(readSynth(ROOT, "knobi-six").models)) expect(identity).toContain(`${model}_956`);
  });
  it("lists the synths", () => expect(listSynths(ROOT)).toContain("knobi-six"));
  it("unparsable synth.json: an error naming the file, not a TypeError", () => {
    const e = err(() => readSynth(tempRepo({ "synths/x/synth.json": "{" }), "x"));
    expect(e).not.toBeInstanceOf(TypeError);
    expect(e.message).toMatch(/synths\/x\/synth\.json/);
  });
  it("missing synth.json: an error naming the file", () => {
    expect(err(() => readSynth(tempRepo(), "nope")).message).toMatch(/synths\/nope\/synth\.json/);
  });
  it("a missing field: an error naming the field", () => {
    const bad = JSON.stringify({ id: "x", name: "X", ident: "957", packageName: "x-{version}{suffix}.fwsc", platforms: {} });
    const e = err(() => readSynth(tempRepo({ "synths/x/synth.json": bad }), "x"));
    expect(e.message).toMatch(/synths\/x\/synth\.json/);
    expect(e.message).toMatch(/models/);
  });
});

describe("loadBaseCatalog", () => {
  it("instruments.json plus every synth's catalog.json, validated", () => {
    const c = loadBaseCatalog(ROOT);
    expect(c.instruments.map((i) => i.id)).toEqual(["smk37-pro", "smk37-elite", "mke-p37"]);
    expect(c.firmwares.map((f) => f.id)).toEqual(["knobi-six"]);
  });
  it("a broken synth catalog: an error naming its file", () => {
    const bad = JSON.stringify({ id: "y", name: "Y", summary: "", license: "MIT", notes: [], releases: [
      { version: "1.0", date: "2026-11-20", packages: [
        { instrument: "smk37-pro", file: "firmware/y/1.0/y.fwsc", identity: "SMK-37 Pro_957", sha256: "x", size: 1 }] }] });
    const d = tempRepo({ "synths/y/catalog.json": bad, "synths/y/synth.json": readFileSync(join(ROOT, "synths/knobi-six/synth.json"), "utf8").replace(/knobi-six/g, "y") });
    const e = err(() => loadBaseCatalog(d));
    expect(e.message).toMatch(/synths\/y\/catalog\.json/);
    expect(e.message).toMatch(/sha256/);
  });
  it("a catalog.json whose id is not its folder's: refused", () => {
    const d = tempRepo({ "synths/z/catalog.json": readFileSync(join(ROOT, "synths/knobi-six/catalog.json"), "utf8") });
    expect(err(() => loadBaseCatalog(d)).message).toMatch(/synths\/z\/catalog\.json.*"knobi-six"/);
  });
});
