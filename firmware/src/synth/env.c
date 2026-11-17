/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX envelopes and VCA (see env.h for the interface, ranges and scalings).
 *
 * Ported from the ANALOG engine (João Hortale, smk37-analog, branch analog-engine, web/synth/analog.c, MIT): the
 * computed-rate envelope (attack_inc / decay_c from the exp2 polynomial, no tables), the Q30 level, the per-sample
 * step level += mulh(target - level, m) with m = 1 - c in Q32, the exponential release towards -1/1024 that ends at
 * zero (so a release time is a time to silence), the 1 ms..5 s / 5 ms..10 s ranges, and the high-multiply VCA; and
 * its exp2 polynomial (web/synth/fixed.c, fm_exp2_fixed). New here: the RC attack (towards 4/3, stopping at full;
 * ANALOG's attack is a linear ramp), m computed as a series of x = -ln c instead of 1 - 2^-e (precise at 10 s, where
 * ANALOG's Q24 exponent has 0.3 % steps), the bipolar filter envelope offset, velocity on the amounts, and the
 * additive clamped VCA (rule 3).
 *
 * Fixed point: per sample (adsr_tick, vca_control, fenv_mod, vca_apply) only 32 x 32 -> 64 products, no division.
 * Per adsr_rates call (control rate): four exp2 polynomials and a few 32 x 32 products; one 32-bit division by a
 * constant (SUSTAIN). Unity-build safe: every file-scope name is adsr_ / vca_ / fenv_ prefixed. */
#include "env.h"

#define ADSR_Q24 INT32_C(16777216)
#define ADSR_A_TGT INT32_C(1431655765)    /* 4/3 in Q30: the attack charges towards it and stops at ADSR_ONE */
#define ADSR_TAIL (INT32_C(1) << 20)      /* the release falls towards -1/1024 and stops at 0 */
/* x = -ln c per sample at knob 0, Q32: ln(4) / (1 ms * fs) for the attack (from 0 to 1 of a charge towards 4/3 is
 * ln 4 time constants), ln(1025) / (5 ms * fs) for decay / release (from 1 to 0 of a fall towards -1/1024). */
#define ADSR_A_X0 135013355u
#define ADSR_D_X0 135032367u
/* per knob step, the time grows by 5000^(1/127) (attack) and 2000^(1/127) (decay / release): log2 of it, Q24 */
#define ADSR_A_STEP 1623257
#define ADSR_D_STEP 1448625
/* amount x velocity (each 0..127, so 0..16129) to a fraction: 2^32 / 16129 and FENV_RANGE x 65536 x 2^32 / 16129 /
 * 65536 (the env is taken in Q16), rounded; at full both land exactly on VCA_CEIL and FENV_RANGE x 65536. */
#define VCA_K 266288u
#define FENV_K 43671315u

#if ADSR_RATE != 44100
#error "the envelope constants are computed for 44100 Hz"
#endif

/* 2^x: x signed Q24, the result with 'fraction' fractional bits, saturating (fm_exp2_fixed, ANALOG's fixed.c). */
static uint32_t adsr_exp2(int32_t x, unsigned fraction)
{
    int n = (x >> 24) + ((x >> 23) & 1);
    int32_t f = (int32_t)(((int64_t)x - (int64_t)n * ADSR_Q24) * 256);
    int32_t p = 164774;
    p = (int32_t)(((int64_t)p * f) >> 32) + 1438807;
    p = (int32_t)(((int64_t)p * f) >> 32) + 10327774;
    p = (int32_t)(((int64_t)p * f) >> 32) + 59596201;
    p = (int32_t)(((int64_t)p * f) >> 32) + 257941207;
    p = (int32_t)(((int64_t)p * f) >> 32) + 744261145;
    p = (int32_t)(((int64_t)p * f) >> 32) + (1 << 30);
    int shift = 30 - (int)fraction - n;
    if (shift >= 32) return 0;
    if (shift >= 0) return (uint32_t)p >> shift;
    if (shift <= -32 || (uint32_t)p > (UINT32_MAX >> -shift)) return UINT32_MAX;
    return (uint32_t)p << -shift;
}

static int adsr_clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* The stage rate m = 1 - c in Q32 for knob k, with -ln c = x = x0 * 2^-(k step): m = 1 - e^-x = x - x^2/2 + x^3/6
 * (x <= 0.0315, so the next term is < 5e-8 of m). x: x0 (< 2^28) x a Q30 power of two <= 1, then >> 30. */
static int32_t adsr_m(uint32_t x0, int32_t step, int k)
{
    uint32_t x = (uint32_t)(((uint64_t)x0 * adsr_exp2(-k * step, 30)) >> 30);
    uint32_t x2 = (uint32_t)(((uint64_t)x * x) >> 32), x3 = (uint32_t)(((uint64_t)x2 * x) >> 32);
    return (int32_t)(x - (x2 >> 1) + x3 / 6u);
}

void adsr_rates(adsr_rates_t *r, int a, int d, int s, int rel)
{
    a = adsr_clamp(a, 0, 127), d = adsr_clamp(d, 0, 127), s = adsr_clamp(s, 0, 127), rel = adsr_clamp(rel, 0, 127);
    r->a_inc = adsr_m(ADSR_A_X0, ADSR_A_STEP, a);
    r->d_coef = adsr_m(ADSR_D_X0, ADSR_D_STEP, d);
    r->r_coef = adsr_m(ADSR_D_X0, ADSR_D_STEP, rel);
    r->s_level = s * 8454660 + (s * 4) / 127;   /* s / 127 in Q30: 8454660 = floor(2^30 / 127); 127 -> 2^30 */
}

void adsr_gate(adsr_t *e, int on)
{
    if (on) e->stage = ADSR_ATTACK;                    /* from the current level: a retrigger does not jump */
    else if (e->stage != ADSR_IDLE) e->stage = ADSR_RELEASE;
}

static inline __attribute__((always_inline)) int32_t adsr_mulh(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 32);
}

