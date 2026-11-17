/* SPDX-License-Identifier: GPL-3.0-only */
/* Modulation (firmware/src/synth/mod.c): rule 4 (one shared LFO, 0.022-500 Hz, polarity per shape, noise at max
 * random rate, key reset on the first key only, depth = INITIAL + wheel + aftertouch, the wheel has no other route,
 * a negative aftertouch inverts the LFO), rule 6 (X-Mod: filter env and VCO 2 into one set of destinations) and
 * rule 7 (aftertouch destinations). All the synth modules are included in one translation unit, as app.c's unity
 * build will: a name clash fails here. Measurements use doubles (host only). Every test prints what it measured. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "params/params.c"
#include "synth/osc.c"
#include "synth/filter.c"
#include "synth/env.c"
#include "synth/mod.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define FS 44100.0
#define MPI 3.14159265358979323846

static int16_t V[PROGRAM_VALUES];

static void v_reset(void)
{
    program_t p;
    program_init(&p);
    memcpy(V, p.v, sizeof V);
}

static int mods_zero(const mods_t *m)
{
    return !m->vco1_pitch && !m->vco1_shape && !m->vco1_pw && !m->flt_freq && !m->flt_mode && !m->bp_mix &&
           !m->vco2_pitch && !m->vco2_pw && !m->amp;
}

/* frequency from the saw: the time between the first and the last wrap, over `secs` seconds ticked `step` frames */
static double lfo_measure(int p, double secs, uint32_t step)
{
    lfo_t l;
    lfo_init(&l, 1);
    lfo_set(&l, p, 0, 0);
    int32_t prev = lfo_tick(&l, LFO_SAW, 0);
    long n = (long)(secs * FS / step), first = -1, last = -1, wraps = 0;
    for (long i = 1; i <= n; i++) {
        int32_t y = lfo_tick(&l, LFO_SAW, step);
        if (y < prev) {
            if (first < 0) first = i; else wraps++;
            last = i;
        }
        prev = y;
    }
    return wraps > 0 ? wraps * FS / ((double)(last - first) * step) : 0;
}

static void lfo_range(void)
{
    double lo = lfo_measure(0, 400, 441), hi = lfo_measure(254, 2, 1), mid = lfo_measure(127, 20, 4);
    lfo_t l;
    lfo_init(&l, 1);
    int mono = 1;
    uint32_t prev = 0;
    for (int p = 0; p <= 254; p++) {
        lfo_set(&l, p, 0, 0);
        if (l.inc <= prev) mono = 0;
        prev = l.inc;
    }
    lfo_set(&l, 0, 0, 0);
    double inc_lo = l.inc * FS / 4294967296.0;
    lfo_set(&l, 254, 0, 0);
    double inc_hi = l.inc * FS / 4294967296.0;
    printf("lfo_range: p 0 -> %.5f Hz (inc %.5f), p 127 -> %.3f Hz (expect %.3f), p 254 -> %.2f Hz (inc %.2f); "
           "increment strictly rising %d\n", lo, inc_lo, mid, 0.022 * pow(500 / 0.022, 0.5), hi, inc_hi, mono);
    CHECK(fabs(lo / 0.022 - 1) < 0.02);
    CHECK(fabs(hi / 500 - 1) < 0.02);
    CHECK(fabs(mid / (0.022 * pow(500 / 0.022, 0.5)) - 1) < 0.02);
    CHECK(l.inc == LFO_INC_TOP && mono);
    /* sync: the clock's increment is used, capped below noise; SYNC off or clock 0 = free */
    lfo_set(&l, 100, 1, 1000000u);
    CHECK(l.inc == 1000000u);
    lfo_set(&l, 100, 1, 0xF0000000u);
    CHECK(l.inc < LFO_INC_TOP);
    uint32_t free100;
    lfo_set(&l, 100, 0, 0), free100 = l.inc;
    lfo_set(&l, 100, 0, 1000000u);
    CHECK(l.inc == free100);
    lfo_set(&l, 100, 1, 0);
    CHECK(l.inc == free100);
    printf("lfo_range: sync clock 1000000 -> inc %u, huge clock capped below top, free p 100 = %.4f Hz\n", 1000000u,
           free100 * FS / 4294967296.0);
}

