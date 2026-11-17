/* SPDX-License-Identifier: GPL-3.0-only */
/* Envelopes and VCA (firmware/src/synth/env.c): rule 2 (filter env amount bipolar, loudness env amount unipolar,
 * velocity scales amounts only) and rule 3 (VCA = env + LFO + aftertouch, clamped at a ceiling), the stage times
 * against the documented ranges, and the release ending at zero. Every test prints what it measured. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "synth/env.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define FS 44100
#define ONE ADSR_ONE

static void test_fenv_bipolar(void)
{
    int32_t full = fenv_mod(ONE, 127, 0, 0), neg = fenv_mod(ONE, -127, 0, 0);
    printf("fenv_bipolar: amt +127 -> %d (%.3f semitones), -127 -> %d, 0 -> %d\n", full, full / 65536.0, neg,
           fenv_mod(ONE, 0, 0, 0));
    CHECK(neg < 0 && neg == -full);
    CHECK(abs(full - FENV_RANGE_Q16) <= 1);           /* the whole cutoff range */
    CHECK(fenv_mod(ONE, 0, 0, 0) == 0 && fenv_mod(0, 127, 0, 0) == 0 && fenv_mod(0, -127, 0, 0) == 0);
    int sym = 1, mono = 1;
    int32_t prev = INT32_MIN;
    for (int a = -127; a <= 127; a++) {
        for (int32_t e = 0; e <= ONE; e += ONE / 64) {
            if (fenv_mod(e, a, 0, 0) != -fenv_mod(e, -a, 0, 0)) sym = 0;
            if (fenv_mod(e, a, 1, 77) != -fenv_mod(e, -a, 1, 77)) sym = 0;
        }
        int32_t v = fenv_mod(ONE / 2, a, 0, 0);
        if (v < prev) mono = 0;
        prev = v;
    }
    int32_t half = fenv_mod(ONE / 2, 127, 0, 0);
    printf("fenv_bipolar: env 0.5, amt 127 -> %.3f semitones; symmetric %d, monotone in amount %d\n", half / 65536.0,
           sym, mono);
    CHECK(sym && mono);
    CHECK(abs(half - FENV_RANGE_Q16 / 2) <= 1);
    CHECK(fenv_mod(-5, 127, 0, 0) == 0 && fenv_mod(ONE + 100, 127, 0, 0) == full);   /* env clamped to 0..1 */
}

static void test_lenv_unipolar(void)
{
    int zero = 1;
    for (int32_t e = 0; e <= ONE; e += ONE / 128)
        for (int a = -127; a <= 0; a++)
            if (vca_control(e, a, 0, 0, 0, 0) != 0 || vca_control(e, a, 1, 127, 0, 0) != 0) zero = 0;
    int32_t full = vca_control(ONE, 127, 0, 0, 0, 0), half = vca_control(ONE, 64, 0, 0, 0, 0);
    int32_t neg = vca_control(ONE, -127, 0, 0, 0, 0);
    printf("lenv_unipolar: amt 127 -> %d, 64 -> %d, -127 -> %d, amt <= 0 always 0: %d\n", full, half, neg, zero);
    CHECK(zero && neg == 0);
    CHECK(full == VCA_CEIL);
    CHECK(abs(half - 64 * 32768 / 127) <= 1);
    CHECK(vca_control(ONE / 4, 127, 0, 0, 0, 0) == VCA_CEIL / 4);
}

