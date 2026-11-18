/* SPDX-License-Identifier: GPL-3.0-only */
/* Phase 2 Task 7: the distortion (synth/voices.h "Distortion"; docs/ob6-reference.md 2.9: per program, 0..127). An
 * odd-symmetric soft drive on the summed voice bus, after the voices (both cores' sets summed) and before the DC
 * blocker and the limiter (and before the future effects). The tests:
 *   dist_param_live      P_DISTORTION is live (its own entry, the reserved range and default; a version-1 record loads
 *                        it at 0 = bypass; no new record version)
 *   dist_bypass_exact    amount 0 is the Phase 1 render sample for sample (synth_render against its own loop by hand:
 *                        tick, the voices, the DC blocker and the limiter), at every block split
 *   dist_odd             f(-x) = -f(x) exactly, every amount; f(0) = 0; f rises with x
 *   dist_thd_monotonic   a sine through the curve: THD rises with the amount, strictly, at -6 and -20 dBFS
 *   dist_max_chord       max drive + 6-voice chords (the init patch and every mixer level at full): no wrap on the bus,
 *                        the output under the limiter's ceiling, DC under -60 dBFS (Review Focus 5); levels printed
 *   dist_subblocks       the same output whatever the render calls' sizes (engine.c splits a block at arp steps)
 *   dist_smoothed        a jump 0 -> 127 under a held chord glides (no step larger than the waveform's own) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846   /* (not in strict C11 on glibc) */
#endif
#include "params/params.c"
#include "synth/osc.c"
#include "synth/filter.c"
#include "synth/env.c"
#include "synth/mod.c"
#include "synth/voice.c"
#include "synth/voices.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define FS 44100
static synth_t S, R;
static int32_t A[2 * 2 * FS], B[2 * 2 * FS];

static void set(synth_t *s, int id, int v) { synth_param(s, (uint16_t)id, (int16_t)v); }

/* n frames of synth_render into out (zeroed first), in calls of 'step' frames (the last one shorter) */
static void render(synth_t *s, int32_t *out, uint32_t n, uint32_t step)
{
    memset(out, 0, 2 * n * sizeof out[0]);
    for (uint32_t f = 0; f < n; f += step) synth_render(s, out + 2 * f, f + step <= n ? step : n - f);
}
/* the Phase 1 render by hand (synth_render's loop with no distortion stage): tick, every busy voice into the bus,
 * the DC blocker and the limiter; whole control periods */
static void render_phase1(synth_t *s, int32_t *out, uint32_t n)
{
    memset(out, 0, 2 * n * sizeof out[0]);
    for (uint32_t f = 0; f < n; f += VOICE_CTL) {
        int32_t bus[2 * VOICE_CTL] = {0};
        synth_tick(s);
        s->ctx.pos = 0;
        synth_render_set(s, SYNTH_ALL_VOICES, bus, VOICE_CTL);
        synth_bus(s, bus, out + 2 * f, VOICE_CTL);
    }
}

static void dist_param_live(void)
{
    const param_t *d = param_get(P_DISTORTION);
    int ok = d != 0;
    if (d) {
        const int16_t *r = PARAMS_RESERVED[P_DISTORTION - P_COUNT_PHASE1];
        ok &= P_IS_LIVE(P_DISTORTION) && d->id == P_DISTORTION && d->kind == PK_KNOB && d->section == PS_MISC &&
              d->min == 0 && d->max == 127 && d->def == 0 && !d->bipolar && d->min == r[0] && d->max == r[1] &&
              d->def == r[2] && !strcmp(d->name, "DISTORTION");
    }
    /* still stored only: the effects */
    ok &= !P_IS_LIVE(P_FX_ON) && !param_get(P_FX_ON) && !P_IS_LIVE(P_FXB_DIV) && !param_get(P_FXB_DIV);
    /* a version-1 record (no reserved values) loads with distortion 0; the record version stays 2 */
    program_t p;
    uint8_t buf[PROGRAM_PACKED_MAX];
    program_init(&p);
    p.v[P_DISTORTION] = 99;
    uint32_t v1 = program_packed_size(1);
    memset(buf, 0, sizeof buf);
    buf[0] = 1;
    uint16_t c = program_crc16(buf, v1 - 2);
    buf[v1 - 2] = (uint8_t)c, buf[v1 - 1] = (uint8_t)(c >> 8);
    int got = program_unpack(&p, buf, v1);
    ok &= got == 0 && p.v[P_DISTORTION] == 0 && PROGRAM_VERSION == 2;
    /* the synth takes it: a live edit and a program load set the amount, clamped */
    synth_init(&S);
    set(&S, P_DISTORTION, 200);
    ok &= S.prog.v[P_DISTORTION] == 127;
    program_init(&p);
    p.v[P_DISTORTION] = 64;
    synth_program(&S, &p);
    ok &= S.prog.v[P_DISTORTION] == 64 && S.sm[SM_DIST] == 64 * 65536;   /* (silent: snapped) */
    printf("dist_param_live: DISTORTION live (knob 0..127, default 0, MISC), v1 record -> %d, edit 200 -> 127, "
           "program 64 -> %d: %d\n", p.v[P_DISTORTION], S.prog.v[P_DISTORTION], ok);
    CHECK(ok);
}