static void lfo_polarity(void)
{
    static const char *nm[] = { "sine", "saw", "rsaw", "square", "random" };
    for (int s = 0; s <= 4; s++) {
        lfo_t l;
        lfo_init(&l, 12345);
        lfo_set(&l, 160, 0, 0);   /* ~3 Hz */
        int32_t mn = INT32_MAX, mx = INT32_MIN;
        for (long i = 0; i < 20 * 44100L; i++) {
            int32_t y = lfo_tick(&l, s, 1);
            if (y < mn) mn = y;
            if (y > mx) mx = y;
        }
        printf("lfo_polarity: %-6s min %6d max %6d\n", nm[s], mn, mx);
        CHECK(mx <= LFO_ONE && mn >= -LFO_ONE);
        if (s == LFO_SINE || s == LFO_RANDOM) CHECK(mn < -LFO_ONE / 2 && mx > LFO_ONE / 2);
        else CHECK(mn >= 0 && mx > LFO_ONE - 64);
        if (s == LFO_SINE) CHECK(mn <= -LFO_ONE + 8 && mx >= LFO_ONE - 8);
        if (s == LFO_SQUARE) CHECK(mn == 0 && mx == LFO_ONE);
    }
    /* sine shape: against sin(2 pi phase) */
    lfo_t l;
    lfo_init(&l, 1);
    double err = 0;
    for (uint32_t k = 0; k < 4096; k++) {
        l.ph = k << 20;
        double e = fabs(lfo_tick(&l, LFO_SINE, 0) - LFO_ONE * sin(2 * MPI * k / 4096.0));
        if (e > err) err = e;
    }
    printf("lfo_polarity: sine max error %.1f LSB (%.2e of full scale)\n", err, err / LFO_ONE);
    CHECK(err < 16);
}

/* Welch: 64 segments of 1024, rectangular window; power in 16 bands of 32 bins (DC bin skipped) */
static void band_powers(const double *x, int n, double *band)
{
    for (int b = 0; b < 16; b++) band[b] = 0;
    for (int seg = 0; seg + 1024 <= n; seg += 1024) {
        for (int k = 1; k < 512; k++) {
            double sr = 0, si = 0;
            for (int t = 0; t < 1024; t++) {
                double a = 2 * MPI * k * t / 1024.0;
                sr += x[seg + t] * cos(a), si -= x[seg + t] * sin(a);
            }
            band[k / 32] += sr * sr + si * si;
        }
    }
    band[0] = band[0] * 32 / 31;   /* band 0 has 31 bins */
}

static void lfo_random_max_is_noise(void)
{
    enum { NS = 16384 };
    static double x[NS];
    double band[16], lo = 1e300, hi = 0, mean = 0;
    lfo_t l;
    lfo_init(&l, 777);
    lfo_set(&l, 254, 0, 0);
    for (int i = 0; i < NS; i++) x[i] = lfo_tick(&l, LFO_RANDOM, 1);
    band_powers(x, NS, band);
    for (int b = 0; b < 16; b++) { mean += band[b] / 16; if (band[b] < lo) lo = band[b]; if (band[b] > hi) hi = band[b]; }
    printf("lfo_random_max_is_noise: p 254 band power %.2f .. %.2f dB around the mean\n", 10 * log10(lo / mean),
           10 * log10(hi / mean));
    CHECK(10 * log10(hi / mean) < 3 && 10 * log10(lo / mean) > -3);
    /* one step below: a 480 Hz sample and hold, not noise (the top band far below the bottom one) */
    lfo_set(&l, 253, 0, 0);
    for (int i = 0; i < NS; i++) x[i] = lfo_tick(&l, LFO_RANDOM, 1);
    band_powers(x, NS, band);
    printf("lfo_random_max_is_noise: p 253 top band %.1f dB below the bottom band\n", 10 * log10(band[0] / band[15]));
    CHECK(10 * log10(band[0] / band[15]) > 20);
    /* at the noise rate every tick is a new value, also when ticked per control block */
    int changed = 0;
    lfo_set(&l, 254, 0, 0);
    int32_t prev = lfo_tick(&l, LFO_RANDOM, 16);
    for (int i = 0; i < 1000; i++) { int32_t y = lfo_tick(&l, LFO_RANDOM, 16); changed += y != prev; prev = y; }
    printf("lfo_random_max_is_noise: per 16-frame tick, %d / 1000 new values\n", changed);
    CHECK(changed >= 995);
}

