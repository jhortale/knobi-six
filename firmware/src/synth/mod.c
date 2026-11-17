/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX modulation: the shared LFO, its depth, X-Mod and aftertouch routing (see mod.h for the interface, the
 * units, the full-scale depths and what runs per sample).
 *
 * The exp2 polynomial is ANALOG's (João Hortale, smk37-analog, web/synth/fixed.c, fm_exp2_fixed, GPL-3.0-only WITH
 * GPL-3.0-linking-exception), as in osc.c / filter.c / env.c. New here: the LFO (exponential 0.022-500 Hz rate,
 * a 5th-order odd sine, sample and hold with the noise mode at the top rate), the depth sum, the routing, and
 * mods_fm1's cubic 2^f for VCO 1's per-sample pitch.
 *
 * Fixed point: only 32 x 32 -> 64 products, no 64-bit division; 32-bit divisions by constants at control rate only
 * (lfo_depth, mods_gain). mods_xm_tick (mod.h) and mods_fm1 are the per-sample parts: no division but mods_fm1's
 * division of a semitone count by 3 (a 32-bit SDIV on the chip, not a multiply; no library call). Unity-build safe: every file-scope name
 * is lfo_ / mods_ / LFO_ / MODS_ prefixed. */
#include "mod.h"
#include "../params/params.h"

#define LFO_Q24 INT32_C(16777216)
#define LFO_Q30 INT32_C(1073741824)
/* log2 of the increment (Q32 cycles per sample at 44100 Hz) at FREQUENCY 0 (0.022 Hz), Q24, and per step:
 * log2(500 / 0.022) / 254, Q24. FREQUENCY 254 is set to LFO_INC_TOP (500 Hz) exactly. */
#define LFO_L0 185642517
#define LFO_LSTEP 955914
#define LFO_FMAX 254
/* sin(pi / 2 y) ~ y (S1 + y^2 (S3 + y^2 S5)), Q30, |y| <= 1: S1 = pi / 2, S3 / S5 a minimax fit with S1 + S3 + S5 =
 * 1 exactly (so the peaks are exact); max error 1.4e-4 (4.6 LSB of LFO_ONE) */
#define LFO_S1 1686629713
#define LFO_S3 (-690849240)
#define LFO_S5 77961351
/* 2^f ~ 1 + f (E1 + f (E2 + f E3)), Q30, 0 <= f <= 1: E1 + E2 + E3 = 1 exactly (2^1 = 2 exact, so octaves join and
 * 0 semitones is exact); max relative error 1.14e-4 (0.2 cent) */
#define MODS_E1 746357942
#define MODS_E2 243962787
#define MODS_E3 83421095
/* the oscillator's note range (osc.c OSC_NOTE_LO / HI) */
#define MODS_NOTE_LO (-60 * 65536)
#define MODS_NOTE_HI (135 * 65536)
#if OSC_RATE != 44100
#error "LFO_L0 and LFO_INC_TOP are computed for 44100 Hz"
#endif

/* 2^x: x signed Q24, the result with 'fraction' fractional bits, saturating (fm_exp2_fixed, ANALOG's fixed.c). */
static uint32_t lfo_exp2(int32_t x, unsigned fraction)
{
    int n = (x >> 24) + ((x >> 23) & 1);
    int32_t f = (int32_t)(((int64_t)x - (int64_t)n * LFO_Q24) * 256);
    int32_t p = 164774;
    p = (int32_t)(((int64_t)p * f) >> 32) + 1438807;
    p = (int32_t)(((int64_t)p * f) >> 32) + 10327774;
    p = (int32_t)(((int64_t)p * f) >> 32) + 59596201;
    p = (int32_t)(((int64_t)p * f) >> 32) + 257941207;
    p = (int32_t)(((int64_t)p * f) >> 32) + 744261145;
    p = (int32_t)(((int64_t)p * f) >> 32) + LFO_Q30;
    int shift = 30 - (int)fraction - n;
    if (shift >= 32) return 0;
    if (shift >= 0) return (uint32_t)p >> shift;
    if (shift <= -32 || (uint32_t)p > (UINT32_MAX >> -shift)) return UINT32_MAX;
    return (uint32_t)p << -shift;
}

static int32_t mods_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* a * b / 2^30 (floor), 32 x 32 -> 64 */
static inline __attribute__((always_inline)) int32_t mods_mul30(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 30);
}

/* ---- LFO ---- */

static uint32_t lfo_rand(lfo_t *l)
{
    uint32_t x = l->rnd;
    x ^= x << 13, x ^= x >> 17, x ^= x << 5;
    return l->rnd = x;
}

