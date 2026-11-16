/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX oscillator section: VCO 1, VCO 2, sub, noise, hard sync (see osc.h for the interface and the levels).
 *
 * Ported from the ANALOG engine (smk37-analog, branch analog-engine, web/synth/analog.c, MIT): the four-sample
 * PolyBLEP (integrated cubic B-spline) blep_side / blep / blep_near / sub_blep, the band-limited saw and the
 * difference-of-two-saws pulse; and its exp2 polynomial (web/synth/fixed.c, fm_exp2_fixed, GPL-3.0-only WITH
 * GPL-3.0-linking-exception). New here: the triangle's BLAMP (the integral of the same kernel), the band-limited
 * hard sync, and increments without a 64-bit division.
 *
 * Fixed point: phases and increments are Q32 cycles; waveforms are computed in Q24 (+-1 = 2^24) and leave as
 * Q24 >> 10 (OSC_ONE = 2^14). Distances to a step are Q24 samples, t * inv >> 22 with inv = Q14 of 2^32 / dt.
 * The per-sample path uses only 32 x 32 -> 64 high multiplies; everything near a step (at most 4 samples per step)
 * is out of line and may use 64-bit products. No division anywhere. */
#include "osc.h"

#define OSC_HOT static inline __attribute__((always_inline))
#define OSC_COLD static __attribute__((noinline, cold))
#define OSC_Q24 INT32_C(16777216)
#define OSC_Q30 INT32_C(1073741824)
#define OSC_NOTE_LO (-60 * 65536)          /* 0.255 Hz */
#define OSC_NOTE_HI (135 * 65536)          /* 19.9 kHz: the increment stays below 2^31 */
#define OSC_LOG2_C 425350554               /* log2(2^32 * 440 / 44100) in Q24: inc = 2^(that + (note - 69) / 12) */
#define OSC_PW_STEP 15099494u              /* 0.45 * 2^32 / 128: PW 0..255 -> width 5.35 %..95 %, 127 = 50 % */
#if OSC_RATE != 44100
#error "OSC_LOG2_C is computed for 44100 Hz"
#endif

/* 2^x: x signed Q24, the result with 'fraction' fractional bits, saturating (fm_exp2_fixed, ANALOG's fixed.c). */
static __attribute__((noinline)) uint32_t osc_exp2(int32_t x, unsigned fraction)
{
    int n = (x >> 24) + ((x >> 23) & 1);
    int32_t f = (int32_t)(((int64_t)x - (int64_t)n * OSC_Q24) * 256);
    int32_t p = 164774;
    p = (int32_t)(((int64_t)p * f) >> 32) + 1438807;
    p = (int32_t)(((int64_t)p * f) >> 32) + 10327774;
    p = (int32_t)(((int64_t)p * f) >> 32) + 59596201;
    p = (int32_t)(((int64_t)p * f) >> 32) + 257941207;
    p = (int32_t)(((int64_t)p * f) >> 32) + 744261145;
    p = (int32_t)(((int64_t)p * f) >> 32) + OSC_Q30;
    int shift = 30 - (int)fraction - n;
    if (shift >= 32) return 0;
    if (shift >= 0) return (uint32_t)p >> shift;
    if (shift <= -32 || (uint32_t)p > (UINT32_MAX >> -shift)) return UINT32_MAX;
    return (uint32_t)p << -shift;
}

/* log2 of the increment for a Q16 note, Q24 (clamped to OSC_NOTE_LO..HI) */
static int32_t osc_log2_inc(int32_t note_q16)
{
    if (note_q16 < OSC_NOTE_LO) note_q16 = OSC_NOTE_LO;
    if (note_q16 > OSC_NOTE_HI) note_q16 = OSC_NOTE_HI;
    return (note_q16 - 69 * 65536) * 64 / 3 + OSC_LOG2_C;   /* semitones Q16 -> octaves Q24: x 256 / 12 */
}

