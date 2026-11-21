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