static void test_vca_sum_clamp(void)
{
    /* gated VCA: loudness amount 0, LFO square (positive only) to AMP at full depth */
    int32_t hi = vca_control(0, 0, 0, 0, VCA_CEIL, 0), lo = vca_control(0, 0, 0, 0, 0, 0);
    int32_t snd_hi = vca_apply(16384, hi), snd_lo = vca_apply(16384, lo);
    printf("vca_sum_clamp: amt 0 + LFO square high -> ctl %d (out %d), low -> ctl %d (out %d)\n", hi, snd_hi, lo,
           snd_lo);
    CHECK(hi > 0 && snd_hi > 0 && lo == 0 && snd_lo == 0);
    /* env idle, amt 0, but the LFO gates the sound: a square of 100 samples on / off through the VCA */
    adsr_t e = {0, ADSR_IDLE};
    adsr_rates_t r;
    adsr_rates(&r, 0, 64, 127, 20);
    long energy_on = 0, energy_off = 0;
    for (int n = 0; n < 1000; n++) {
        int32_t lfo = (n / 100) & 1 ? 0 : VCA_CEIL;
        int32_t y = vca_apply(16384, vca_control(adsr_tick(&e, &r), 0, 0, 0, lfo, 0));
        if (lfo) energy_on += labs(y); else energy_off += labs(y);
    }
    printf("vca_sum_clamp: gated square through the VCA: |out| sum on %ld, off %ld\n", energy_on, energy_off);
    CHECK(energy_on > 0 && energy_off == 0);
    /* aftertouch to AMP adds nothing over a full envelope at full amount */
    int32_t env_full = vca_control(ONE, 127, 0, 0, 0, 0);
    int32_t with_at = vca_control(ONE, 127, 0, 0, 0, VCA_CEIL), with_both = vca_control(ONE, 127, 1, 127, VCA_CEIL, VCA_CEIL);
    printf("vca_sum_clamp: env full amt 127 -> %d, + AT full -> %d, + LFO + AT -> %d (ceiling %d)\n", env_full,
           with_at, with_both, VCA_CEIL);
    CHECK(env_full == VCA_CEIL && with_at == VCA_CEIL && with_both == VCA_CEIL);
    /* below the ceiling it is a sum, not a product */
    int32_t e_half = vca_control(ONE / 2, 127, 0, 0, 0, 0), sum = vca_control(ONE / 2, 127, 0, 0, 4096, 2048);
    int32_t at_only = vca_control(0, 127, 0, 0, 0, 8192);
    printf("vca_sum_clamp: env 0.5 -> %d, + LFO 4096 + AT 2048 -> %d; env 0 + AT 8192 -> %d\n", e_half, sum, at_only);
    CHECK(sum == e_half + 4096 + 2048 && at_only == 8192);
    /* negative (bipolar LFO) contributions clamp at 0, never invert the signal */
    CHECK(vca_control(ONE / 4, 127, 0, 0, -VCA_CEIL, 0) == 0 && vca_control(0, 0, 0, 0, -100, -100) == 0);
    CHECK(vca_control(ONE / 2, 127, 0, 0, -4096, 0) == e_half - 4096);
    /* vca_apply keeps the filter's headroom: +-65536 at the ceiling, no wrap */
    printf("vca_sum_clamp: vca_apply(+-65536, ceiling) = %d, %d\n", vca_apply(65536, VCA_CEIL), vca_apply(-65536, VCA_CEIL));
    CHECK(vca_apply(65536, VCA_CEIL) == 65536 && vca_apply(-65536, VCA_CEIL) == -65536);
    CHECK(vca_apply(65536, VCA_CEIL / 2) == 32768 && vca_apply(1000, 0) == 0);
}

