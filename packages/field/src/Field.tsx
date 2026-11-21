// SPDX-License-Identifier: GPL-3.0-only
// midi·chlorian's living field: a fixed layer of glowing organisms behind the page (React Three Fiber). It leans
// toward the cursor, pulses while searching, gathers around the keyboard when found, streams into it while writing,
// blooms when done and draws back on an error.
//
// Performance is a hard constraint (the page answers the keyboard's data requests in real time): all motion is in
// the vertex shader, a frame sets a few uniforms and allocates nothing, the frame rate is capped per state
// (tuning.ts, 24 fps while writing) and the field stops when the tab is hidden. With prefers-reduced-motion it draws
// one still frame and stays still.
import { Canvas, useFrame, useThree } from "@react-three/fiber";
import { Component, useEffect, useMemo, useRef, useState, type CSSProperties, type ReactNode } from "react";
import {
  AdditiveBlending, BufferAttribute, BufferGeometry, LineSegments, Points, ShaderMaterial, Vector2, Vector3,
} from "three";
import { buildField, buildThreads, rgb } from "./geometry.ts";
import { pointsFragment, pointsVertex, threadsFragment, threadsVertex } from "./shaders.ts";
import {
  approach, approachParams, BLOOM, breathHz, frameRate, isSmall, MODES, particleBudget, SETTLE, type FieldMode,
  type ModeParams,
} from "./tuning.ts";

export interface FieldProps {
  mode: FieldMode;
  /** the write's progress, 0..100 */
  progress?: number;
  /** the row the colony gathers at (the device status, the progress bar) */
  target?: HTMLElement | null;
  /** the opaque panel the colony hugs from outside (its left and right edges); defaults to the target */
  frame?: HTMLElement | null;
  /** the synth's colour signature: the colony and the streams take it */
  accent?: string;
  /** the breathing shimmer's tempo (one breath per bar) */
  bpm?: number;
  className?: string;
  style?: CSSProperties;
}

/** WebGL2 is there (the field draws nothing otherwise; the page's CSS backdrop stays) */
export function supportsField(): boolean {
  return typeof window !== "undefined" && "WebGL2RenderingContext" in window;
}

function useMedia(query: string): boolean {
  const [on, setOn] = useState(() => typeof matchMedia === "function" && matchMedia(query).matches);
  useEffect(() => {
    if (typeof matchMedia !== "function") return;
    const m = matchMedia(query);
    const fn = () => setOn(m.matches);
    fn();
    m.addEventListener("change", fn);
    return () => m.removeEventListener("change", fn);
  }, [query]);
  return on;
}

/** no WebGL context after all (blocked, lost, out of contexts): draw nothing rather than break the page */
class Quiet extends Component<{ children: ReactNode }, { failed: boolean }> {
  state = { failed: false };
  static getDerivedStateFromError() {
    return { failed: true };
  }
  render() {
    return this.state.failed ? null : this.props.children;
  }
}

export function Field({ mode, progress = 0, target, frame, accent = "#2f6be0", bpm = 120, className, style }: FieldProps) {
  const reduced = useMedia("(prefers-reduced-motion: reduce)");
  const coarse = useMedia("(pointer: coarse)");
  const [ok] = useState(supportsField);
  // sized once for the screen it opens on: a resize only changes the aspect, never reallocates
  const [shape] = useState(() => {
    const w = typeof innerWidth === "number" ? innerWidth : 1280, h = typeof innerHeight === "number" ? innerHeight : 800;
    return { small: isSmall(w, h, coarse), count: particleBudget(w, h) };
  });
  const small = shape.small || coarse;
  return (
    <div aria-hidden="true" className={className}
      style={{ position: "fixed", inset: 0, zIndex: 0, pointerEvents: "none", ...style }}>
      {ok && (
        <Quiet>
          <Canvas frameloop="demand" dpr={[1, small ? 1.5 : 2]} style={{ pointerEvents: "none" }}
            gl={{ antialias: false, alpha: true, depth: false, stencil: false, powerPreference: "low-power" }}>
            <Scene mode={mode} progress={progress} target={target ?? null} frame={frame ?? null} accent={accent}
              bpm={bpm} reduced={reduced} small={small} count={small ? Math.min(shape.count, 500) : shape.count} />
          </Canvas>
        </Quiet>
      )}
    </div>
  );
}

// ------------------------------------------------------------------------------------------------- the scene ---