/* a uniform value in -LFO_ONE..LFO_ONE */
static int32_t lfo_draw(lfo_t *l)
{
    int32_t r = (int32_t)lfo_rand(l) >> 16;
    return r < -LFO_ONE ? -LFO_ONE : r;
}

void lfo_init(lfo_t *l, uint32_t seed)
{
    l->ph = 0;
    l->rnd = seed ? seed : 0x2545f491u;
    l->held = lfo_draw(l);
    l->keyed = 0;
    lfo_set(l, 100, 0, 0);
}

void lfo_set(lfo_t *l, int freq_param, int synced, uint32_t clock_inc)
{
    if (synced && clock_inc) {
        l->inc = clock_inc < LFO_INC_TOP ? clock_inc : LFO_INC_TOP - 1;   /* never the noise rate */
        return;
    }
    int p = (int)mods_clamp(freq_param, 0, LFO_FMAX);
    l->inc = p == LFO_FMAX ? LFO_INC_TOP : lfo_exp2(LFO_L0 + p * LFO_LSTEP, 0);
}

void lfo_key(lfo_t *l, int first_key_down)
{
    if (!first_key_down) return;
    l->ph = 0;
    l->keyed = 1;
}

/* sin(2 pi ph / 2^32), Q15 (+-LFO_ONE) */
static int32_t lfo_sine(uint32_t ph)
{
    int32_t a = (int32_t)ph;                                   /* -0.5..0.5 cycle, Q32: a quarter cycle is 2^30 */
    if (a > LFO_Q30 || a < -LFO_Q30) a = (int32_t)(0x80000000u - (uint32_t)a);   /* fold: y = sin argument, Q30 */
    int32_t y2 = mods_mul30(a, a);
    int32_t p = LFO_S5;
    p = LFO_S3 + mods_mul30(p, y2);
    p = LFO_S1 + mods_mul30(p, y2);
    int32_t s = mods_mul30(p, a);                              /* Q30, |s| <= 2^30 */
    return (int32_t)(((int64_t)s * LFO_ONE + LFO_Q30 / 2) >> 30);
}

int32_t lfo_tick(lfo_t *l, int shape, uint32_t frames)
{
    uint64_t np = (uint64_t)l->ph + (uint64_t)l->inc * frames;
    int wrapped = (np >> 32) != 0;
    l->ph = (uint32_t)np;
    /* sample and hold: a new value per cycle (and at a key reset); at the top rate, on every tick that moves */
    if (l->keyed || wrapped || (shape == LFO_RANDOM && l->inc >= LFO_INC_TOP && frames)) l->held = lfo_draw(l);
    l->keyed = 0;
    switch (shape) {
    case LFO_SINE: return lfo_sine(l->ph);
    case LFO_SAW: return (int32_t)(l->ph >> 17);
    case LFO_RSAW: return LFO_ONE - (int32_t)(l->ph >> 17);
    case LFO_SQUARE: return l->ph < 0x80000000u ? LFO_ONE : 0;
    default: return l->held;
    }
}

int32_t lfo_depth(int initial, int wheel_0_127, int32_t at_q15, int at_to_lfoamt)
{
    int32_t i = mods_clamp(initial, 0, 255), w = mods_clamp(wheel_0_127, 0, 127);
    int32_t d = (i * MODS_DEPTH_ONE + 127) / 255 + (w * MODS_DEPTH_ONE + 63) / 127;
    if (at_to_lfoamt) d += mods_clamp(at_q15, -MODS_DEPTH_ONE, MODS_DEPTH_ONE);
    return mods_clamp(d, -MODS_DEPTH_ONE, MODS_DEPTH_ONE);
}

/* ---- routing ---- */

#define MODS_AT_K 133144u                 /* 2^16 x 32768 / 16129: pressure x |amount| (0..16129) -> Q15 */

int32_t mods_at(int pressure_0_127, int at_amt)
{
    int32_t p = mods_clamp(pressure_0_127, 0, 127), a = mods_clamp(at_amt, -127, 127);
    uint32_t m = (uint32_t)(p * (a < 0 ? -a : a));
    int32_t r = (int32_t)((m * MODS_AT_K + 32768u) >> 16);   /* 16129 x K + 2^15 < 2^32; 16129 -> 32768 exactly */
    return a < 0 ? -r : r;
}

/* full x amount / 127, rounded, exactly symmetric in the amount: the gain of a signed source amount. full <= 255 x
 * 2^16, so full x 127 < 2^31 */