/* amount 0 = the stage not there at all: synth_render against the Phase 1 loop, sample for sample, for the init chord
 * and a loud patch (every mixer level at full, pulses, sync: the bus well over full scale), in whole periods and in
 * odd-sized calls */
static void dist_bypass_exact(void)
{
    static const uint32_t steps[3] = {VOICE_CTL, 256, 37};
    int diff = 0;
    uint32_t n = FS / 2;
    for (int pt = 0; pt < 2; pt++)
        for (int k = 0; k < 3; k++) {
            synth_init(&S), synth_init(&R);
            for (int t = 0; t < 2; t++) {
                synth_t *s = t ? &R : &S;
                if (pt) {
                    set(s, P_MIX_VCO2, 127), set(s, P_MIX_SUB, 127), set(s, P_MIX_NOISE, 127), set(s, P_VCO1_SHAPE, 255);
                    set(s, P_VCO2_SHAPE, 254), set(s, P_VCO1_SYNC, 1), set(s, P_PRG_VOL, 127), set(s, P_FLT_FREQ, 164);
                }
                set(s, P_DISTORTION, 0);
                for (int j = 0; j < 6; j++) synth_note(s, 48 + 5 * j, 127);
            }
            render(&S, A, n, steps[k]);
            if (steps[k] == VOICE_CTL) render_phase1(&R, B, n);
            else {   /* the same split as A's calls, by hand: periods as synth_render cuts them */
                memset(B, 0, 2 * n * sizeof B[0]);
                for (uint32_t f = 0; f < n;) {
                    if (!R.ctl_left) synth_tick(&R), R.ctl_left = VOICE_CTL;
                    uint32_t room = steps[k] - f % steps[k], m = room < R.ctl_left ? room : R.ctl_left;
                    if (m > n - f) m = n - f;
                    int32_t bus[2 * VOICE_CTL] = {0};
                    R.ctx.pos = (uint8_t)(VOICE_CTL - R.ctl_left);
                    synth_render_set(&R, SYNTH_ALL_VOICES, bus, m);
                    synth_bus(&R, bus, B + 2 * f, (int)m);
                    R.ctl_left = (uint8_t)(R.ctl_left - m);
                    f += m;
                }
            }
            for (uint32_t i = 0; i < 2 * n; i++) diff += A[i] != B[i];
        }
    printf("dist_bypass_exact: amount 0, init chord and a loud chord, calls of 16 / 256 / 37 frames: %d samples differ "
           "from the Phase 1 render\n", diff);
    CHECK(diff == 0);
}

static void dist_odd(void)
{
    int bad = 0, mono = 0, zero = 0;
    for (int a = 0; a <= 127; a++) {
        float kz, ky;
        synth_dist_coef(a * 65536, &kz, &ky);
        if (!a && kz != 0.0f) bad++;
        if (!a) continue;
        zero += synth_dist_sample(0, kz, ky) != 0;
        int32_t prev = 0;
        for (int32_t x = 1; x < 400000; x += 1 + x / 64) {
            int32_t yp = synth_dist_sample(x, kz, ky), yn = synth_dist_sample(-x, kz, ky);
            bad += yn != -yp;
            mono += yp < prev;
            prev = yp;
        }
    }
    printf("dist_odd: f(-x) != -f(x) %d times, f(0) != 0 %d times, f falling %d times (amounts 1..127, x to 400000)\n",
           bad, zero, mono);
    CHECK(bad == 0 && zero == 0 && mono == 0);
}

/* THD of the curve: a 1 kHz sine (an exact number of cycles in the window, not quantised: the bus's integer
 * truncation is a noise floor of its own, ~1e-4 at -20 dBFS, that would hide the curve's first steps) through
 * synth_dist_f at amount a (0: the sine itself) */
