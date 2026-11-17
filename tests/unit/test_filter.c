/* SPDX-License-Identifier: GPL-3.0-only */
/* Filter (firmware/src/synth/filter.c): mode responses, band-pass, the Norm<->BP crossfade, no self-oscillation,
 * overdrive, cutoff steps, the float path's bounds on extreme states, and the per-sample coefficient path. The measurements use doubles (host
 * only): a sine's level is its projection on sin/cos over a whole second after one second of settling. Every test
 * prints what it measured. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "synth/filter.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define FS 44100
#define T_PI 3.14159265358979323846
#define AMP 8192.0                                   /* test sine: -12 dB below Q15 full scale */
#define CUT_Q16(step) ((int32_t)lrint((step) * 65536.0))

/* the cutoff position (Q16 semitones; "step" below = one semitone, not one FILTER FREQ knob step) of a frequency:
 * 99 = 440 Hz */
static int32_t cut_for_hz(double hz) { return CUT_Q16(99 + 12 * log2(hz / 440)); }

/* steady-state gain (dB) of the filter at f: 1 s settle, then project 1 s of output on sin / cos */
static double gain_db(int32_t cut, int reso, int mode, int32_t bp, double f)
{
    svf_coef_t c;
    svf_t s = {0, 0};
    svf_coef(&c, cut, reso, mode, bp);
    double ps = 0, pc = 0;
    for (int n = 0; n < 2 * FS; n++) {
        double ph = 2 * T_PI * f * n / FS;
        int32_t y = svf_tick(&s, (int32_t)lrint(AMP * sin(ph)), &c);
        if (n >= FS) { ps += y * sin(ph); pc += y * cos(ph); }
    }
    double a = 2 * sqrt(ps * ps + pc * pc) / FS;
    return 20 * log10(a / AMP + 1e-12);
}

static void test_cutoff_map(void)
{
    uint32_t lo = svf_cutoff_hz(0), a4 = svf_cutoff_hz(CUT_Q16(99)), hi = svf_cutoff_hz(CUT_Q16(164));
    printf("cutoff_map: step 0 = %u Hz, 99 = %u Hz, 164 = %u Hz\n", lo, a4, hi);
    CHECK(lo >= 1 && lo <= 2);
    CHECK(a4 >= 439 && a4 <= 441);
    CHECK(hi >= 18700 && hi <= 18900 && hi < FS / 2);
    double worst = 0;   /* cents off the mapping, at every half step */
    for (int h = 2 * 24; h <= 2 * 164; h++) {   /* from 4 Hz up: whole Hz resolution is too coarse below */
        double want = 440 * pow(2, (h / 2.0 - 99) / 12), got = svf_cutoff_hz(CUT_Q16(h / 2.0));
        double cents = fabs(1200 * log2(got / want)) - 1200 * log2(1 + 0.5 / want);   /* minus the Hz rounding */
        if (cents > worst) worst = cents;
    }
    printf("cutoff_map: worst %.2f cents off 440 * 2^((step - 99) / 12) (steps 24..164, half steps)\n", worst);
    CHECK(worst < 10);
    CHECK(svf_cutoff_hz(-5 * 65536) == lo && svf_cutoff_hz(300 * 65536) == hi);   /* clamped */
}

/* the FILTER FREQ knob (svf_knob_q16): 0 = 16 Hz, 164 = 18.8 kHz, exponential in between (0.746 semitone a step),
 * monotone; the old one-semitone map put 0 at 1.44 Hz */
static void test_knob_map(void)
{
    uint32_t k0 = svf_cutoff_hz(svf_knob_q16(0)), k82 = svf_cutoff_hz(svf_knob_q16(82));
    uint32_t k164 = svf_cutoff_hz(svf_knob_q16(164));
    double worst = 0, smin = 1e9, smax = 0;
    for (int k = 0; k <= 164; k++) {
        double want = 16 * pow(18794.5 / 16, k / 164.0), got = svf_cutoff_hz(svf_knob_q16(k));
        double cents = fabs(1200 * log2(got / want)) - 1200 * log2(1 + 0.5 / want);
        if (cents > worst) worst = cents;
        if (k) {
            double st = (svf_knob_q16(k) - svf_knob_q16(k - 1)) / 65536.0;
            if (st < smin) smin = st;
            if (st > smax) smax = st;
        }
    }
    printf("knob_map: FREQ 0 = %u Hz, 82 = %u Hz, 164 = %u Hz; worst %.2f cents off 16 Hz * (18794.5 / 16)^(k / 164); "
           "a step %.4f .. %.4f semitones\n", k0, k82, k164, worst, smin, smax);
    CHECK(k0 == 16 && k82 >= 545 && k82 <= 552 && k164 >= 18700 && k164 <= 18900);
    CHECK(worst < 10 && smin > 0.745 && smax < 0.748);
    CHECK(svf_knob_q16(-3) == svf_knob_q16(0) && svf_knob_q16(200) == SVF_CUT_MAX && svf_knob_q16(164) == SVF_CUT_MAX);
}

