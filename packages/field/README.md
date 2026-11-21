# @knobi/field

midi·chlorian's living field: glowing organisms drifting behind the page, in React Three Fiber. It reacts to the
cursor and to the installer's state (the brief: `docs/design/portal/BRIEF.md`, "Theme and motion").

```tsx
const Field = lazy(() => import("@knobi/field"));           // brings three.js: always lazy
import type { FieldMode } from "@knobi/field/tuning";       // types and tuning, no three.js

<Suspense fallback={null}>
  <Field mode="found" progress={pct} frame={panelEl} target={deviceRowEl} accent="#2f6be0" />
</Suspense>
```

`frame` is the opaque panel: the colony gathers just outside its left and right edges, at the height of `target`.
In the installer, `apps/installer/src/ui/field.ts` maps the page's state to a mode and picks the row.

## Motion

| mode | what moves | particles | fps (desktop / phone) |
|---|---|---|---|
| `idle` | slow drift in three depth layers (parallax), twinkle, threads between neighbours, a breath once per bar (`bpm`, 120) | 100 % | 60 / 30 |
| `searching` | sonar rings travel out from the panel's edges, the particles they cross brighten and step aside | 100 % | 60 / 30 |
| `found` | ~12 % gather into a colony orbiting just outside the panel at the device's row, in the synth's `accent` | 100 % | 60 / 30 |
| `writing` | ~14 % stream into the panel at the progress bar's row; colony members light up as `progress` rises | 40 % (free drifters fade, the colony and streams stay) | 24 / 20 |
| `done` | one bloom ring from the panel (fades over 1.4 s), then the field settles brighter | 100 % | 60 / 30 |
| `error` | the field dims, loses colour and draws back from the panel, settling in 0.35 s, never a jolt | 70 % | 30 / 30 |

**The cursor (or a finger).** Particles within ~0.25 of the screen height lean toward it, swirl a little around it
and brighten; the threads near it light up. Read from `window` (`pointermove`), so the canvas never takes pointer
events. The pull eases in and out (0.3 s) and follows with a 0.12 s lag.

**Transitions.** Every parameter eases toward the new mode (exponential, `SETTLE` in `tuning.ts`, 0.35–0.9 s), frame
rate independent. Particle count changes fade by rank, nothing pops.

**prefers-reduced-motion.** One still frame of the current mode (the colony in place, no rings, no bloom, no cursor
pull), redrawn only when the state or the layout changes. Without WebGL2 the field draws nothing and the page's CSS
backdrop (`apps/installer/src/index.css`) is the whole look.

## Performance

The page answers the keyboard's data requests in real time, so:

- all motion is in the vertex shader (`shaders.ts`): a frame sets ~20 uniforms and two draw ranges, allocates nothing;
- `frameloop="demand"` with our own clock: a frame-rate ceiling per mode, nothing while the tab is hidden;
- the panel's position is read on scroll, resize and panel resize, never every frame;
- particles: one per ~700 px² (300–1600), at most 500 on a phone, `dpr` capped at 2 (1.5 on a phone);
- three.js is a separate chunk loaded after the page, only where `WebGL2RenderingContext` exists.