static void lfo_key_reset_first_only(void)
{
    lfo_t l;
    lfo_init(&l, 99);
    lfo_set(&l, 120, 0, 0);
    for (int i = 0; i < 12345; i++) lfo_tick(&l, LFO_SAW, 1);
    uint32_t ph = l.ph;
    lfo_key(&l, 0);   /* other keys held */
    int32_t held = lfo_tick(&l, LFO_SAW, 0);
    CHECK(l.ph == ph);
    lfo_key(&l, 1);   /* first key down */
    int32_t saw0 = lfo_tick(&l, LFO_SAW, 0), sine0 = lfo_tick(&l, LFO_SINE, 0), sq0 = lfo_tick(&l, LFO_SQUARE, 0);
    printf("lfo_key_reset_first_only: before %d (ph %u), key(0) keeps ph %u, key(1) -> saw %d sine %d square %d\n",
           held, ph, l.ph == 0 ? ph : l.ph, saw0, sine0, sq0);
    CHECK(l.ph == 0 && saw0 == 0 && sine0 == 0 && sq0 == LFO_ONE);
    /* random: a key reset draws a new value; key(0) does not */
    lfo_set(&l, 30, 0, 0);
    int32_t r1 = lfo_tick(&l, LFO_RANDOM, 1);
    lfo_key(&l, 0);
    int32_t r2 = lfo_tick(&l, LFO_RANDOM, 1);
    lfo_key(&l, 1);
    int32_t r3 = lfo_tick(&l, LFO_RANDOM, 1);
    printf("lfo_key_reset_first_only: random %d, after key(0) %d, after key(1) %d\n", r1, r2, r3);
    CHECK(r1 == r2 && r3 != r2);
    /* also with SYNC on */
    lfo_set(&l, 0, 1, 5000000u);
    for (int i = 0; i < 1000; i++) lfo_tick(&l, LFO_SAW, 1);
    lfo_key(&l, 1);
    CHECK(lfo_tick(&l, LFO_SAW, 0) == 0);
}

static void wheel_only_adds_depth(void)
{
    mods_t m, m0;
    v_reset();
    CHECK(lfo_depth(0, 0, 0, 0) == 0 && lfo_depth(0, 127, 0, 0) == MODS_DEPTH_ONE);
    CHECK(lfo_depth(255, 0, 0, 0) == MODS_DEPTH_ONE && lfo_depth(255, 127, 32768, 1) == MODS_DEPTH_ONE);
    /* every LFO destination off, everything else on: the wheel changes nothing */
    V[P_XM_FENV] = 90, V[P_XM_VCO2] = -60, V[P_AT_AMT] = 100;
    for (int id = P_XM_D_VCO1; id <= P_XM_D_BP; id++) V[id] = 1;
    for (int id = P_AT_D_VCO1; id <= P_AT_D_FMODE; id++) V[id] = 1;
    V[P_AT_D_LFOAMT] = 0;
    int same = 1;
    mods_sum(&m0, V, LFO_ONE, lfo_depth(40, 0, 0, 0), ADSR_ONE / 3, 9000, mods_at(64, 100));
    for (int w = 0; w <= 127; w++) {
        mods_sum(&m, V, LFO_ONE, lfo_depth(40, w, 0, 0), ADSR_ONE / 3, 9000, mods_at(64, 100));
        if (memcmp(&m, &m0, sizeof m)) same = 0;
    }
    v_reset();
    int zero = 1;
    for (int w = 0; w <= 127; w++) {
        mods_sum(&m, V, LFO_ONE, lfo_depth(0, w, 0, 0), 0, 0, 0);
        if (!mods_zero(&m)) zero = 0;
    }
    /* one destination on: the wheel adds depth, monotonically, full wheel = full depth */
    V[P_LFO_D_FFREQ] = 1;
    int mono = 1;
    int32_t prev = -1, at64 = 0;
    for (int w = 0; w <= 127; w++) {
        mods_sum(&m, V, LFO_ONE, lfo_depth(0, w, 0, 0), 0, 0, 0);
        if (m.flt_freq <= prev) mono = 0;
        prev = m.flt_freq;
        if (w == 64) at64 = m.flt_freq;
        m.flt_freq = 0;
        if (!mods_zero(&m)) mono = 0;
    }
    mods_sum(&m, V, LFO_ONE, lfo_depth(100, 27, 0, 0), 0, 0, 0);
    int32_t sum = m.flt_freq;
    mods_sum(&m, V, LFO_ONE, lfo_depth(100, 0, 0, 0), 0, 0, 0);
    printf("wheel_only_adds_depth: dests off -> unchanged %d, all zero %d; LFO>FFREQ: wheel 64 -> %.2f steps, "
           "127 -> %.2f steps, rising %d; initial 100 + wheel 27 -> %.2f (initial alone %.2f)\n", same, zero,
           at64 / 65536.0, prev / 65536.0, mono, sum / 65536.0, m.flt_freq / 65536.0);
    CHECK(same && zero && mono);
    CHECK(abs(prev - (int32_t)((int64_t)MODS_LFO_FREQ * LFO_ONE / 32768)) <= 2);
    CHECK(sum > m.flt_freq);
}

