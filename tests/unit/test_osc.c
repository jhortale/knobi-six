/* SPDX-License-Identifier: GPL-3.0-only */
/* Oscillator section (firmware/src/synth/osc.c): pitch, shapes, pulse width, sub, noise, hard sync, aliasing.
 * The measurements use doubles (host only): a 65536-point FFT with a 4-term Blackman-Harris window (side lobes
 * -92 dB), so a harmonic's level is the peak of its main lobe. Every test prints what it measured. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "synth/osc.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define FS 44100.0
#define OSC_PI 3.14159265358979323846
#define N 65536
#define NQ16(n) ((int32_t)lrint((n) * 65536.0))

static double sig[N], re[N], im[N], mag[N / 2];

static void fft(double *x, double *y, int n)
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t = x[i]; x[i] = x[j]; x[j] = t; t = y[i]; y[i] = y[j]; y[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double a = -2 * OSC_PI / len, wr = cos(a), wi = sin(a);
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                double ur = x[i + k], ui = y[i + k];
                double vr = x[i + k + len / 2] * cr - y[i + k + len / 2] * ci;
                double vi = x[i + k + len / 2] * ci + y[i + k + len / 2] * cr;
                x[i + k] = ur + vr; y[i + k] = ui + vi;
                x[i + k + len / 2] = ur - vr; y[i + k + len / 2] = ui - vi;
                double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
}

/* magnitude spectrum of sig[0..N) (Blackman-Harris), into mag[] */
static void spectrum(void)
{
    for (int i = 0; i < N; i++) {
        double p = 2 * OSC_PI * i / (N - 1);
        double w = 0.35875 - 0.48829 * cos(p) + 0.14128 * cos(2 * p) - 0.01168 * cos(3 * p);
        re[i] = sig[i] * w; im[i] = 0;
    }
    fft(re, im, N);
    for (int i = 0; i < N / 2; i++) mag[i] = sqrt(re[i] * re[i] + im[i] * im[i]) + 1e-12;
}

static double level_db(double f)   /* peak of the main lobe around f */
{
    int c = (int)lrint(f * N / FS);
    if (c < 1) c = 1;
    if (c > N / 2 - 1) c = N / 2 - 1;
    int best = c;
    for (int b = c - 3; b <= c + 3; b++)
        if (b > 0 && b < N / 2 && mag[b] > mag[best]) best = b;
    return 20 * log10(mag[best]);
}
static double harm_db(double f, int k) { return level_db(k * f) - level_db(f); }

/* the strongest component that is not a harmonic of f (nor DC), in dB relative to the fundamental */
static double worst_alias(double f, double *where)
{
    double ref = level_db(f), worst = -400;
    for (int b = (int)(40 * N / FS); b < N / 2; b++) {
        double hz = b * FS / N, k = floor(hz / f + 0.5);
        if (k >= 1 && fabs(hz - k * f) * N / FS <= 10) continue;   /* a harmonic's main lobe and skirt */
        double d = 20 * log10(mag[b]) - ref;
        if (d > worst) { worst = d; *where = hz; }
    }
    return worst;
}

static double hz_of(uint32_t inc) { return inc * FS / 4294967296.0; }

/* render N samples of output 'which' */
static void render(osc_t *o, int which)
{
    int32_t out[4];
    for (int i = 0; i < N; i++) { osc_tick(o, out); sig[i] = out[which]; }
}
static void setup(osc_t *o, double n1, double n2, int s1, int p1, int s2, int p2, int sync)
{
    osc_init(o, 12345);
    osc_set_pitch(o, NQ16(n1), NQ16(n2));
    osc_set_shape(o, s1, p1, s2, p2);
    o->sync = (uint8_t)sync;
}

