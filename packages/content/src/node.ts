import type { SanityClient } from "@sanity/client";
import type { Build, PortableText, ReleaseSource } from "./types.ts";

export interface DraftInput {
  synth: string;
  version: string;
  date: string;
  tag: string;
  source: ReleaseSource;
  builds: Build[];
  notes?: PortableText;
}

export function releaseIds(synth: string, version: string): { id: string; draftId: string } {
  const id = `release.${synth}.${version}`;
  return { id, draftId: `drafts.${id}` };
}

type WriteClient = Pick<SanityClient, "getDocument" | "createOrReplace">;

/**
 * Creates or refreshes the draft of a release. Never writes the published id, and refuses when it already exists
 * (a published release never changes). On an existing draft, João's `notes` and `current` are kept.
 */
export async function writeReleaseDraft(client: WriteClient, input: DraftInput): Promise<"created" | "updated"> {
  const { id, draftId } = releaseIds(input.synth, input.version);
  if (await client.getDocument(id)) {
    throw new Error(`${id} is already published; a published release never changes`);
  }
  const existing = await client.getDocument<{ notes?: PortableText; current?: boolean }>(draftId);
  const notes = existing ? existing.notes : input.notes;
  await client.createOrReplace({
    _id: draftId,
    _type: "release",
    synth: { _type: "reference", _ref: `synth.${input.synth}` },
    version: input.version,
    date: input.date,
    tag: input.tag,
    source: input.source,
    builds: input.builds,
    ...(notes !== undefined ? { notes } : {}),
    ...(existing?.current !== undefined ? { current: existing.current } : {}),
  });
  return existing ? "updated" : "created";
}
