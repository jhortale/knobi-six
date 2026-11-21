// SPDX-License-Identifier: GPL-3.0-only
// The particles and their threads, built once as typed arrays. Everything that moves is computed on the GPU
// (shaders.ts) from these values, so a frame costs the CPU a few uniforms and nothing is allocated while writing.

/** the cosmos: cool cyans, violets and greens, a deep blue, and the occasional warm spark */
export const PALETTE: readonly { hex: number; weight: number }[] = [
  { hex: 0x5fe3ff, weight: 0.32 },   // cyan
  { hex: 0x9d8cff, weight: 0.26 },   // violet
  { hex: 0x62f0b0, weight: 0.2 },    // green
  { hex: 0x3d7bff, weight: 0.17 },   // deep blue
  { hex: 0xffb38a, weight: 0.05 },   // warm spark
];

/** each particle has at most this many threads */
export const MAX_LINKS = 2;
/** the farthest two particles start apart to get a thread (field units on a 16:10 screen) */
export const LINK_DIST = 0.11;

export interface FieldGeometry {
  count: number;
  /** x, y in -1..1 (the shader stretches x to the screen's aspect), z = rank (i / count) for the density fade */
  position: Float32Array;
  /** per particle: speed, brightness, depth/size (random) and role (colony, stream, free; rising with the rank) */
  seed: Float32Array;
  /** RGB, 0..1 (sRGB) */
  color: Float32Array;
  /** thread endpoints (i, j) with j < i, sorted by i */
  pairs: Uint32Array;
  /** pairsBefore[n] = how many threads link only particles below n (the draw range for n particles) */
  pairsBefore: Uint32Array;
}

/** a small seeded PRNG: the same field on every visit */
export function mulberry32(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/** "#2f6be0" or 0x2f6be0 → [r, g, b] in 0..1 (sRGB, as written: the shaders draw in display space) */
export function rgb(c: string | number): [number, number, number] {
  const n = typeof c === "number" ? c : parseInt(c.replace(/^#/, "").replace(/^(.)(.)(.)$/, "$1$1$2$2$3$3"), 16);
  if (!Number.isFinite(n)) throw new Error(`not a colour: ${c}`);
  return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255];
}

function pick(r: number): number {
  let acc = 0;
  for (const p of PALETTE) {
    acc += p.weight;
    if (r < acc) return p.hex;
  }
  return PALETTE[PALETTE.length - 1].hex;
}

/** the depth layer (0 far … 2 near) the shader derives from the third seed */
export const layerOf = (z: number) => Math.min(2, Math.floor(z * 3));

export function buildField(count: number, seedValue = 0x6d1d1c): FieldGeometry {
  const rnd = mulberry32(seedValue);
  const position = new Float32Array(count * 3);
  const seed = new Float32Array(count * 4);
  const color = new Float32Array(count * 3);
  for (let i = 0; i < count; i++) {
    position[i * 3] = rnd() * 2 - 1;
    position[i * 3 + 1] = rnd() * 2 - 1;
    position[i * 3 + 2] = i / count;
    for (let k = 0; k < 3; k++) seed[i * 4 + k] = rnd();
    // the role (colony < 0.12 < streams < 0.26 < free) rises with the rank: the particles that tell the story are
    // drawn first, so the write's lower density only drops free drifters
    seed[i * 4 + 3] = (i + rnd()) / count;
    color.set(rgb(pick(rnd())), i * 3);
  }

  // threads: neighbours in the same depth layer (they drift together, so a thread lasts), at most MAX_LINKS each
  const pairs: number[] = [];
  const pairsBefore = new Uint32Array(count + 1);
  const degree = new Uint8Array(count);
  const sx = 1.1 * 1.6, sy = 1.1;   // the shader's scale on a 16:10 screen
  for (let i = 0; i < count; i++) {
    pairsBefore[i] = pairs.length / 2;
    const li = layerOf(seed[i * 4 + 2]);
    for (let j = 0; j < i && degree[i] < MAX_LINKS; j++) {
      if (degree[j] >= MAX_LINKS || layerOf(seed[j * 4 + 2]) !== li) continue;
      const dx = (position[i * 3] - position[j * 3]) * sx;
      const dy = (position[i * 3 + 1] - position[j * 3 + 1]) * sy;
      if (dx * dx + dy * dy < LINK_DIST * LINK_DIST) {
        pairs.push(i, j);
        degree[i]++;
        degree[j]++;
      }
    }
  }
  pairsBefore[count] = pairs.length / 2;
  return { count, position, seed, color, pairs: Uint32Array.from(pairs), pairsBefore };
}

/** the thread vertices: two per thread, each carrying its own particle and the other end */
export function buildThreads(g: FieldGeometry) {
  const n = g.pairs.length;   // = 2 × threads = vertices
  const position = new Float32Array(n * 3), seed = new Float32Array(n * 4), color = new Float32Array(n * 3);
  const other = new Float32Array(n * 3), otherSeed = new Float32Array(n * 4);
  for (let v = 0; v < n; v++) {
    const own = g.pairs[v], far = g.pairs[v ^ 1];
    position.set(g.position.subarray(own * 3, own * 3 + 3), v * 3);
    seed.set(g.seed.subarray(own * 4, own * 4 + 4), v * 4);
    color.set(g.color.subarray(own * 3, own * 3 + 3), v * 3);
    other.set(g.position.subarray(far * 3, far * 3 + 3), v * 3);
    otherSeed.set(g.seed.subarray(far * 4, far * 4 + 4), v * 4);
  }
  return { position, seed, color, other, otherSeed };
}