static void pitch_a4(void)
{
    osc_t o;
    setup(&o, 69, 69, 0, 127, 0, 127, 0);
    for (int which = OSC_VCO1; which <= OSC_VCO2; which++) {
        render(&o, which);
        spectrum();
        int best = 1;
        for (int b = 1; b < (int)(2000 * N / FS); b++) if (mag[b] > mag[best]) best = b;
        double a = log(mag[best - 1]), b = log(mag[best]), c = log(mag[best + 1]);
        double hz = (best + 0.5 * (a - c) / (a - 2 * b + c)) * FS / N;
        printf("pitch_a4: VCO%d %.3f Hz (inc %.4f Hz)\n", which + 1, hz, hz_of(which ? o.inc2 : o.inc1));
        CHECK(fabs(hz / 440 - 1) < 0.005);
    }
    /* note_to_inc against the exact formula over the whole range, in cents */
    double worst = 0, lsb = 0;
    for (double n = -60; n <= 135; n += 0.37) {
        double exact = 4294967296.0 * 440 * pow(2, (n - 69) / 12) / FS, inc = note_to_inc(NQ16(n));
        double c = 1200 * log2(inc / exact);
        if (n >= 0 && fabs(c) > worst) worst = fabs(c);
        if (n < 0 && fabs(inc - exact) > lsb) lsb = fabs(inc - exact);
    }
    printf("pitch_a4: note_to_inc worst error %.5f cents over notes 0..135; %.3f increment steps (absolute) below note 0\n",
           worst, lsb);
    CHECK(worst < 0.01 && lsb <= 1.5);
    CHECK(note_to_inc(NQ16(200)) == note_to_inc(NQ16(135)) && note_to_inc(NQ16(-300)) == note_to_inc(NQ16(-60)));
}

static void vco1_shape_ends(void)
{
    osc_t o;
    setup(&o, 57, 57, 0, 127, 0, 127, 0);
    render(&o, OSC_VCO1); spectrum();
    double f = hz_of(o.inc1), h2 = harm_db(f, 2), h3 = harm_db(f, 3);
    printf("vco1_shape_ends: shape 0 (saw) H2 %.2f dB, H3 %.2f dB\n", h2, h3);
    CHECK(fabs(h2 + 6.02) <= 1 && fabs(h3 + 9.54) <= 1);
    setup(&o, 57, 57, 255, 127, 0, 127, 0);
    render(&o, OSC_VCO1); spectrum();
    double e = fmax(harm_db(f, 2), fmax(harm_db(f, 4), harm_db(f, 6)));
    h3 = harm_db(f, 3);
    printf("vco1_shape_ends: shape 255 PW 127 (square) worst even harmonic %.2f dB, H3 %.2f dB\n", e, h3);
    CHECK(e < -30 && fabs(h3 + 9.54) <= 1);
    setup(&o, 57, 57, 128, 127, 0, 127, 0);   /* in between: a blend, the 2nd harmonic between saw and square */
    render(&o, OSC_VCO1); spectrum();
    h2 = harm_db(f, 2);
    printf("vco1_shape_ends: shape 128 (blend) H2 %.2f dB\n", h2);
    CHECK(h2 < -8 && h2 > -30);
}

static void vco2_shape_points(void)
{
    osc_t o;
    setup(&o, 57, 57, 0, 127, 0, 127, 0);
    render(&o, OSC_VCO2); spectrum();
    double f = hz_of(o.inc2), h2 = harm_db(f, 2), h3 = harm_db(f, 3), h5 = harm_db(f, 5);
    printf("vco2_shape_points: shape 0 (triangle) H2 %.2f dB, H3 %.2f dB, H5 %.2f dB\n", h2, h3, h5);
    CHECK(fabs(h3 + 19.08) <= 1.5 && fabs(h5 + 27.96) <= 1.5 && h2 < -40);
    setup(&o, 57, 57, 0, 127, 127, 127, 0);
    render(&o, OSC_VCO2); spectrum();
    h2 = harm_db(f, 2); h3 = harm_db(f, 3);
    printf("vco2_shape_points: shape 127 (saw) H2 %.2f dB, H3 %.2f dB\n", h2, h3);
    CHECK(fabs(h2 + 6.02) <= 1 && fabs(h3 + 9.54) <= 1);
    setup(&o, 57, 57, 0, 127, 254, 127, 0);
    render(&o, OSC_VCO2); spectrum();
    double e = fmax(harm_db(f, 2), fmax(harm_db(f, 4), harm_db(f, 6)));
    h3 = harm_db(f, 3);
    printf("vco2_shape_points: shape 254 PW 127 (square) worst even harmonic %.2f dB, H3 %.2f dB\n", e, h3);
    CHECK(e < -30 && fabs(h3 + 9.54) <= 1);
}