static int32_t mods_gain(int32_t full, int amt)
{
    int32_t a = mods_clamp(amt, -127, 127);
    int32_t g = (int32_t)(((uint32_t)full * (uint32_t)(a < 0 ? -a : a) + 63u) / 127u);
    return a < 0 ? -g : g;
}

/* a Q15 source (+-32768 = 1) x full scale, rounded */
static inline __attribute__((always_inline)) int32_t mods_q15(int32_t src, int32_t full)
{
    return (int32_t)(((int64_t)src * full + 16384) >> 15);
}

/* the filter envelope (Q30, 0..1) x a gain, rounded */
static inline __attribute__((always_inline)) int32_t mods_env(int32_t env, int32_t gain)
{
    return (int32_t)(((int64_t)env * gain + (LFO_Q30 / 2)) >> 30);
}

void mods_xm_gains(mods_xm_t *g, const int16_t *v)
{
    int a = v[P_XM_VCO2];
    g->pitch = v[P_XM_D_VCO1] ? mods_gain(MODS_XM_PITCH, a) : 0;
    g->shape = v[P_XM_D_SHAPE1] ? mods_gain(MODS_XM_SHAPE, a) : 0;
    g->pw = v[P_XM_D_PW1] ? mods_gain(MODS_XM_PW, a) : 0;
    g->freq = v[P_XM_D_FFREQ] ? mods_gain(MODS_XM_FREQ, a) : 0;
    g->mode = v[P_XM_D_FMODE] ? mods_gain(MODS_XM_MODE, a) : 0;
    g->bp = v[P_XM_D_BP] ? mods_gain(MODS_XM_BP, a) : 0;
}

void mods_sum(mods_t *m, const int16_t *v, int32_t lfo_q15, int32_t depth_q15, int32_t fenv_q30, int32_t vco2_q15,
              int32_t at_q15)
{
    mods_t o = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    /* LFO (rule 4): one value x one depth into every enabled destination */
    int32_t lfo = mods_clamp(lfo_q15, -LFO_ONE, LFO_ONE) * mods_clamp(depth_q15, -MODS_DEPTH_ONE, MODS_DEPTH_ONE);
    lfo = (lfo + 16384) >> 15;                                  /* Q15, |lfo| <= LFO_ONE */
    if (lfo) {
        if (v[P_LFO_D_VCO1]) o.vco1_pitch += mods_q15(lfo, MODS_LFO_PITCH);
        if (v[P_LFO_D_VCO2]) o.vco2_pitch += mods_q15(lfo, MODS_LFO_PITCH);
        if (v[P_LFO_D_PW] & 1) o.vco1_pw += mods_q15(lfo, MODS_LFO_PW);   /* 0 none, 1 PW1, 2 PW2, 3 both */
        if (v[P_LFO_D_PW] & 2) o.vco2_pw += mods_q15(lfo, MODS_LFO_PW);
        if (v[P_LFO_D_AMP]) o.amp += mods_q15(lfo, MODS_LFO_AMP);
        if (v[P_LFO_D_FFREQ]) o.flt_freq += mods_q15(lfo, MODS_LFO_FREQ);
        if (v[P_LFO_D_FMODE]) o.flt_mode += mods_q15(lfo, MODS_LFO_MODE);
    }
    /* aftertouch (rule 7): AT -> LFO AMT lives in lfo_depth */
    int32_t at = mods_clamp(at_q15, -32768, 32768);
    if (at) {
        if (v[P_AT_D_VCO1]) o.vco1_pitch += mods_q15(at, MODS_AT_PITCH);
        if (v[P_AT_D_VCO2]) o.vco2_pitch += mods_q15(at, MODS_AT_PITCH);
        if (v[P_AT_D_AMP]) o.amp += mods_q15(at, MODS_AT_AMP);
        if (v[P_AT_D_FFREQ]) o.flt_freq += mods_q15(at, MODS_AT_FREQ);
        if (v[P_AT_D_FMODE]) o.flt_mode += mods_q15(at, MODS_AT_MODE);
    }
    /* X-Mod (rule 6): the filter envelope here, VCO 2 through the per-sample term (same code as mods_xm_tick) */
    int32_t fe = mods_clamp(fenv_q30, 0, LFO_Q30);
    int fa = v[P_XM_FENV];
    if (fe && fa) {
        if (v[P_XM_D_VCO1]) o.vco1_pitch += mods_env(fe, mods_gain(MODS_XM_PITCH, fa));
        if (v[P_XM_D_SHAPE1]) o.vco1_shape += mods_env(fe, mods_gain(MODS_XM_SHAPE, fa));
        if (v[P_XM_D_PW1]) o.vco1_pw += mods_env(fe, mods_gain(MODS_XM_PW, fa));
        if (v[P_XM_D_FFREQ]) o.flt_freq += mods_env(fe, mods_gain(MODS_XM_FREQ, fa));
        if (v[P_XM_D_FMODE]) o.flt_mode += mods_env(fe, mods_gain(MODS_XM_MODE, fa));
        if (v[P_XM_D_BP]) o.bp_mix += mods_env(fe, mods_gain(MODS_XM_BP, fa));
    }
    if (vco2_q15) {
        mods_xm_t g;
        mods_xm_gains(&g, v);
        mods_xm_tick(m, &o, &g, vco2_q15);
    } else {
        *m = o;
    }
}