static void test_lp_hp_notch(void)
{
    int32_t fc = cut_for_hz(1000);
    double lp_pass = gain_db(fc, 0, 0, 0, 100), lp_dec = gain_db(fc, 0, 0, 0, 10000);
    double hp_pass = gain_db(fc, 0, 255, 0, 10000), hp_dec = gain_db(fc, 0, 255, 0, 100);
    double notch = gain_db(fc, 0, 128, 0, 1000), notch_lo = gain_db(fc, 0, 128, 0, 100);
    double notch_hi = gain_db(fc, 0, 128, 0, 10000);
    double lp_fc = gain_db(fc, 0, 0, 0, 1000), lp_res = gain_db(fc, 255, 0, 0, 1000);
    double lp_2dec = gain_db(cut_for_hz(100), 0, 0, 0, 10000);
    printf("lp_hp_notch (fc 1 kHz, RESO 0): LP 100 Hz %.2f dB, 10 kHz %.2f dB (100 Hz fc: %.2f dB two decades up)\n",
           lp_pass, lp_dec, lp_2dec);
    printf("lp_hp_notch: HP 10 kHz %.2f dB, 100 Hz %.2f dB; notch at fc %.2f dB (100 Hz %.2f, 10 kHz %.2f)\n",
           hp_pass, hp_dec, notch, notch_lo, notch_hi);
    printf("lp_hp_notch: LP at fc %.2f dB (Q 0.5), at RESO 255 %.2f dB\n", lp_fc, lp_res);
    CHECK(fabs(lp_pass) < 0.5 && lp_dec <= -36);
    CHECK(fabs(hp_pass) < 0.5 && hp_dec <= -36);
    CHECK(lp_2dec <= -72);                           /* 12 dB / octave: -40 dB a decade, -80 two */
    CHECK(notch <= -20 && fabs(notch_lo) < 0.5 && fabs(notch_hi) < 0.5);
    CHECK(fabs(lp_fc + 6.02) < 0.5);                 /* Q 0.5: |H(fc)| = Q */
    CHECK(lp_res > 15);   /* Q 25 is +28 dB small-signal; at -12 dBFS the band-pass clip already holds it down */
    /* MODE is continuous: the amplitude at fc / 4 and 4 fc moves in small steps from LP through notch to HP (in dB
     * it can dip: past the notch the fading LP cancels the HP at a frequency that slides down, as on a SEM) */
    double maxstep = 0;
    for (int f = 0; f < 2; f++) {
        double hz = f ? 4000 : 250, prev = pow(10, gain_db(fc, 0, 0, 0, hz) / 20);
        for (int m = 8; m <= 256; m += 8) {
            double a = pow(10, gain_db(fc, 0, m > 255 ? 255 : m, 0, hz) / 20);
            if (fabs(a - prev) > maxstep) maxstep = fabs(a - prev);
            prev = a;
        }
    }
    printf("lp_hp_notch: MODE sweep 0..255 in 8s, largest amplitude change at fc / 4 or 4 fc: %.3f (input = 1)\n",
           maxstep);
    CHECK(maxstep < 0.1);
}

