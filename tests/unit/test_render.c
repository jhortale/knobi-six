/* SPDX-License-Identifier: GPL-3.0-only */
/* The audio path as app.c runs it (firmware/src/engine.c): the main loop's calls go into the event queue, eng_render
 * drains it and renders, in 256-frame blocks like app_audio. Review Focus 1 (voice_worst_case_bounded) and 2
 * (params_smoothed_no_step), and silence_costs_nothing; plus the queue itself (order, coalescing, program double
 * buffer, overflow policy). Measurements use doubles (host only). Every test prints what it measured. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "params/params.c"
#include "synth/osc.c"
#include "synth/filter.c"
#include "synth/env.c"
#include "synth/mod.c"
#include "synth/voice.c"
#include "synth/voices.c"
#include "engine.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define FS 44100
#define BLK 256
#define SECS_MAX 10
static int32_t OUT[2 * FS * SECS_MAX + 2 * BLK];

/* n frames (whole blocks) into OUT from frame 0, each block zeroed first, as the platform hands it over */
static void render(uint32_t frames)
{
    for (uint32_t f = 0; f < frames; f += BLK) {
        memset(&OUT[2 * f], 0, 2 * BLK * sizeof OUT[0]);
        eng_render(&OUT[2 * f], BLK);
    }
}
/* the largest |x[i] - x[i-1]| per channel over frames a..b */
static int32_t max_step(uint32_t a, uint32_t b)
{
    int32_t m = 0;
    for (uint32_t i = a + 1; i < b; i++)
        for (int c = 0; c < 2; c++) {
            int32_t d = abs(OUT[2 * i + c] - OUT[2 * (i - 1) + c]);
            if (d > m) m = d;
        }
    return m;
}

/* Review Focus 1: 6 voices, every mixer level at max, VCO2 -> VCO1 X-Mod at max (and every other X-Mod destination),
 * sync on, max resonance, LFO at 500 Hz on every destination at full depth (+ the mod wheel), aftertouch at full on
 * every destination, volume 127, velocity 127, 10 s. The output stays in +-32767 (the platform's clip never acts) and
 * the voice bus before the soft limiter keeps int32 headroom (no wrap). tests/scripts/worst.txt is the same patch. */
