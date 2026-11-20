// SPDX-License-Identifier: GPL-3.0-only
// port of tools/fm1_install.py: Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments;
// M. Geisler (genkigenki/makkuro)
// A .fwsc package: interleave blocks of 0x30 bytes whose last byte is an identity marker (0x7D: none), then the
// update image the device reads. Checked here before anything is sent: the device only checks the loader.
import { InstallError } from "./identity.ts";

export const BLOCKS = 20;
export const BLK = 0x30;
export const KEEP = 0x2f;
export const MAX_BLOCKS = 64;
export const LOADER_MARK = new TextEncoder().encode("FELUCCA-LOADER-1");

const rd = (b: Uint8Array, off: number, n: number) => {
  let v = 0;
  for (let i = n - 1; i >= 0; i--) v = v * 256 + b[off + i];
  return v;
};

const idChar = (m: number, i: number) => {
  const c = (m - i - 1) & 0xff;
  return c >= 0x20 && c < 0x7f;
};

function marker(raw: Uint8Array, k: number): number | null {
  return (k + 1) * BLK <= raw.length ? raw[k * BLK + KEEP] : null;
}

/** the identity blocks: 20, or more when the identity is longer and unused 0x7D markers follow (36-block layout) */
export function identityBlocks(raw: Uint8Array): number {
  let n = BLOCKS;
  for (;;) {
    const m = marker(raw, n);
    if (n >= MAX_BLOCKS || m === null || m === 0x7d || !idChar(m, n)) break;
    n++;
  }
  return n > BLOCKS && marker(raw, n) === 0x7d ? n : BLOCKS;
}

/** interleave blocks: the identity blocks, then more while the marker is the unused 0x7D */
export function blockCount(raw: Uint8Array): number {
  let n = identityBlocks(raw);
  while (n < MAX_BLOCKS && (n + 1) * BLK <= raw.length && raw[n * BLK + KEEP] === 0x7d) n++;
  return n;
}

/** the image the device reads: the package without the marker bytes */
export function logicalImage(raw: Uint8Array, blocks = BLOCKS): Uint8Array {
  const tail = raw.subarray(Math.min(blocks * BLK, raw.length));
  const out = new Uint8Array(blocks * KEEP + tail.length);
  for (let i = 0; i < blocks; i++) out.set(raw.subarray(i * BLK, i * BLK + KEEP), i * KEEP);
  out.set(tail, blocks * KEEP);
  return out.subarray(0, Math.min(out.length, blocks * KEEP + tail.length));
}

/** the logical image and its block count: the detected count when the files' CRCs agree, else 20 */
export function deinterleave(raw: Uint8Array): { image: Uint8Array; blocks: number } {
  const n = blockCount(raw);
  const image = logicalImage(raw, n);
  if (n !== BLOCKS && packageDamage(image)) {
    const alt = logicalImage(raw, BLOCKS);
    if (!packageDamage(alt)) return { image: alt, blocks: BLOCKS };
  }
  return { image, blocks: n };
}

/** the package identity ("SMK-37 Pro_956"): one marker byte after each identity block */
export function productOf(raw: Uint8Array): string {
  if (raw.length < BLOCKS * BLK) throw new InstallError("badpkg", "not a keyboard package (too short)");
  const ib = identityBlocks(raw);
  const n = ib === BLOCKS ? BLOCKS : Math.min(ib, deinterleave(raw).blocks);
  let s = "";
  for (let i = 0; i < n; i++) {
    const m = raw[i * BLK + KEEP];
    if (m !== 0x7d) s += String.fromCharCode((m - i - 1) & 0xff);
  }
  return s;
}

/** CRC-16/XMODEM, as the package and the device use */
export function crc16(data: Uint8Array, crc = 0): number {
  for (const b of data) {
    crc ^= b << 8;
    for (let k = 0; k < 8; k++) crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
  }
  return crc;
}

/** JieLi ENC stream cipher (its own inverse) */
export function jlDec(b: Uint8Array): Uint8Array {
  const out = Uint8Array.from(b);
  let key = 0xffff;
  for (let i = 0; i < out.length; i++) {
    out[i] ^= key & 0xff;
    key = ((key << 1) ^ (key & 0x8000 ? 0x1021 : 0)) & 0xffff;
  }
  return out;
}

/** what is wrong with the update image (header, file list, CRCs of flash.bin / ota.bin), or null */
export function packageDamage(image: Uint8Array): string | null {
  if (image.length < 0x40) return "too short";
  const hdr = jlDec(image.subarray(0, 0x40));
  if (crc16(hdr.subarray(2, 0x40)) !== rd(hdr, 0, 2)) return "header CRC";
  const count = rd(hdr, 8, 2);
  if (count < 1 || count > 8 || image.length < 0x40 + 0x50 * count) return "file list";
  const lst = image.subarray(0x40, 0x40 + 0x50 * count);
  if (crc16(lst) !== rd(hdr, 2, 2)) return "file list CRC";
  for (let i = 0; i < count; i++) {
    const e = jlDec(lst.subarray(i * 0x50, (i + 1) * 0x50));
    const dcrc = rd(e, 4, 2), off = rd(e, 8, 4), size = rd(e, 12, 4);
    const nameBytes = e.subarray(0x40, 0x50);
    const z = nameBytes.indexOf(0);
    const name = String.fromCharCode(...(z < 0 ? nameBytes : nameBytes.subarray(0, z)));
    if (off + size > image.length) return `${name}: cut short`;
    const typ = rd(e, 0, 2);
    // flash.bin (0) and ota.bin (100) carry the CRC of their bytes as served; the stock packer's other entries do not
    if ((typ === 0 || typ === 100) && crc16(image.subarray(off, off + size)) !== dcrc) return `${name}: CRC`;
  }
  return null;
}

export function hasLoaderMark(raw: Uint8Array): boolean {
  outer: for (let i = 0; i + LOADER_MARK.length <= raw.length; i++) {
    for (let k = 0; k < LOADER_MARK.length; k++) if (raw[i + k] !== LOADER_MARK[k]) continue outer;
    return true;
  }
  return false;
}

/** the package's identity and the image to serve; throws InstallError("badpkg") (the web page has no --force) */
export function loadPackage(raw: Uint8Array): { product: string; image: Uint8Array } {
  const product = productOf(raw);
  if (!/^[^_]+_\d+$/.test(product)) throw new InstallError("badpkg", `not a keyboard package (identity "${product}")`);
  if (!hasLoaderMark(raw)) {
    throw new InstallError("badpkg", "no Felucca update loader in this package; only Felucca-family packages are installed");
  }
  const { image } = deinterleave(raw);
  const bad = packageDamage(image);
  if (bad) throw new InstallError("badpkg", `damaged package (${bad}); nothing was written. Download it again.`);
  return { product, image };
}
