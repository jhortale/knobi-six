// SPDX-License-Identifier: GPL-3.0-only
// catalog.json, schema 1: what knobi-flash offers. See docs/superpowers/specs/2026-11-20-web-installer-design.md.

export interface Protocol {
  /** Felucca-family update loaders this installer may resume (fm1_install.py OWN_LOADER_RE) */
  ownLoader: string;
}

export interface Family {
  name: string;
  models: string[];
  /** models that are the same hardware (fm1_install.py SMK_FAMILY) */
  match: string;
  /** MIDI port names worth probing (fm1_install.py PORT_RE) */
  ports: string;
}

export interface Instrument {
  id: string;
  name: string;
  family: string;
  model: string;
  protocol: string;
}

export interface Package {
  instrument: string;
  /** path relative to the site */
  file: string;
  identity: string;
  sha256: string;
  size: number;
}

export interface Release {
  version: string;
  date: string;
  /** the GPL source archive, relative to the site */
  source?: string;
  packages: Package[];
}

export interface Firmware {
  id: string;
  name: string;
  summary: string;
  license: string;
  notes: string[];
  releases: Release[];
}

export interface Catalog {
  schema: 1;
  generated?: string;
  protocols: Record<string, Protocol>;
  families: Record<string, Family>;
  instruments: Instrument[];
  firmwares: Firmware[];
}