static const int16_t WORST[][2] = {
    {P_MIX_VCO1, 127}, {P_MIX_VCO2, 127}, {P_MIX_SUB, 127}, {P_MIX_NOISE, 127},
    {P_VCO1_SHAPE, 255}, {P_VCO1_PW, 0}, {P_VCO2_SHAPE, 254}, {P_VCO2_PW, 255}, {P_VCO1_SYNC, 1},
    {P_FLT_FREQ, 100}, {P_FLT_RESO, 255}, {P_FLT_ENV_AMT, 127}, {P_PRG_VOL, 127},
    {P_XM_VCO2, 127}, {P_XM_FENV, 127}, {P_XM_D_VCO1, 1}, {P_XM_D_SHAPE1, 1}, {P_XM_D_PW1, 1}, {P_XM_D_FFREQ, 1},
    {P_XM_D_FMODE, 1}, {P_XM_D_BP, 1},
    {P_LFO_FREQ, 254}, {P_LFO_SHAPE, 3}, {P_LFO_AMT, 255}, {P_LFO_D_VCO1, 1}, {P_LFO_D_VCO2, 1}, {P_LFO_D_PW, 3},
    {P_LFO_D_AMP, 1}, {P_LFO_D_FFREQ, 1}, {P_LFO_D_FMODE, 1},
    {P_AT_AMT, 127}, {P_AT_D_VCO1, 1}, {P_AT_D_VCO2, 1}, {P_AT_D_LFOAMT, 1}, {P_AT_D_AMP, 1}, {P_AT_D_FFREQ, 1},
    {P_AT_D_FMODE, 1},
};
static void worst_patch(void)
{
    eng_init();
    for (unsigned i = 0; i < sizeof WORST / sizeof WORST[0]; i++) CHECK(eng_param(WORST[i][0], WORST[i][1]) == 0);
    eng_wheel(127);
    eng_pressure(127);
    for (int k = 0; k < 6; k++) eng_note(36 + 7 * k, 127);
}
static void voice_worst_case_bounded(void)
{
    worst_patch();
    render(FS * SECS_MAX);
    int32_t mx = 0;
    double r = 0;
    for (uint32_t i = 0; i < 2u * FS * SECS_MAX; i++) {
        int32_t a = abs(OUT[i]);
        if (a > mx) mx = a;
        r += (double)OUT[i] * OUT[i];
    }
    double rms = 10 * log10(r / (2.0 * FS * SECS_MAX) / (32767.0 * 32767));
    int act = synth_active(&eng_synth);
    /* the bus before the limiter, 1 s more of the same patch: synth_render's loop by hand */
    int64_t bmax = 0;
    synth_t *s = &eng_synth;
    for (uint32_t f = 0; f < FS; f += VOICE_CTL) {
        int32_t bus[2 * VOICE_CTL] = {0};
        synth_tick(s);
        for (int i = 0; i < NVOICE; i++)
            if (s->voice[i].busy) voice_render(&s->voice[i], &s->ctx, s->prog.v, bus, VOICE_CTL);
        for (int i = 0; i < 2 * VOICE_CTL; i++) if (llabs(bus[i]) > bmax) bmax = llabs(bus[i]);
    }
    s->ctl_left = 0;
    printf("voice_worst_case_bounded: 6 voices 10 s: max |out| %d (limit 32767), RMS %.1f dBFS, %d voices sounding; "
           "bus before the limiter max |x| %lld (%.1f bits of int32 headroom left)\n", mx, rms, act, (long long)bmax,
           31 - log2((double)bmax));
    CHECK(mx <= 32767 && mx > 16384 && act == 6 && bmax < (1 << 28));
}

/* Review Focus 2: a parameter jumped while a note sounds (sent through the queue, as the main loop will) gives no
 * step larger than 2x the largest steady-state step (before the jump, or after it settles). The base tone is VCO 2's
 * triangle through the open filter (small steps), so a jump that is not smoothed shows. A control run loads the same
 * jump with the smoothing skipped (the smoothed values snapped to their targets): it must break the rule, or the
 * measurement proves nothing. */