static double thd(int a, double amp)
{
    enum { N = 4410, CYC = 100 };   /* 1 kHz at 44.1 kHz: harmonics up to 22 below Nyquist */
    static float y[N];
    float kz, ky;
    synth_dist_coef(a * 65536, &kz, &ky);
    for (int i = 0; i < N; i++) {
        float x = (float)(amp * sin(2 * M_PI * CYC * i / N));
        y[i] = a ? synth_dist_f(x, kz, ky) : x;
    }
    double h[23] = {0};
    for (int k = 1; k <= 22; k++) {
        double s = 0, c = 0;
        for (int i = 0; i < N; i++) s += y[i] * sin(2 * M_PI * k * CYC * i / N), c += y[i] * cos(2 * M_PI * k * CYC * i / N);
        h[k] = s * s + c * c;
    }
    double e = 0;
    for (int k = 2; k <= 22; k++) e += h[k];
    return sqrt(e / h[1]);
}
/* THD rises with the amount: strictly from one step to the next wherever it is over float's own floor (1e-6, -120 dB:
 * the first few steps at -20 dBFS add a cubic term under float precision), and never falls by more than that floor */
static void dist_thd_monotonic(void)
{
    static const double amp[2] = {16384, 3277};   /* -6 and -20 dBFS */
    int ok = 1;
    for (int j = 0; j < 2; j++) {
        double prev = thd(0, amp[j]), t0 = prev, t[5];
        int falls = 0, first = 0;
        for (int a = 1; a <= 127; a++) {
            double x = thd(a, amp[j]);
            if (x > 1e-6 && !first) first = a;
            if (x > 1e-6 ? !(x > prev) : x < prev - 1e-6) falls++, printf("  [%d: %.3g <= %.3g]", a, x, prev);
            prev = x;
            if (a == 16) t[0] = x;
            if (a == 32) t[1] = x;
            if (a == 64) t[2] = x;
            if (a == 96) t[3] = x;
            if (a == 127) t[4] = x;
        }
        printf("dist_thd_monotonic: sine at %.0f dBFS: THD at 0 %.1e, 16 %.3f %%, 32 %.2f %%, 64 %.1f %%, 96 %.1f %%, "
               "127 %.1f %%; over 1e-6 from amount %d; %d steps not rising\n", 20 * log10(amp[j] / 32767), t0,
               100 * t[0], 100 * t[1], 100 * t[2], 100 * t[3], 100 * t[4], first, falls);
        ok &= falls == 0 && t0 < 1e-6 && t[4] > 0.1 && first > 0 && first <= 16;
    }
    CHECK(ok);
}

/* max drive, six voices: the bus after the stage stays well inside int32 (no wrap), the output under the limiter's
 * ceiling, the mean (DC) over 10 s under -60 dBFS; levels at amounts 0, 32, 64, 96, 127 for the record */