static void test_velocity_scales_amounts_only(void)
{
    int indep = 1;
    for (int v = 0; v <= 127; v++)
        for (int32_t e = 0; e <= ONE; e += ONE / 32) {
            if (vca_control(e, 100, 0, v, 0, 0) != vca_control(e, 100, 0, 127, 0, 0)) indep = 0;
            if (fenv_mod(e, -90, 0, v) != fenv_mod(e, -90, 0, 127)) indep = 0;
        }
    printf("velocity_scales_amounts_only: switch off -> independent of velocity: %d\n", indep);
    CHECK(indep);
    /* switch on: velocity 127 = switch off; velocity v scales the amount by v / 127, sign kept */
    CHECK(vca_control(ONE, 127, 1, 127, 0, 0) == vca_control(ONE, 127, 0, 0, 0, 0));
    CHECK(fenv_mod(ONE, -127, 1, 127) == fenv_mod(ONE, -127, 0, 0));
    int32_t v64 = vca_control(ONE, 127, 1, 64, 0, 0), f64 = fenv_mod(ONE, 127, 1, 64), fn64 = fenv_mod(ONE, -127, 1, 64);
    printf("velocity_scales_amounts_only: vel 64 -> VCA %d (want %d), fenv %+.3f / %+.3f semitones (want %+.3f)\n", v64,
           64 * 32768 / 127, f64 / 65536.0, fn64 / 65536.0, FENV_RANGE_Q16 / 65536.0 * 64 / 127.0);
    CHECK(abs(v64 - 64 * 32768 / 127) <= 1);
    CHECK(fabs(f64 / 65536.0 - FENV_RANGE_Q16 / 65536.0 * 64 / 127.0) < 0.001 && fn64 == -f64);
    CHECK(vca_control(ONE, 127, 1, 0, 0, 0) == 0 && fenv_mod(ONE, 127, 1, 0) == 0);
    /* harder = more: monotone in velocity, for both polarities of the filter amount (positive brighter, negative darker) */
    int mono = 1;
    for (int v = 1; v <= 127; v++) {
        if (vca_control(ONE, 127, 1, v, 0, 0) < vca_control(ONE, 127, 1, v - 1, 0, 0)) mono = 0;
        if (fenv_mod(ONE, 127, 1, v) < fenv_mod(ONE, 127, 1, v - 1)) mono = 0;
        if (fenv_mod(ONE, -127, 1, v) > fenv_mod(ONE, -127, 1, v - 1)) mono = 0;
    }
    CHECK(mono);
    /* timing: two notes, velocity 20 and 127, same gate times: the envelopes are identical sample for sample (the
     * ADSR has no velocity input), the outputs are that one curve scaled, and the soft note does not ring longer */
    enum { N = 30000 };
    adsr_rates_t r;
    adsr_rates(&r, 70, 60, 40, 50);
    adsr_t es = {0, ADSR_IDLE}, eh = {0, ADSR_IDLE};
    adsr_gate(&es, 1);
    adsr_gate(&eh, 1);
    int same = 1, scaled = 1, peak_s = 0, peak_h = 0, end_lo = -1, end_hi = -1;
    int32_t top_s = -1, top_h = -1;
    for (int n = 0; n < N; n++) {
        if (n == 15000) { adsr_gate(&es, 0); adsr_gate(&eh, 0); }
        int32_t ls = adsr_tick(&es, &r), lh = adsr_tick(&eh, &r);
        if (ls != lh || es.stage != eh.stage) same = 0;
        int32_t lo = vca_control(ls, 127, 1, 20, 0, 0), hi = vca_control(lh, 127, 1, 127, 0, 0);
        int32_t flo = fenv_mod(ls, 127, 1, 20), fhi = fenv_mod(lh, 127, 1, 127);
        if (fabs(lo - (ls / 32768) * 20 / 127.0) > 1 || fabs(flo - fhi * 20 / 127.0) > 1) scaled = 0;
        if (ls > top_s) top_s = ls, peak_s = n;
        if (lh > top_h) top_h = lh, peak_h = n;
        if (n > 15000 && lo == 0 && end_lo < 0) end_lo = n;
        if (n > 15000 && hi == 0 && end_hi < 0) end_hi = n;
    }
    printf("velocity_scales_amounts_only: vel 20 / 127: envelopes identical %d, peak at sample %d / %d, outputs the "
           "same curve scaled by 20/127 (+-1) %d, silent at %d / %d\n", same, peak_s, peak_h, scaled, end_lo, end_hi);
    CHECK(same && peak_s == peak_h && scaled);
    CHECK(end_hi > 0 && end_lo > 0 && end_lo <= end_hi);
}

/* samples an ADSR stage takes, as documented in env.h */
static long attack_samples(int a)
{
    adsr_rates_t r;
    adsr_rates(&r, a, 0, 127, 0);
    adsr_t e = {0, ADSR_IDLE};
    adsr_gate(&e, 1);
    long n = 0;
    while (e.stage == ADSR_ATTACK && n < 20 * FS) { adsr_tick(&e, &r); n++; }
    return n;
}
static long decay_samples(int d)   /* full -> within 1/1024 of SUSTAIN 0 */
{
    adsr_rates_t r;
    adsr_rates(&r, 0, d, 0, 0);
    adsr_t e = {ONE, ADSR_DECAY};
    long n = 0;
    while (e.level > (ONE >> 10) && n < 20 * FS) { adsr_tick(&e, &r); n++; }
    return n;
}
static long release_samples(int rel, int32_t from)   /* from -> zero */
{
    adsr_rates_t r;
    adsr_rates(&r, 0, 0, 127, rel);
    adsr_t e = {from, ADSR_DECAY};
    adsr_gate(&e, 0);
    long n = 0;
    while (e.level > 0 && n < 20 * FS) { adsr_tick(&e, &r); n++; }
    return n;
}