static void at_negative_inverts_lfo(void)
{
    mods_t mp, mn;
    v_reset();
    V[P_LFO_D_VCO1] = 1, V[P_LFO_D_VCO2] = 1, V[P_LFO_D_AMP] = 1;
    int32_t atp = mods_at(127, 127), atn = mods_at(127, -127), ath = mods_at(64, -100);
    int32_t dp = lfo_depth(0, 0, atp, 1), dn = lfo_depth(0, 0, atn, 1), doff = lfo_depth(0, 0, atn, 0);
    int32_t lfo = 20000;
    mods_sum(&mp, V, lfo, dp, 0, 0, atp);
    mods_sum(&mn, V, lfo, dn, 0, 0, atn);
    printf("at_negative_inverts_lfo: mods_at(127, +-127) = %d / %d, (64, -100) = %d; depth +%d / %d (dest off %d); "
           "VCO1 %+.3f / %+.3f st, AMP %d / %d\n", atp, atn, ath, dp, dn, doff, mp.vco1_pitch / 65536.0,
           mn.vco1_pitch / 65536.0, mp.amp, mn.amp);
    CHECK(atp == 32768 && atn == -32768 && abs(ath - (int32_t)lrint(64 * -100 * 32768.0 / 16129)) <= 1);
    CHECK(dp == MODS_DEPTH_ONE && dn == -MODS_DEPTH_ONE && doff == 0);
    CHECK(mn.vco1_pitch < 0 && abs(mn.vco1_pitch + mp.vco1_pitch) <= 1);
    CHECK(mn.vco2_pitch == mn.vco1_pitch && mn.amp < 0 && abs(mn.amp + mp.amp) <= 1);
    int sym = 1;
    for (int p = 0; p <= 127; p++)
        for (int a = -127; a <= 127; a++)
            if (mods_at(p, a) != -mods_at(p, -a)) sym = 0;
    CHECK(sym);
    /* a negative aftertouch against a positive INITIAL: linear, it cancels then inverts */
    int32_t d1 = lfo_depth(255, 0, mods_at(64, -127), 1), d2 = lfo_depth(0, 0, mods_at(127, -127), 1);
    printf("at_negative_inverts_lfo: initial 255 + AT(64, -127) -> depth %d; initial 0 + AT(127, -127) -> %d\n", d1, d2);
    CHECK(d1 > 0 && d1 < MODS_DEPTH_ONE && d2 == -MODS_DEPTH_ONE);
}

