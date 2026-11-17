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
    /* overflow: ENG_QLEN - ENG_RESERVE note-ons fit, the next is refused and counted (the reserve is the note-offs') */
    eng_init();
    int ok = 0;
    for (unsigned i = 0; i < ENG_QLEN; i++) ok += eng_note(40 + (int)(i & 31), 100) == 0;
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
    CHECK(ok == (int)(ENG_QLEN - ENG_RESERVE) && over == -1 && drops == ENG_RESERVE + 1u && drained && more);
    CHECK(l1 == 0 && l2 == -1 && l3 == 0 && got1 == 50 && got2 == 60);
    CHECK(held == 1 && panicked == 0);
}

/* Task 7 review, folded into Task 8: a note-off is never lost. The last ENG_RESERVE slots take only note-offs (and
 * PANIC / HOLD): note-ons are refused first. A note-off that finds even the reserve full is kept (eng_off_pend) and
 * eng_flush (the main loop, every pass) queues it once there is room; a note-on of the same note waits for it. */
static void queue_never_drops_note_off(void)
{
    eng_init();
    eng_param(P_LENV_R, 0);
    render(BLK);
    int on = 0, off = 0, deferred = 0;
    for (int n = 0; n < 256; n++) on += eng_note(n & 127, 100) == 0;    /* 256 note-ons: only QLEN - RESERVE fit */
    for (int n = 0; n < 128; n++) { int r = eng_note(n, 0); off += r == 0; deferred += r == 1; }   /* 128 note-offs */
    uint32_t pend0 = 0;
    for (int i = 0; i < 4; i++) for (uint32_t b = eng_off_pend[i]; b; b &= b - 1u) pend0++;
    render(BLK);                                                        /* drains: then the pending offs fit */
    int left = eng_flush();
    render(BLK);
    int left2 = eng_flush();
    int stuck = 0;
    for (int i = 0; i < NVOICE; i++) stuck += eng_synth.voice[i].busy && eng_synth.voice[i].key;
    render(FS / 4);
    int active = eng_active();
    /* a note-on after its own pending note-off: the off goes first (the note is not cut by a late off) */
    eng_init();
    for (int n = 0; n < (int)ENG_QLEN; n++) eng_note(n & 127, 100);      /* fill to the reserve */
    for (int n = 0; n < (int)ENG_RESERVE; n++) eng_note(100 + (n & 15), 0);   /* the reserve: note-offs */
    int r_off = eng_note(60, 0);                                         /* queue full: kept for later */
    int r_on = eng_note(60, 100);                                        /* refused (full); never before its off */
    render(BLK);
    eng_flush();
    int r_on2 = eng_note(60, 100);
    render(BLK);
    int s60 = 0;
    for (int i = 0; i < NVOICE; i++) s60 += eng_synth.voice[i].busy && eng_synth.voice[i].key && eng_synth.voice[i].note == 60;
    printf("queue_never_drops_note_off: %d note-ons taken, note-offs %d queued + %d kept (%u pending), pending after "
           "flushes %d %d, keys stuck %d, voices after 0.25 s %d; off then on of a pending note: %d %d then %d, "
           "60 sounding %d\n", on, off, deferred, pend0, left, left2, stuck, active, r_off, r_on, r_on2, s60);
    CHECK(on == (int)(ENG_QLEN - ENG_RESERVE) && off + deferred == 128 && deferred > 0 && pend0 == (uint32_t)deferred);
    CHECK(left == 0 && left2 == 0 && stuck == 0 && active == 0);
    CHECK(r_off == 1 && r_on == -1 && r_on2 == 0 && s60 == 1);
}

/* Task 7 review, folded into Task 8: a param edit the queue cannot take is not lost. eng_edit keeps the new value at
 * once (screens show it), the id is marked (eng_par_pend) and eng_flush queues its latest value when there is room;
 * several edits meanwhile send only the last. A program load supersedes pending edits. */
