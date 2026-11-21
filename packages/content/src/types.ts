// The content model (docs/superpowers/specs/2026-11-20-portal-design.md). Browser-safe: types only.

/** Portable Text blocks, kept opaque here: the site renders them, this package never reads inside. */
export type PortableText = Array<Record<string, unknown>>;

export interface Synth {
  _id: string;
  id: string;
  name: string;
  summary: string;
  description?: PortableText;
  image?: unknown;
  origin: "own" | "external";
  order: number;
}

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

export interface Release {
  _id: string;
  synth: { _ref: string };
  version: string;
  date: string;
  tag: string;
  notes?: PortableText;
  current?: boolean;
  source: ReleaseSource;
  builds: Build[];
}

export interface PublishedContent {
  synths: Synth[];
  releases: Release[];
}