typedef struct { const char *name; int16_t set[4][2]; int nset; int16_t jump[4][2]; int njump; int ctl; } jump_t;
static double jump_ratio(const jump_t *j, int snap, int32_t *s0o, int32_t *sto, int32_t *s1o)
{
    const uint32_t pre = FS * 3 / 10 / BLK * BLK, win = FS / 10 / BLK * BLK, settle = FS * 4 / 10 / BLK * BLK;
    eng_init();
    eng_param(P_MIX_VCO1, 0), eng_param(P_MIX_VCO2, 127), eng_param(P_VCO2_SHAPE, 0);
    for (int i = 0; i < j->nset; i++) eng_param(j->set[i][0], j->set[i][1]);
    eng_note(45, 100);
    render(pre);
    int32_t s0 = max_step(pre - win, pre);
    for (int i = 0; i < j->njump; i++) eng_param(j->jump[i][0], j->jump[i][1]);
    if (snap) {                               /* the control: the jump applied now, smoothing skipped */
        eng_drain();
        for (int i = 0; i < SM_COUNT; i++) eng_synth.sm[i] = eng_synth.tgt[i];
    }
    memmove(OUT, &OUT[2 * (pre - BLK)], 2 * BLK * sizeof OUT[0]);   /* keep the last block: the step across it */
    for (uint32_t f = BLK; f < BLK + settle + win; f += BLK) {
        memset(&OUT[2 * f], 0, 2 * BLK * sizeof OUT[0]);
        eng_render(&OUT[2 * f], BLK);
    }
    int32_t st = max_step(BLK - 1, BLK + win), s1 = max_step(BLK + settle, BLK + settle + win);
    *s0o = s0, *sto = st, *s1o = s1;
    return (double)st / (s0 > s1 ? s0 : s1);
}
static void params_smoothed_no_step(void)
{
    static const jump_t J[] = {
        {"FILTER FREQ 164 -> 40", {{0}}, 0, {{P_FLT_FREQ, 40}}, 1, 0},
        {"FILTER FREQ 30 -> 164", {{P_FLT_FREQ, 30}}, 1, {{P_FLT_FREQ, 164}}, 1, 0},
        {"MODE LP -> HP (127)", {{0}}, 0, {{P_FLT_MODE, 127}}, 1, 1},
        {"BP on", {{0}}, 0, {{P_FLT_BP, 1}}, 1, 0},
        {"VCO2 SHAPE tri -> pulse", {{0}}, 0, {{P_VCO2_SHAPE, 254}}, 1, 0},
        {"VCO1 SHAPE saw -> pulse (VCO1 at 127)", {{P_MIX_VCO1, 127}}, 1, {{P_VCO1_SHAPE, 255}}, 1, 0},
        {"VCO2 PW 127 -> 10 (pulse)", {{P_VCO2_SHAPE, 254}}, 1, {{P_VCO2_PW, 10}}, 1, 0},
        {"VCO2 LEVEL 127 -> 20", {{0}}, 0, {{P_MIX_VCO2, 20}}, 1, 1},
        {"VCO2 LEVEL 20 -> 127", {{P_MIX_VCO2, 20}}, 1, {{P_MIX_VCO2, 127}}, 1, 1},
        {"SUB + NOISE 0 -> 127, VCO1 0 -> 127", {{0}}, 0, {{P_MIX_SUB, 127}, {P_MIX_NOISE, 127}, {P_MIX_VCO1, 127}}, 3, 0},
    };
    for (unsigned k = 0; k < sizeof J / sizeof J[0]; k++) {
        int32_t s0, st, s1;
        double r = jump_ratio(&J[k], 0, &s0, &st, &s1);
        printf("params_smoothed_no_step: %-38s steady %5d / %5d, jump %5d: %.2f x", J[k].name, s0, s1, st, r);
        CHECK(r <= 2.0);
        if (J[k].ctl) {
            int32_t c0, ct, c1;
            double rc = jump_ratio(&J[k], 1, &c0, &ct, &c1);
            printf("; unsmoothed %.2f x", rc);
            CHECK(rc > 2.0);
        }
        printf("\n");
    }
}

/* no voice: the render adds nothing (out stays zero, before any note and after the last release frees its voice),
 * and the block costs next to nothing: under 5 % of one voice's */
static void silence_costs_nothing(void)
{
    eng_init();
    render(FS);
    int nz = 0;
    for (uint32_t i = 0; i < 2u * FS; i++) nz += OUT[i] != 0;
    eng_param(P_LENV_R, 10);
    eng_note(60, 100);
    render(FS / 2);
    eng_note(60, 0);
    render(FS);                               /* the release ends and the voice is freed */
    int act = synth_active(&eng_synth);
    render(FS);
    int nz2 = 0;
    for (uint32_t i = 0; i < 2u * FS; i++) nz2 += OUT[i] != 0;
    const int blocks = 20000;
    int32_t blk[2 * BLK];
    clock_t t0 = clock();
    for (int b = 0; b < blocks; b++) eng_render(blk, BLK);
    double silent = (double)(clock() - t0);
    eng_note(60, 100);
    t0 = clock();
    for (int b = 0; b < blocks; b++) eng_render(blk, BLK);
    double one = (double)(clock() - t0);
    printf("silence_costs_nothing: nonzero samples before any note %d, after the release %d (voices %d); a silent "
           "block costs %.2f %% of a one-voice block (%.0f ns vs %.0f ns)\n", nz, nz2, act, 100 * silent / one,
           1e9 * silent / CLOCKS_PER_SEC / blocks, 1e9 * one / CLOCKS_PER_SEC / blocks);
    CHECK(nz == 0 && nz2 == 0 && act == 0 && silent < 0.05 * one);
}

