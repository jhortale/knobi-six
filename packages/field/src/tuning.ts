// SPDX-License-Identifier: GPL-3.0-only
// What the field does in each state, as numbers: no three.js here, so the app can import the types for free and the
// tests can check the rules (the brief: docs/design/portal/BRIEF.md, "Theme and motion").

/** the page's state, as the field sees it */
export type FieldMode = "idle" | "searching" | "found" | "writing" | "done" | "error";

export interface ModeParams {
  /** share of the particles drawn (0..1): fewer while writing */
  density: number;
  /** frame-rate ceiling on a desktop */
  fps: number;
  /** speed of the field's own clock (calmer while writing) */
  speed: number;
  /** overall brightness */
  bright: number;
  /** how strongly particles lean toward the cursor */
  pointer: number;
  /** the colony gathering around the device (0..1) */
  converge: number;
  /** sonar rings while searching */
  pulse: number;
  /** particles streaming into the keyboard while writing */
  stream: number;
  /** dim and scatter on an error */
  scatter: number;
  /** breathing shimmer in time with the music */
  breath: number;
  /** the faint light threads between neighbours */
  threads: number;
}

export const MODES: Record<FieldMode, ModeParams> = {
  idle:      { density: 1,   fps: 60, speed: 1,   bright: 0.8,  pointer: 1,   converge: 0,   pulse: 0, stream: 0, scatter: 0, breath: 1,   threads: 1 },
  searching: { density: 1,   fps: 60, speed: 1,   bright: 0.85, pointer: 0.6, converge: 0,   pulse: 1, stream: 0, scatter: 0, breath: 0,   threads: 0.8 },
  found:     { density: 1,   fps: 60, speed: 1,   bright: 0.9,  pointer: 0.8, converge: 1,   pulse: 0, stream: 0, scatter: 0, breath: 0.6, threads: 1 },
  // the page answers the keyboard in real time: fewer particles, a lower frame rate, a slower clock
  writing:   { density: 0.4, fps: 24, speed: 0.5, bright: 0.75, pointer: 0.3, converge: 1,   pulse: 0, stream: 1, scatter: 0, breath: 0,   threads: 0.3 },
  done:      { density: 1,   fps: 60, speed: 1,   bright: 1.05, pointer: 1,   converge: 0.6, pulse: 0, stream: 0, scatter: 0, breath: 0.8, threads: 1.1 },
  error:     { density: 0.7, fps: 30, speed: 0.7, bright: 0.45, pointer: 0.4, converge: 0,   pulse: 0, stream: 0, scatter: 1, breath: 0,   threads: 0.2 },
};

/** seconds for a parameter to get ~63 % of the way to a new state (errors settle faster, never with a jolt) */
export const SETTLE: Record<FieldMode, number> = {
  idle: 0.9, searching: 0.6, found: 0.7, writing: 0.5, done: 0.8, error: 0.35,
};

/** the done bloom: how long it takes to fade, how far its ring travels (field units: 1 = half the screen height) */
export const BLOOM = { fade: 1.4, speed: 1.1 };

/** a phone, or anything small or touch-first: fewer particles, at most 30 fps */
export function isSmall(width: number, height: number, coarse = false): boolean {
  return coarse || Math.min(width, height) < 600;
}

/** how many particles a screen of this size gets (about one per 700 px², 300..1600) */
export function particleBudget(width: number, height: number): number {
  return Math.max(300, Math.min(1600, Math.round((width * height) / 700)));
}

export function frameRate(mode: FieldMode, small: boolean): number {
  return Math.min(MODES[mode].fps, small ? (mode === "writing" ? 20 : 30) : 60);
}

/** one smoothing step: move `cur` toward `target`, frame-rate independent */
export function approach(cur: number, target: number, dt: number, tau: number): number {
  return tau <= 0 ? target : cur + (target - cur) * (1 - Math.exp(-dt / tau));
}

/** move every parameter of `cur` toward `target` in place (no allocation: this runs every frame) */
export function approachParams(cur: ModeParams, target: ModeParams, dt: number, tau: number): void {
  for (const k in cur) {
    const key = k as keyof ModeParams;
    cur[key] = approach(cur[key], target[key], dt, tau);
  }
}

/** the breathing rate: one breath per bar of 4/4 at the given tempo */
export function breathHz(bpm: number): number {
  return bpm / 60 / 4;
}