static void queue_keeps_param_edit(void)
{
    eng_init();
    for (int n = 0; n < (int)ENG_QLEN; n++) eng_note(n & 127, 100);      /* full up to the reserve */
    int r1 = eng_param(P_FLT_FREQ, 50), r2 = eng_param(P_FLT_FREQ, 40);
    int shown = eng_edit.v[P_FLT_FREQ];
    render(BLK);
    int before = eng_synth.prog.v[P_FLT_FREQ];
    uint32_t q0 = eng_qw;
    int left = eng_flush();
    int sent = (int)(eng_qw - q0);
    render(BLK);
    int after = eng_synth.prog.v[P_FLT_FREQ];
    /* a program load drops pending edits (its values win) */
    for (int n = 0; n < (int)ENG_QLEN; n++) eng_note(n & 127, 0);
    render(BLK);
    for (int n = 0; n < (int)ENG_QLEN; n++) eng_note(n & 127, 100);
    eng_param(P_FLT_RESO, 200);
    render(BLK);
    program_t p;
    program_init(&p);
    p.v[P_FLT_RESO] = 7;
    int lp = eng_program(&p);
    eng_flush();
    render(BLK);
    int reso = eng_synth.prog.v[P_FLT_RESO];
    printf("queue_keeps_param_edit: edits on a full queue %d %d (kept), shown %d, synth before flush %d, flush leaves "
           "%d and sends %d event, synth after %d; program load %d after a pending edit -> RESO %d\n",
           r1, r2, shown, before, left, sent, after, lp, reso);
    CHECK(r1 == 1 && r2 == 1 && shown == 40 && before == 164 && left == 0 && sent == 1 && after == 40);
    CHECK(lp == 0 && reso == 7);
}

/* chord memory goes to the synth through the queue too (eng_chord: 6 slots, absolute MIDI notes, 0 = empty) */
static void queue_chord(void)
{
    static const uint8_t c[6] = {60, 64, 67, 0, 0, 0};
    eng_init();
    eng_chord(c);
    int left = eng_flush();                                              /* (the main loop's pass) */
    int shown = eng_edit.chord[1];
    render(BLK);
    printf("queue_chord: flush leaves %d, edit %d, synth %d %d %d %d\n", left, shown, eng_synth.prog.chord[0], eng_synth.prog.chord[1],
           eng_synth.prog.chord[2], eng_synth.prog.chord[3]);
    CHECK(left == 0 && shown == 64 && eng_synth.prog.chord[0] == 60 && eng_synth.prog.chord[1] == 64 && eng_synth.prog.chord[2] == 67
          && eng_synth.prog.chord[3] == 0);
}

/* fix round 1: a note-on after a PANIC the queue had to keep is not cut by it (the PANIC goes first) */
static void queue_panic_before_note(void)
{
    eng_init();
    for (int n = 0; n < (int)ENG_QLEN; n++) eng_note(n & 127, 100);     /* up to the reserve */
    for (int n = 0; n < (int)ENG_RESERVE; n++) eng_note(n & 127, 0);    /* the reserve: full */
    int rp = eng_panic();                                               /* kept */
    render(BLK);
    int rn = eng_note(60, 100);                                         /* PANIC queued first, then the note */
    eng_flush();
    render(BLK);
    int s60 = 0;
    for (int i = 0; i < NVOICE; i++) s60 += eng_synth.voice[i].busy && eng_synth.voice[i].key && eng_synth.voice[i].note == 60;
    printf("queue_panic_before_note: panic kept %d, note %d, 60 sounding after the flush %d, voices %d\n", rp, rn, s60, eng_active());
    CHECK(rp == 1 && rn == 0 && s60 == 1 && eng_active() == 1);
}

/* fix round 1: ALL NOTES OFF (MIDI CC 123) releases every note (the envelopes release, no cut) and leaves HOLD as it
 * was; PANIC (CC 120) still cuts */