static void pw_symmetric(void)
{
    static const int d[] = { 20, 60, 127 };
    double worst = 0;
    for (int v = 0; v < 2; v++)
        for (unsigned i = 0; i < sizeof d / sizeof *d; i++) {
            double lv[2][9];
            for (int side = 0; side < 2; side++) {
                int pw = side ? 127 + d[i] : 127 - d[i];
                osc_t o;
                setup(&o, 57, 57, 255, pw, 254, pw, 0);
                render(&o, v ? OSC_VCO2 : OSC_VCO1); spectrum();
                double f = hz_of(v ? o.inc2 : o.inc1);
                for (int k = 1; k <= 8; k++) lv[side][k] = level_db(k * f);
            }
            for (int k = 1; k <= 8; k++) if (fabs(lv[0][k] - lv[1][k]) > worst) worst = fabs(lv[0][k] - lv[1][k]);
            if (d[i] == 127) {   /* both ends are thin pulses: the 2nd harmonic is close to the fundamental */
                printf("pw_symmetric: VCO%d PW 0 / 254: H2 %.2f / %.2f dB\n", v + 1, lv[0][2] - lv[0][1], lv[1][2] - lv[1][1]);
                CHECK(lv[0][2] - lv[0][1] > -1 && lv[1][2] - lv[1][1] > -1);
            }
        }
    printf("pw_symmetric: PW 127 +- d, H1..H8, worst difference %.4f dB\n", worst);
    CHECK(worst < 0.1);
}

static void sub_octave(void)
{
    osc_t o;
    setup(&o, 69, 69, 0, 127, 0, 127, 0);
    render(&o, OSC_SUB); spectrum();
    double f = hz_of(o.inc1) / 2, s = level_db(f), at = level_db(2 * f) - s, h3 = harm_db(f, 3);
    printf("sub_octave: %.2f Hz, VCO1 frequency %.2f dB, H3 %.2f dB\n", f, at, h3);
    CHECK(at < -30 && fabs(h3 + 9.54) <= 1);
    double peak = 0;
    for (int i = 0; i < N; i++) if (fabs(sig[i]) > peak) peak = fabs(sig[i]);
    CHECK(peak <= OSC_ONE && peak > OSC_ONE * 0.99);
}

/* naive hard-synced saw (no band-limiting), the reference for the sync alias level */
static void naive_sync(double f1, double f2)
{
    double p1 = 0, p2 = 0;
    for (int i = 0; i < N; i++) {
        sig[i] = (2 * p1 - 1) * OSC_ONE;
        p1 += f1 / FS; p2 += f2 / FS;
        if (p2 >= 1) { p2 -= 1; p1 = p2 * f1 / f2; } else if (p1 >= 1) p1 -= 1;
    }
}

/* the strongest non-harmonic component of f below 'top' Hz, in dB relative to the strongest component */
static double alias_below(double f, double top, double *where)
{
    double ref = -400, worst = -400;
    for (int b = (int)(40 * N / FS); b < N / 2; b++) if (20 * log10(mag[b]) > ref) ref = 20 * log10(mag[b]);
    for (int b = (int)(40 * N / FS); b < (int)(top * N / FS) && b < N / 2; b++) {
        double hz = b * FS / N, k = floor(hz / f + 0.5);
        if (k >= 1 && fabs(hz - k * f) * N / FS <= 10) continue;
        double d = 20 * log10(mag[b]) - ref;
        if (d > worst) { worst = d; *where = hz; }
    }
    return worst;
}

static void sync_locks_to_vco2(void)
{
    osc_t o;
    setup(&o, 64, 57, 0, 127, 0, 127, 1);   /* VCO1 seven semitones above VCO2, synced */
    render(&o, OSC_VCO1);
    double f2 = hz_of(o.inc2), f1 = hz_of(o.inc1), best = -1;
    int lag = 0;
    for (int l = 50; l < 400; l++) {
        double c = 0, e = 0;
        for (int i = 0; i < 20000; i++) { c += sig[i] * sig[i + l]; e += sig[i + l] * sig[i + l]; }
        c /= sqrt(e);
        if (c > best) { best = c; lag = l; }
    }
    spectrum();
    double at1 = level_db(f1) - level_db(f2);
    printf("sync_locks_to_vco2: period %d samples (VCO2 %.2f, VCO1 free %.2f), VCO1's own frequency %.2f dB\n",
           lag, FS / f2, FS / f1, at1);
    CHECK(fabs(lag - FS / f2) <= 1 && at1 < -40);
    o.sync = 0;
    render(&o, OSC_VCO1); spectrum();
    at1 = level_db(f1) - level_db(f2);
    printf("sync_locks_to_vco2: sync off: VCO1's own frequency %+.2f dB over VCO2's\n", at1);
    CHECK(at1 > 30);
}

/* The PolyBLEP with this kernel is exact: every output equals the naive (continuous) waveform convolved with the
 * cubic B-spline, sampled. Checked against a numerical convolution of the naive waveform on the same phases. This
 * proves each step and corner, the sync restarts included, has the right height and the right place. */