static void xmod_sources_share_dests(void)
{
    mods_t m, ma, mb, mc;
    v_reset();
    V[P_XM_FENV] = 64, V[P_XM_VCO2] = -100, V[P_XM_D_VCO1] = 1;
    mods_sum(&ma, V, 0, 0, ADSR_ONE, 0, 0);
    mods_sum(&mb, V, 0, 0, 0, OSC_ONE, 0);
    mods_sum(&mc, V, 0, 0, ADSR_ONE, OSC_ONE, 0);
    double ea = 64 / 127.0 * 48, eb = -100 / 127.0 * 48;
    printf("xmod_sources_share_dests: VCO1 dest, fenv 1 x amt 64 -> %+.4f st (expect %+.4f), VCO2 +1 x amt -100 -> "
           "%+.4f (expect %+.4f), both %+.4f\n", ma.vco1_pitch / 65536.0, ea, mb.vco1_pitch / 65536.0, eb,
           mc.vco1_pitch / 65536.0);
    CHECK(fabs(ma.vco1_pitch / 65536.0 - ea) < 1e-3 && fabs(mb.vco1_pitch / 65536.0 - eb) < 1e-3);
    CHECK(abs(mc.vco1_pitch - ma.vco1_pitch - mb.vco1_pitch) <= 1);
    m = mc, m.vco1_pitch = 0;
    CHECK(mods_zero(&m));
    /* every destination: both sources, each with its own signed amount; off = nothing */
    static const int dst[6] = { P_XM_D_VCO1, P_XM_D_SHAPE1, P_XM_D_PW1, P_XM_D_FFREQ, P_XM_D_FMODE, P_XM_D_BP };
    static const int32_t full[6] = { MODS_XM_PITCH, MODS_XM_SHAPE, MODS_XM_PW, MODS_XM_FREQ, MODS_XM_MODE, MODS_XM_BP };
    for (int d = 0; d < 6; d++) {
        v_reset();
        V[P_XM_FENV] = -37, V[P_XM_VCO2] = 113, V[dst[d]] = 1;
        mods_sum(&m, V, LFO_ONE, MODS_DEPTH_ONE, ADSR_ONE / 2, -OSC_ONE / 4, 32768);
        int32_t got[6] = { m.vco1_pitch, m.vco1_shape, m.vco1_pw, m.flt_freq, m.flt_mode, m.bp_mix };
        double want = full[d] * (-37 / 127.0 * 0.5 + 113 / 127.0 * -0.25);
        printf("xmod_sources_share_dests: dest %d: %.1f (expect %.1f)\n", d, (double)got[d], want);
        CHECK(fabs(got[d] - want) <= 2);
        for (int e = 0; e < 6; e++) if (e != d) CHECK(got[e] == 0);
        CHECK(m.vco2_pitch == 0 && m.vco2_pw == 0 && m.amp == 0);
    }
    /* the per-sample split: mods_sum(vco2) == mods_sum(0) + mods_xm_tick, exactly, for any inputs */
    srand(5);
    int exact = 1;
    mods_xm_t g;
    for (int it = 0; it < 20000; it++) {
        for (int id = P_LFO_D_VCO1; id <= P_AT_D_FMODE; id++) V[id] = (int16_t)(rand() % 2);
        V[P_LFO_D_PW] = (int16_t)(rand() % 4);
        V[P_XM_FENV] = (int16_t)(rand() % 255 - 127), V[P_XM_VCO2] = (int16_t)(rand() % 255 - 127);
        int32_t lfo = rand() % 65535 - 32767, dep = rand() % 65537 - 32768, fe = rand() % (ADSR_ONE + 1);
        int32_t v2 = rand() % 62031 - 31015, at = rand() % 65537 - 32768;
        mods_sum(&ma, V, lfo, dep, fe, v2, at);
        mods_sum(&mb, V, lfo, dep, fe, 0, at);
        mods_xm_gains(&g, V);
        mods_xm_tick(&mc, &mb, &g, v2);
        if (memcmp(&ma, &mc, sizeof ma)) exact = 0;
    }
    printf("xmod_sources_share_dests: mods_sum(vco2) == mods_sum(0) + mods_xm_tick over 20000 random cases: %d\n", exact);
    CHECK(exact);
}

static void xmod_bp_dest(void)
{
    mods_t m;
    v_reset();
    V[P_XM_FENV] = 127, V[P_XM_D_BP] = 1;
    int mono = 1;
    int32_t prev = -1, half = 0;
    for (int32_t e = 0; e <= ADSR_ONE; e += ADSR_ONE / 64) {
        mods_sum(&m, V, 0, 0, e, 0, 0);
        if (m.bp_mix < prev) mono = 0;
        prev = m.bp_mix;
        if (e == ADSR_ONE / 2) half = m.bp_mix;
    }
    mods_sum(&m, V, 0, 0, 0, 0, 0);
    int32_t b0 = m.bp_mix;
    mods_sum(&m, V, 0, 0, ADSR_ONE, 0, 0);
    printf("xmod_bp_dest: fenv 0 -> %d, 0.5 -> %d, 1 -> %d (svf bp_mix 0..32768), rising %d\n", b0, half, m.bp_mix, mono);
    CHECK(b0 == 0 && m.bp_mix == 32768 && abs(half - 16384) <= 1 && mono);
    V[P_XM_D_BP] = 0;
    mods_sum(&m, V, 0, 0, ADSR_ONE, 0, 0);
    CHECK(mods_zero(&m));
}