static void all_notes_off_releases(void)
{
    eng_init();
    eng_param(P_LENV_R, 60);
    eng_hold(1);
    eng_note(60, 100), eng_note(64, 100), eng_note(67, 100);
    render(FS / 10);
    eng_note(67, 0);                                                    /* latched by HOLD */
    render(BLK);
    int before = eng_active();
    eng_all_notes_off();
    render(BLK);
    int keys = 0, latched = 0;
    for (int i = 0; i < NVOICE; i++) keys += eng_synth.voice[i].busy && eng_synth.voice[i].key, latched += eng_synth.voice[i].latched;
    int releasing = eng_active(), hold = eng_synth.hold;
    int32_t peak = 0;
    for (int i = 0; i < 2 * BLK; i++) if (abs(OUT[i]) > peak) peak = abs(OUT[i]);
    render(FS);
    int after = eng_active();
    printf("all_notes_off_releases: %d voices, after ALL NOTES OFF %d still sounding (keys %d, latched %d, peak %d), HOLD "
           "%d; 1 s later %d\n", before, releasing, keys, latched, peak, hold, after);
    CHECK(before == 3 && releasing == 3 && keys == 0 && latched == 0 && peak > 1000 && hold == 1 && after == 0);
}

/* fix round 1: MIDI volume (CC 7) scales PROGRAM VOLUME, glides (no zipper), survives a program load */
static void midi_volume(void)
{
    eng_init();
    eng_note(57, 100);
    render(FS / 2);
    int32_t full = 0;
    for (uint32_t i = FS / 4; i < FS / 2; i++) if (abs(OUT[2 * i]) > full) full = abs(OUT[2 * i]);
    int32_t steady = max_step(FS / 4, FS / 2);
    eng_volume(64);
    render(FS / 2);
    int32_t half = 0;
    for (uint32_t i = FS / 4; i < FS / 2; i++) if (abs(OUT[2 * i]) > half) half = abs(OUT[2 * i]);
    int32_t jump = max_step(0, FS / 20);
    program_t p;
    program_init(&p);
    eng_program(&p);
    render(BLK);
    int kept = eng_synth.midi_vol;
    printf("midi_volume: peak at 127 %d, at 64 %d (ratio %.3f, want 0.504); max step at the change %d (steady %d); after a "
           "program load %d\n", full, half, (double)half / full, jump, steady, kept);
    CHECK(fabs((double)half / full - 64.0 / 127.0) < 0.03 && jump <= 2 * steady && kept == 64);
}

/* WRITE's fade (engine.c): out to exact silence in ENG_FADE_MS along a line (no step larger than the sound's own),
 * eng_faded() once two whole blocks went out silent, back to full in the same time; the synth plays on underneath.
 * And the master tune (the globals TRANSPOSE + TUNE) reaches the synth, clamped to +-1250 cents */
static void fade_and_tune(void)
{
    eng_init();
    eng_param(P_FLT_FREQ, 80);                     /* a smooth wave: a step would stand out */
    eng_note(60, 100);
    render(FS / 4);
    int32_t peak = 0, steady = max_step(FS / 8, FS / 4), after = 0, rest = 0, ramp, back = 0;
    for (uint32_t i = FS / 8; i < FS / 4; i++) if (abs(OUT[2 * i]) > peak) peak = abs(OUT[2 * i]);
    eng_fade(1);
    render(BLK);
    int early = eng_faded();
    render(4 * BLK);                               /* (OUT from 0 again: 4 blocks of the fade) */
    int faded = eng_faded();
    for (uint32_t i = BLK; i < 4 * BLK; i++) rest |= OUT[2 * i] | OUT[2 * i + 1];   /* (the line ends in block 2) */
    eng_fade(0);
    render(2 * BLK);
    ramp = max_step(0, 2 * BLK);
    for (uint32_t i = 0; i < 2 * BLK; i++) if (i >= 443 && abs(OUT[2 * i]) > back) back = abs(OUT[2 * i]);
    render(FS / 8);
    for (uint32_t i = 0; i < FS / 8; i++) if (abs(OUT[2 * i]) > after) after = abs(OUT[2 * i]);
    int voices = eng_active();
    eng_tune(1200);
    render(BLK);
    int32_t t12 = eng_synth.tune;
    eng_tune(5000);
    render(BLK);
    int32_t tmax = eng_synth.tune;
    printf("fade_and_tune: peak %d (steady step %d); faded after 1 block %d, after 5 %d, silent %d; back: step %d, peak %d "
           "then %d; voices %d; tune +1200 c -> %d (Q16), +5000 -> %d\n", peak, steady, early, faded, rest == 0, ramp, back,
           after, voices, t12, tmax);
    CHECK(!early && faded && rest == 0 && ramp <= steady + steady / 4 && voices == 1);
    CHECK(after > peak * 9 / 10 && after < peak * 11 / 10 && back > peak * 8 / 10);
    CHECK(t12 == 12 * 65536 && tmax == 1250 * 65536 / 100);
}

