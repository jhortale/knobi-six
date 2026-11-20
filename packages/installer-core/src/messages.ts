// SPDX-License-Identifier: GPL-3.0-only
// port of tools/fm1_install.py: Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments;
// M. Geisler (genkigenki/makkuro)
// The update protocol's SysEx messages: the identity handshake, the upgrade command, the device's read requests
// and the host's answers.
import { pack7, unpack7 } from "./sevenbit.ts";

export const HS_QUERY = Uint8Array.of(0xf0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7f, 0xf7);
export const UPGRADE = Uint8Array.of(0xf0, 0x22, 0x24, 0x35, 0x7f, 0xf7);
export const FINISH_CHECK = 0xe0000000;
export const FINISH_WRITE = 0xf0000000;
export const MAXDATA = 512;
export const SUCCESS = new TextEncoder().encode("success\0");

export interface Identity {
  text: string;
  model: string;
  version: number;
}

export interface Request {
  flags: number;
  addr: number;
  len: number;
}

const le = (v: number, n: number) => Array.from({ length: n }, (_, i) => Math.floor(v / 2 ** (8 * i)) & 0xff);
const rdLe = (b: Uint8Array, off: number, n: number) => {
  let v = 0;
  for (let i = n - 1; i >= 0; i--) v = v * 256 + b[off + i];
  return v;
};
const sum = (b: Uint8Array | number[]) => {
  let s = 0;
  for (const x of b) s += x;
  return s;
};
const sysex = (body: Uint8Array) => {
  const p = pack7(body);
  const out = new Uint8Array(p.length + 2);
  out[0] = 0xf0;
  out.set(p, 1);
  out[out.length - 1] = 0xf7;
  return out;
};
const framed = (pkt: Uint8Array) => pkt.length >= 2 && pkt[0] === 0xf0 && pkt[pkt.length - 1] === 0xf7;

/** the host's answer to a read request: the data at addr */
export function response(addr: number, data: Uint8Array, flags = 0): Uint8Array {
  const n = data.length;
  const body = [0x00, 0x59, 0x30, ...le(n + 8, 2), 0, flags, ...le(addr, 4), ...le(n, 3), ...data];
  body.push(~sum(body.slice(6)) & 0xff);
  return sysex(Uint8Array.from(body));
}

/** a read request (command 0x30) from the device, or null */
export function parseRequest(pkt: Uint8Array): Request | null {
  if (!framed(pkt)) return null;
  const u = unpack7(pkt.subarray(1, -1));
  if (u.length !== 15 || u[0] !== 0x00 || u[1] !== 0x59 || u[2] !== 0x30) return null;
  if ((~sum(u.subarray(6, 14)) & 0xff) !== u[14]) return null;
  return { flags: u[6], addr: rdLe(u, 7, 4), len: rdLe(u, 11, 3) };
}

/** the device's identity reply (command 0x11, 27 bytes of text): "SMK-37 Pro_956" -> model and version */
export function parseIdentity(pkt: Uint8Array): Identity | null {
  if (!framed(pkt)) return null;
  const d = unpack7(pkt.subarray(1, -1));
  if (d.length !== 34 || d[0] !== 0x00 || d[1] !== 0x59 || d[2] !== 0x11) return null;
  const raw = d.subarray(6, 33);
  const end = raw.indexOf(0);
  const text = String.fromCharCode(...(end < 0 ? raw : raw.subarray(0, end)));
  const m = /^([^_]+)_(\d+)$/.exec(text);
  return m ? { text, model: m[1], version: parseInt(m[2], 10) } : null;
}

/** what a device sends in answer to HS_QUERY (used by the simulated keyboard and the tests) */
export function identityReply(text: string): Uint8Array {
  const t = Array.from(text, (c) => c.charCodeAt(0) & 0xff);
  return sysex(Uint8Array.from([0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)]));
}

/** a read request as the device sends it (used by the simulated keyboard) */
export function request(addr: number, len: number, flags = 0): Uint8Array {
  const u = [0, 0x59, 0x30, 0, 0, 0, flags, ...le(addr, 4), ...le(len, 3)];
  u.push(~sum(u.slice(6)) & 0xff);
  return sysex(Uint8Array.from(u));
}
