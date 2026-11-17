/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX filter: the voice's 2-pole state-variable filter (see filter.h for the interface, levels and mapping).
 *
 * Ported from the ANALOG engine (João Hortale, smk37-analog, branch analog-engine, web/synth/analog.c, MIT): the
 * zero-delay-feedback (TPT, Zavalishin / Simper) SVF, the soft clip soft / soft_shoulder (band-pass node, output) and
 * coef_at's coefficients, the cutoff table interpolation and reso_k's exponential Q map; and its exp2 polynomial
 * (web/synth/fixed.c, fm_exp2_fixed). New here: the 165-step semitone cutoff table, the damping floor at low cutoffs,
 * the input drive polynomial, the band-pass output and its crossfade with the MODE output.
 *
 * The audio path runs in single-precision float on the AC79's FPU (-mfprev1, tools/build.py; docs/RULES.md): the
 * phase-1 CPU gate measured it cheaper than the 32-bit / 64-bit integer SVF it replaces (docs/MEASUREMENTS.md), whose
 * exact 64-bit path the overdriven, resonant voice took on most samples. Float units: 1.0 = Q15 32768 = the soft-clip
 * knee (the old Q28 2^28). The cutoff and RESONANCE maps stay integer (tables and the exp2 polynomial, no libm): g and
 * k leave them in Q28 and become floats; 1 / (1 + g (g + k)) is one FPU division. No double anywhere; every float
 * operation is a native FPU instruction (no library call). */
#include "filter.h"

#define SVF_HOT static inline __attribute__((always_inline))
/* SVF_INLINE: the per-sample function, inlined into voice_render (unity build; the CPU gate, docs/MEASUREMENTS.md);
 * the external name is a wrapper for the rest (tests, control rate) */
#define SVF_INLINE static inline __attribute__((always_inline))
#define SVF_KNEE INT32_C(268435456)        /* 2^28: Q28 nominal full scale, the soft-clip knee */
#define SVF_Q24 INT32_C(16777216)
#define SVF_Q30 INT32_C(1073741824)
#define SVF_RESO_STEP 371326               /* log2(50) / 255 in Q24: k = 2 * 50^(-reso / 255), Q 0.5 .. 25 */
/* Damping floor: k >= ALPHA / (pi fc), i.e. the ring of the resonance decays at least ALPHA = 15 neper / s (an
 * impulse falls by 130 dB a second), whatever RESO. It only acts below ~120 Hz, where RESO 255's Q 25 would ring for
 * seconds (Q <= pi fc / ALPHA: 4.3 at 20 Hz, 21 at 100 Hz). SVF_FLOOR_C = log2(15 / (pi 440)) in Q24. */
#ifndef SVF_FLOOR_C
#define SVF_FLOOR_C (-109487529)
#endif
#if SVF_RATE != 44100
#error "svf_gtab is computed for 44100 Hz"
#endif

/* g = tan(pi fc / fs) in Q28 at every FILTER FREQ step, fc = 440 * 2^((step - 99) / 12) (1.44 Hz .. 18.8 kHz).
 * Generated offline (python: round(tan(pi * 440 * 2 ** ((s - 99) / 12) / 44100) * 2 ** 28)); linear interpolation
 * between steps stays within 9 cents (worst just under the top). */
static const int32_t svf_gtab[166] = {   /* the last entry twice: svf_g interpolates without a test */
    27638, 29281, 31023, 32867, 34822, 36892, 39086, 41410,
    43873, 46481, 49245, 52174, 55276, 58563, 62045, 65735,
    69643, 73785, 78172, 82820, 87745, 92963, 98491, 104347,
    110552, 117126, 124090, 131469, 139287, 147569, 156344, 165641,
    175490, 185925, 196981, 208694, 221104, 234251, 248181, 262938,
    278574, 295138, 312688, 331282, 350981, 371851, 393963, 417389,
    442208, 468503, 496362, 525877, 557148, 590277, 625377, 662564,
    701963, 743704, 787927, 834780, 884419, 937009, 992727, 1051759,
    1114300, 1180561, 1250761, 1325137, 1403935, 1487419, 1575867, 1669576,
    1768857, 1874042, 1985482, 2103550, 2228639, 2361167, 2501577, 2650338,
    2807947, 2974929, 3151843, 3339281, 3537867, 3748266, 3971181, 4207357,
    4457585, 4722699, 5003589, 5301193, 5616508, 5950589, 6304556, 6679595,
    7076963, 7497994, 7944101, 8416782, 8917628, 9448323, 10010656, 10606523,
    11237935, 11907029, 12616071, 13367467, 14163771, 15007697, 15902129, 16850131,
    17854961, 18920087, 20049196, 21246216, 22515332, 23861007, 25288000, 26801395,
    28406627, 30109507, 31916264, 33833575, 35868614, 38029095, 40323333, 42760301,
    45349709, 48102082, 51028861, 54142517, 57456682, 60986305, 64747841, 68759467,
    73041350, 77615961, 82508460, 87747165, 93364124, 99395820, 105884053, 112877044,
    120430821, 128611004, 137495093, 147175454, 157763261, 169393763, 182233436, 196489846,
    212425517, 230377822, 250788216, 274246337, 301558657, 333859260, 372796369, 420862882,
    482019896, 562968858, 676025055, 846569048, 1136663213, 1136663213,
};