function makeField(count: number) {
  const g = buildField(count);
  const th = buildThreads(g);
  const f = (value: number) => ({ value });
  const uniforms = {
    uTime: f(8), uAspect: f(1.6), uBright: f(0), uPointerOn: f(0), uPointerK: f(0), uConverge: f(0), uPulse: f(0),
    uStream: f(0), uScatter: f(0), uBreath: f(0), uBeat: f(0.5), uBloom: f(0), uBloomR: f(0), uPct: f(0),
    uDensity: f(1), uThreads: f(1), uSize: f(2.2), uDpr: f(1),
    uPointer: { value: new Vector2(9, 9) }, uL: { value: new Vector2(-1, 0) }, uR: { value: new Vector2(1, 0) },
    uAccent: { value: new Vector3(0.18, 0.42, 0.88) },
  };
  const material = (vertexShader: string, fragmentShader: string) => new ShaderMaterial({
    uniforms, vertexShader, fragmentShader, transparent: true, depthWrite: false, depthTest: false,
    blending: AdditiveBlending,
  });

  const pg = new BufferGeometry();
  pg.setAttribute("position", new BufferAttribute(g.position, 3));
  pg.setAttribute("aSeed", new BufferAttribute(g.seed, 4));
  pg.setAttribute("aColor", new BufferAttribute(g.color, 3));
  const points = new Points(pg, material(pointsVertex, pointsFragment));
  points.frustumCulled = false;
  points.renderOrder = 1;

  const tg = new BufferGeometry();
  tg.setAttribute("position", new BufferAttribute(th.position, 3));
  tg.setAttribute("aSeed", new BufferAttribute(th.seed, 4));
  tg.setAttribute("aColor", new BufferAttribute(th.color, 3));
  tg.setAttribute("aOther", new BufferAttribute(th.other, 3));
  tg.setAttribute("aSeedO", new BufferAttribute(th.otherSeed, 4));
  const threads = new LineSegments(tg, material(threadsVertex, threadsFragment));
  threads.frustumCulled = false;

  const dispose = () => {
    for (const o of [points, threads]) {
      o.geometry.dispose();
      (o.material as ShaderMaterial).dispose();
    }
  };
  return { g, points, threads, uniforms, dispose };
}

interface SceneProps {
  mode: FieldMode;
  progress: number;
  target: HTMLElement | null;
  frame: HTMLElement | null;
  accent: string;
  bpm: number;
  reduced: boolean;
  small: boolean;
  count: number;
}

