// SPDX-License-Identifier: GPL-3.0-only
// port of tools/fm1_install.py: Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments;
// M. Geisler (genkigenki/makkuro)
// Identities ("SMK-37 Pro_956", "ota-SMK-37 Pro_900") and the installer's errors.
import type { Identity } from "./messages.ts";

export type ErrorCode =
  | "usage" | "badpkg" | "notfound" | "model" | "lost" | "stopped" | "badreq" | "noloader" | "noreturn"
  | "mismatch" | "foreign" | "denied" | "hash";

export class InstallError extends Error {
  readonly code: ErrorCode;
  constructor(code: ErrorCode, message: string) {
    super(message);
    this.name = "InstallError";
    this.code = code;
  }
}

/** "ota-SMK-37 Pro_900" -> "SMK-37 Pro" */
export function modelOf(text: string): string {
  const m = text.split("_")[0];
  return m.startsWith("ota-") ? m.slice(4) : m;
}

/** the device runs an update loader (its model starts with "ota-") */
export function isLoader(id: Identity): boolean {
  return id.model.toLowerCase().startsWith("ota-");
}

/** a and b are the same hardware: the same model, or both in the family (familyMatch is anchored) */
export function sameKeyboard(a: string, b: string, familyMatch: RegExp): boolean {
  const ma = modelOf(a), mb = modelOf(b);
  return ma === mb || (familyMatch.test(ma) && familyMatch.test(mb));
}

/** a Felucca-family update loader, which this installer may resume (ownLoader is anchored) */
export function isOwnLoader(text: string, ownLoader: RegExp): boolean {
  return ownLoader.test(text);
}