/* The CPU guard (voices.h synth_guard) on the host's fake clock (engine.c: each render advances it by eng_fake_per_voice
 * ticks per voice sounding at its end). Six voices at 140 % of the block: one step sheds to the voices that fit under
 * 80 % (three, at 23 % each), so the next block is already under 100 % and under 80 %; the output has no step beyond
 * the steady sound's (each shed voice fades out in ~2.2 ms); the limit holds while the load stays over 55 % and comes
 * back a voice per SYNTH_GUARD_HOLD calm blocks. */
static void cpu_guard_sheds(void)
{
    const uint32_t budget = BLK * ENG_TICKS_PER_FRAME_X10 / 10;
    eng_init();
    eng_param(P_MIX_VCO1, 0), eng_param(P_MIX_VCO2, 127), eng_param(P_VCO2_SHAPE, 0);   /* triangles: small steps */
    for (int k = 0; k < 6; k++) eng_note(48 + 5 * k, 100);
    eng_fake_per_voice = 0;
    render(FS / 4 / BLK * BLK);
    int a6 = synth_active(&eng_synth);
    int32_t s0 = max_step(FS / 8, FS / 4 / BLK * BLK);
    eng_fake_per_voice = budget * 14 / 10 / 6;                 /* six voices: 140 % */
    double pct[4];
    for (int b = 0; b < 4; b++) {
        uint32_t t0 = eng_fake_ticks;
        memset(&OUT[2 * b * BLK], 0, 2 * BLK * sizeof OUT[0]);
        eng_render(&OUT[2 * b * BLK], BLK);
        pct[b] = 100.0 * (eng_fake_ticks - t0) / budget;
    }
    const uint32_t n = FS / 4 / BLK * BLK;
    render(n);
    int32_t st = max_step(1, n);
    int lim = eng_synth.vlimit, live = synth_active(&eng_synth);
    eng_note(90, 100);                                         /* a new note steals under the limit */
    render(BLK * 4);
    int after_note = synth_active(&eng_synth);
    eng_fake_per_voice = budget / 10;                          /* calm: 10 % a voice */
    render(BLK * (SYNTH_GUARD_HOLD - 2));
    int lim_early = eng_synth.vlimit;
    render(BLK * (3 * SYNTH_GUARD_HOLD + 4));
    int lim_back = eng_synth.vlimit;
    eng_fake_per_voice = 0;
    printf("cpu_guard_sheds: 6 voices at 140 %%: blocks %.0f / %.0f / %.0f / %.0f %%, limit %d, %d voices sounding, "
           "max step %d (steady %d); a new note: %d sounding; calm: limit %d after %d blocks, %d after %d\n", pct[0],
           pct[1], pct[2], pct[3], lim, live, st, s0, after_note, lim_early, SYNTH_GUARD_HOLD - 2, lim_back,
           4 * SYNTH_GUARD_HOLD + 2);
    CHECK(a6 == 6 && pct[0] > 100 && pct[1] < 100 && pct[2] < 80 && pct[3] < 80);
    CHECK(lim == 3 && live == 3 && st <= 2 * s0 && after_note == 3);
    CHECK(lim_early == 3 && lim_back == NVOICE);
}

int main(void)
{
    voice_worst_case_bounded();
    params_smoothed_no_step();
    silence_costs_nothing();
    event_queue();
    queue_never_drops_note_off();
    queue_keeps_param_edit();
    queue_chord();
    queue_panic_before_note();
    all_notes_off_releases();
    midi_volume();
    fade_and_tune();
    cpu_guard_sheds();
    if (fails) { printf("test_render: %d FAILED\n", fails); return 1; }
    printf("test_render: all passed\n");
    return 0;
}