static void test_bp_switch(void)
{
    int32_t fc = cut_for_hz(1000), f5 = cut_for_hz(500);
    double peak = gain_db(fc, 0, 0, 32768, 1000);
    double up1 = gain_db(fc, 0, 0, 32768, 2000), dn1 = gain_db(fc, 0, 0, 32768, 500);
    /* the skirts at fc 500 Hz, 3 -> 4 octaves away (an octave lower keeps the upper one clear of Nyquist's warp) */
    double up3 = gain_db(f5, 0, 0, 32768, 4000), up4 = gain_db(f5, 0, 0, 32768, 8000);
    double dn3 = gain_db(f5, 0, 0, 32768, 62.5), dn4 = gain_db(f5, 0, 0, 32768, 31.25);
    double qup = gain_db(fc, 0, 0, 32768, 1189.2), qdn = gain_db(fc, 0, 0, 32768, 840.9);
    double rpeak = gain_db(fc, 160, 0, 32768, 1000), rup1 = gain_db(fc, 160, 0, 32768, 2000);
    double rdn1 = gain_db(fc, 160, 0, 32768, 500);
    printf("bp_switch (fc 1 kHz, RESO 0): peak %.2f dB, +-1/4 oct %.2f / %.2f, +-1 oct %.2f / %.2f\n",
           peak, qup, qdn, up1, dn1);
    printf("bp_switch: skirt (fc 500 Hz) 3->4 octaves above %.2f dB/oct, below %.2f dB/oct\n", up4 - up3, dn4 - dn3);
    printf("bp_switch: RESO 160 peak %.2f dB, +-1 oct %.2f / %.2f (%.2f / %.2f below the peak)\n",
           rpeak, rup1, rdn1, rup1 - rpeak, rdn1 - rpeak);
    CHECK(peak >= -1 && peak <= 0.5 && qup < peak && qdn < peak);
    /* a 2-pole band-pass falls 6 dB / octave on each side (see the report: the brief's "-12 dB per octave" is not
     * reachable with the 2-pole filter rule 1 asks for); checked +-2 dB, and symmetric */
    CHECK(fabs(up4 - up3 + 6) <= 2 && fabs(dn4 - dn3 + 6) <= 2);
    CHECK(fabs(up1 - dn1) < 1);
    CHECK(rpeak > peak && rup1 - rpeak <= -10 && rdn1 - rpeak <= -10);   /* RESO narrows it */
}

static void test_bp_crossfade(void)
{
    int32_t fc = cut_for_hz(1000);
    /* sample-exact ends: bp 0 is the mode output, 32768 is the band-pass output whatever the mode */
    svf_coef_t c0, c1, cb, cm;
    svf_t s0 = {0, 0}, s1 = {0, 0};
    svf_coef(&c0, fc, 100, 60, 0);
    svf_coef(&cm, fc, 100, 60, 0);
    CHECK(c0.bp == 0 && c0.lp == cm.lp && c0.hp == cm.hp);
    svf_coef(&c1, fc, 100, 60, 32768);
    svf_coef(&cb, fc, 100, 200, 32768);
    CHECK(c1.lp == 0 && c1.hp == 0 && cb.lp == 0 && cb.hp == 0 && c1.bp == cb.bp && c1.bp > 0);
    int same = 1;
    for (int n = 0; n < 4000; n++) {
        int32_t in = (int32_t)lrint(AMP * sin(2 * T_PI * 700.0 * n / FS));
        if (svf_tick(&s0, in, &c1) != svf_tick(&s1, in, &cb)) same = 0;
    }
    CHECK(same);
    /* monotone between: in LP mode the level two octaves above fc rises from the LP's to the BP's */
    double prev = -400, lo = 0, hi = 0;
    int mono = 1;
    for (int b = 0; b <= 32768; b += 2048) {
        double g = gain_db(fc, 0, 0, b, 4000);
        if (b == 0) lo = g;
        if (b == 32768) hi = g;
        if (g < prev - 0.01) mono = 0;
        prev = g;
    }
    printf("bp_crossfade: LP mode, level at 4 fc from %.2f dB (bp 0) to %.2f dB (bp 32768), monotone %d\n",
           lo, hi, mono);
    CHECK(mono && hi > lo + 10);
    prev = 400, mono = 1;
    for (int b = 0; b <= 32768; b += 2048) {   /* and at fc / 16 it falls from LP pass-band to the BP skirt */
        double g = gain_db(fc, 0, 0, b, 62.5);
        if (g > prev + 0.01) mono = 0;
        prev = g;
    }
    CHECK(mono);
}

