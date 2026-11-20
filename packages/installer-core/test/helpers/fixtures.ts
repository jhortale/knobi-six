// SPDX-License-Identifier: GPL-3.0-only
// test/fixtures/python.json: what tools/fm1_install.py answers (tests/web_fixtures.py).
import { readFileSync } from "node:fs";

type Parsed = { flags: number; addr: number; len: number } | null;
export interface Fixtures {
  wire: {
    pack7: { in: string; out: string }[];
    response: { addr: number; data: string; flags: number; out: string }[];
    request: { pkt: string; parsed: Parsed }[];
    identity: { text: string; pkt: string; parsed: { text: string; model: string; version: number } }[];
  };
  packages: {
    params: { product: string; marker?: boolean; size?: number; damage?: boolean; blocks?: number };
    raw: string; product: string; blocks: number; imageSha256: string; damage: string | null; loaderMark: boolean;
  }[];
}

export const fixtures: Fixtures = JSON.parse(
  readFileSync(new URL("../fixtures/python.json", import.meta.url), "utf8"),
);

export const hex = (b: Uint8Array) => Buffer.from(b).toString("hex");
export const unhex = (s: string) => new Uint8Array(Buffer.from(s, "hex"));
export const unb64 = (s: string) => new Uint8Array(Buffer.from(s, "base64"));