static void pw_dest_4state(void)
{
    mods_t m;
    static const int want1[4] = { 0, 1, 0, 1 }, want2[4] = { 0, 0, 1, 1 };
    for (int s = 0; s < 4; s++) {
        v_reset();
        V[P_LFO_D_PW] = (int16_t)s;
        mods_sum(&m, V, LFO_ONE, MODS_DEPTH_ONE, 0, 0, 0);
        printf("pw_dest_4state: %s -> vco1_pw %.2f, vco2_pw %.2f PW steps\n", PARAMS[P_LFO_D_PW].labels[s],
               m.vco1_pw / 65536.0, m.vco2_pw / 65536.0);
        CHECK((m.vco1_pw != 0) == want1[s] && (m.vco2_pw != 0) == want2[s]);
        if (want1[s]) CHECK(abs(m.vco1_pw - (int32_t)((int64_t)MODS_LFO_PW * LFO_ONE / 32768)) <= 2);
        if (want2[s]) CHECK(abs(m.vco2_pw - (int32_t)((int64_t)MODS_LFO_PW * LFO_ONE / 32768)) <= 2);
        if (want1[s] && want2[s]) CHECK(m.vco2_pw == m.vco1_pw);
        m.vco1_pw = m.vco2_pw = 0;
        CHECK(mods_zero(&m));
    }
}

/* the rest of the routing: every LFO and AT destination lands in its field with the documented full scale */
static void routing_full_scales(void)
{
    struct { int id; int off; int32_t full; } lfo[] = {
        { P_LFO_D_VCO1, offsetof(mods_t, vco1_pitch), MODS_LFO_PITCH },
        { P_LFO_D_VCO2, offsetof(mods_t, vco2_pitch), MODS_LFO_PITCH },
        { P_LFO_D_AMP, offsetof(mods_t, amp), MODS_LFO_AMP },
        { P_LFO_D_FFREQ, offsetof(mods_t, flt_freq), MODS_LFO_FREQ },
        { P_LFO_D_FMODE, offsetof(mods_t, flt_mode), MODS_LFO_MODE },
    }, at[] = {
        { P_AT_D_VCO1, offsetof(mods_t, vco1_pitch), MODS_AT_PITCH },
        { P_AT_D_VCO2, offsetof(mods_t, vco2_pitch), MODS_AT_PITCH },
        { P_AT_D_AMP, offsetof(mods_t, amp), MODS_AT_AMP },
        { P_AT_D_FFREQ, offsetof(mods_t, flt_freq), MODS_AT_FREQ },
        { P_AT_D_FMODE, offsetof(mods_t, flt_mode), MODS_AT_MODE },
    };
    mods_t m;
    for (unsigned i = 0; i < 5; i++) {
        v_reset();
        V[lfo[i].id] = 1;
        mods_sum(&m, V, -LFO_ONE, MODS_DEPTH_ONE, 0, 0, 0);
        int32_t got = *(int32_t *)((char *)&m + lfo[i].off);
        *(int32_t *)((char *)&m + lfo[i].off) = 0;
        printf("routing_full_scales: %-16s lfo -1 x depth 1 -> %d (full %d)\n", PARAMS[lfo[i].id].name, got, lfo[i].full);
        CHECK(abs(got + (int32_t)((int64_t)lfo[i].full * LFO_ONE / 32768)) <= 2 && mods_zero(&m));
        v_reset();
        V[at[i].id] = 1;
        mods_sum(&m, V, LFO_ONE, MODS_DEPTH_ONE, 0, 0, mods_at(127, -127));
        got = *(int32_t *)((char *)&m + at[i].off);
        *(int32_t *)((char *)&m + at[i].off) = 0;
        printf("routing_full_scales: %-16s AT 127 x -127 -> %d (full %d)\n", PARAMS[at[i].id].name, got, at[i].full);
        CHECK(got == -at[i].full && mods_zero(&m));
    }
    /* AT -> LFO AMT is not a mods_sum destination (it is in lfo_depth) */
    v_reset();
    V[P_AT_D_LFOAMT] = 1;
    mods_sum(&m, V, LFO_ONE, 0, 0, 0, 32768);
    CHECK(mods_zero(&m));
}

