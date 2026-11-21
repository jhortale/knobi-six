import { mkdtempSync, mkdirSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { describe, expect, it } from "vitest";
import { ciFieldsChanged, readContent, releaseFileText } from "../src/node.ts";

const SHA = "b".repeat(64);
const input = {
  synth: "knobi-six",
  version: "1.1",
  date: "2026-11-21",
  tag: "knobi-six-v1.1",
  source: { file: "src.tar.gz", sha256: SHA, size: 10 },
  builds: [
    { kind: "keyboard" as const, instrument: "pro", identity: "KNOBI-SIX", file: "p.fwsc", sha256: SHA, size: 20 },
  ],
  notes: "Notes with **bold**.",
};

describe("releaseFileText", () => {
  it("round-trips through readContent", () => {
    const root = mkdtempSync(join(tmpdir(), "rf-"));
    mkdirSync(join(root, "synths/knobi-six/releases"), { recursive: true });
    writeFileSync(join(root, "synths/knobi-six/synth.md"), "---\nname: K\nsummary: s\norder: 1\norigin: own\n---\nd\n");
    writeFileSync(join(root, "synths/knobi-six/releases/1.1.md"), releaseFileText(input));
    const r = readContent(root).releases[0]!;
    expect(r).toMatchObject({ synth: "knobi-six", version: "1.1", date: "2026-11-21", tag: "knobi-six-v1.1" });
    expect(r.source).toEqual(input.source);
    expect(r.builds).toEqual(input.builds);
    expect(r.notesHtml).toContain("<strong>bold</strong>");
    expect(r.current).toBeUndefined();
  });

  it("starts with the frontmatter in a stable order and keeps current when given", () => {
    const text = releaseFileText({ ...input, current: true });
    expect(text.startsWith("---\nversion: ")).toBe(true);
    const keys = ["version", "date", "tag", "current", "source", "builds"].map((k) => text.indexOf(`\n${k}:`) + (k === "version" ? 1 : 0));
    expect(keys).toEqual([...keys].sort((a, b) => a - b));
    expect(text.endsWith("Notes with **bold**.\n")).toBe(true);
  });
});

describe("ciFieldsChanged", () => {
  const base = releaseFileText(input);
  it("reports builds when a sha256 changes", () => {
    const after = base.replace(`sha256: ${SHA}\n    size: 20`, `sha256: ${"c".repeat(64)}\n    size: 20`);
    expect(after).not.toBe(base);
    expect(ciFieldsChanged(base, after)).toEqual(["builds"]);
  });
  it("reports nothing when only the body changes", () => {
    expect(ciFieldsChanged(base, base.replace("Notes with", "Other words with"))).toEqual([]);
  });
  it("reports nothing when only current changes", () => {
    expect(ciFieldsChanged(base, releaseFileText({ ...input, current: true }))).toEqual([]);
  });
  it("reports several fields", () => {
    const after = releaseFileText({ ...input, date: "2026-12-01", tag: "knobi-six-v1.2" });
    expect(ciFieldsChanged(base, after)).toEqual(["date", "tag"]);
  });
});