static void test_no_self_oscillation(void)
{
    double worst = -400, w100 = -400;
    int wstep = 0, wmode = 0, last = 0, lstep = 0;
    static const int modes[4][2] = { {0, 0}, {128, 0}, {255, 0}, {0, 32768} };   /* LP, notch, HP, BP */
    for (int step = 0; step <= 164; step += 2) {      /* the FILTER FREQ knob's whole range: 16 Hz .. 18.8 kHz */
        for (int m = 0; m < 4; m++) {
            svf_coef_t c;
            svf_t s = {0, 0};
            svf_coef(&c, svf_knob_q16(step), 255, modes[m][0], modes[m][1]);
            double e0 = 0, e1 = 0, e100 = 0;
            for (int n = 0; n < FS + 441; n++) {
                double y = svf_tick(&s, n == 0 ? 32767 : 0, &c);
                if (n < 441) e0 += y * y;
                else if (n >= 4410 && n < 4851) e100 += y * y;
                else if (n >= FS) e1 += y * y;
                if (y != 0 && n > last) last = n, lstep = step;
            }
            double db = e0 <= 0 ? 400 : e1 > 0 ? 10 * log10(e1 / e0) : -400;   /* silence is a failure */
            if (db > worst) worst = db, wstep = step, wmode = m;
            if (e100 > 0 && 10 * log10(e100 / e0) > w100) w100 = 10 * log10(e100 / e0);
        }
    }
    printf("no_self_oscillation: RESO 255, impulse, energy of [1 s, 1.01 s) vs the first 10 ms, worst %.1f dB "
           "(FREQ %d = %u Hz, mode %d)\n", worst, wstep, svf_cutoff_hz(svf_knob_q16(wstep)), wmode);
    printf("no_self_oscillation: the last nonzero output sample of any of them: %.3f s (FREQ %d = %u Hz); "
           "worst energy at [100 ms, 110 ms): %.1f dB\n", last / (double)FS, lstep, svf_cutoff_hz(svf_knob_q16(lstep)),
           w100);
    CHECK(worst < -80);
    /* and a loud sustained input at max RESO stops ringing once the input stops */
    svf_coef_t c;
    svf_t s = {0, 0};
    svf_coef(&c, cut_for_hz(200), 255, 0, 0);
    int32_t peak = 0, tail = 0;
    for (int n = 0; n < 3 * FS; n++) {
        int32_t in = n < FS ? (int32_t)lrint(131068 * sin(2 * T_PI * 200.0 * n / FS)) : 0;
        int32_t y = svf_tick(&s, in, &c);
        if (abs(y) > peak) peak = abs(y);
        if (n >= 2 * FS && abs(y) > tail) tail = abs(y);
    }
    printf("no_self_oscillation: 4x full-scale sine at fc (200 Hz), RESO 255: peak %d, 1 s after it stops %d\n",
           peak, tail);
    CHECK(peak <= 65536 && tail == 0);
}

/* THD (dB) and peak of a 100 Hz sine of amplitude a (x Q15 full scale) through the filter at (cut, reso) */
static double thd_db(double a, int32_t cut, int reso, int32_t *peak, int *signflip)
{
    svf_coef_t c;
    svf_t s = {0, 0};
    svf_coef(&c, cut, reso, 0, 0);
    static double y[FS];
    int32_t pk = 0;
    int flip = 0;
    for (int n = 0; n < 2 * FS; n++) {
        double x = a * 32767 * sin(2 * T_PI * 100.0 * n / FS);
        int32_t v = svf_tick(&s, (int32_t)lrint(x), &c);
        if (abs(v) > pk) pk = abs(v);
        if (n >= FS) {
            y[n - FS] = v;
            if (fabs(x) > 3000 && (v > 0) != (x > 0)) flip = 1;
        }
    }
    double ps = 0, pc = 0, dc = 0;
    for (int n = 0; n < FS; n++) {   /* 100 whole periods */
        double ph = 2 * T_PI * 100.0 * (n + FS) / FS;
        ps += y[n] * sin(ph), pc += y[n] * cos(ph), dc += y[n];
    }
    ps *= 2.0 / FS, pc *= 2.0 / FS, dc /= FS;
    double ef = 0, er = 0;
    for (int n = 0; n < FS; n++) {
        double ph = 2 * T_PI * 100.0 * (n + FS) / FS;
        double f = ps * sin(ph) + pc * cos(ph);
        ef += f * f, er += (y[n] - f - dc) * (y[n] - f - dc);
    }
    *peak = pk;
    *signflip = flip;
    return 10 * log10(er / ef + 1e-30);
}

