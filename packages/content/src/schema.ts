import { defineArrayMember, defineField, defineType } from "sanity";

const ci = { readOnly: true } as const;

export const synthType = defineType({
  name: "synth",
  title: "Synth",
  type: "document",
  fields: [
    defineField({
      name: "id",
      type: "string",
      title: "Id (equals synths/<id>/)",
      validation: (r) => r.required().regex(/^[a-z0-9-]+$/, { name: "id" }),
    }),
    defineField({ name: "name", type: "string", validation: (r) => r.required() }),
    defineField({ name: "summary", type: "text", rows: 3, validation: (r) => r.required() }),
    defineField({ name: "description", type: "array", of: [defineArrayMember({ type: "block" })] }),
    defineField({ name: "image", type: "image", options: { hotspot: true } }),
    defineField({
      name: "origin",
      type: "string",
      options: { list: ["own", "external"], layout: "radio" },
      initialValue: "own",
      validation: (r) => r.required(),
    }),
    defineField({ name: "order", type: "number", validation: (r) => r.required() }),
  ],
});

const keyboardBuild = defineArrayMember({
  name: "keyboardBuild",
  type: "object",
  fields: [
    defineField({ name: "kind", type: "string", initialValue: "keyboard", ...ci }),
    defineField({ name: "instrument", type: "string", ...ci }),
    defineField({ name: "identity", type: "string", ...ci }),
    defineField({ name: "file", type: "string", ...ci }),
    defineField({ name: "sha256", type: "string", ...ci }),
    defineField({ name: "size", type: "number", ...ci }),
  ],
});

const desktopBuild = defineArrayMember({
  name: "desktopBuild",
  type: "object",
  fields: [
    defineField({ name: "kind", type: "string", initialValue: "desktop", ...ci }),
    defineField({ name: "format", type: "string", options: { list: ["vst3", "au"] }, ...ci }),
    defineField({ name: "os", type: "string", options: { list: ["mac", "windows"] }, ...ci }),
    defineField({ name: "arch", type: "string", ...ci }),
    defineField({ name: "file", type: "string", ...ci }),
    defineField({ name: "sha256", type: "string", ...ci }),
    defineField({ name: "size", type: "number", ...ci }),
  ],
});

export const releaseType = defineType({
  name: "release",
  title: "Release",
  type: "document",
  fields: [
    // CI-owned: written by writeReleaseDraft, read-only in the Studio.
    defineField({ name: "synth", type: "reference", to: [{ type: "synth" }], ...ci }),
    defineField({ name: "version", type: "string", ...ci }),
    defineField({ name: "date", type: "date", ...ci }),
    defineField({ name: "tag", type: "string", ...ci }),
    defineField({
      name: "source",
      type: "object",
      ...ci,
      fields: [
        defineField({ name: "file", type: "string", ...ci }),
        defineField({ name: "sha256", type: "string", ...ci }),
        defineField({ name: "size", type: "number", ...ci }),
      ],
    }),
    defineField({ name: "builds", type: "array", of: [keyboardBuild, desktopBuild], ...ci }),
    // João-owned.
    defineField({ name: "notes", type: "array", of: [defineArrayMember({ type: "block" })] }),
    defineField({ name: "current", type: "boolean", title: "Current (the one the synth page installs)" }),
  ],
});

export const schemaTypes = [synthType, releaseType];