static void dist_max_chord(void)
{
    static const int amts[5] = {0, 32, 64, 96, 127};
    int ok = 1;
    for (int pt = 0; pt < 3; pt++) {
        printf("dist_max_chord: %s:", pt == 0 ? "init chord C3..C5 vel 100" : pt == 1 ? "one note C4 vel 100" :
                                    "every level at full, pulses, sync, vel 127");
        for (int j = 0; j < 5; j++) {
            synth_init(&S);
            if (pt == 2) {
                set(&S, P_MIX_VCO2, 127), set(&S, P_MIX_SUB, 127), set(&S, P_MIX_NOISE, 127), set(&S, P_VCO1_SHAPE, 255);
                set(&S, P_VCO1_PW, 0), set(&S, P_VCO2_SHAPE, 254), set(&S, P_VCO2_PW, 255), set(&S, P_VCO1_SYNC, 1);
                set(&S, P_PRG_VOL, 127), set(&S, P_FLT_FREQ, 164);
            }
            set(&S, P_DISTORTION, amts[j]);
            if (pt == 1) synth_note(&S, 60, 100);
            else for (int k = 0; k < 6; k++) synth_note(&S, (int[]){48, 55, 60, 64, 67, 72}[k], pt == 2 ? 127 : 100);
            render(&S, A, FS / 5, 256);
            /* the bus after the stage, by hand for 1 s: its largest |value| */
            double busmax = 0;
            for (int f = 0; f < FS; f += VOICE_CTL) {
                int32_t bus[2 * VOICE_CTL] = {0};
                synth_tick(&S);
                S.ctx.pos = 0;
                synth_render_set(&S, SYNTH_ALL_VOICES, bus, VOICE_CTL);
                if (S.dist_kz != 0.0f) synth_dist(&S, bus, VOICE_CTL);
                for (int i = 0; i < 2 * VOICE_CTL; i++) if (fabs((double)bus[i]) > busmax) busmax = fabs((double)bus[i]);
            }
            S.ctl_left = 0;
            /* 10 s, streamed: peak, RMS, and the mean (DC; over 10 s: a distorted chord's intermodulation has products
             * of a fraction of a hertz, under the blocker's 5 Hz, that make a 1 s mean wander) */
            double p = 0, r = 0, m = 0;
            int32_t mx = 0;
            for (int sec = 0; sec < 10; sec++) {
                render(&S, A, FS, 256);
                for (int i = 0; i < 2 * FS; i++) {
                    double v = A[i];
                    if (abs(A[i]) > mx) mx = abs(A[i]);
                    r += v * v, m += v;
                }
            }
            p = mx;
            double n = 2.0 * FS * 10, mn = m / n;
            double pk = 20 * log10(p / 32767), rm = 10 * log10(r / n / (32767.0 * 32767));
            printf(" %d: peak %.1f RMS %.1f DC %.1f dBFS (bus %.1f)%s", amts[j], pk, rm,
                   20 * log10(fabs(mn) / 32767 + 1e-12), 20 * log10(busmax / 32767), j < 4 ? ";" : "\n");
            ok &= mx < SYNTH_LIMIT_CEIL && busmax < 1073741824.0 && fabs(mn) / 32767 < 0.001;
        }
    }
    CHECK(ok);
}

/* the stage is memoryless and its coefficients follow the control ticks: any split of the render calls gives the same
 * output (engine.c eng_render_arp cuts a block at arp steps and gates) */
static void dist_subblocks(void)
{
    static const uint32_t steps[4] = {256, 1, 7, 100};
    int diff = 0;
    for (int k = 1; k < 4; k++) {
        for (int t = 0; t < 2; t++) {
            synth_t *s = t ? &R : &S;
            int32_t *o = t ? B : A;
            synth_init(s);
            set(s, P_DISTORTION, 100);
            for (int j = 0; j < 6; j++) synth_note(s, 48 + 5 * j, 100);
            render(s, o, FS / 8, steps[t ? k : 0]);
            set(s, P_DISTORTION, 30);   /* (and a glide under way) */
            render(s, o + 2 * (FS / 8), FS / 8, steps[t ? k : 0]);
        }
        for (uint32_t i = 0; i < 4u * (FS / 8); i++) diff += A[i] != B[i];
    }
    printf("dist_subblocks: amount 100 then 30, calls of 1 / 7 / 100 frames against 256: %d samples differ\n", diff);
    CHECK(diff == 0);
}

/* a jump 0 -> 127 under a held chord: the amount glides (the smoothers' 5.8 ms), so the largest sample-to-sample step
 * around the jump is no larger than the waveform's own once settled */
static void dist_smoothed(void)
{
    synth_init(&S);
    for (int j = 0; j < 6; j++) synth_note(&S, 48 + 5 * j, 100);
    render(&S, A, FS / 5, 256);
    set(&S, P_DISTORTION, 127);
    render(&S, A, FS / 10, 256);
    int32_t jump = 0, settled = 0;
    for (int i = 1; i < FS / 10; i++)
        for (int c = 0; c < 2; c++) {
            int32_t d = abs(A[2 * i + c] - A[2 * (i - 1) + c]);
            if (i < FS / 50 && d > jump) jump = d;
            if (i >= FS / 20 && d > settled) settled = d;
        }
    printf("dist_smoothed: 0 -> 127 on a held chord: largest step in the first 20 ms %d, once settled %d; smoothed "
           "amount after 20 ms %.1f\n", jump, settled, S.sm[SM_DIST] / 65536.0);
    CHECK(jump <= settled + settled / 10 && S.sm[SM_DIST] == 127 * 65536);
}

int main(void)
{
    dist_param_live();
    dist_bypass_exact();
    dist_odd();
    dist_thd_monotonic();
    dist_max_chord();
    dist_subblocks();
    dist_smoothed();
    if (fails) { printf("test_dist: %d FAILED\n", fails); return 1; }
    printf("test_dist: all passed\n");
    return 0;
}