static void test_overdrive_soft(void)
{
    int32_t open = CUT_Q16(164);
    double prev = -400, maxjump = 0, t50 = 0, t25 = 0;
    int mono = 1, anyflip = 0;
    int32_t maxpk = 0;
    printf("overdrive_soft (LP open, RESO 0, 100 Hz sine), THD vs amplitude (x Q15 full scale):\n ");
    for (int i = 1; i <= 16; i++) {   /* every point counts: 0.25x .. 4x */
        int32_t pk;
        int flip;
        double t = thd_db(i / 4.0, open, 0, &pk, &flip);
        printf(" %.2fx %.1f%s", i / 4.0, t, i == 8 ? "\n " : i == 16 ? "\n" : ",");
        if (i == 1) t25 = t;
        if (i == 2) t50 = t;
        if (t < prev) mono = 0;
        if (i > 1 && t - prev > maxjump) maxjump = t - prev;
        prev = t;
        if (pk > maxpk) maxpk = pk;
        anyflip |= flip;
    }
    int32_t pk;
    int flip;
    double tr = thd_db(4.0, cut_for_hz(400), 255, &pk, &flip);
    printf("overdrive_soft: peak %d (Q15), largest THD rise per 0.25x %.1f dB (limit 12), monotone %d, sign flips %d; "
           "RESO 255 fc 400 Hz 4x: THD %.1f dB peak %d\n", maxpk, maxjump, mono, anyflip, tr, pk);
    CHECK(mono && !anyflip && maxpk <= 65536 && pk <= 65536);
    CHECK(maxjump <= 12);
    CHECK(t25 < -60 && t50 < -60);   /* normal levels (up to one oscillator at full level) stay clean */
    /* inputs far beyond full scale (and the int32 extremes) neither wrap nor escape the bound */
    svf_coef_t c;
    svf_t s = {0, 0};
    svf_coef(&c, open, 255, 0, 0);
    int32_t big = 0;
    int bad = 0;
    for (int n = 0; n < 20000; n++) {
        int32_t in = (n / 50) & 1 ? INT32_MAX : INT32_MIN;
        int32_t v = svf_tick(&s, in, &c);
        if (abs(v) > big) big = abs(v);
        if (n % 50 == 49 && (v > 0) != (in > 0)) bad = 1;
    }
    printf("overdrive_soft: int32 extremes as input, RESO 255: peak %d, sign follows the input %d\n", big, !bad);
    CHECK(big <= 65536 && !bad);
}

/* LP output for a sine at hz (amplitude 16384), the cutoff stepping from c0 to c1 at sample 'at'. The sine sits
 * below both cutoffs, so the output is a smooth sine whose sample-to-sample change is small: a glitch shows. */
static void run_step(int32_t c0, int32_t c1, int reso, double hz, int at, int32_t *ys, int len)
{
    svf_coef_t ca, cb;
    svf_t s = {0, 0};
    svf_coef(&ca, c0, reso, 0, 0);
    svf_coef(&cb, c1, reso, 0, 0);
    for (int n = 0; n < len; n++)
        ys[n] = svf_tick(&s, (int32_t)lrint(16384 * sin(2 * T_PI * hz * n / FS)), n < at ? &ca : &cb);
}
static int32_t maxdelta(const int32_t *y, int a, int b)
{
    int32_t m = 0;
    for (int n = a; n < b; n++) if (abs(y[n] - y[n - 1]) > m) m = abs(y[n] - y[n - 1]);
    return m;
}