/* The voice runs the envelopes at control rate (adsr_tick16 every 16 samples, a linear ramp between, clamped to
 * 0..ONE: voice.c voice_env / voice_render). The ramp's crossing of the same thresholds as the per-sample model
 * (attack: full; decay: 1/1024 of full; release: zero), in samples, for each stage. */
static long ramp_samples(int stage, int k)
{
    adsr_rates_t r;
    adsr_rates(&r, stage == 0 ? k : 0, stage == 1 ? k : 0, stage == 2 ? 127 : 0, stage == 2 ? k : 0);
    adsr_t e = {stage == 0 ? 0 : ONE, stage == 0 ? ADSR_ATTACK : ADSR_DECAY};
    if (stage == 2) adsr_gate(&e, 0);
    for (long t = 0; t < 20 * FS; t += 16) {
        int32_t l0 = e.level, d = (int32_t)(((int64_t)adsr_tick16(&e, &r) - l0) >> 4), l = l0;
        for (int i = 1; i <= 16; i++) {
            l += d;
            int32_t v = l < 0 ? 0 : l > ONE ? ONE : l;
            if (stage == 0 ? v >= ONE : stage == 1 ? v <= (ONE >> 10) : v <= 0) return t + i;
        }
    }
    return 20 * FS;
}
static void test_adsr_times16(void)
{
    double worst = 0;
    int in5 = 1;
    long shortest = 1 << 30;
    for (int k = 0; k <= 127; k++) {
        double ta = 0.001 * pow(5000, k / 127.0) * FS, td = 0.005 * pow(2000, k / 127.0) * FS;
        double e[3] = { ramp_samples(0, k) / ta - 1, ramp_samples(1, k) / td - 1, ramp_samples(2, k) / td - 1 };
        for (int i = 0; i < 3; i++) {
            if (fabs(e[i]) > worst) worst = fabs(e[i]);
            if (fabs(e[i]) > 0.05) in5 = 0;
        }
        if (ramp_samples(0, k) < shortest) shortest = ramp_samples(0, k);
    }
    printf("adsr_times16: control-rate envelopes (16-sample steps, linear ramps): attack / decay / release of all 128 "
           "positions within 5 %% %d (worst %.2f %%); fastest attack %ld samples\n", in5, 100 * worst, shortest);
    CHECK(in5);
}

