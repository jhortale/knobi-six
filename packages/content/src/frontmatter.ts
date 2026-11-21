import { parse } from "yaml";

/** Splits a Markdown file into its YAML frontmatter (between leading `---` lines) and body. Throws plain messages; callers add the file name. */
export function splitFrontmatter(text: string): { data: Record<string, unknown>; body: string } {
  const m = /^---\r?\n([\s\S]*?)\r?\n---[ \t]*(?:\r?\n|$)([\s\S]*)$/.exec(text);
  if (!m) throw new Error("missing frontmatter (--- lines)");
  const data: unknown = parse(m[1]!);
  if (data === null || typeof data !== "object" || Array.isArray(data)) throw new Error("frontmatter is not a mapping");
  return { data: data as Record<string, unknown>, body: m[2]! };
}