int32_t note_to_inc(int32_t note_q16)
{
    return (int32_t)osc_exp2(osc_log2_inc(note_q16), 0);
}

/* inv = 2^46 / inc = 2^(46 - log2 inc): Q14 of 2^32 / inc. Over the whole note range (0.255 Hz .. 19.9 kHz) it
 * stays below 2^32 (2.83e9 at the bottom), so no cap: step distances, the sync restart time and every sync
 * distance are exact at LOW FREQ rates too. At the top it is ~36000, a relative resolution of 3e-5. */
static uint32_t osc_inv(int32_t log2_inc)
{
    return osc_exp2(46 * OSC_Q24 - log2_inc, 0);
}

void osc_set_pitch(osc_t *o, int32_t note1_q16, int32_t note2_q16)
{
    int32_t l1 = osc_log2_inc(note1_q16), l2 = osc_log2_inc(note2_q16);
    o->inc1 = osc_exp2(l1, 0);
    o->inc2 = osc_exp2(l2, 0);
    o->inv1 = osc_inv(l1);
    o->inv2 = osc_inv(l2);
}

static int osc_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* VCO1 SHAPE 0..255: pulse amount 0..1 (saw at 0, pulse at 255, blends between: saw - a saw(t - w)).
 * VCO2 SHAPE 0..254: 0..127 crossfades triangle -> saw (b), 127..254 adds the pulse amount (a); 127 = saw.
 * PW 0..255: the pulse's width 5.35 %..95 %, 127 = square; 127 - d and 127 + d are mirror images (w, 1 - w):
 * the same spectrum, thinner toward both ends. Multiplies only, so X-Mod can call this at audio rate. */
void osc_set_shape(osc_t *o, int shape1, int pw1, int shape2, int pw2)
{
    shape1 = osc_clamp(shape1, 0, 255), pw1 = osc_clamp(pw1, 0, 255);
    shape2 = osc_clamp(shape2, 0, 254), pw2 = osc_clamp(pw2, 0, 255);
    o->shape1 = (int16_t)shape1, o->pw1 = (int16_t)pw1, o->shape2 = (int16_t)shape2, o->pw2 = (int16_t)pw2;
    /* Q15 amounts, rounded so the ends are exact (526344 = 2^27 / 255, 1056833 = 2^27 / 127), then << 15 */
    o->a1 = ((shape1 * 526344 + 2048) >> 12) << 15;
    o->b2 = shape2 >= 127 ? OSC_Q30 : ((shape2 * 1056833 + 2048) >> 12) << 15;
    o->a2 = shape2 <= 127 ? 0 : (((shape2 - 127) * 1056833 + 2048) >> 12) << 15;
    o->w1 = 0x80000000u + (uint32_t)(pw1 - 127) * OSC_PW_STEP;
    o->w2 = 0x80000000u + (uint32_t)(pw2 - 127) * OSC_PW_STEP;
}

void osc_init(osc_t *o, uint32_t seed)
{
    uint8_t *b = (uint8_t *)o;
    for (unsigned i = 0; i < sizeof *o; i++) b[i] = 0;
    o->noise = seed ? seed : 0x9e3779b9u;
    osc_set_pitch(o, 69 * 65536, 69 * 65536);
    osc_set_shape(o, 0, 127, 0, 127);
}

/* ---- per-sample helpers (ANALOG) ---- */

/* floor(a b / 2^32): one 32 x 32 high multiply */
OSC_HOT int32_t osc_mulh(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 32);
}

/* Q32 x Q32 high word and Q24 x Q24 (32 x 32 -> 64, one umull): the sync path's distances and phases */
OSC_HOT uint32_t osc_mul24(uint32_t a, uint32_t b)
{
    return (uint32_t)(((uint64_t)a * b) >> 24);
}
/* distance d (Q32 cycles, < 2 dt) in samples, Q24: d * inv >> 22 < 2^25 */
OSC_HOT uint32_t osc_dist(uint32_t d, uint32_t inv)
{
    return (uint32_t)(((uint64_t)d * inv) >> 22);
}