/* ---- per-sample VCO 1 pitch ---- */

/* 2^f, f Q24 in 0..2^24 (inclusive), Q30: 2^30..2^31 (2^31 exactly at f = 1) */
static inline __attribute__((always_inline)) uint32_t mods_exp2f(int32_t f24)
{
    int32_t f = f24 << 6;                                       /* Q30, <= 2^30 */
    int32_t p = MODS_E3;
    p = MODS_E2 + mods_mul30(p, f);
    p = MODS_E1 + mods_mul30(p, f);
    return (uint32_t)LFO_Q30 + (uint32_t)mods_mul30(p, f);
}

/* clamp without branches (v - lo, v - hi must not overflow) */
static inline __attribute__((always_inline)) int32_t mods_clampb(int32_t v, int32_t lo, int32_t hi)
{
    int32_t d = v - hi;
    v = hi + (d & (d >> 31));
    d = v - lo;
    return lo + (d & ~(d >> 31));
}

/* per sample: inlined into voice_render (the CPU gate); mods_fm1 is the external wrapper */
static inline __attribute__((always_inline)) void mods_fm1_hot(osc_t *o, uint32_t inc1, uint32_t inv1,
                                                               int32_t note1_q16, int32_t semis_q16)
{
    /* clamp the modulated note to the oscillator's range (as osc_set_pitch would). In 32 bits, on the notes above
     * the floor (b = note - MODS_NOTE_LO, a + 0x3C0000: MODS_NOTE_LO, 0xFFC40000, is in the chip's ROM window, see
     * osc.c osc_log2_inc), and with semis first limited to the range's width (195 semitones), which changes no result
     * and keeps b + semis from overflowing for absurd inputs */
    const int32_t span = MODS_NOTE_HI - MODS_NOTE_LO;
    int32_t b = (int32_t)((uint32_t)note1_q16 + 60 * 65536);
    int32_t m = mods_clampb(semis_q16, -span, span);
    int32_t t = mods_clampb((int32_t)((uint32_t)b + (uint32_t)m), 0, span);
    int32_t s = (t - b) & -(int32_t)((uint32_t)b <= (uint32_t)span);   /* base outside the range: leave it (s = 0) */
    /* (no branches: a taken branch costs ~7.5 cycles on this core; |s| <= 195 semitones, Q16) */
    /* octaves, Q24 (|x| < 2^30): s x 64 / 3, truncated as the division would, with a multiply (the compiler keeps a
     * 10-cycle SDIV at -Os) */
    int32_t x = (int32_t)(((int64_t)(s * 64) * 0x55555556) >> 32) - ((s * 64) >> 31);
    int n = x >> 24;                                            /* floor */
    int32_t f = x & (LFO_Q24 - 1);
    /* inc x 2^x = inc x 2^f x 2^n; inv x 2^-x = inv x 2^(1 - f) / 2 x 2^-n. The 64-bit products take constant
     * shifts only (no variable 64-bit shift, which may need a libgcc helper): inc x 2^f < 2^31 x 2 and
     * inv x 2^(1 - f) / 2 < 2^32 fit 32 bits; the octave n is a 32-bit shift after that (|n| <= 17; the clamp keeps
     * the final increment below 2^31 and the inverse below 2^32). */
    uint32_t ri = (uint32_t)(((uint64_t)inc1 * mods_exp2f(f)) >> 30);
    uint32_t rv = (uint32_t)(((uint64_t)inv1 * mods_exp2f(LFO_Q24 - f)) >> 31);
    int nl = n & ~(n >> 31), nr = -n & (n >> 31);              /* max(n, 0), max(-n, 0) */
    o->inc1 = ri << nl >> nr;
    o->inv1 = rv << nr >> nl;
}
void mods_fm1(osc_t *o, uint32_t inc1, uint32_t inv1, int32_t note1_q16, int32_t semis_q16)
{
    mods_fm1_hot(o, inc1, inv1, note1_q16, semis_q16);
}