static double bspline(double x)
{
    x = fabs(x);
    return x >= 2 ? 0 : x >= 1 ? (2 - x) * (2 - x) * (2 - x) / 6 : 2.0 / 3 - x * x + x * x * x / 2;
}
static double ramp(double p) { p -= floor(p); return 2 * p - 1; }
static double naive_at(const osc_t *o, int vco, double t)   /* t in samples from the start (phases 0 at t = 0) */
{
    double dt1 = o->inc1 / 4294967296.0, dt2 = o->inc2 / 4294967296.0;
    if (vco == 2) {
        double p = t * dt2 - floor(t * dt2), tri = 1 - 4 * fabs(p - 0.5), b = o->b2 / 1073741824.0;
        return tri + b * (ramp(p) - tri) - o->a2 / 1073741824.0 * ramp(p - o->w2 / 4294967296.0);
    }
    double p = o->sync ? (t - floor(t * dt2) / dt2) * dt1 : t * dt1;
    if (vco == 3) {   /* the sub flips at every VCO 1 wrap and restart */
        double flips = o->sync ? floor(t * dt2) * (1 + floor(dt1 / dt2)) + floor(p) : floor(p);
        return fmod(flips, 2) ? -1 : 1;
    }
    return ramp(p) - o->a1 / 1073741824.0 * ramp(p - o->w1 / 4294967296.0);
}
static void bspline_exact(void)
{
    static const struct { int vco; double n1, n2; int s1, p1, s2, p2, sync; } c[] = {
        { 1, 64, 57, 0, 127, 0, 127, 0 }, { 1, 100.2, 57, 255, 30, 0, 127, 0 }, { 1, 100.2, 57, 140, 220, 0, 127, 0 },
        { 2, 57, 100.2, 0, 127, 0, 127, 0 }, { 2, 57, 100.2, 0, 127, 60, 127, 0 }, { 2, 57, 100.2, 0, 127, 200, 40, 0 },
        { 1, 64, 57, 0, 127, 0, 127, 1 }, { 1, 103.3, 84, 0, 127, 0, 127, 1 }, { 1, 103.3, 84, 255, 60, 0, 127, 1 },
        { 1, 103.3, 84, 140, 200, 0, 127, 1 }, { 1, 70, 95, 255, 20, 0, 127, 1 }, { 1, 122, 111, 255, 250, 0, 127, 1 },
        { 3, 100.2, 57, 0, 127, 0, 127, 0 }, { 3, 64, 57, 0, 127, 0, 127, 1 }, { 3, 103.3, 84, 0, 127, 0, 127, 1 },
        { 3, 70, 95, 0, 127, 0, 127, 1 }, { 1, 120, 100, 255, 5, 0, 127, 1 }, { 3, 120, 100, 0, 127, 0, 127, 1 },
    };
    double worst = 0;
    for (unsigned k = 0; k < sizeof c / sizeof *c; k++) {
        osc_t o;
        setup(&o, c[k].n1, c[k].n2, c[k].s1, c[k].p1, c[k].s2, c[k].p2, c[k].sync);
        int32_t out[4];
        for (int n = 0; n < 1200; n++) {
            osc_tick(&o, out);
            if (n < 4) continue;   /* the kernel reaches before the start */
            double acc = 0;
            const int m = 16000;
            for (int j = 0; j < m; j++) {
                double tau = -2 + 4.0 * (j + 0.5) / m;
                acc += bspline(tau) * naive_at(&o, c[k].vco, n - tau);
            }
            double e = fabs(out[c[k].vco == 3 ? OSC_SUB : c[k].vco - 1] / (double)OSC_ONE - acc * 4 / m);
            if (e > worst) worst = e;
        }
    }
    printf("bspline_exact: saw, pulse, blends, triangle, sync, sub (18 cases): worst deviation %.6f of full scale\n", worst);
    CHECK(worst < 0.001);
}