/* One side of the four-sample PolyBLEP residual, u = distance to the step in samples (Q24, 0..2); Q24 in 0..1:
 * u < 1: 1 - 4u/3 + 2u^3/3 - u^4/4;  1 <= u < 2: (2 - u)^4 / 12. side(u) = 2 * (integral of the cubic B-spline
 * from u to 2). Evaluated in Q16 with plain 32-bit multiplies (every operand < 2^16, every product < 2^32; the
 * constants are 2^16 / 3, 2^17 / 3, 2^16 / 12), returned as Q24: a few Q16 steps of error, ~1e-4 of the step. */
static int32_t osc_side(uint32_t u)
{
    if (u >= 2u * OSC_Q24) return 0;
    uint32_t v = u >> 8;   /* Q16 */
    if (v >= 65536u) {
        uint32_t w = 131072u - v;
        w -= w >> 16;   /* u = 1 exactly gives w = 2^16: 65535, so w * w stays below 2^32 */
        uint32_t w2 = (w * w) >> 16, w4 = (w2 * w2) >> 16;
        return (int32_t)((w4 * 5461u) >> 16) << 8;   /* /12 */
    }
    uint32_t v2 = (v * v) >> 16, v3 = (v2 * v) >> 16, v4 = (v2 * v2) >> 16;
    return (65536 - (int32_t)((v * 21845u) >> 14)    /* 4u/3 */
            + (int32_t)((v3 * 43690u) >> 16)          /* 2u^3/3 */
            - (int32_t)(v4 >> 2)) << 8;
}

/* PolyBLEP residual in Q24 for a downward unit-2 step at phase 0 (the saw wrap): +side before, -side after.
 * t: Q32 phase, dt: Q32 increment (< 2^31), inv: Q14 of 2^32/dt. Callers first test the window with osc_near. */
OSC_COLD int32_t osc_blep(uint32_t t, uint32_t dt, uint32_t inv)
{
    uint32_t span = 2 * dt, ahead = 0u - t;   /* ahead in 1..span: the step is still to come */
    int32_t r = 0;
    if (t < span) r -= osc_side(osc_dist(t, inv));
    if (ahead - 1u < span) r += osc_side(osc_dist(ahead, inv));
    return r;
}

/* The sub flips at every VCO 1 wrap: the flip just behind went from -now to now, the one ahead goes from now to
 * -now; both residuals are -now/2 x side(distance in VCO 1 samples). Returns side(behind) + side(ahead) (Q24): the
 * caller subtracts it times the sub's sign. (ANALOG's sub_blep on the half-rate phase, in VCO 1's distances.) */
OSC_COLD int32_t osc_sub_blep(uint32_t t, uint32_t dt, uint32_t inv)
{
    uint32_t span = 2 * dt, ahead = 0u - t;
    int32_t r = 0;
    if (t < span) r += osc_side(osc_dist(t, inv));
    if (ahead - 1u < span) r += osc_side(osc_dist(ahead, inv));
    return r;
}

/* Integrated BLEP (BLAMP) for the triangle's corners: the residual of a slope change of +1 per sample at distance
 * u (Q24 samples, either side; the residual is even), P(u) = 1/2 * integral of side from u to 2:
 * u < 1: 7/30 - u/2 + u^2/3 - u^4/12 + u^5/40;  1 <= u < 2: (2 - u)^5 / 120. */
static int32_t osc_blamp_p(uint32_t u)
{
    if (u >= 2u * OSC_Q24) return 0;
    uint32_t v = u >> 8;   /* Q16, 32-bit multiplies as in osc_side */
    if (v >= 65536u) {
        uint32_t w = 131072u - v;
        w -= w >> 16;   /* as in osc_side */
        uint32_t w2 = (w * w) >> 16, w4 = (w2 * w2) >> 16, w5 = (w4 * w) >> 16;
        return (int32_t)((w5 * 546u) >> 16) << 8;   /* /120 */
    }
    uint32_t v2 = (v * v) >> 16, v4 = (v2 * v2) >> 16, v5 = (v4 * v) >> 16;
    return (15292 - (int32_t)(v >> 1)                 /* 7/30 */
            + (int32_t)((v2 * 21845u) >> 16)          /* u^2/3 */
            - (int32_t)((v4 * 5461u) >> 16)           /* u^4/12 */
            + (int32_t)((v5 * 1638u) >> 16)) << 8;    /* u^5/40 */
}

