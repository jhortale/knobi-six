// SPDX-License-Identifier: GPL-3.0-only
// The field's rules (docs/design/portal/BRIEF.md, "Motion rules") and its geometry.
import { describe, expect, it } from "vitest";
import { buildField, buildThreads, layerOf, LINK_DIST, MAX_LINKS, mulberry32, PALETTE, rgb } from "../src/geometry.ts";
import {
  approach, approachParams, breathHz, frameRate, isSmall, MODES, particleBudget, type FieldMode,
} from "../src/tuning.ts";

const modes = Object.keys(MODES) as FieldMode[];

describe("tuning", () => {
  it("calms the field while writing: fewer particles, a lower frame rate, a slower clock", () => {
    const w = MODES.writing;
    for (const m of modes.filter((x) => x !== "writing")) {
      expect(w.density).toBeLessThanOrEqual(MODES[m].density);
      expect(frameRate("writing", false)).toBeLessThanOrEqual(frameRate(m, false));
    }
    expect(frameRate("writing", false)).toBeLessThanOrEqual(24);
    expect(w.speed).toBeLessThan(1);
    expect(w.stream).toBe(1);
  });

  it("caps phones at 30 fps (20 while writing) and gives them fewer particles", () => {
    for (const m of modes) expect(frameRate(m, true)).toBeLessThanOrEqual(30);
    expect(frameRate("writing", true)).toBe(20);
    expect(isSmall(375, 812)).toBe(true);
    expect(isSmall(1280, 800)).toBe(false);
    expect(isSmall(1280, 800, true)).toBe(true);
    expect(particleBudget(375, 812)).toBeLessThan(particleBudget(1280, 800));
    expect(particleBudget(10, 10)).toBe(300);
    expect(particleBudget(5000, 3000)).toBe(1600);
  });

  it("errors dim and scatter, searching pulses, found gathers", () => {
    expect(MODES.error.bright).toBeLessThan(MODES.idle.bright);
    expect(MODES.error.scatter).toBe(1);
    expect(MODES.searching.pulse).toBe(1);
    expect(MODES.found.converge).toBe(1);
    expect(MODES.done.bright).toBeGreaterThan(MODES.idle.bright);
    expect(MODES.idle.breath).toBeGreaterThan(0);
  });

  it("approaches a target independent of the frame rate", () => {
    let a = 0, b = 0;
    for (let i = 0; i < 60; i++) a = approach(a, 1, 1 / 60, 0.5);
    for (let i = 0; i < 24; i++) b = approach(b, 1, 1 / 24, 0.5);
    expect(a).toBeCloseTo(b, 6);
    expect(a).toBeCloseTo(1 - Math.exp(-2), 6);
    expect(approach(0.3, 1, 0.1, 0)).toBe(1);
  });

  it("moves every parameter in place", () => {
    const cur = { ...MODES.idle };
    approachParams(cur, MODES.error, 10, 0.1);
    for (const k of Object.keys(cur) as (keyof typeof cur)[]) expect(cur[k]).toBeCloseTo(MODES.error[k], 6);
  });

  it("breathes once per bar", () => expect(breathHz(120)).toBe(0.5));
});

describe("geometry", () => {
  const g = buildField(1200);

  it("is the same field on every visit", () => {
    const h = buildField(1200);
    expect(h.position).toEqual(g.position);
    expect(h.pairs).toEqual(g.pairs);
    expect(mulberry32(1)()).toBe(mulberry32(1)());
  });

  it("puts particles on the screen, ranked for the density fade", () => {
    for (let i = 0; i < g.count; i++) {
      expect(Math.abs(g.position[i * 3])).toBeLessThanOrEqual(1);
      expect(Math.abs(g.position[i * 3 + 1])).toBeLessThanOrEqual(1);
      expect(g.position[i * 3 + 2]).toBeCloseTo(i / g.count, 6);
    }
  });

  it("draws the colony and the streams first (lowest ranks), so a lower density only drops free drifters", () => {
    for (let i = 0; i < g.count; i++) {
      const role = g.seed[i * 4 + 3];
      expect(role).toBeGreaterThanOrEqual(i / g.count);
      expect(role).toBeLessThan((i + 1) / g.count);
    }
    // everything with a role (< 0.26) is inside the writing state's 40 %
    expect(0.26).toBeLessThan(MODES.writing.density);
  });

  it("uses the palette, mostly cool, a few warm sparks", () => {
    const warm = rgb(PALETTE.at(-1)!.hex);
    let n = 0;
    for (let i = 0; i < g.count; i++) if (g.color[i * 3] === Math.fround(warm[0])) n++;
    expect(n / g.count).toBeGreaterThan(0.01);
    expect(n / g.count).toBeLessThan(0.12);
  });

  it("links neighbours in the same layer, at most MAX_LINKS each, sorted for the draw range", () => {
    const degree = new Map<number, number>();
    expect(g.pairs.length / 2).toBeGreaterThan(g.count * 0.3);
    for (let k = 0; k < g.pairs.length; k += 2) {
      const i = g.pairs[k], j = g.pairs[k + 1];
      expect(j).toBeLessThan(i);
      if (k) expect(i).toBeGreaterThanOrEqual(g.pairs[k - 2]);
      expect(layerOf(g.seed[i * 4 + 2])).toBe(layerOf(g.seed[j * 4 + 2]));
      const dx = (g.position[i * 3] - g.position[j * 3]) * 1.76, dy = (g.position[i * 3 + 1] - g.position[j * 3 + 1]) * 1.1;
      expect(Math.hypot(dx, dy)).toBeLessThan(LINK_DIST);
      for (const p of [i, j]) degree.set(p, (degree.get(p) ?? 0) + 1);
    }
    for (const d of degree.values()) expect(d).toBeLessThanOrEqual(MAX_LINKS);
  });

  it("knows how many threads fit under any particle count", () => {
    for (const n of [0, 1, 100, 480, 1200]) {
      const want = [...Array(g.pairs.length / 2).keys()].filter((k) => g.pairs[k * 2] < n).length;
      expect(g.pairsBefore[n]).toBe(want);
    }
  });

  it("gives every thread vertex its own end and the other", () => {
    const th = buildThreads(g);
    expect(th.position.length).toBe(g.pairs.length * 3);
    const i = g.pairs[0], j = g.pairs[1];
    expect([...th.position.subarray(0, 3)]).toEqual([...g.position.subarray(i * 3, i * 3 + 3)]);
    expect([...th.other.subarray(0, 3)]).toEqual([...g.position.subarray(j * 3, j * 3 + 3)]);
    expect([...th.position.subarray(3, 6)]).toEqual([...g.position.subarray(j * 3, j * 3 + 3)]);
    expect([...th.otherSeed.subarray(4, 8)]).toEqual([...g.seed.subarray(i * 4, i * 4 + 4)]);
  });

  it("reads colours", () => {
    expect(rgb("#2f6be0")).toEqual([0x2f / 255, 0x6b / 255, 0xe0 / 255]);
    expect(rgb("#fff")).toEqual([1, 1, 1]);
    expect(() => rgb("nope")).toThrow();
  });
});