/* 2^x: x signed Q24, the result with 'fraction' fractional bits, saturating (fm_exp2_fixed, ANALOG's fixed.c). */
static uint32_t svf_exp2(int32_t x, unsigned fraction)
{
    int n = (x >> 24) + ((x >> 23) & 1);
    int32_t f = (int32_t)(((int64_t)x - (int64_t)n * SVF_Q24) * 256);
    int32_t p = 164774;
    p = (int32_t)(((int64_t)p * f) >> 32) + 1438807;
    p = (int32_t)(((int64_t)p * f) >> 32) + 10327774;
    p = (int32_t)(((int64_t)p * f) >> 32) + 59596201;
    p = (int32_t)(((int64_t)p * f) >> 32) + 257941207;
    p = (int32_t)(((int64_t)p * f) >> 32) + 744261145;
    p = (int32_t)(((int64_t)p * f) >> 32) + SVF_Q30;
    int shift = 30 - (int)fraction - n;
    if (shift >= 32) return 0;
    if (shift >= 0) return (uint32_t)p >> shift;
    if (shift <= -32 || (uint32_t)p > (UINT32_MAX >> -shift)) return UINT32_MAX;
    return (uint32_t)p << -shift;
}


#define SVF_F28 (1.0f / 268435456.0f)     /* Q28 -> float */

/* branch-free integer min / max (a taken branch costs ~7.5 cycles on this core, an ALU op 1); a - b must not overflow */
SVF_HOT int32_t svf_imin(int32_t a, int32_t b) { int32_t d = a - b; return b + (d & (d >> 31)); }
SVF_HOT int32_t svf_imax(int32_t a, int32_t b) { int32_t d = a - b; return b + (d & ~(d >> 31)); }

/* Soft clip, odd and C1-continuous: linear up to the knee 1, then a quadratic shoulder 1 + 2u - u^2 (u = (|x| - 1) / 2)
 * rising to 2 at |x| = 3; capped at 2 - 2^-12. |soft(x)| <= |x|, same sign. NaN maps to the cap. (A branch-free form
 * measured slower: the knee test is mostly not taken.) */
SVF_HOT float svf_soft(float x)
{
    float a = __builtin_fabsf(x);
    if (a <= 1.0f) return x;
    float y = 2.0f - 1.0f / 4096.0f;
    if (a < 3.0f) {
        float u = (a - 1.0f) * 0.5f, v = 1.0f + 2.0f * u - u * u;
        if (v < y) y = v;
    }
    return x < 0.0f ? -y : y;
}

/* Input drive (the mixer overdrive): Q15 in, float out (1.0 = Q15 32768). An odd polynomial with no linear zone, so
 * saturation grows gradually from the lowest levels instead of switching on at a knee: with A = |x| / 32768,
 *   P(A) = A - 0.0012 A^2 - 0.007 A^3 - 0.005 A^5,   flat at P(2.429) = 1.899 for A >= 2.429 (where P' = 0).
 * THD of a sine: -75 dB at 0.25 x, -64 at 0.5 x (OSC_ONE: one oscillator at full level), -49 at 1 x, -32 at 1.5 x,
 * -12 at 4 x, rising by <= 12 dB per 0.25 x (the A^2 term keeps the bottom from rising 12 dB per doubling like a pure
 * cubic would). Gain at 0.5 x: -0.02 dB. P is monotone, |P(A)| <= A, same sign. */
#define SVF_DRIVE_AM 2.429f
#define SVF_DRIVE_CAP 1.8988f                  /* P(2.429) */
SVF_HOT float svf_drive(int32_t in)
{
    float x = (float)in * (1.0f / 32768.0f), a = __builtin_fabsf(x), y = SVF_DRIVE_CAP;
    if (a < SVF_DRIVE_AM) {
        float a2 = a * a;
        y = a * (1.0f - 0.0012f * a - 0.007f * a2 - 0.005f * (a2 * a2));
    }
    return x < 0.0f ? -y : y;
}