/* The triangle's corner at phase 0 (valley, slope -4 -> +4 cycles, +8 dt per sample): residual + 8 dt P(dist / dt)
 * for the corner behind and the one ahead (both can be within two samples above 11 kHz). The peak at 1/2 is the same
 * with the opposite sign. 8 dt in Q24 is dt >> 5: P (Q24) x dt / 2^29. */
OSC_COLD int32_t osc_blamp(uint32_t t, uint32_t dt, uint32_t inv)
{
    uint32_t span = 2 * dt, ahead = 0u - t;
    int32_t p = 0;
    if (t < span) p += osc_blamp_p(osc_dist(t, inv));
    if (ahead - 1u < span) p += osc_blamp_p(osc_dist(ahead, inv));
    return osc_mulh(p << 7, (int32_t)(dt >> 4));
}

/* the residual is nonzero only for |t| < span (t as int32), tested on the ones' complement magnitude */
OSC_HOT int osc_near(uint32_t t, uint32_t span)
{
    int32_t s = (int32_t)t;
    return __builtin_expect((uint32_t)(s ^ (s >> 31)) < span, 0);
}

/* band-limited saw in Q24, -1..1 */
OSC_HOT int32_t osc_saw(uint32_t t, uint32_t dt, uint32_t inv, uint32_t span)
{
    int32_t s = (int32_t)(t >> 7) - OSC_Q24;
    if (osc_near(t, span)) s -= osc_blep(t, dt, inv);
    return s;
}

/* band-limited triangle in Q24: -1 at phase 0, +1 at 1/2 */
OSC_HOT int32_t osc_tri(uint32_t t, uint32_t dt, uint32_t inv, uint32_t span)
{
    uint32_t m = t < 0x80000000u ? t : 0u - t;
    int32_t v = (int32_t)(m >> 6) - OSC_Q24;
    if (osc_near(t, span)) v += osc_blamp(t, dt, inv);
    if (osc_near(t ^ 0x80000000u, span)) v -= osc_blamp(t ^ 0x80000000u, dt, inv);
    return v;
}

/* ---- hard sync ---- */

/* VCO 1's naive waveform s(t) - a s(t - w), s(t) = 2t - 1 (Q24) */
static int32_t osc_w1(uint32_t t, uint32_t w, int32_t a)
{
    int32_t s = (int32_t)(t >> 7) - OSC_Q24, s2 = (int32_t)((t - w) >> 7) - OSC_Q24;
    return s - (int32_t)(((int64_t)s2 * a) >> 30);
}

/* h x the residual of a unit upward step at x = now - step (Q24 samples): after it -side(x)/2, before side(-x)/2 */
static int64_t osc_step(int32_t h, int32_t x)   /* Q48 x 2; one signed 32 x 32 product */
{
    int32_t r = x >= 0 ? -osc_side((uint32_t)x) : osc_side(0u - (uint32_t)x);
    return (int64_t)h * r;
}

/* VCO 1 near a VCO 2 wrap with sync: the naive value plus the residual of every step within two samples, on VCO 1's
 * real, piecewise-linear phase. Segments: A before the last restart (it ended at sync_tb, x samples ago), B from
 * that restart (or 2 samples back) to the next one (tau samples ahead, or 2), C after the next restart (from 0).
 * Steps: the restarts (height W(0) - W(phase before)), the saw's wrap (phase 0, -2) and the pulse edge (phase w,
 * +2a) where VCO 1 crosses them. A crossing exactly at a restart belongs to the side whose naive value keeps it.
 * The sub flips at every wrap and restart: its steps are those times, with alternating heights; *subr gets their
 * residual (Q24), which replaces the sub's own PolyBLEP in this window. */
