import { describe, expect, it } from "vitest";
import { releaseIds, writeReleaseDraft, type DraftInput } from "../src/node.ts";

type Doc = Record<string, unknown> & { _id: string };

function fakeClient(docs: Doc[] = []) {
  const store = new Map(docs.map((d) => [d._id, d]));
  return {
    store,
    getDocument: async (id: string) => store.get(id),
    createOrReplace: async (doc: Doc) => {
      store.set(doc._id, doc);
      return doc;
    },
  } as never as Parameters<typeof writeReleaseDraft>[0] & { store: Map<string, Doc> };
}

const build = (sha256: string) => ({
  kind: "keyboard" as const, instrument: "smk37-pro", identity: "956", file: "a.fwsc", sha256, size: 1,
});
const input = (sha256 = "aa"): DraftInput => ({
  synth: "knobi-six", version: "1.0", date: "2026-11-21", tag: "knobi-six-v1.0",
  source: { file: "s.tar.gz", sha256: "ss", size: 2 }, builds: [build(sha256)],
  notes: [{ _type: "block", note: "from github" }],
});

describe("writeReleaseDraft", () => {
  it("computes the ids", () => {
    expect(releaseIds("knobi-six", "1.0")).toEqual({ id: "release.knobi-six.1.0", draftId: "drafts.release.knobi-six.1.0" });
  });

  it("creates the draft with the CI fields", async () => {
    const c = fakeClient();
    expect(await writeReleaseDraft(c, input())).toBe("created");
    const doc = c.store.get("drafts.release.knobi-six.1.0")!;
    expect(doc).toMatchObject({
      _type: "release", version: "1.0", date: "2026-11-21", tag: "knobi-six-v1.0",
      synth: { _type: "reference", _ref: "synth.knobi-six" }, builds: [build("aa")],
      notes: [{ _type: "block", note: "from github" }],
    });
    expect(doc).not.toHaveProperty("current");
    expect(c.store.has("release.knobi-six.1.0")).toBe(false);
  });

  it("keeps an edited draft's notes and current, and replaces builds", async () => {
    const c = fakeClient([
      { _id: "drafts.release.knobi-six.1.0", _type: "release", notes: [{ edited: true }], current: true, builds: [build("old")] },
    ]);
    expect(await writeReleaseDraft(c, input("new"))).toBe("updated");
    const doc = c.store.get("drafts.release.knobi-six.1.0")!;
    expect(doc.notes).toEqual([{ edited: true }]);
    expect(doc.current).toBe(true);
    expect(doc.builds).toEqual([build("new")]);
  });

  it("refuses when the published release exists, naming it", async () => {
    const c = fakeClient([{ _id: "release.knobi-six.1.0", _type: "release" }]);
    await expect(writeReleaseDraft(c, input())).rejects.toThrow("release.knobi-six.1.0");
    expect(c.store.has("drafts.release.knobi-six.1.0")).toBe(false);
  });
});