/* One TPT SVF sample: input drive; band-pass v1 = (s1 + g (x - s2)) / D = a1 s1 + a2 (x - s2), soft-clipped at twice
 * the knee (the resonance limit: it bounds the loop at any RESO like an overdriven analog core); low-pass v2 = s2 + g v1
 * (exact for the clipped v1); high-pass x - k v1 - v2; y = lp v2 + hp hp + bp v1, soft-clipped (|y| <= 2, so the output
 * is <= 65536 in Q15). lp + hp = x - k v1: the notch at MODE 128. The clip keeps v1 between 0 and its linear value,
 * which keeps the states within the linear filter's bounds (the integer version's proof); in float nothing can wrap
 * anyway. The output is truncated toward zero (no DC tail from a floor). */
SVF_INLINE int32_t svf_tick_hot(svf_t *f, int32_t in_q15, const svf_coef_t *c)
{
    float x = svf_drive(in_q15), s1 = f->s1, s2 = f->s2;
    float v1 = 2.0f * svf_soft(0.5f * (c->a1 * s1 + c->a2 * (x - s2)));
    float gv = c->g * v1, v2 = s2 + gv;
    float hp = x - c->k * v1 - v2;
    float y = c->lp * v2 + c->hp * hp + c->bp * v1;
    f->s1 = 2.0f * v1 - s1;
    f->s2 = v2 + gv;
    return (int32_t)(svf_soft(y) * 32768.0f);
}
int32_t svf_tick(svf_t *f, int32_t in_q15, const svf_coef_t *c) { return svf_tick_hot(f, in_q15, c); }

SVF_HOT int32_t svf_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
/* the same without branches (a taken branch costs ~7.5 cycles on this core, an ALU op 1), for the per-sample path;
 * v - lo and v - hi must not overflow (|v| < 2^30 here) */
SVF_HOT int32_t svf_clampb(int32_t v, int32_t lo, int32_t hi)
{
    int32_t d = v - hi;
    v = hi + (d & (d >> 31));
    d = v - lo;
    return lo + (d & ~(d >> 31));
}

/* g at a Q16 FILTER FREQ position (clamped), interpolated linearly between the table's steps, Q28 */
SVF_HOT int32_t svf_g(int32_t cut)
{
    unsigned i = (unsigned)cut >> 16;
    int32_t g = svf_gtab[i];
    return g + (int32_t)(((int64_t)(svf_gtab[i + 1] - g) * (cut & 0xffff)) >> 16);   /* at 164 exactly: + 0 */
}

int32_t svf_reso_k(int reso)
{
    reso = (int)svf_clamp(reso, 0, 255);
    return (int32_t)svf_exp2(SVF_Q24 - reso * SVF_RESO_STEP, 28);
}

/* the float coefficients from g, k (Q28), MODE 0..255 and the band-pass mix 0..32768. MODE: 0 LP, 128 notch (LP + HP),
 * 255 HP; linear either side. Then the crossfade with the band-pass, whose gain is max(k, 1): 0 dB at the peak for
 * Q <= 1, Q above (the LP's resonant peak). */
SVF_HOT void svf_coef_set(svf_coef_t *c, int32_t g28, int32_t k28, int mode, int32_t bp_mix)
{
    float g = (float)g28 * SVF_F28, k = (float)k28 * SVF_F28;
    float a1 = 1.0f / (1.0f + g * (g + k));
    c->g = g, c->k = k, c->a1 = a1, c->a2 = g * a1;
    float b = (float)bp_mix * (1.0f / 32768.0f), nb = 1.0f - b;
    float m = (float)mode;
    c->lp = (mode <= 128 ? 1.0f : (255.0f - m) * (1.0f / 127.0f)) * nb;
    c->hp = (mode <= 128 ? m * (1.0f / 128.0f) : 1.0f) * nb;
    c->bp = b * (k > 1.0f ? k : 1.0f);
}

void svf_coef(svf_coef_t *c, int32_t cutoff_q16, int reso, int mode, int32_t bp_mix)
{
    int32_t cut = svf_clamp(cutoff_q16, 0, SVF_CUT_MAX);
    reso = (int)svf_clamp(reso, 0, 255);
    mode = (int)svf_clamp(mode, 0, 255);
    int32_t b = svf_clamp(bp_mix, 0, 32768);
    /* k = 1 / Q: RESO's exponential map, raised to the damping floor ALPHA / (pi fc) at low cutoffs, at most 2 */
    int32_t k = svf_reso_k(reso);
    int32_t lf = SVF_FLOOR_C + (99 * 65536 - cut) * 64 / 3;   /* log2 of the floor, Q24: semitones / 12 */
    int32_t kf = lf >= SVF_Q24 ? 2 * SVF_KNEE : (int32_t)svf_exp2(lf, 28);
    if (k < kf) k = kf;
    svf_coef_set(c, svf_g(cut), k, mode, b);
}

