import { describe, expect, it } from "vitest";
import { releaseType, schemaTypes, synthType } from "../src/schema.ts";
import { PUBLISHED_QUERY, type PublishedContent } from "../src/index.ts";
import fixture from "./fixtures/published.json" with { type: "json" };

const fields = releaseType.fields as { name: string; readOnly?: unknown }[];
const field = (name: string) => fields.find((f) => f.name === name)!;

describe("schema", () => {
  it("makes every CI-owned release field read-only", () => {
    for (const name of ["synth", "version", "date", "tag", "source", "builds"]) {
      expect(field(name).readOnly, name).toBe(true);
    }
  });
  it("leaves notes and current editable", () => {
    for (const name of ["notes", "current"]) expect(field(name).readOnly, name).toBeFalsy();
  });
  it("makes the nested build and source fields read-only", () => {
    const source = field("source") as unknown as { fields: { name: string; readOnly?: boolean }[] };
    expect(source.fields.every((f) => f.readOnly)).toBe(true);
    const builds = field("builds") as unknown as { of: { fields: { readOnly?: boolean }[] }[] };
    for (const member of builds.of) expect(member.fields.every((f) => f.readOnly)).toBe(true);
  });
  it("exports both types", () => {
    expect(schemaTypes.map((t) => t.name)).toEqual(["synth", "release"]);
  });
});

describe("synth schema", () => {
  it("stores id as a plain string, matching Synth.id", () => {
    const id = (synthType.fields as { name: string; type: string }[]).find((f) => f.name === "id")!;
    expect(id.type).toBe("string");
  });
});

describe("queries", () => {
  it("shows only own synths", () => {
    expect(PUBLISHED_QUERY).toContain('origin == "own"');
  });
  it("matches the published fixture's shape", () => {
    const content: PublishedContent = fixture as PublishedContent;
    expect(content.releases[0]!.synth._ref).toBe(content.synths[0]!._id);
  });
});
