import { isDeepStrictEqual } from "node:util";
import { stringify } from "yaml";
import { splitFrontmatter } from "./frontmatter.ts";
import type { ReleaseDoc } from "./types.ts";

/** The frontmatter fields CI writes; the body and `current` belong to João. */
const CI_FIELDS = ["version", "date", "tag", "source", "builds"] as const;

/** A release file: YAML frontmatter in a stable field order, then the notes. */
export function releaseFileText(input: Omit<ReleaseDoc, "notesHtml"> & { notes: string }): string {
  const front: Record<string, unknown> = { version: input.version, date: input.date, tag: input.tag };
  if (input.current !== undefined) front.current = input.current;
  front.source = { file: input.source.file, sha256: input.source.sha256, size: input.source.size };
  front.builds = input.builds;
  const notes = input.notes.endsWith("\n") ? input.notes : `${input.notes}\n`;
  return `---\n${stringify(front, { lineWidth: 0 })}---\n${notes}`;
}

/** Names of the CI-owned frontmatter fields that differ between two release files. Body and `current` are ignored. */
export function ciFieldsChanged(before: string, after: string): string[] {
  const a = splitFrontmatter(before).data;
  const b = splitFrontmatter(after).data;
  return CI_FIELDS.filter((k) => !isDeepStrictEqual(a[k], b[k]));
}
