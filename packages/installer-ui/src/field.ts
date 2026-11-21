// SPDX-License-Identifier: GPL-3.0-only
// The installer's state as the living field sees it (@knobi/field; docs/design/portal/BRIEF.md, "Theme and motion").
// Type-only import: three.js stays out of the page's first chunk.
import type { FieldMode } from "@knobi/field/tuning";
import type { InstallerState } from "./installer.ts";

export function fieldModeOf(s: InstallerState): FieldMode {
  // busy = the page talks to the keyboard in real time (check, confirm, write, restart): the calm field, always
  if (s.busy) return "writing";
  if (s.error) return "error";
  if (s.result) return "done";
  switch (s.device.kind) {
    case "searching": return "searching";
    case "found": return "found";
    case "foreign": return "error";       // refused: another firmware's update mode
    default: return "idle";               // nothing yet, or no keyboard answered: stay calm, the text says what to do
  }
}

/** where the colony gathers: beside the device status, or beside the progress bar once an install has started */
export function fieldTargetOf(mode: FieldMode): "device" | "progress" {
  return mode === "writing" || mode === "done" || mode === "error" ? "progress" : "device";
}