static void test_adsr_times(void)
{
    static const int knob[] = {0, 32, 64, 96, 127};
    double worst = 0;
    for (unsigned i = 0; i < sizeof knob / sizeof *knob; i++) {
        int k = knob[i];
        double ta = 0.001 * pow(5000, k / 127.0), td = 0.005 * pow(2000, k / 127.0);
        long a = attack_samples(k), d = decay_samples(k), rl = release_samples(k, ONE);
        double ea = a / (ta * FS) - 1, ed = d / (td * FS) - 1, er = rl / (td * FS) - 1;
        printf("adsr_times: knob %3d  attack %9.2f ms (want %9.2f, %+.2f %%)  decay %9.2f ms  release %9.2f ms "
               "(want %9.2f, %+.2f / %+.2f %%)\n", k, 1000.0 * a / FS, 1000 * ta, 100 * ea, 1000.0 * d / FS,
               1000.0 * rl / FS, 1000 * td, 100 * ed, 100 * er);
        CHECK(fabs(ea) <= 0.05 && fabs(ed) <= 0.05 && fabs(er) <= 0.05);
        if (fabs(ea) > worst) worst = fabs(ea);
        if (fabs(ed) > worst) worst = fabs(ed);
        if (fabs(er) > worst) worst = fabs(er);
    }
    /* every knob position, monotone and within 5 % */
    int mono = 1, in5 = 1;
    long pa = 0, pd = 0;
    for (int k = 0; k <= 127; k++) {
        long a = attack_samples(k), d = release_samples(k, ONE);
        double ta = 0.001 * pow(5000, k / 127.0) * FS, td = 0.005 * pow(2000, k / 127.0) * FS;
        if (a < pa || d < pd) mono = 0;
        if (fabs(a / ta - 1) > 0.05 || fabs(d / td - 1) > 0.05) in5 = 0;
        if (fabs(a / ta - 1) > worst) worst = fabs(a / ta - 1);
        if (fabs(d / td - 1) > worst) worst = fabs(d / td - 1);
        pa = a, pd = d;
    }
    printf("adsr_times: all 128 positions monotone %d, within 5 %% %d (worst %.2f %%)\n", mono, in5, 100 * worst);
    CHECK(mono && in5);
    /* the curves are exponential (RC): at 1/3 of its time the attack is at 4/3 (1 - 4^(-1/3)) = 0.493 (linear:
     * 0.333); at 1/5 of its time the decay is at 1025^(-1/5) = 0.25 (linear: 0.8) */
    adsr_rates_t r;
    adsr_rates(&r, 64, 64, 0, 64);
    long ta = attack_samples(64), td = decay_samples(64);
    adsr_t e = {0, ADSR_IDLE};
    adsr_gate(&e, 1);
    for (long n = 0; n < ta / 3; n++) adsr_tick(&e, &r);
    int32_t a_third = e.level;
    e = (adsr_t){ONE, ADSR_DECAY};
    for (long n = 0; n < td / 5; n++) adsr_tick(&e, &r);
    printf("adsr_times: attack at 1/3 time %.3f (RC 0.493, linear 0.333), decay at 1/5 time %.3f (RC 0.250, linear 0.800)\n",
           a_third / (double)ONE, e.level / (double)ONE);
    CHECK(fabs(a_third / (double)ONE - 0.493) < 0.01 && fabs(e.level / (double)ONE - 0.250) < 0.01);
    /* decay settles exactly on SUSTAIN and stays there */
    adsr_rates(&r, 0, 10, 77, 10);
    e = (adsr_t){0, ADSR_IDLE};
    adsr_gate(&e, 1);
    for (long n = 0; n < FS; n++) adsr_tick(&e, &r);
    printf("adsr_times: sustain 77 settles at %d (s_level %d), stage %u\n", e.level, r.s_level, e.stage);
    CHECK(e.level == r.s_level && e.stage == ADSR_DECAY && abs(r.s_level - (int32_t)(77.0 / 127 * ONE)) <= 2);
    adsr_rates(&r, 0, 0, 127, 0);
    CHECK(r.s_level == ONE);
    adsr_rates(&r, 0, 0, 0, 0);
    CHECK(r.s_level == 0);
    /* a retrigger restarts the attack from the current level: no jump */
    adsr_rates(&r, 80, 20, 60, 90);
    e = (adsr_t){0, ADSR_IDLE};
    adsr_gate(&e, 1);
    for (long n = 0; n < FS / 2; n++) adsr_tick(&e, &r);
    adsr_gate(&e, 0);
    for (long n = 0; n < FS / 10; n++) adsr_tick(&e, &r);
    int32_t before = e.level;
    adsr_gate(&e, 1);
    int32_t after = adsr_tick(&e, &r);
    printf("adsr_times: retrigger at %.4f -> %.4f next sample (stage %u)\n", before / (double)ONE, after / (double)ONE,
           e.stage);
    CHECK(e.stage == ADSR_ATTACK && after >= before && after - before < ONE / 100);
    /* out-of-range params clamp */
    adsr_rates_t r1, r2;
    adsr_rates(&r1, -5, 200, 300, -1);
    adsr_rates(&r2, 0, 127, 127, 0);
    CHECK(r1.a_inc == r2.a_inc && r1.d_coef == r2.d_coef && r1.s_level == r2.s_level && r1.r_coef == r2.r_coef);
}

