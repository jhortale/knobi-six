// SPDX-License-Identifier: GPL-3.0-only
// The field's motion, all of it on the GPU. Field space: y from -1 (bottom) to 1 (top), x from -aspect to aspect.
// The colony gathers on the two edges of the instrument panel (uL, uR): the panel is opaque, so the organisms hug
// it from outside, and the write streams flow into it.

const common = /* glsl */ `
uniform float uTime;
uniform float uAspect;
uniform float uBright;
uniform float uPointerOn;
uniform float uPointerK;
uniform float uConverge;
uniform float uPulse;
uniform float uStream;
uniform float uScatter;
uniform float uBreath;
uniform float uBeat;
uniform float uBloom;
uniform float uBloomR;
uniform float uPct;
uniform float uDensity;
uniform float uThreads;
uniform vec2 uPointer;
uniform vec2 uL;
uniform vec2 uR;

attribute vec4 aSeed;
attribute vec3 aColor;

const float TAU = 6.2831853;

// where a particle is now, and how much extra light it gets from what is happening around it
vec2 motion(vec3 base, vec4 s, out float glow) {
  float t = uTime;
  float layer = min(2.0, floor(s.z * 3.0));          // 0 far .. 2 near
  vec2 span = vec2(uAspect, 1.0) * 2.2;
  vec2 p = base.xy * span * 0.5;
  p += vec2(0.012, 0.004) * t * (0.5 + 0.5 * layer);  // a slow current, faster in front (parallax)
  p = mod(p + span * 0.5, span) - span * 0.5;
  p += 0.02 * vec2(sin(t * (0.21 + 0.3 * s.x) + TAU * s.y), cos(t * (0.17 + 0.3 * s.y) + TAU * s.x));
  glow = 0.0;

  // the cursor: lean toward it, swirl a little around it, brighten near it
  vec2 d = uPointer - p;
  float f = exp(-dot(d, d) / 0.06) * uPointerOn * uPointerK;
  p += d * f * (0.25 + 0.15 * layer) + vec2(-d.y, d.x) * f * 0.2;
  glow += f;

  // roles: ~12 % can join the colony, the next ~14 % carry the write streams, the rest drift free
  vec2 tg = s.x < 0.5 ? uL : uR;
  float member = step(s.w, 0.12);
  float streamer = step(0.12, s.w) * step(s.w, 0.26);

  // the colony: tall ellipses hugging the panel's edge at the device's row
  float dir = s.y < 0.5 ? 1.0 : -1.0;
  float a = TAU * s.x * 2.0 + t * (0.3 + 0.4 * s.z) * dir;
  float r = 0.025 + 0.09 * s.z * s.z + 0.01 * sin(t * 1.3 + s.y * TAU);
  float side = s.x < 0.5 ? -1.0 : 1.0;   // centred just outside the edge: the panel would hide half of it
  p = mix(p, tg + vec2(side * (0.03 + r * 0.6), 0.0) + vec2(cos(a) * 0.8, sin(a) * 1.4) * r, member * uConverge);
  glow += member * uConverge * 0.35;

  // the write: streams flowing into the keyboard; the colony lights up with the progress
  float ph = fract(t * (0.22 + 0.1 * s.z) + s.y);
  p = mix(p, mix(p, tg, ph * ph * (3.0 - 2.0 * ph)), streamer * uStream);
  glow += streamer * uStream * smoothstep(0.0, 0.6, ph) * 1.1;
  glow += member * uStream * step(s.y, uPct) * 0.5;

  vec2 away = p - tg;
  float dist = length(away);
  vec2 outward = away / max(dist, 1e-3);

  // searching: sonar rings travelling out from the panel
  float ring = fract(t * 0.45);
  float band = exp(-pow((dist - (0.05 + ring * 1.4)) / 0.045, 2.0)) * (1.0 - ring) * uPulse;
  p += outward * band * 0.04;
  glow += band * 1.5;

  // done: one bloom
  float bloom = exp(-pow((dist - uBloomR) / 0.09, 2.0)) * uBloom;
  p += outward * bloom * 0.05;
  glow += bloom * 1.6 + uBloom * 0.25;

  // an error: everything draws back from the panel, quietly
  p += outward * uScatter * (0.03 + 0.09 * s.y) * exp(-dist * 1.2);
  return p;
}

float shine(vec4 s, float glow, float rank) {
  float twinkle = 0.75 + 0.25 * sin(uTime * (0.6 + 1.7 * s.x) + TAU * s.w);
  float breath = 1.0 + 0.22 * uBreath * sin(TAU * uBeat * uTime + s.x * 0.8);
  float base = 0.25 + 0.75 * s.y * s.y;
  float present = 1.0 - smoothstep(uDensity - 0.06, uDensity, rank);   // fewer particles, faded, never popped
  return (uBright * base * twinkle * breath + glow) * present;
}
`;

export const pointsVertex = /* glsl */ `
${common}
uniform float uSize;
uniform float uDpr;
uniform vec3 uAccent;
varying vec3 vColor;
varying float vA;

void main() {
  float glow;
  vec2 p = motion(position, aSeed, glow);
  vA = clamp(shine(aSeed, glow, position.z), 0.0, 1.6);
  // the colony and the streams take the synth's colour; an error drains the colour
  vec3 c = mix(aColor, uAccent, step(aSeed.w, 0.26) * max(uConverge, uStream) * 0.55);
  c = mix(c, vec3(dot(c, vec3(0.299, 0.587, 0.114))), uScatter * 0.6);
  vColor = c;
  float layer = min(2.0, floor(aSeed.z * 3.0));
  gl_PointSize = uSize * uDpr * (0.7 + 0.6 * layer + 1.6 * aSeed.z * aSeed.z * aSeed.z) * (1.0 + 0.5 * min(glow, 1.5));
  gl_Position = vec4(p.x / uAspect, p.y, 0.0, 1.0);
}
`;

export const pointsFragment = /* glsl */ `
varying vec3 vColor;
varying float vA;

void main() {
  float d = length(gl_PointCoord - 0.5);
  if (d > 0.5) discard;
  float halo = smoothstep(0.5, 0.0, d);
  float core = smoothstep(0.2, 0.0, d);
  gl_FragColor = vec4(vColor * (1.0 + core * 0.4), (halo * halo * 0.55 + core * 0.9) * vA);
}
`;

export const threadsVertex = /* glsl */ `
${common}
attribute vec3 aOther;
attribute vec4 aSeedO;
varying vec3 vColor;
varying float vA;

void main() {
  float g1;
  float g2;
  vec2 p = motion(position, aSeed, g1);
  vec2 o = motion(aOther, aSeedO, g2);
  float near = smoothstep(0.13, 0.03, length(p - o));   // a thread fades as its ends drift apart
  float a = min(shine(aSeed, g1, position.z), shine(aSeedO, g2, aOther.z));
  vA = near * a * uThreads * (0.18 + 0.5 * min(g1 + g2, 1.5));   // signal lines light up near the cursor
  vColor = aColor;
  gl_Position = vec4(p.x / uAspect, p.y, 0.0, 1.0);
}
`;

export const threadsFragment = /* glsl */ `
varying vec3 vColor;
varying float vA;

void main() {
  gl_FragColor = vec4(vColor, vA * 0.5);
}
`;
