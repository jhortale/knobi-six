// SPDX-License-Identifier: GPL-3.0-only
// port of tools/fm1_install.py: Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments;
// M. Geisler (genkigenki/makkuro)
// 8-bit bytes in 7-bit MIDI data bytes, least significant bits first.

export function pack7(data: Uint8Array): Uint8Array {
  const out: number[] = [];
  let acc = 0, nb = 0;
  for (const b of data) {
    acc |= b << nb;
    nb += 8;
    while (nb >= 7) {
      out.push(acc & 0x7f);
      acc >>>= 7;
      nb -= 7;
    }
  }
  if (nb) out.push(acc & 0x7f);
  return Uint8Array.from(out);
}

export function unpack7(s: Uint8Array): Uint8Array {
  const out: number[] = [];
  let acc = 0, nb = 0;
  for (const b of s) {
    acc |= (b & 0x7f) << nb;
    nb += 7;
    while (nb >= 8) {
      out.push(acc & 0xff);
      acc >>>= 8;
      nb -= 8;
    }
  }
  return Uint8Array.from(out);
}
