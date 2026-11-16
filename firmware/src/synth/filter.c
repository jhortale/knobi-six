/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX filter: the voice's 2-pole state-variable filter (see filter.h for the interface, levels and mapping).
 *
 * Ported from the ANALOG engine (João Hortale, smk37-analog, branch analog-engine, web/synth/analog.c, MIT): the
 * zero-delay-feedback (TPT, Zavalishin / Simper) SVF with its 32-bit fast path svf_tick and the exact fallback
 * svf_slow, the soft clip soft / soft_shoulder (input, band-pass node, output), recip_q30 and coef_at's
 * coefficients, the cutoff table interpolation and reso_k's exponential Q map; and its exp2 polynomial
 * (web/synth/fixed.c, fm_exp2_fixed). New here: the 165-step semitone cutoff table, the damping floor at low cutoffs,
 * the band-pass output and its crossfade with the MODE output, a slow path with only 32 x 32 products, and the
 * Newton step of recip_q30 in 32 x 32 products.
 *
 * Fixed point: the filter domain is Q28 (2^28 = Q15 32768 = the soft-clip knee); g Q28, k Q28, a1 / a2 Q30, the
 * output gains Q15. Per sample: only 32 x 32 -> 64 products, no division. Per svf_coef call (control rate): one
 * 32-bit division (recip_q30's seed), at most two exp2 polynomials. */
#include "filter.h"

#define SVF_HOT static inline __attribute__((always_inline))
#define SVF_COLD static __attribute__((noinline, cold))
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
 * between steps stays within 9 cents (worst just under the top). The top keeps g + k < 2^31 and 1 + g (g + k) < 128,
 * recip_q30's domain. */
static const int32_t svf_gtab[165] = {
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
    482019896, 562968858, 676025055, 846569048, 1136663213,
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

SVF_HOT int32_t svf_sat32(int64_t v)
{
    return v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : (int32_t)v;
}

/* Soft clip, odd and C1-continuous: linear up to the 2^28 knee, then a quadratic shoulder 2^28 + 2u - u^2 / 2^28
 * (u = (|x| - 2^28) / 2) rising to 2^29 at 3 * 2^28; capped just below 2^29. |soft(x)| <= |x|, same sign. The
 * shoulder is the cold path: normal levels stay under the knee. */
SVF_COLD int32_t svf_soft_shoulder(int32_t x)
{
    uint32_t a = x < 0 ? 0u - (uint32_t)x : (uint32_t)x;
    int32_t y = 2 * SVF_KNEE - 65536;
    if (a < 3u * SVF_KNEE) {
        int32_t u = (int32_t)((a - SVF_KNEE) >> 1);   /* < 2^28: u^2 >> 28 = (4u)(4u) >> 32 */
        int32_t v = SVF_KNEE + 2 * u - (int32_t)(((int64_t)(u * 4) * (u * 4)) >> 32);
        if (v < y) y = v;
    }
    return x < 0 ? -y : y;
}
SVF_HOT int32_t svf_soft(int32_t x)
{
    if ((uint32_t)x + SVF_KNEE <= 2u * SVF_KNEE) return x;   /* |x| <= knee */
    return svf_soft_shoulder(x);
}

/* Q15 input -> Q28, clamped to +-2^17 (4 x Q15 full scale) first so the shift cannot overflow (+-2^30) */
SVF_HOT int32_t svf_in(int32_t in)
{
    if (in > 131072) in = 131072;
    if (in < -131072) in = -131072;
    return in * 8192;
}
/* Q28 -> Q15, rounded (floor would leave a -1 LSB tail on a decayed negative ring) */
SVF_HOT int32_t svf_out(int32_t y)
{
    return (y + 4096) >> 13;
}

/* One TPT SVF sample, the reference arithmetic (64-bit sums of 32 x 32 products, saturating to int32): input soft
 * clip; band-pass v1 = a1 s1 + a2 (x - s2), soft-clipped at twice the knee (the resonance limit: it bounds the loop
 * at any RESO like an overdriven analog core); low-pass v2 = s2 + g v1 (exact for the clipped v1); high-pass
 * x - k v1 - v2; y = lp v2 + hp hp + bp v1, soft-clipped. lp + hp = x - k v1: the notch at MODE 128. Used when the
 * states are large or the output overflows; svf_tick below is the fast path and agrees with it bit for bit. */
struct svf_out {
    int32_t y, s1, s2;
};
SVF_COLD struct svf_out svf_slow(int32_t s1, int32_t s2, int32_t in, const svf_coef_t *c)
{
    int32_t x = svf_soft(svf_in(in));
    /* |a1 s1| < 2^61, |a2 x|, |a2 s2| < 2^60 (a2 = g / (1 + g (g + k)) <= 1/2): the sum fits, and >> 31 fits int32 */
    int32_t v1 = 2 * svf_soft(svf_sat32(((int64_t)c->a1 * s1 + (int64_t)c->a2 * x - (int64_t)c->a2 * s2) >> 31));
    int32_t v2 = svf_sat32(s2 + (((int64_t)c->g * v1) >> 28));
    int32_t hp = svf_sat32((int64_t)x - (((int64_t)c->k * v1) >> 28) - v2);
    int64_t y = ((int64_t)v2 * c->lp + (int64_t)hp * c->hp + (int64_t)v1 * c->bp) >> 15;
    struct svf_out o = {svf_out(svf_soft(svf_sat32(y))), svf_sat32(2 * (int64_t)v1 - s1), svf_sat32(2 * (int64_t)v2 - s2)};
    return o;
}

/* The same sample in 32-bit arithmetic. Inside the gate |s1|, |s2| < 2^29, and |x| <= 2^29 - 65536 after the input
 * clip, so x - s2 fits. For the linear SVF (D = 1 + g (g + k), 0 <= k <= 2, any g >= 0; 1 / D <= 1, 2 g / D <= 1,
 * g^2 / D < 1, (g + k) / D <= 2):
 *   v1 = (s1 + g (x - s2)) / D            |g v1| <= |s1| / 2 + |x - s2| < 1.25 x 2^30
 *   2 v1 - s1 = (2 / D - 1) s1 + 2 g (x - s2) / D        <= |s1| + |x - s2| < 1.5 x 2^30
 *   v2 = s2 + g v1 = (1 - g^2 / D) s2 + g s1 / D + g^2 x / D    <= |s2| + |s1| / 2 + |x| < 1.25 x 2^30
 *   2 v2 - s2 = (1 - 2 g^2 / D) s2 + 2 g s1 / D + 2 g^2 x / D   <= |s2| + |s1| + 2 |x| < 2^31 - 2^17
 *   hp = x - k v1 - v2 = ((x - s2) - (g + k) s1) / D            <= |x| + |s2| + 2 |s1| < 2^31 - 2^16
 * The band-pass clip replaces v1 by a value between 0 and v1 (same sign, smaller), and every quantity above is affine
 * in v1, so it lies between its value at v1 = 0 (x - s2, -s1, s2, s2, x - s2) and the linear one: the bounds hold
 * clipped too. Rounding adds a few LSB against margins of 2^16. hp may wrap through x - k v1 (unsigned) but its value
 * fits; |k v1| >> 28 < 2^31 since k <= 2^29 and |v1| < 2^30. Only the output sum can overflow (the band-pass gain is
 * up to 2): it is checked, and anything outside takes svf_slow from the unchanged state. The bounds need k <= 2 (RESO
 * 0 and the floor's cap) and nothing of g, so the 1.44 Hz bottom of the table is safe (ANALOG's comment asked for
 * >= 20 Hz; the bounds above do not shrink with g). test_filter's fast_equals_slow checks it on random states,
 * coefficients and inputs. */
int32_t svf_tick(svf_t *f, int32_t in_q15, const svf_coef_t *c)
{
    int32_t s1 = f->s1, s2 = f->s2;
    if (__builtin_expect(((uint32_t)s1 + (1u << 29) | (uint32_t)s2 + (1u << 29)) < 1u << 30, 1)) {
        int32_t x = svf_soft(svf_in(in_q15));
        int32_t v1 = 2 * svf_soft((int32_t)(((int64_t)c->a1 * s1 + (int64_t)c->a2 * (x - s2)) >> 31));
        int32_t gv = (int32_t)(((int64_t)c->g * v1) >> 28), v2 = s2 + gv;
        int32_t hp = (int32_t)((uint32_t)x - (uint32_t)(int32_t)(((int64_t)c->k * v1) >> 28) - (uint32_t)v2);
        int64_t y = ((int64_t)v2 * c->lp + (int64_t)hp * c->hp + (int64_t)v1 * c->bp) >> 15;
        if (__builtin_expect((int32_t)y == y, 1)) {
            f->s1 = 2 * v1 - s1;
            f->s2 = v2 + gv;   /* 2 v2 - s2 */
            return svf_out(svf_soft((int32_t)y));
        }
    }
    struct svf_out o = svf_slow(s1, s2, in_q15, c);
    f->s1 = o.s1;
    f->s2 = o.s2;
    return o.y;
}

/* 2^54 / d: Q30 reciprocal of a Q24 value d in [1, 128). A 32-bit divide gives a 16-bit seed x (within 2^-14 of the
 * answer, so x < 2^31 and |e| < 2^40), one Newton step brings it to ~30 bits; every product is 32 x 32. */
static uint32_t svf_recip_q30(uint32_t d)
{
    unsigned s = 9;
    while ((d >> s) >= 65536u) ++s;
    uint32_t x = (0x80000000u / (d >> s)) << (23 - s);
    int64_t e = (INT64_C(1) << 54) - (int64_t)((uint64_t)d * x);
    return (uint32_t)((int64_t)x + (((int64_t)(int32_t)x * (int32_t)(e >> 24)) >> 30));
}

static int32_t svf_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* g at a Q16 FILTER FREQ position (clamped), interpolated linearly between the table's steps */
static int32_t svf_g(int32_t cut)
{
    unsigned i = (unsigned)cut >> 16;
    int32_t g = svf_gtab[i];
    if (i < 164) g += (int32_t)(((int64_t)(svf_gtab[i + 1] - g) * (cut & 0xffff)) >> 16);
    return g;
}

void svf_coef(svf_coef_t *c, int32_t cutoff_q16, int reso, int mode, int32_t bp_mix)
{
    int32_t cut = svf_clamp(cutoff_q16, 0, SVF_CUT_MAX);
    reso = (int)svf_clamp(reso, 0, 255);
    mode = (int)svf_clamp(mode, 0, 255);
    int32_t b = svf_clamp(bp_mix, 0, 32768);
    int32_t g = svf_g(cut);
    /* k = 1 / Q: RESO's exponential map, raised to the damping floor ALPHA / (pi fc) at low cutoffs, at most 2 */
    int32_t k = (int32_t)svf_exp2(SVF_Q24 - reso * SVF_RESO_STEP, 28);
    int32_t lf = SVF_FLOOR_C + (99 * 65536 - cut) * 64 / 3;   /* log2 of the floor, Q24: semitones / 12 */
    int32_t kf = lf >= SVF_Q24 ? 2 * SVF_KNEE : (int32_t)svf_exp2(lf, 28);
    if (k < kf) k = kf;
    uint32_t d = (uint32_t)SVF_Q24 + (uint32_t)(((int64_t)g * (g + k)) >> 32);
    c->g = g;
    c->k = k;
    c->a1 = (int32_t)svf_recip_q30(d);
    c->a2 = (int32_t)(((int64_t)g * c->a1) >> 28);
    /* MODE: 0 LP, 128 notch (LP + HP), 255 HP; linear either side. Then the crossfade with the band-pass, whose gain
     * is max(k, 1): 0 dB at the peak for Q <= 1, Q above (the LP's resonant peak). */
    int32_t lp = mode <= 128 ? 32768 : (255 - mode) * 32768 / 127;
    int32_t hp = mode <= 128 ? mode * 256 : 32768;
    c->lp = (lp * (32768 - b)) >> 15;
    c->hp = (hp * (32768 - b)) >> 15;
    c->bp = (int32_t)(((int64_t)b * (k > SVF_KNEE ? k : SVF_KNEE)) >> 28);
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
