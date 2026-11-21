// The content model (docs/superpowers/specs/2026-11-20-portal-design.md, amendment 2026-11-21). Browser-safe: types only.

export interface KeyboardBuild {
  kind: "keyboard";
  instrument: string;
  identity: string;
  file: string;
  sha256: string;
  size: number;
}

export interface DesktopBuild {
  kind: "desktop";
  format: "vst3" | "au";
  os: "mac" | "windows";
  arch: string;
  file: string;
  sha256: string;
  size: number;
}

export type Build = KeyboardBuild | DesktopBuild;

export interface ReleaseSource {
  file: string;
  sha256: string;
  size: number;
}

export interface SynthDoc {
  id: string;
  name: string;
  summary: string;
  order: number;
  image?: string;
  origin: "own" | "external";
  descriptionHtml: string;
}

export interface ReleaseDoc {
  synth: string;
  version: string;
  date: string;
  tag: string;
  current?: boolean;
  source: ReleaseSource;
  builds: Build[];
  notesHtml: string;
}

export interface Content {
  synths: SynthDoc[];
  releases: ReleaseDoc[];
}

/**
 * content.json: what the portal app (Task 5) reads, written by the site builder. Every `url` and `image` is relative to
 * the site root (`firmware/<synth>/<version>/<file>`, `synths/<id>/<file>`), so it works under any base path.
 * Synths follow `order`; releases are newest first (numeric version order); each build was re-checked at build time.
 */
export interface PortalRelease {
  version: string;
  date: string;
  tag: string;
  /** true for exactly one release of a synth that has any: the one marked `current`, else the newest */
  current: boolean;
  notesHtml: string;
  source: ReleaseSource & { url: string };
  builds: Array<(KeyboardBuild | DesktopBuild) & { url: string }>;
}

export interface PortalSynth {
  id: string;
  name: string;
  summary: string;
  order: number;
  /** the synth's picture on the site, when it has one */
  image?: string;
  descriptionHtml: string;
  /** instrument ids (e.g. "smk37-pro") of the keyboard builds of any release */
  keyboards: string[];
  releases: PortalRelease[];
}

export interface PortalContent {
  /** ISO timestamp of the build */
  generated: string;
  synths: PortalSynth[];
}
