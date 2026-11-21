// SPDX-License-Identifier: GPL-3.0-only
// The page's state → the living field's state.
import { describe, expect, it } from "vitest";
import { fieldModeOf, fieldTargetOf } from "../src/index.ts";
import type { InstallerState } from "../src/index.ts";

const base: InstallerState = {
  catalogLoaded: true, device: { kind: "none" }, offers: [], selected: null, resume: false, backup: false,
  searchAll: false, busy: false, steps: { connect: "", verify: "", update: "", write: "", restart: "", done: "" },
  pct: 0, error: null, result: null, log: [], confirm: null,
};
const at = (patch: Partial<InstallerState>) => fieldModeOf({ ...base, ...patch });

describe("fieldModeOf", () => {
  it("follows the device", () => {
    expect(at({})).toBe("idle");
    expect(at({ device: { kind: "searching" } })).toBe("searching");
    expect(at({ device: { kind: "found", text: "SMK-37 Pro_016", port: "X0X FM-1", loader: false } })).toBe("found");
    expect(at({ device: { kind: "notfound", inputs: [] } })).toBe("idle");
    expect(at({ device: { kind: "foreign", text: "x", port: "y" } })).toBe("error");
  });

  it("is calm whenever the page talks to the keyboard, even with an error on the way", () => {
    expect(at({ busy: true, pct: 40 })).toBe("writing");
    expect(at({ busy: true, error: { message: "lost" } })).toBe("writing");
  });

  it("blooms when done, scatters on an error", () => {
    expect(at({ result: "SMK-37 Pro_956" })).toBe("done");
    expect(at({ error: { message: "stopped" }, device: { kind: "found", text: "a", port: "b", loader: true } })).toBe("error");
  });

  it("moves the colony to the progress bar once an install has started", () => {
    expect(fieldTargetOf("found")).toBe("device");
    expect(fieldTargetOf("searching")).toBe("device");
    expect(fieldTargetOf("writing")).toBe("progress");
    expect(fieldTargetOf("done")).toBe("progress");
  });
});
