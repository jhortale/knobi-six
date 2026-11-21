import { mkdtempSync, mkdirSync, writeFileSync, cpSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { describe, expect, it } from "vitest";
import { readContent } from "../src/node.ts";

const fixture = fileURLToPath(new URL("./fixtures/repo", import.meta.url));
const SHA = "a".repeat(64);

function tmpRepo(): string {
  const root = mkdtempSync(join(tmpdir(), "content-"));
  cpSync(fixture, root, { recursive: true });
  return root;
}

describe("readContent", () => {
  it("reads KNOBI-SIX and its two releases with rendered HTML", () => {
    const c = readContent(fixture);
    expect(c.synths).toHaveLength(1);
    expect(c.synths[0]).toMatchObject({
      id: "knobi-six",
      name: "KNOBI-SIX",
      summary: "OB-6-inspired six-voice polysynth for the SMK-37",
      order: 1,
      origin: "own",
    });
    expect(c.synths[0]!.descriptionHtml).toContain("<strong>filter</strong>");
    expect(c.releases.map((r) => r.version).sort()).toEqual(["0.9", "1.0"]);
    const r = c.releases.find((x) => x.version === "1.0")!;
    expect(r).toMatchObject({ synth: "knobi-six", tag: "knobi-six-v1.0", date: "2026-11-20", current: true });
    expect(r.notesHtml).toContain("<strong>drive</strong>");
    expect(r.builds[0]).toMatchObject({ kind: "keyboard", instrument: "pro", sha256: SHA });
    expect(r.source).toEqual({ file: "knobi-six-1.0-src.tar.gz", sha256: SHA, size: 1000 });
    expect(c.releases.find((x) => x.version === "0.9")!.builds[0]).toMatchObject({ kind: "desktop", format: "vst3" });
  });

  it("skips a synth folder without synth.md", () => {
    expect(readContent(fixture).synths.map((s) => s.id)).toEqual(["knobi-six"]);
    expect(readContent(fixture).releases.every((r) => r.synth === "knobi-six")).toBe(true);
  });

  it("sorts synths by order and ignores origin external", () => {
    const root = tmpRepo();
    for (const [id, order, origin] of [["zeta", 0, "own"], ["ext", 5, "external"]] as const) {
      mkdirSync(join(root, "synths", id), { recursive: true });
      writeFileSync(join(root, "synths", id, "synth.md"), `---\nname: ${id}\nsummary: s\norder: ${order}\norigin: ${origin}\n---\nd\n`);
    }
    expect(readContent(root).synths.map((s) => s.id)).toEqual(["zeta", "knobi-six"]);
  });

  it("throws naming the file and field for a non-numeric order", () => {
    const root = tmpRepo();
    writeFileSync(join(root, "synths/knobi-six/synth.md"), `---\nname: K\nsummary: s\norder: "x"\norigin: own\n---\nd\n`);
    expect(() => readContent(root)).toThrow(/synths\/knobi-six\/synth\.md.*order/);
  });

  it("throws naming the file and builds[0].sha256 when a build lacks sha256", () => {
    const root = tmpRepo();
    writeFileSync(
      join(root, "synths/knobi-six/releases/1.0.md"),
      `---\nversion: "1.0"\ndate: "2026-11-20"\ntag: knobi-six-v1.0\nsource:\n  file: a\n  sha256: ${SHA}\n  size: 1\nbuilds:\n  - kind: keyboard\n    instrument: pro\n    identity: KNOBI-SIX\n    file: f\n    size: 2\n---\nn\n`,
    );
    expect(() => readContent(root)).toThrow(/releases\/1\.0\.md.*builds\[0\]\.sha256/);
  });

  it("throws when frontmatter is missing", () => {
    const root = tmpRepo();
    writeFileSync(join(root, "synths/knobi-six/synth.md"), "no frontmatter\n");
    expect(() => readContent(root)).toThrow(/synths\/knobi-six\/synth\.md/);
  });

  it("throws when the release file name, version or tag disagree", () => {
    const root = tmpRepo();
    const p = join(root, "synths/knobi-six/releases/1.0.md");
    writeFileSync(p, `---\nversion: "2.0"\ndate: "2026-11-20"\ntag: knobi-six-v2.0\nsource:\n  file: a\n  sha256: ${SHA}\n  size: 1\nbuilds: []\n---\nn\n`);
    expect(() => readContent(root)).toThrow(/releases\/1\.0\.md.*version/);
  });
});