static void test_cutoff_smooth(void)
{
    static int32_t ys[2 * FS];
    /* jumps of 1 and 6 steps (what a control-rate update of a knob or an envelope moves) must not jump the output
     * more than 2x its steady sample-to-sample change. Larger instant jumps are listed, not checked: there the
     * output legitimately catches up with the input at the new bandwidth (state-continuous, as an analog filter
     * would), which the caller avoids by updating the cutoff every control tick. */
    static const int pairs[][3] = { {60, 61, 0}, {61, 60, 0}, {120, 121, 255}, {121, 120, 255}, {46, 47, 255},
                                    {60, 66, 0}, {66, 60, 0}, {100, 106, 200}, {106, 100, 200}, {46, 52, 255},
                                    {52, 46, 255}, {140, 146, 255}, {146, 140, 255}, {100, 106, 128},
                                    {60, 72, 0}, {100, 112, 200}, {60, 84, 0}, {60, 130, 0} };
    double worst = 0;
    for (unsigned i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
        int lo = pairs[i][0] < pairs[i][1] ? pairs[i][0] : pairs[i][1];
        double hz = svf_cutoff_hz(CUT_Q16(lo)) / 2.0;   /* an octave below the lower cutoff */
        int at = FS / 2 + 1234;
        run_step(CUT_Q16(pairs[i][0]), CUT_Q16(pairs[i][1]), pairs[i][2], hz, at, ys, 2 * FS);
        int32_t before = maxdelta(ys, at - FS / 4, at), after = maxdelta(ys, at + FS / 2, 2 * FS);
        int32_t jump = maxdelta(ys, at, at + FS / 2);
        int32_t ref = before > after ? before : after;
        double r = ref > 0 ? (double)jump / ref : 1e9;   /* silence is a failure */
        int checked = abs(pairs[i][1] - pairs[i][0]) <= 6;
        printf("cutoff_smooth: step %3d -> %3d RESO %3d, sine %6.1f Hz: steady max delta %d / %d, transition %d "
               "(%.2fx)%s\n", pairs[i][0], pairs[i][1], pairs[i][2], hz, before, after, jump, r,
               checked ? "" : " (listed only)");
        if (checked && r > worst) worst = r;
    }
    CHECK(worst <= 2);
}

/* after any excitation, two seconds of silence leave no output: no DC, no limit cycle. The states settle to a
 * residual far below the output's LSB (Q28 states, Q15 out = 8192 state LSB): exact zero is not reachable at low
 * cutoffs, where one LSB of v1 needs a2 |s2| >= 2^30, i.e. |s2| >= 2^28 / (4 g) (9710 at step 0). */
static void test_decay_to_zero(void)
{
    int bad = 0, cases = 0, lowest_clean = -1, zeros = 0;
    int32_t worst_s = 0, worst_out = 0;   /* states in the old Q28 units (float x 2^28) */
    for (int q = 4 * 164; q >= 0; q--) {
        int clean = 1;
        for (int r = 0; r < 3; r++) {
            static const int resos[3] = {0, 128, 255};
            static const int modes[3][2] = { {0, 0}, {255, 0}, {0, 32768} };   /* LP, HP, BP */
            svf_coef_t c, co[3];
            svf_t s = {0, 0};
            svf_coef(&c, q * 16384, resos[r], 128, 0);
            for (int m = 0; m < 3; m++) svf_coef(&co[m], q * 16384, resos[r], modes[m][0], modes[m][1]);
            uint32_t x = 777 + (uint32_t)q;
            for (int n = 0; n < FS / 10; n++) {   /* 0.1 s of loud noise */
                x ^= x << 13, x ^= x >> 17, x ^= x << 5;
                svf_tick(&s, (int32_t)(x >> 15) - 65536, &c);
            }
            for (int n = 0; n < 2 * FS; n++) svf_tick(&s, 0, &c);
            int32_t out = 0;
            for (int m = 0; m < 3; m++) {   /* then 0.1 s more of each output, from the same state */
                svf_t t = s;
                for (int n = 0; n < FS / 10; n++) {
                    int32_t y = abs(svf_tick(&t, 0, &co[m]));
                    if (y > out) out = y;
                }
            }
            cases++;
            zeros += s.s1 == 0 && s.s2 == 0;
            int32_t q1 = (int32_t)fabs(s.s1 * 268435456.0), q2 = (int32_t)fabs(s.s2 * 268435456.0);
            if (q1 > worst_s) worst_s = q1;
            if (q2 > worst_s) worst_s = q2;
            if (out > worst_out) worst_out = out;
            if (out) clean = 0;
            if (out && q >= 4 * 46) bad++;   /* the audio band, from 20.6 Hz: exactly silent */
        }
        if (clean && lowest_clean == q + 1) lowest_clean = q;
        if (q == 4 * 164 && clean) lowest_clean = q;
    }
    printf("decay_to_zero: after 2 s of silence (every quarter step, RESO 0/128/255): LP / HP / BP output exactly 0 "
           "from step %.2f (%u Hz) up, worst output below that %d LSB; states: %d of %d exactly 0, largest residual %d "
           "(%.3f output LSB)\n", lowest_clean / 4.0, svf_cutoff_hz(lowest_clean * 16384), worst_out, zeros, cases,
           worst_s, worst_s / 8192.0);
    CHECK(bad == 0 && worst_out <= 2 && worst_s < 16384);   /* sub-audio: <= 2 LSB, still decaying or stuck */
}

