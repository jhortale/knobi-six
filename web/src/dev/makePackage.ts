// SPDX-License-Identifier: GPL-3.0-only
// port of tests/install_test.py's package(): Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments;
// M. Geisler (genkigenki/makkuro)
// A package with a valid header and file list (one file, flash.bin: 0x400 to the end), as fm1pkg_make writes.
import { KEEP, LOADER_MARK, crc16, jlDec } from "@knobi/installer-core";

export interface PackageOpts {
  product?: string;
  marker?: boolean;
  size?: number;
  damage?: boolean;
  blocks?: number;
  seed?: number;
}

const le = (v: number, n: number) => Array.from({ length: n }, (_, i) => (v >>> (8 * i)) & 0xff);

export function makePackage({ product = "FM-1_900", marker = true, size = 0x2000, damage = false, blocks = 20, seed = 7 }: PackageOpts = {}): Uint8Array {
  const img = Uint8Array.from({ length: size }, (_, i) => (i * seed) & 0xff);
  img.set(marker ? LOADER_MARK : new Uint8Array(16), 0x1800);
  const data = img.slice(0x400);
  const e = new Uint8Array(0x50);
  e.set([0, 0, 0, 0, ...le(crc16(data), 2), 0, 0, ...le(0x400, 4), ...le(data.length, 4)], 0);
  e.set(new TextEncoder().encode("flash.bin"), 0x40);
  const lst = jlDec(e);
  const hdr = new Uint8Array(0x40);
  hdr.set(le(size, 4), 4);
  hdr.set(le(1, 2), 8);
  hdr.set(le(crc16(lst), 2), 2);
  hdr.set(le(crc16(hdr.subarray(2, 0x40)), 2), 0);
  img.set(jlDec(hdr), 0);
  img.set(lst, 0x40);
  if (damage) img[0x1000] ^= 1;
  const out: number[] = [];
  for (let i = 0; i < blocks; i++) {
    const m = i < product.length ? (product.charCodeAt(i) + i + 1) & 0xff : 0x7d;
    out.push(...img.subarray(i * KEEP, (i + 1) * KEEP), m);
  }
  out.push(...img.subarray(blocks * KEEP));
  return Uint8Array.from(out);
}