function Scene(props: SceneProps) {
  const { mode, accent, bpm, reduced, small, count } = props;
  const invalidate = useThree((s) => s.invalidate);
  const gl = useThree((s) => s.gl);
  const field = useMemo(() => makeField(count), [count]);
  useEffect(() => field.dispose, [field]);

  // the latest props for the frame loop, without re-subscribing it
  const now = useRef(props);
  now.current = props;
  const cur = useRef<ModeParams>({ ...MODES[mode] });
  const live = useRef({
    t: 8, pct: 0, bloomT: 99, dirty: true, snap: true,
    rawX: 0, rawY: 0, rawOn: 0, px: 0, py: 0, on: 0,
    gl: [-1, 0], gr: [1, 0], l: [-1, 0], r: [1, 0],
  });

  // a new state: the done bloom starts, the colony moves to its new row
  const prev = useRef(mode);
  useEffect(() => {
    if (mode === "done" && prev.current !== "done") live.current.bloomT = 0;
    prev.current = mode;
    live.current.dirty = true;
    invalidate();
  }, [mode, invalidate]);

  useEffect(() => {
    const [r, g, b] = rgb(accent);
    field.uniforms.uAccent.value.set(r, g, b);
    field.uniforms.uBeat.value = breathHz(bpm);
    invalidate();
  }, [accent, bpm, field, invalidate]);

  // the cursor (or a finger): read from the window, the canvas never takes pointer events
  useEffect(() => {
    if (reduced) return;
    const L = live.current;
    const move = (e: PointerEvent) => {
      L.rawX = e.clientX;
      L.rawY = e.clientY;
      L.rawOn = 1;
    };
    const leave = (e: PointerEvent) => {
      if (e.pointerType !== "mouse" || !e.relatedTarget) L.rawOn = 0;
    };
    addEventListener("pointermove", move, { passive: true });
    addEventListener("pointerdown", move, { passive: true });
    addEventListener("pointerup", leave, { passive: true });
    document.addEventListener("pointerout", leave, { passive: true });
    return () => {
      removeEventListener("pointermove", move);
      removeEventListener("pointerdown", move);
      removeEventListener("pointerup", leave);
      document.removeEventListener("pointerout", leave);
    };
  }, [reduced]);

  // where the panel is: re-read on scroll, resize and when the panel changes size (never every frame)
  useEffect(() => {
    const L = live.current;
    const mark = () => {
      L.dirty = true;
      if (reduced) invalidate();
    };
    addEventListener("scroll", mark, { passive: true });
    addEventListener("resize", mark, { passive: true });
    const ro = typeof ResizeObserver === "function" ? new ResizeObserver(mark) : null;
    for (const el of [props.frame, props.target]) if (el) ro?.observe(el);
    mark();
    return () => {
      removeEventListener("scroll", mark);
      removeEventListener("resize", mark);
      ro?.disconnect();
    };
  }, [props.frame, props.target, reduced, invalidate]);

  // the clock: a frame-rate ceiling per state; nothing while the tab is hidden; one still frame when reduced
  useEffect(() => {
    if (reduced) {
      live.current.snap = true;
      invalidate();
      return;
    }
    const step = 1000 / frameRate(mode, small);
    let id = 0, last = 0;
    const tick = (t: number) => {
      id = requestAnimationFrame(tick);
      if (document.hidden || t - last < step - 2) return;
      last = t;
      invalidate();
    };
    id = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(id);
  }, [mode, small, reduced, invalidate]);

  useFrame((_, delta) => {
    const P = now.current, L = live.current, c = cur.current, u = field.uniforms;
    const dt = Math.min(delta, 0.1);
    const W = innerWidth, H = innerHeight, aspect = W / H;
    const goal = MODES[P.mode];
    const snap = L.snap || P.reduced;
    L.snap = false;

    if (snap) Object.assign(c, goal);
    else approachParams(c, goal, dt, SETTLE[P.mode]);
    if (!P.reduced) L.t += dt * c.speed;

    if (L.dirty) {
      L.dirty = false;
      const fr = (P.frame ?? P.target)?.getBoundingClientRect();
      const tr = (P.target ?? P.frame)?.getBoundingClientRect();
      const cy = tr ? tr.top + tr.height / 2 : H / 2;
      const left = fr ? fr.left : W * 0.2, right = fr ? fr.right : W * 0.8;
      L.gl[0] = (left / W * 2 - 1) * aspect;
      L.gr[0] = (right / W * 2 - 1) * aspect;
      L.gl[1] = L.gr[1] = 1 - (cy / H) * 2;
    }
    const tt = snap ? 0 : 0.15;
    for (let k = 0; k < 2; k++) {
      L.l[k] = approach(L.l[k], L.gl[k], dt, tt);
      L.r[k] = approach(L.r[k], L.gr[k], dt, tt);
    }

    const tx = (L.rawX / W * 2 - 1) * aspect, ty = 1 - (L.rawY / H) * 2;
    L.px = L.on < 0.01 ? tx : approach(L.px, tx, dt, 0.12);
    L.py = L.on < 0.01 ? ty : approach(L.py, ty, dt, 0.12);
    L.on = P.reduced ? 0 : approach(L.on, L.rawOn, dt, 0.3);
    L.pct = snap ? P.progress / 100 : approach(L.pct, P.progress / 100, dt, 0.25);
    if (!P.reduced) L.bloomT += dt;
    const bloom = P.reduced ? 0 : Math.exp(-L.bloomT / BLOOM.fade);

    u.uTime.value = L.t;
    u.uAspect.value = aspect;
    u.uDpr.value = gl.getPixelRatio();
    u.uBright.value = c.bright;
    u.uPointerK.value = c.pointer;
    u.uPointerOn.value = L.on;
    u.uPointer.value.set(L.px, L.py);
    u.uConverge.value = c.converge;
    u.uPulse.value = c.pulse;
    u.uStream.value = c.stream;
    u.uScatter.value = c.scatter;
    u.uBreath.value = c.breath;
    u.uDensity.value = c.density;
    u.uThreads.value = c.threads;
    u.uPct.value = L.pct;
    u.uBloom.value = bloom;
    u.uBloomR.value = L.bloomT * BLOOM.speed;
    u.uL.value.set(L.l[0], L.l[1]);
    u.uR.value.set(L.r[0], L.r[1]);

    // draw only what is visible: the density fade ends at `density`, the rest is skipped
    const n = Math.min(count, Math.ceil(count * Math.min(1, c.density + 0.02)));
    field.points.geometry.setDrawRange(0, n);
    field.threads.geometry.setDrawRange(0, field.g.pairsBefore[n] * 2);
  });

  return (
    <>
      <primitive object={field.threads} />
      <primitive object={field.points} />
    </>
  );
}