/* the float path on random coefficients, random (also huge) states and any input, int32 extremes included: the output
 * stays within +-65536 (Q15 + 1 bit), never NaN, and the next states are finite (the integer path this replaced
 * saturated in its 64-bit fallback; in float nothing wraps) */
static uint32_t rnd = 12345;
static uint32_t xs(void) { rnd ^= rnd << 13; rnd ^= rnd >> 17; rnd ^= rnd << 5; return rnd; }
static void test_extreme_states(void)
{
    long bad = 0;
    int32_t peak = 0;
    for (int i = 0; i < 2000000; i++) {
        svf_coef_t c;
        int32_t cut = (int32_t)(xs() % (165u * 65536u));
        int reso = (int)(xs() % 256), mode = (int)(xs() % 256);
        int32_t bp = (int32_t)(xs() % 4) == 0 ? 0 : (int32_t)(xs() % 32769);
        if (i % 7 == 0) cut = (int32_t)(xs() % 8) * 65536;
        if (i % 11 == 0) reso = 0;
        svf_coef(&c, cut, reso, mode, bp);
        float lim = (xs() % 4) == 0 ? 8.0f : 2.0f;   /* 8 = the old int32 range in Q28 */
        svf_t f = {lim * ((int32_t)xs() / 2147483648.0f), lim * ((int32_t)xs() / 2147483648.0f)};
        int32_t in = (int32_t)xs();
        if (i % 3) in >>= (xs() % 16);
        int32_t y = svf_tick(&f, in, &c);
        if (abs(y) > peak) peak = abs(y);
        if (abs(y) > 65536 || !isfinite(f.s1) || !isfinite(f.s2)) bad++;
    }
    svf_t z = {1e-21f, -3e-30f}, k = {2e-20f, -1.0f};
    svf_flush(&z), svf_flush(&k);
    CHECK(z.s1 == 0 && z.s2 == 0 && k.s1 == 2e-20f && k.s2 == -1.0f);   /* svf_flush: tiny -> 0, the rest kept */
    printf("extreme_states: 2000000 random samples (states up to +-8 = the old int32 range): peak %d, %ld out of "
           "bounds or not finite\n", peak, bad);
    CHECK(bad == 0);
}

/* svf_coef_fast (the per-sample path for audio-rate X-Mod) against svf_coef: the reciprocal over its whole domain,
 * every coefficient on random settings, the output of a modulated filter, and the cost of both */