/* ---- the per-sample coefficient path (audio-rate X-Mod into the filter) ----
 * svf_coef_fast gives svf_coef's coefficients without its exp2 calls: g from svf_gtab as svf_coef does (the same
 * interpolation, bit for bit), the damping floor from svf_ktab (its value at every step where it can win, interpolated
 * linearly: <= 0.05 % off the exp2, 2 % at the cap's kink near step 8.7). */

/* the damping floor k = ALPHA / (pi fc), Q28, capped at 2, at FILTER FREQ steps 0..80; from step 80 up it is below
 * RESO 255's k (0.04) and never wins. Generated with svf_coef's own expression (svf_exp2 of SVF_FLOOR_C + ...). */
static const int32_t svf_ktab[82] = {   /* + the last entry twice (svf_gk at step 80 exactly) */
    536870912, 536870912, 536870912, 536870912, 536870912, 536870912, 536870912, 536870912,
    536870912, 527294872, 497700070, 469766318, 443400368, 418514206, 395024813, 372853780,
    351927095, 332174949, 313531406, 295934233, 279324725, 263647436, 248850035, 234883159,
    221700184, 209257103, 197512406, 186426890, 175963547, 166087474, 156765703, 147967116,
    139662362, 131823718, 124425017, 117441579, 110850092, 104628551, 98756203, 93213445,
    87981773, 83043737, 78382851, 73983558, 69831181, 65911859, 62212508, 58720789,
    55425046, 52314275, 49378101, 46606722, 43990886, 41521868, 39191425, 36991779,
    34915590, 32955929, 31106254, 29360394, 27712523, 26157137, 24689050, 23303361,
    21995443, 20760934, 19595712, 18495889, 17457795, 16477964, 15553127, 14680197,
    13856261, 13078568, 12344525, 11651680, 10997721, 10380467, 9797856, 9247944,
    8728897, 8728897,
};
#define SVF_KTAB_TOP (80 * 65536)

/* g and k of a cutoff (clamped) for the per-sample path, without branches: the floor at min(cut, step 80), where it
 * is already below every RESONANCE's k (so the max keeps kres above it) */
SVF_HOT int32_t svf_gk(int32_t cutoff_q16, int32_t kres, int32_t *k)
{
    int32_t cut = svf_clampb(cutoff_q16, 0, SVF_CUT_MAX), cf = svf_clampb(cut, 0, SVF_KTAB_TOP);
    unsigned i = (unsigned)cf >> 16;
    int32_t kf = svf_ktab[i] - (int32_t)(((int64_t)(svf_ktab[i] - svf_ktab[i + 1]) * (cf & 0xffff)) >> 16);
    int32_t d = kres - kf;
    *k = kf + (d & ~(d >> 31));                               /* max(kres, kf) */
    return svf_g(cut);
}
SVF_INLINE void svf_coef_fast_hot(svf_coef_t *c, int32_t cutoff_q16, int32_t kres, int mode, int32_t bp_mix)
{
    int32_t k, g = svf_gk(cutoff_q16, kres, &k);
    svf_coef_set(c, g, k, svf_clampb(mode, 0, 255), svf_clampb(bp_mix, 0, 32768));
}
/* X-Mod into F FREQ only (MODE and BP not modulated): g, k, a1, a2 and the band-pass gain (bp_f = the BP mix as a
 * fraction, from the control tick); lp and hp stay as the tick set them */
SVF_INLINE void svf_coef_gk_hot(svf_coef_t *c, int32_t cutoff_q16, int32_t kres, float bp_f)
{
    int32_t k28, g28 = svf_gk(cutoff_q16, kres, &k28);
    float g = (float)g28 * SVF_F28, k = (float)k28 * SVF_F28;
    float a1 = 1.0f / (1.0f + g * (g + k));
    c->g = g, c->k = k, c->a1 = a1, c->a2 = g * a1;
    c->bp = bp_f * (k > 1.0f ? k : 1.0f);
}
void svf_coef_fast(svf_coef_t *c, int32_t cutoff_q16, int32_t kres, int mode, int32_t bp_mix)
{
    svf_coef_fast_hot(c, cutoff_q16, kres, mode, bp_mix);
}

#ifdef DINGHY_HOST
#include <math.h>
/* host only (tests): the cutoff the interpolated g realises, fc = fs / pi * atan(g) */
uint32_t svf_cutoff_hz(int32_t cutoff_q16)
{
    double g = svf_g(svf_clamp(cutoff_q16, 0, SVF_CUT_MAX)) / 268435456.0;
    return (uint32_t)lrint(SVF_RATE / 3.14159265358979323846 * atan(g));
}
#endif