/* the queue: events reach the synth in order at the next render, and not before; unchanged values are not queued;
 * a full queue drops the newest event and counts it; a program load waits for the previous one */
static void event_queue(void)
{
    eng_init();
    eng_note(60, 100), eng_note(64, 100), eng_note(60, 0);
    int before = synth_active(&eng_synth);
    render(BLK);
    int after = synth_active(&eng_synth), k64 = 0, k60 = 0;
    for (int i = 0; i < NVOICE; i++)
        if (eng_synth.voice[i].busy && eng_synth.voice[i].key) k64 += eng_synth.voice[i].note == 64, k60 += eng_synth.voice[i].note == 60;
    uint32_t q0 = eng_qw;
    eng_bend(8192), eng_wheel(0), eng_pressure(0), eng_param(P_FLT_FREQ, 164);   /* all unchanged: nothing queued */
    int coalesced = eng_qw == q0;
    eng_bend(12000);
    render(BLK);
    int bent = eng_synth.bend14 == 12000;
    /* overflow: ENG_QLEN events fit, the next is dropped and counted */
    eng_init();
    int ok = 0;
    for (unsigned i = 0; i < ENG_QLEN; i++) ok += eng_note(40 + (int)(i & 31), (int)(i & 1) ? 0 : 100) == 0;
    int over = eng_note(90, 100);
    uint32_t drops = eng_drops;
    render(BLK);
    int drained = eng_qr == eng_qw, more = eng_note(91, 100) == 0;
    /* program double buffer: a second load before the first is rendered is refused (the caller retries) */
    program_t p;
    program_init(&p);
    p.v[P_FLT_FREQ] = 50;
    int l1 = eng_program(&p);
    p.v[P_FLT_FREQ] = 60;
    int l2 = eng_program(&p);
    render(BLK);
    int got1 = eng_synth.prog.v[P_FLT_FREQ];
    int l3 = eng_program(&p);
    render(BLK);
    int got2 = eng_synth.prog.v[P_FLT_FREQ];
    /* HOLD latches a released key; PANIC silences at once (and eng_active, the main loop's view, follows) */
    eng_hold(1), eng_note(70, 100), eng_note(70, 0);
    render(FS / 10);
    int held = eng_active();
    eng_panic();
    render(BLK);
    int panicked = eng_active();
    printf("event_queue: before render %d voices, after %d (60 on+off, 64 held: %d/%d); unchanged bend/wheel/pressure/"
           "param queued nothing %d; bend arrives %d; %d of %u fit, the next %d (drops %u), drained %d, then %d; "
           "program loads %d %d(busy) %d -> FREQ %d then %d; HOLD keeps %d voice, PANIC leaves %d\n", before, after,
           k60, k64, coalesced, bent, ok, ENG_QLEN, over, drops, drained, more, l1, l2, l3, got1, got2, held, panicked);
    CHECK(before == 0 && after == 2 && k64 == 1 && k60 == 0 && coalesced && bent);
    CHECK(ok == (int)ENG_QLEN && over == -1 && drops == 1 && drained && more);
    CHECK(l1 == 0 && l2 == -1 && l3 == 0 && got1 == 50 && got2 == 60);
    CHECK(held == 1 && panicked == 0);
}

int main(void)
{
    voice_worst_case_bounded();
    params_smoothed_no_step();
    silence_costs_nothing();
    event_queue();
    if (fails) { printf("test_render: %d FAILED\n", fails); return 1; }
    printf("test_render: all passed\n");
    return 0;
}