static double rel(double a, double b) { return b == 0 ? fabs(a) : fabs(a / b - 1); }
static void test_coef_fast_matches(void)
{
    srand(7);
    double eg = 0, ek = 0, ekcap = 0, ea1 = 0, ea2 = 0;
    int dgain = 0;
    for (int n = 0; n < 200000; n++) {
        int32_t cut = rand() % (SVF_CUT_MAX + 1);
        int reso = rand() % 256, mode = rand() % 256;
        int32_t bp = rand() % 3 == 0 ? 0 : rand() % 32769;
        svf_coef_t a, b;
        svf_coef(&a, cut, reso, mode, bp);
        svf_coef_fast(&b, cut, svf_reso_k(reso), mode, bp);
        if (rel(b.g, a.g) > eg) eg = rel(b.g, a.g);
        /* the floor table is interpolated: 0.05 % between steps, up to 2 % at its cap's kink (step 8.7, 2.4 Hz) */
        double kr = rel(b.k, a.k);
        if (cut >= 9 << 16 && kr > ek) ek = kr;
        if (kr > ekcap) ekcap = kr;
        if (rel(b.a1, a.a1) > ea1) ea1 = rel(b.a1, a.a1);
        if (rel(b.a2, a.a2) > ea2) ea2 = rel(b.a2, a.a2);
        int dg = (int)lrint(fabs(b.lp - a.lp) * 32768) + (int)lrint(fabs(b.hp - a.hp) * 32768);   /* Q15 LSB */
        if (fabs(b.bp - a.bp) > a.bp * kr + 1e-4) dg += 100;   /* bp = b max(k, 1): follows k, floors */
        if (dg > dgain) dgain = dg;
    }
    /* a saw through a filter whose cutoff a 300 Hz sine sweeps +-40 steps per sample, both paths */
    svf_t s1 = {0, 0}, s2 = {0, 0};
    double se = 0, sy = 0;
    for (int n = 0; n < FS; n++) {
        int32_t cut = CUT_Q16(90 + 40 * sin(2 * T_PI * 300 * n / FS));
        int32_t x = (int32_t)((n * 223 % 32768) - 16384);
        svf_coef_t a, b;
        svf_coef(&a, cut, 200, 60, 8000);
        svf_coef_fast(&b, cut, svf_reso_k(200), 60, 8000);
        int32_t y1 = svf_tick(&s1, x, &a), y2 = svf_tick(&s2, x, &b);
        se += (double)(y1 - y2) * (y1 - y2), sy += (double)y1 * y1;
    }
    double snr = 10 * log10(sy / (se + 1e-9));
    printf("coef_fast_matches: g %.1e k %.1e (%.1e below 2.4 Hz) a1 %.1e a2 %.1e; lp/hp %d LSB; swept output "
           "differs by %.1f dB below the signal\n", eg, ek, ekcap, ea1, ea2, dgain, snr);
    CHECK(eg == 0 && ek < 1e-3 && ekcap < 2e-2 && ea1 < 1e-3 && ea2 < 1e-3 && dgain <= 2 && snr > 60);
    /* cost */
    svf_coef_t c;
    volatile uint32_t sink = 0;
    const long total = 5000000;
    clock_t t0 = clock();
    for (long i = 0; i < total; i++) {
        svf_coef_fast(&c, (int32_t)((i * 40503) & 0x7fffff) + (20 << 16), 0x1234567, (int)(i & 255), (int32_t)(i & 32767));
        sink += (uint32_t)(c.a1 * 1000);
    }
    double nf = 1e9 * (double)(clock() - t0) / CLOCKS_PER_SEC / total;
    t0 = clock();
    for (long i = 0; i < total; i++) {
        svf_coef(&c, (int32_t)((i * 40503) & 0x7fffff) + (20 << 16), 100, (int)(i & 255), (int32_t)(i & 32767));
        sink += (uint32_t)(c.a1 * 1000);
    }
    double ns = 1e9 * (double)(clock() - t0) / CLOCKS_PER_SEC / total;
    printf("coef_fast_matches: host cost svf_coef_fast %.2f ns, svf_coef %.2f ns per call (%d)\n", nf, ns, sink != 1);
}

static void bench(void)
{
    svf_coef_t c;
    svf_t s = {0, 0};
    svf_coef(&c, cut_for_hz(800), 180, 90, 0);
    static int32_t in[4096];
    for (int i = 0; i < 4096; i++) in[i] = (int32_t)lrint(20000 * sin(2 * T_PI * 110.0 * i / FS));
    volatile int32_t sink = 0;
    for (int r = 0; r < 2; r++) {
        clock_t t0 = clock();
        int32_t acc = 0;
        for (int k = 0; k < 20000; k++)
            for (int i = 0; i < 4096; i++) acc += svf_tick(&s, in[i], &c);
        sink += acc;
        printf("bench: %.2f ns per svf_tick (host)\n", 1e9 * (double)(clock() - t0) / CLOCKS_PER_SEC / 20000 / 4096);
    }
    clock_t t0 = clock();
    for (int k = 0; k < 2000000; k++) svf_coef(&c, (k * 997) % (165 * 65536), k & 255, k & 255, k & 1 ? 0 : 32768);
    printf("bench: %.2f ns per svf_coef (host)\n", 1e9 * (double)(clock() - t0) / CLOCKS_PER_SEC / 2000000);
    (void)sink;
}

int main(void)
{
    test_cutoff_map();
    test_knob_map();
    test_lp_hp_notch();
    test_bp_switch();
    test_bp_crossfade();
    test_no_self_oscillation();
    test_overdrive_soft();
    test_cutoff_smooth();
    test_decay_to_zero();
    test_extreme_states();
    test_coef_fast_matches();
    if (getenv("SVF_BENCH")) bench();
    printf(fails ? "test_filter: %d FAILED\n" : "test_filter: ok\n", fails);
    return fails != 0;
}