/* hard sync: the alias level against the naive sync and against a free-running VCO 1 at the same frequency */
static void sync_alias(void)
{
    static const double ratio[] = { 7, 19.3, 31.7 };   /* VCO1 above VCO2, semitones */
    static const double base[] = { 57, 84 };           /* VCO2 at A3 and C6 */
    for (unsigned b = 0; b < 2; b++)
        for (unsigned r = 0; r < 3; r++) {
            osc_t o;
            double w, ours[2], naive[2], free[2];
            setup(&o, base[b] + ratio[r], base[b], 0, 127, 0, 127, 1);
            double f2 = hz_of(o.inc2), f1 = hz_of(o.inc1);
            render(&o, OSC_VCO1); spectrum();
            ours[0] = alias_below(f2, FS / 2, &w), ours[1] = alias_below(f2, 10000, &w);
            naive_sync(f1, f2); spectrum();
            naive[0] = alias_below(f2, FS / 2, &w), naive[1] = alias_below(f2, 10000, &w);
            o.sync = 0;
            render(&o, OSC_VCO1); spectrum();
            free[0] = alias_below(f1, FS / 2, &w), free[1] = alias_below(f1, 10000, &w);
            printf("sync_alias: VCO2 %6.1f Hz, VCO1 +%4.1f st: worst alias %.1f dB (below 10 kHz %.1f); naive sync %.1f (%.1f);"
                   " free VCO1 %.1f (%.1f)\n", f2, ratio[r], ours[0], ours[1], naive[0], naive[1], free[0], free[1]);
            CHECK(ours[0] < naive[0] - 15 && ours[1] < naive[1] - 30);
            CHECK(ours[0] < free[0] + 3 && ours[1] < free[1] + 6);
        }
}

static void alias_saw_c8(void)
{
    osc_t o;
    double w;
    setup(&o, 108, 108, 0, 127, 0, 127, 0);
    render(&o, OSC_VCO1); spectrum();
    double f = hz_of(o.inc1), fund = level_db(f), alias = level_db(FS - 6 * f), worst = worst_alias(f, &w);
    printf("alias_saw_c8: alias@%.0f %.2f dB below the fundamental; worst alias %.2f dB (at %.0f Hz)\n",
           FS - 6 * f, fund - alias, worst, w);
    CHECK(fund - alias >= 30);
}

static void alias_tri_c8(void)
{
    osc_t o;
    double w;
    setup(&o, 108, 108, 0, 127, 0, 127, 0);
    render(&o, OSC_VCO2); spectrum();
    double f = hz_of(o.inc2), fund = level_db(f), alias = level_db(FS - 7 * f), worst = worst_alias(f, &w);
    printf("alias_tri_c8: alias@%.0f %.2f dB below the fundamental; worst alias %.2f dB (at %.0f Hz)\n",
           FS - 7 * f, fund - alias, worst, w);
    for (int i = 0; i < N; i++) {   /* the same triangle without the BLAMP, for scale */
        double p = fmod(i * f / FS, 1);
        sig[i] = (1 - 4 * fabs(p - 0.5)) * OSC_ONE;
    }
    spectrum();
    double naive = worst_alias(f, &w);
    printf("alias_tri_c8: naive triangle worst alias %.2f dB (at %.0f Hz)\n", naive, w);
    CHECK(fund - alias >= 50 && worst < -45 && worst < naive - 15);
}

static void alias_pulse_c8(void)
{
    static const int pw[] = { 127, 40, 230 };
    for (unsigned i = 0; i < 3; i++) {
        osc_t o;
        double w;
        setup(&o, 108, 108, 255, pw[i], 254, pw[i], 0);
        render(&o, OSC_VCO1); spectrum();
        double f = hz_of(o.inc1), fund = level_db(f), alias = level_db(FS - 7 * f), worst = worst_alias(f, &w);
        printf("alias_pulse_c8: PW %d alias@%.0f %.2f dB below; worst alias %.2f dB (at %.0f Hz)\n",
               pw[i], FS - 7 * f, fund - alias, worst, w);
        CHECK(fund - alias >= 30 && worst < -25);
    }
}