/* One sample: level += mulh(target - level, m), the exact RC step target + (level - target) c (to the floor of the
 * high multiply, which rounds towards the falling targets, so decay lands exactly on SUSTAIN and release always
 * crosses zero). Attack: the target is 4/3, the difference >= 1/3 so the step never stalls. */
int32_t adsr_tick(adsr_t *e, const adsr_rates_t *r)
{
    int32_t l = e->level;
    switch (e->stage) {
    case ADSR_ATTACK:
        l += adsr_mulh(ADSR_A_TGT - l, r->a_inc);
        if (l >= ADSR_ONE) l = ADSR_ONE, e->stage = ADSR_DECAY;
        break;
    case ADSR_DECAY:   /* and sustain: follows SUSTAIN changes at the decay rate */
        l += adsr_mulh(r->s_level - l, r->d_coef);
        break;
    case ADSR_RELEASE:
        l += adsr_mulh(-ADSR_TAIL - l, r->r_coef);
        if (l <= 0) l = 0, e->stage = ADSR_IDLE;
        break;
    default:
        l = 0;
        break;
    }
    e->level = l;
    return l;
}

/* amount (0..127 or its magnitude) x velocity (127 when the switch is off): 0..16129 */
static inline __attribute__((always_inline)) uint32_t adsr_amount(int amt, int vel_on, int vel)
{
    return (uint32_t)amt * (uint32_t)(vel_on ? adsr_clamp(vel, 0, 127) : 127);
}

static inline __attribute__((always_inline)) uint32_t adsr_level(int32_t env_q30)
{
    return env_q30 <= 0 ? 0u : env_q30 >= ADSR_ONE ? (uint32_t)ADSR_ONE : (uint32_t)env_q30;
}

int32_t vca_control(int32_t env_q30, int lenv_amt, int vel_on, int vel, int32_t lfo_q15, int32_t at_q15)
{
    /* env Q15 (0..32768) x amount (0..16129) < 2^29.1, x VCA_K >> 32 rounded: 0..32768 */
    uint32_t p = (adsr_level(env_q30) >> 15) * adsr_amount(adsr_clamp(lenv_amt, 0, 127), vel_on, vel);
    int32_t env = (int32_t)(((uint64_t)p * VCA_K + (1u << 31)) >> 32);
    /* the sum, each term clamped first so the addition cannot overflow */
    int32_t sum = env + adsr_clamp(lfo_q15, -2 * VCA_CEIL, 2 * VCA_CEIL) + adsr_clamp(at_q15, -2 * VCA_CEIL, 2 * VCA_CEIL);
    return adsr_clamp(sum, 0, VCA_CEIL);
}

int32_t fenv_mod(int32_t env_q30, int fenv_amt, int vel_on, int vel)
{
    int a = adsr_clamp(fenv_amt, -127, 127);
    /* magnitude first, sign after: exactly symmetric. env Q16 (0..65536) x amount (0..16129) < 2^30 */
    uint32_t p = (adsr_level(env_q30) >> 14) * adsr_amount(a < 0 ? -a : a, vel_on, vel);
    int32_t off = (int32_t)(((uint64_t)p * FENV_K + (1u << 31)) >> 32);
    return a < 0 ? -off : off;
}

int32_t vca_apply(int32_t in, int32_t ctl_q15)
{
    return (int32_t)(((int64_t)in * ctl_q15) >> 15);
}