struct osc_ev {
    int64_t acc;          /* VCO 1: sum of h x 2 x residual, Q48 */
    int32_t tx[6];        /* the sub's flips, x = now - flip (Q24 samples) */
    int n;
};
static void osc_ev(struct osc_ev *e, int32_t h, int32_t x, int flip)
{
    e->acc += osc_step(h, x);
    if (flip && e->n < 6) e->tx[e->n++] = x;
}
OSC_COLD int32_t osc_vco1_sync(const osc_t *o, uint32_t t1, uint32_t t2, int32_t *subr)
{
    const uint32_t d1 = o->inc1, i1 = o->inv1, l1 = 2 * d1, w = o->w1, l2 = 2 * o->inc2;
    const int32_t a = o->a1, two = 2 * OSC_Q24, w0 = osc_w1(0, w, a);
    const int32_t hw = a >> 5;   /* the pulse edge, +2a in Q24 */
    struct osc_ev e;
    e.acc = 0, e.n = 0;
    int last = o->synced && t2 < l2, next = o->sync && (0u - t2) - 1u < l2;
    uint32_t pn = 0;
    if (last) {   /* the restart x samples ago, and segment A before it */
        uint32_t tb = o->sync_tb;
        int32_t x = (int32_t)osc_dist(t2, o->inv2);
        osc_ev(&e, w0 - osc_w1(tb, w, a), x, 1);
        if (tb < l1) {   /* the saw wrap before the restart */
            int32_t tc = x + (int32_t)osc_dist(tb, i1);
            if (tc < two) osc_ev(&e, -two, tc, 1);
        }
        if (a && tb - w < l1) {   /* the pulse edge before the restart */
            int32_t tc = x + (int32_t)osc_dist(tb - w, i1);
            if (tc < two) osc_ev(&e, hw, tc, 0);
        }
    }
    if (next) {   /* the restart tau samples ahead, and segment C after it */
        int32_t tau = (int32_t)osc_dist(0u - t2, o->inv2);
        pn = osc_mul24((uint32_t)tau, d1);   /* VCO 1 phase gained until then */
        osc_ev(&e, w0 - osc_w1(t1 + pn, w, a), -tau, 1);
        if (a && w < l1) {
            int32_t tc = tau + (int32_t)osc_dist(w, i1);
            if (tc < two) osc_ev(&e, hw, -tc, 0);
        }
    }
    /* segment B: crossings behind (b = t1 - c) and ahead (f = c - t1) */
    if (!last && t1 < l1) osc_ev(&e, -two, (int32_t)osc_dist(t1, i1), 1);
    if (a) {
        uint32_t b = t1 - w;
        if (last ? b <= t1 : b < l1) osc_ev(&e, hw, (int32_t)osc_dist(b, i1), 0);
    }
    uint32_t f = 0u - t1;
    if (f - 1u < (next ? pn : l1) && f - 1u < l1) osc_ev(&e, -two, -(int32_t)osc_dist(f, i1), 1);
    if (a) {
        f = w - t1;
        if (f - 1u < (next ? pn : l1) && f - 1u < l1) osc_ev(&e, hw, -(int32_t)osc_dist(f, i1), 0);
    }
    /* the sub: sort the flips by x; the nearest flip behind went from -now to now (2 now), the
     * one before it the other way; the nearest ahead goes from now to -now, the next one back */
    for (int i = 1; i < e.n; i++)
        for (int j = i; j > 0 && e.tx[j] < e.tx[j - 1]; j--) {
            int32_t t = e.tx[j]; e.tx[j] = e.tx[j - 1]; e.tx[j - 1] = t;
        }
    int32_t now = o->sub ? -two : two;   /* 2 x the sub's value now */
    int64_t s = 0;
    int ahead = 0;   /* tx[0..ahead) are ahead (x < 0), the nearest last; tx[ahead..n) behind, the nearest first */
    while (ahead < e.n && e.tx[ahead] < 0) ahead++;
    for (int i = ahead, k = 0; i < e.n; i++, k++) s += osc_step(k & 1 ? -now : now, e.tx[i]);
    for (int i = ahead - 1, k = 0; i >= 0; i--, k++) s += osc_step(k & 1 ? now : -now, e.tx[i]);
    *subr = (int32_t)(s >> 25);
    return osc_w1(t1, w, a) + (int32_t)(e.acc >> 25);
}