static void test_release_reaches_zero(void)
{
    int all = 1, mono = 1, nonneg = 1, stays = 1;
    long worst_tail = 0;
    for (int k = 0; k <= 127; k++) {
        static const int32_t from[] = {ONE, ONE / 2, ONE / 1000, 1};
        for (unsigned j = 0; j < 4; j++) {
            adsr_rates_t r;
            adsr_rates(&r, 0, 0, 127, k);
            adsr_t e = {from[j], ADSR_DECAY};
            adsr_gate(&e, 0);
            double limit = 0.005 * pow(2000, k / 127.0) * FS * 1.05 + 1;
            int32_t prev = e.level;
            long n = 0;
            while (e.stage != ADSR_IDLE && n < (long)limit) {
                int32_t l = adsr_tick(&e, &r);
                if (l > prev) mono = 0;
                if (l < 0 || e.level < 0) nonneg = 0;
                prev = l, n++;
            }
            if (e.stage != ADSR_IDLE || e.level != 0) all = 0;
            for (int m = 0; m < 1000; m++) if (adsr_tick(&e, &r) != 0) stays = 0;
            if (j == 3 && n > worst_tail) worst_tail = n;
        }
    }
    printf("release_reaches_zero: every RELEASE from 1, 0.5, 0.001, 1 LSB ends at exactly 0 within its time: %d; "
           "monotone %d, never negative %d, stays 0 %d; from 1 LSB takes <= %ld samples\n", all, mono, nonneg, stays,
           worst_tail);
    CHECK(all && mono && nonneg && stays);
    CHECK(worst_tail <= 2);
    /* the full note: gate, sustain, release, idle */
    adsr_rates_t r;
    adsr_rates(&r, 30, 40, 90, 60);
    adsr_t e = {0, ADSR_IDLE};
    CHECK(adsr_tick(&e, &r) == 0);
    adsr_gate(&e, 1);
    for (long n = 0; n < FS; n++) adsr_tick(&e, &r);
    adsr_gate(&e, 0);
    long n = 0;
    while (e.stage != ADSR_IDLE && n < 20 * FS) { adsr_tick(&e, &r); n++; }
    printf("release_reaches_zero: note released from sustain 90 idle after %.1f ms (release 60: %.1f ms from full)\n",
           1000.0 * n / FS, 5 * pow(2000, 60 / 127.0));
    CHECK(e.stage == ADSR_IDLE && e.level == 0 && n <= 0.005 * pow(2000, 60 / 127.0) * FS);
    /* a gate off on an idle envelope stays idle; release during the attack releases from there */
    e = (adsr_t){0, ADSR_IDLE};
    adsr_gate(&e, 0);
    CHECK(e.stage == ADSR_IDLE && adsr_tick(&e, &r) == 0);
    adsr_gate(&e, 1);
    for (int m = 0; m < 50; m++) adsr_tick(&e, &r);
    int32_t mid = e.level;
    adsr_gate(&e, 0);
    CHECK(e.stage == ADSR_RELEASE && adsr_tick(&e, &r) <= mid);
}

/* host cost of the per-sample work of two envelopes + VCA + filter env offset (one voice) */
static void test_cost(void)
{
    adsr_rates_t ra, rb;
    adsr_rates(&ra, 20, 70, 80, 60);
    adsr_rates(&rb, 40, 50, 30, 70);
    adsr_t fa = {0, ADSR_IDLE}, la = {0, ADSR_IDLE};
    volatile int32_t sink = 0;
    long total = 20L * FS;
    clock_t t0 = clock();
    int64_t acc = 0;
    for (long n = 0; n < total; n++) {
        if ((n & 32767) == 0) { adsr_gate(&fa, (n >> 15) & 1 ? 0 : 1); adsr_gate(&la, (n >> 15) & 1 ? 0 : 1); }
        int32_t f = fenv_mod(adsr_tick(&fa, &ra), 90, 1, 100);
        int32_t c = vca_control(adsr_tick(&la, &rb), 127, 1, 100, (int32_t)(n & 4095), 0);
        acc += vca_apply(f >> 8, c);
    }
    sink = (int32_t)acc;
    double ns = 1e9 * (double)(clock() - t0) / CLOCKS_PER_SEC / total;
    printf("cost: 2 x adsr_tick + fenv_mod + vca_control + vca_apply = %.2f ns per voice-sample on the host (%d)\n",
           ns, sink != 0);
}

int main(void)
{
    test_fenv_bipolar();
    test_lenv_unipolar();
    test_vca_sum_clamp();
    test_velocity_scales_amounts_only();
    test_adsr_times();
    test_adsr_times16();
    test_release_reaches_zero();
    test_cost();
    if (fails) { printf("test_env: %d FAILED\n", fails); return 1; }
    printf("test_env: all passed\n");
    return 0;
}