static void noise_flat(void)
{
    osc_t o, p;
    setup(&o, 69, 69, 0, 127, 0, 127, 0);
    osc_init(&p, 777);
    int32_t out[4], q[4];
    double mean = 0, corr = 0, e = 0;
    static double other[N];
    for (int i = 0; i < N; i++) {
        osc_tick(&o, out); osc_tick(&p, q);
        sig[i] = out[OSC_NOISE]; other[i] = q[OSC_NOISE];
        mean += sig[i]; corr += sig[i] * other[i]; e += sig[i] * sig[i];
        CHECK(abs(out[OSC_NOISE]) <= OSC_ONE);
    }
    mean /= N; corr /= e;
    /* Welch: 64 Hann frames of 1024 */
    static double pw[512];
    for (int f = 0; f < 64; f++) {
        for (int i = 0; i < 1024; i++) {
            re[i] = sig[f * 1024 + i] * (0.5 - 0.5 * cos(2 * OSC_PI * i / 1023)); im[i] = 0;
        }
        fft(re, im, 1024);
        for (int i = 0; i < 512; i++) pw[i] += re[i] * re[i] + im[i] * im[i];
    }
    double band[4], avg = 0;
    for (int b = 0; b < 4; b++) {   /* octaves from 1378 Hz: bins 32..64, 64..128, 128..256, 256..512 */
        band[b] = 0;
        for (int i = 32 << b; i < 64 << b; i++) band[b] += pw[i];
        band[b] /= 32 << b;
        avg += band[b] / 4;
    }
    double dev = 0;
    for (int b = 0; b < 4; b++) if (fabs(10 * log10(band[b] / avg)) > dev) dev = fabs(10 * log10(band[b] / avg));
    printf("noise_flat: mean %.2f (of %d), octave bands within %.2f dB, voice-to-voice correlation %.4f\n",
           mean, OSC_ONE, dev, corr);
    CHECK(fabs(mean) < 0.02 * OSC_ONE && dev < 3 && fabs(corr) < 0.05);
}

/* every output stays in Q15 for random pitches, shapes and sync */
static void bounds(void)
{
    srand(7);
    int32_t peak[4] = { 0 };
    for (int trial = 0; trial < 300; trial++) {
        osc_t o;
        osc_init(&o, (uint32_t)trial + 1);
        osc_set_pitch(&o, (rand() % (195 * 64) - 60 * 64) * 1024, (rand() % (195 * 64) - 60 * 64) * 1024);
        osc_set_shape(&o, rand() % 256, rand() % 256, rand() % 255, rand() % 256);
        o.sync = (uint8_t)(rand() & 1);
        o.ph1 = (uint32_t)rand() * 2654435761u; o.ph2 = (uint32_t)rand() * 2246822519u;
        int32_t out[4];
        for (int i = 0; i < 3000; i++) {
            osc_tick(&o, out);
            for (int k = 0; k < 4; k++) if (abs(out[k]) > peak[k]) peak[k] = abs(out[k]);
        }
    }
    printf("bounds: peaks VCO1 %d, VCO2 %d, SUB %d, NOISE %d\n", peak[0], peak[1], peak[2], peak[3]);
    CHECK(peak[0] <= 32767 && peak[1] <= 32767 && peak[2] <= OSC_ONE && peak[3] <= OSC_ONE);
    CHECK(peak[0] <= 31100 && peak[1] <= 31100);   /* thin pulse 1.893 x OSC_ONE, plus rounding */
}

/* host time per voice-sample (a proxy for the device; the real figure comes from the emulator) */
static void bench(void)
{
    static const struct { const char *name; int s1, p1, s2, p2, sync; double n1, n2; } c[] = {
        { "saw + triangle (init)", 0, 127, 0, 127, 0, 60, 60 },
        { "pulse + pulse", 255, 60, 254, 60, 0, 60, 60 },
        { "blend + blend", 100, 60, 190, 60, 0, 60, 67 },
        { "sync saw, VCO2 C4", 0, 127, 0, 127, 1, 79, 60 },
        { "sync pulse, VCO2 C6", 255, 60, 254, 60, 1, 103, 84 },
    };
    for (unsigned i = 0; i < sizeof c / sizeof *c; i++) {
        osc_t o;
        setup(&o, c[i].n1, c[i].n2, c[i].s1, c[i].p1, c[i].s2, c[i].p2, c[i].sync);
        int32_t out[4];
        volatile int32_t sink = 0;
        long n = 4000000;
        clock_t t0 = clock();
        for (long k = 0; k < n; k++) { osc_tick(&o, out); sink += out[0] + out[1] + out[2] + out[3]; }
        double ns = (double)(clock() - t0) / CLOCKS_PER_SEC * 1e9 / n;
        printf("bench: %-24s %.2f ns per voice-sample (host)\n", c[i].name, ns);
    }
}

int main(void)
{
    pitch_a4();
    vco1_shape_ends();
    vco2_shape_points();
    pw_symmetric();
    sub_octave();
    sync_locks_to_vco2();
    bspline_exact();
    sync_alias();
    alias_saw_c8();
    alias_tri_c8();
    alias_pulse_c8();
    noise_flat();
    bounds();
    if (getenv("OSC_BENCH")) bench();
    printf(fails ? "test_osc: %d FAILED\n" : "test_osc: ok\n", fails);
    return fails != 0;
}