/* VCO 2 wrapped during this sample (n2 = its phase at the next one): VCO 1 restarts from 0 at that instant,
 * n2 / dt2 samples before the next sample. Returns VCO 1's next phase and keeps its phase at the restart in sync_tb
 * (the sub flips on the restart, and on a natural wrap only if it came first: sync_tb < t1). */
OSC_COLD uint32_t osc_restart(osc_t *o, uint32_t t1, uint32_t n2)
{
    uint32_t x = osc_dist(n2, o->inv2);        /* Q24 samples, < 1 */
    uint32_t nt = osc_mul24(x, o->inc1);
    o->sync_tb = t1 + (o->inc1 - nt);                                /* VCO 1's phase at the restart */
    o->synced = 1;
    return nt;
}

void osc_tick(osc_t *o, int32_t out[4])
{
    const uint32_t t1 = o->ph1, t2 = o->ph2, d1 = o->inc1, d2 = o->inc2, i1 = o->inv1, i2 = o->inv2;
    const uint32_t l1 = 2 * d1, l2 = 2 * d2;
    unsigned sub = o->sub;
    int32_t v1, v2, sq = sub ? -OSC_Q24 : OSC_Q24;

    /* VCO 1 (saw - a saw(t - w)) and the sub (a divide-by-2 flip-flop on VCO 1: see osc.h) */
    if (__builtin_expect((o->sync | o->synced) && osc_near(t2, l2), 0)) {
        int32_t res;
        v1 = osc_vco1_sync(o, t1, t2, &res);
        sq += res;
    } else {
        v1 = osc_saw(t1, d1, i1, l1);
        if (o->a1) v1 -= osc_mulh(osc_saw(t1 - o->w1, d1, i1, l1) * 4, o->a1);
        if (osc_near(t1, l1)) sq += sub ? osc_sub_blep(t1, d1, i1) : -osc_sub_blep(t1, d1, i1);
    }
    /* VCO 2: triangle + b (saw - triangle) - a saw(t - w) */
    if (o->b2 == OSC_Q30) {
        v2 = osc_saw(t2, d2, i2, l2);
    } else {
        v2 = osc_tri(t2, d2, i2, l2);
        if (o->b2) v2 += osc_mulh((osc_saw(t2, d2, i2, l2) - v2) * 4, o->b2);
    }
    if (o->a2) v2 -= osc_mulh(osc_saw(t2 - o->w2, d2, i2, l2) * 4, o->a2);
    /* noise: xorshift32 */
    uint32_t r = o->noise;
    r ^= r << 13, r ^= r >> 17, r ^= r << 5;
    o->noise = r;

    out[OSC_VCO1] = v1 >> 10;
    out[OSC_VCO2] = v2 >> 10;
    out[OSC_SUB] = sq >> 10;
    out[OSC_NOISE] = (int32_t)r >> 17;

    uint32_t n1 = t1 + d1, n2 = t2 + d2;
    if (__builtin_expect(n2 < t2, 0)) {
        if (o->sync) n1 = osc_restart(o, t1, n2), sub ^= 1u ^ (o->sync_tb < t1);
        else o->synced = 0, sub ^= n1 < t1;
    } else {
        sub ^= n1 < t1;
    }
    o->ph1 = n1;
    o->ph2 = n2;
    o->sub = (uint8_t)sub;
}