/* mods_fm1: per-sample VCO 1 pitch against osc_set_pitch at the same note */
static void fm1_matches_set_pitch(void)
{
    osc_t a, b;
    osc_init(&a, 1), osc_init(&b, 1);
    double einc = 0, einv = 0;
    int exact0 = 1, clamp = 1;
    for (int32_t n = -60 * 65536; n <= 135 * 65536; n += 65536 * 3 + 4321) {
        osc_set_pitch(&a, n, 69 << 16);
        uint32_t inc = a.inc1, inv = a.inv1;
        mods_fm1(&b, inc, inv, n, 0);
        if (b.inc1 != inc || b.inv1 != inv) exact0 = 0;
        for (int32_t s = -160 * 65536; s <= 160 * 65536; s += 65536 / 3 + 77) {   /* the real +-160 st sums */
            mods_fm1(&b, inc, inv, n, s);
            int32_t t = n + s;
            if (t < -60 * 65536 || t > 135 * 65536) {
                osc_set_pitch(&a, t, 69 << 16);   /* osc_set_pitch clamps the same way */
                if (fabs((double)b.inc1 / a.inc1 - 1) > 3e-4) clamp = 0;
                continue;
            }
            osc_set_pitch(&a, t, 69 << 16);
            double ei = fabs((double)b.inc1 / a.inc1 - 1), ev = fabs((double)b.inv1 / a.inv1 - 1);
            if (ei > einc) einc = ei;
            if (ev > einv) einv = ev;
        }
    }
    printf("fm1_matches_set_pitch: max relative error inc %.2e (%.3f cent), inv %.2e; 0 st exact %d; clamped %d\n",
           einc, einc * 1200 / log(2), einv, exact0, clamp);
    CHECK(einc < 2.5e-4 && einv < 2.5e-4 && exact0 && clamp);
}

static void cost(void)
{
    mods_t ctl, out;
    mods_xm_t g;
    osc_t o;
    osc_init(&o, 1);
    v_reset();
    for (int id = P_XM_D_VCO1; id <= P_XM_D_BP; id++) V[id] = 1;
    V[P_XM_VCO2] = 77, V[P_XM_FENV] = -20;
    mods_sum(&ctl, V, 1000, 20000, ADSR_ONE / 2, 0, 0);
    mods_xm_gains(&g, V);
    osc_set_pitch(&o, 60 << 16, 60 << 16);
    uint32_t inc = o.inc1, inv = o.inv1;
    volatile int32_t sink = 0;
    int64_t acc = 0;
    const long total = 20000000;
    clock_t t0 = clock();
    for (long i = 0; i < total; i++) {
        int32_t v2 = (int32_t)(((uint32_t)i * 2654435761u) >> 17) - 16384;   /* -16384..16383 */
        mods_xm_tick(&out, &ctl, &g, v2);
        mods_fm1(&o, inc, inv, 60 << 16, out.vco1_pitch);
        acc += o.inc1 + out.flt_freq + out.vco1_pw + out.bp_mix;
    }
    sink = (int32_t)acc;
    double ns = 1e9 * (double)(clock() - t0) / CLOCKS_PER_SEC / total;
    printf("cost: mods_xm_tick (6 dests) + mods_fm1 = %.2f ns per voice-sample on the host (%d)\n", ns, sink != 0);
}

int main(void)
{
    lfo_range();
    lfo_polarity();
    lfo_random_max_is_noise();
    lfo_key_reset_first_only();
    wheel_only_adds_depth();
    at_negative_inverts_lfo();
    xmod_sources_share_dests();
    xmod_bp_dest();
    pw_dest_4state();
    routing_full_scales();
    fm1_matches_set_pitch();
    cost();
    if (fails) { printf("test_mod: %d FAILED\n", fails); return 1; }
    printf("test_mod: all passed\n");
    return 0;
}
