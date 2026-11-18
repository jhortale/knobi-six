/* SPDX-License-Identifier: GPL-3.0-only */
/* The sequencer's player (firmware/src/synth/seqplay.c, Phase 3 Task 4), alone on a synth and through the engine
 * (engine.c) as app.c runs it: steps on the clock's steps, sample-exact like the arp, a 50 % gate, ties held across
 * steps with no retrigger, rests silent, the recorded length looped (seq_play_timing); the arp's start rules (BEAT SYNC
 * off: a restart; on, or MIDI CLOCK OUT: the next step, no restart, no 0xFA; seq_start_rules); an external clock: armed
 * until it runs, Start from step 1, Continue from the held step, Stop pauses, no clock silent (seq_ext_clock);
 * transpose (middle C = none, out of range dropped, unison; seq_transpose); arp and sequencer exclusive both ways
 * (seq_arp_exclusive); no note left on, locally or on MIDI out, after a stop, a program load, ARP ON, all notes off,
 * panic, an external stop, the clock gone or the CPU guard shedding voices (seq_no_stuck_notes); the MIDI out ring:
 * every note-on paired with its note-off, ties and rests sending nothing (seq_midi_out_paired). PARAM TX / LOCAL and
 * the channel kept are ui/midi_map.c's (the arp's path): tests/scripts/seq.txt. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define BLK 256u
#define STEP 1000u                              /* a step in the pure tests, frames */
#define Q120 22050u                             /* a quarter at 120 BPM, frames */

static synth_t S;
static arp_t A;
static seqp_t P;
static seq_t Q;

static void step_of(seq_t *q, int n, int a, int b, int c)   /* a step of up to 3 notes (0 = none), velocity 100 - 10 i */
{
    uint8_t nt[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c}, vl[3] = {100, 90, 80};
    (void)seq_rec_step(q, nt, vl, n);
}
/* the demo: C4 | rest | E4 | tie | G4 + B4 */
static void demo(seq_t *q)
{
    seq_clear(q);
    step_of(q, 1, 60, 0, 0);
    (void)seq_rec_rest(q);
    step_of(q, 1, 64, 0, 0);
    (void)seq_rec_tie(q);
    step_of(q, 2, 67, 71, 0);
}
static void fresh(void)
{
    synth_init(&S);
    arp_init(&A, 1u);
    seqp_init(&P, &Q, &A);
}
static int keyed(const synth_t *s)               /* voices with their key down (or latched) */
{
    int n = 0;
    for (int i = 0; i < NVOICE; i++) n += s->voice[i].busy && (s->voice[i].key || s->voice[i].latched);
    return n;
}
static int keyed_note(const synth_t *s, int note)
{
    for (int i = 0; i < NVOICE; i++)
        if (s->voice[i].busy && (s->voice[i].key || s->voice[i].latched) && s->voice[i].note == note) return 1;
    return 0;
}

/* ---- the MIDI out ring, read as ui/midi_map.c reads it (every block): a bit per note on at the receiver */
static uint32_t rr, r_on, r_off, r_dup, r_stray;
static uint8_t r_set[128];
static void ring_reset(const arp_t *a)
{
    rr = a->out_w, r_on = r_off = r_dup = r_stray = 0;
    memset(r_set, 0, sizeof r_set);
}
static void ring_read(const arp_t *a)
{
    CHECK(a->out_w - rr <= ARP_OUT_N);            /* (never overrun between two reads) */
    for (; rr != a->out_w; rr++) {
        uint32_t note = a->out[rr & (ARP_OUT_N - 1u)][0] & 127u, vel = a->out[rr & (ARP_OUT_N - 1u)][1];
        if (vel) { r_dup += r_set[note]; r_set[note] = 1, r_on++; }
        else { r_stray += !r_set[note]; r_set[note] = 0, r_off++; }
    }
}
static int ring_hanging(void)                   /* notes on at the receiver */
{
    int n = 0;
    for (int i = 0; i < 128; i++) n += r_set[i];
    return n;
}

/* ---- through the engine: 256-frame blocks as app_audio, the ring read after each */
static int32_t OUT[2u * 44100u * 5u];
static void render_at(uint32_t f0, uint32_t frames)
{
    for (uint32_t f = f0; f < f0 + frames; f += BLK) {
        memset(&OUT[2u * f], 0, 2u * BLK * sizeof OUT[0]);
        eng_render(&OUT[2u * f], BLK);
        ring_read(&eng_arp);
    }
}
static void render(uint32_t frames)
{
    static int32_t tmp[2u * BLK];
    for (uint32_t f = 0; f < frames; f += BLK) {
        memset(tmp, 0, sizeof tmp);
        eng_render(tmp, BLK);
        ring_read(&eng_arp);
    }
}
static int eng_keyed(void) { return keyed(&eng_synth); }
static int loud(uint32_t i) { return abs(OUT[2u * i]) > 16 || abs(OUT[2u * i + 1u]) > 16; }
static uint32_t first_sound(uint32_t a, uint32_t b)
{
    for (uint32_t i = a; i < b; i++) if (loud(i)) return i;
    return ~0u;
}
static void eng_fresh(const seq_t *q)
{
    eng_init();
    ring_reset(&eng_arp);
    CHECK(eng_seq_load(q) == 0);
}

/* steps on the clock's steps, a 50 % gate, ties held across steps (no retrigger, nothing sent), rests silent, the
 * length looped: alone on a synth, then sample-exact through the engine */
static void seq_play_timing(void)
{
    int ok_step0, ok_gate, ok_rest, ok_tie, ok_chord, ok_loop;
    uint32_t w;
    demo(&Q);
    fresh();
    CHECK(seq_play(&P, 1) == 1);
    P.step_len = STEP;
    seq_step(&P, &S);                                                     /* step 1: C4, gate 500 */
    ok_step0 = P.n == 1 && P.note[0] == 60 && keyed_note(&S, 60) && keyed(&S) == 1 && seq_until(&P) == 500u && P.cur == 0;
    seq_run(&P, &S, 499);
    ok_gate = keyed(&S) == 1;
    seq_run(&P, &S, 1);
    ok_gate &= keyed(&S) == 0 && P.n == 0 && seq_until(&P) == SEQ_NONE;
    seq_run(&P, &S, 500);
    seq_step(&P, &S);                                                     /* step 2: rest */
    ok_rest = P.n == 0 && keyed(&S) == 0 && seq_until(&P) == SEQ_NONE && P.cur == 1;
    seq_run(&P, &S, STEP);
    seq_step(&P, &S);                                                     /* step 3: E4, a tie next: held */
    ok_tie = P.n == 1 && keyed_note(&S, 64) && seq_until(&P) == SEQ_NONE;
    seq_run(&P, &S, STEP);
    ok_tie &= keyed_note(&S, 64);
    w = A.out_w;
    seq_step(&P, &S);                                                     /* step 4: the tie: on, no retrigger */
    ok_tie &= A.out_w == w && keyed_note(&S, 64) && keyed(&S) == 1 && seq_until(&P) == 500u && P.cur == 3;
    seq_run(&P, &S, 500);
    ok_tie &= keyed(&S) == 0;
    seq_run(&P, &S, 500);
    seq_step(&P, &S);                                                     /* step 5: G4 + B4 */
    ok_chord = P.n == 2 && keyed_note(&S, 67) && keyed_note(&S, 71) && keyed(&S) == 2;
    seq_run(&P, &S, STEP);
    seq_step(&P, &S);                                                     /* the loop: step 1 again */
    ok_loop = P.cur == 0 && P.n == 1 && P.note[0] == 60 && keyed(&S) == 1 && P.steps == 6u;
    printf("seq_play_timing: step 1 C4 gate 500 %d; released at the gate %d; rest silent %d; tie held, no retrigger %d; "
           "chord %d; looped %d\n", ok_step0, ok_gate, ok_rest, ok_tie, ok_chord, ok_loop);
    CHECK(ok_step0 && ok_gate && ok_rest && ok_tie && ok_chord && ok_loop);

    /* the engine, 120 BPM QTR, BEAT SYNC off: PLAY at block 10 (frame 2560) restarts the clock: step k starts at 2560 +
     * 22050 k. Sounds start at steps 1 (C4), 3 (E4, held through the tie to 50 % of step 4) and 5 (the chord); the rest
     * (step 2) and the tie (step 4) start nothing. A short release, so the gaps are silent */
    eng_fresh(&Q);
    eng_param(P_LENV_R, 0);
    eng_arp_sync(0);
    render_at(0, 10u * BLK);
    eng_seq_play(1);
    render_at(10u * BLK, 380u * BLK);                                     /* to frame 99840 */
    {
        uint32_t b = 2560u, on[8], non = 0, i, quiet = 0;
        int held, gap_rest, gap_tie;
        for (i = 1; i < 99840u; i++) {                                    /* onsets: sound after >= 400 quiet frames */
            if (!loud(i)) { quiet++; continue; }
            if (quiet >= 400u && non < 8u) on[non++] = i;
            quiet = 0;
        }
        if (loud(0)) quiet = 0;
        held = loud(b + 3u * Q120 + 5000u) && loud(b + 3u * Q120 + Q120 / 2u - 200u);   /* the tie: E4 still on */
        gap_rest = first_sound(b + Q120 / 2u + 3000u, b + 2u * Q120) == b + 2u * Q120 ||
                   first_sound(b + Q120 / 2u + 3000u, b + 2u * Q120 + 3u) <= b + 2u * Q120 + 2u;
        gap_tie = first_sound(b + 3u * Q120 + Q120 / 2u + 3000u, b + 4u * Q120) == ~0u;
        printf("seq_play_timing: engine: onsets at");
        for (i = 0; i < non; i++) printf(" %u", (unsigned)on[i]);
        printf(" (expected %u %u %u); the tie held %d; the rest silent %d; silent after the tie's gate %d\n",
               (unsigned)b, (unsigned)(b + 2u * Q120), (unsigned)(b + 4u * Q120), held, gap_rest, gap_tie);
        CHECK(non == 3u);
        CHECK(non >= 1u && on[0] >= b && on[0] <= b + 2u);
        CHECK(non >= 2u && on[1] >= b + 2u * Q120 && on[1] <= b + 2u * Q120 + 2u);
        CHECK(non >= 3u && on[2] >= b + 4u * Q120 && on[2] <= b + 4u * Q120 + 2u);
        CHECK(held && gap_rest && gap_tie);
    }
    eng_seq_play(0);
    render(4u * BLK);
    CHECK(eng_keyed() == 0 && ring_hanging() == 0);
}

/* the arp's start rules: BEAT SYNC off (internal): the clock restarts, step 1 in PLAY's block; BEAT SYNC on: no
 * restart, the next step of the division; MIDI CLOCK OUT: no restart, so no extra 0xFA, the next step */
static void seq_start_rules(void)
{
    uint32_t s0, first, fa = 0, i, n, k;
    int off_ok, on_ok, out_ok;
    clk_out_t o;
    uint8_t b[3u + CLOCK_OUT_OWED];
    demo(&Q);

    eng_fresh(&Q);                                                        /* BEAT SYNC off */
    eng_arp_sync(0);
    render_at(0, 10u * BLK + 128u);                                       /* (the clock mid-step) */
    s0 = eng_clk.starts;
    eng_seq_play(1);
    render_at(0, 4u * BLK);
    first = first_sound(0, 4u * BLK);
    off_ok = eng_clk.starts == s0 + 1u && first <= 2u && eng_seqp.cur == 0;
    printf("seq_start_rules: BEAT SYNC off: clock restarts %u, the first sound at frame %u of PLAY's block\n",
           (unsigned)(eng_clk.starts - s0), first);

    eng_fresh(&Q);                                                        /* BEAT SYNC on */
    eng_arp_sync(1);
    render_at(0, 10u * BLK);
    s0 = eng_clk.starts;
    eng_seq_play(1);
    render_at(10u * BLK, 90u * BLK);
    first = first_sound(0, 100u * BLK);
    on_ok = eng_clk.starts == s0 && first >= Q120 && first <= Q120 + 2u;
    printf("seq_start_rules: BEAT SYNC on: clock restarts %u, PLAY at frame 2560, the first sound at %u (the step at "
           "22050)\n", (unsigned)(eng_clk.starts - s0), first);

    eng_fresh(&Q);                                                        /* MIDI CLOCK OUT, BEAT SYNC off */
    eng_arp_sync(0);
    eng_clock_mode(CLK_MIDI_OUT);
    clock_out_init(&o);
    for (k = 0; k < 100u; k++) {
        if (k == 10u) eng_seq_play(1);
        memset(&OUT[2u * k * BLK], 0, 2u * BLK * sizeof OUT[0]);
        eng_render(&OUT[2u * k * BLK], BLK);
        ring_read(&eng_arp);
        n = clock_out_poll(&o, CLK_MIDI_OUT, eng_clk_mode, eng_clk_starts, eng_clk_start_ticks, eng_clk_ticks, b, sizeof b);
        for (i = 0; i < n; i++) fa += b[i] == 0xFAu;
    }
    first = first_sound(0, 100u * BLK);
    out_ok = eng_clk.starts == 1u && fa == 1u && first >= Q120 && first <= Q120 + 2u;
    printf("seq_start_rules: MIDI CLOCK OUT: clock starts %u (OUT's own), 0xFA sent %u, PLAY at frame 2560, the first "
           "sound at %u\n", (unsigned)eng_clk.starts, (unsigned)fa, first);
    CHECK(off_ok && on_ok && out_ok);
}

/* MIDI CLOCK IN (and THRU): PLAY arms; no clock: silent; 0xFA + ticks: from step 1; 0xFC: paused, notes released, the
 * position held; 0xFB: on from the held step; 0xFA again: step 1; the clock gone: silent, released */
static uint32_t tick_t, tick_next;               /* the test's MIDI clock: 120 BPM, a tick every 918.75 frames */
static void ticks_render(int blocks, int ticks)
{
    for (int k = 0; k < blocks; k++) {
        if (ticks) while (tick_next <= tick_t * 4u) eng_midi_rt(0xF8), tick_next += 3675u;
        render(BLK), tick_t += BLK;
    }
}
static uint32_t until_step(int ticks)            /* blocks until the player plays a step (at most 200) */
{
    uint32_t s = eng_seqp.steps;
    for (int k = 0; k < 200; k++) {
        ticks_render(1, ticks);
        if (eng_seqp.steps != s) return 1;
    }
    return 0;
}
static void seq_ext_clock(int mode)
{
    int armed, from1, paused, cont, restart, gone;
    uint32_t held, any = 0;
    seq_clear(&Q);
    for (int i = 0; i < 8; i++) step_of(&Q, 1, 60 + i, 0, 0);
    eng_fresh(&Q);
    eng_clock_mode(mode);
    eng_param(P_CLK_DIV, CLK_DIV_16TH);
    eng_seq_play(1);
    for (int b = 0; b < 200; b++) { render(BLK); any |= (uint32_t)eng_keyed(); }   /* ~1.2 s, no clock */
    armed = !any && eng_seqp.steps == 0u && eng_seqp.on && eng_seq_state == 2;
    tick_t = tick_next = 0;
    eng_midi_rt(0xFA);
    from1 = until_step(1) && eng_seqp.cur == 0 && keyed_note(&eng_synth, 60) && eng_seq_state == 1;
    ticks_render(60, 1);                                                  /* a few steps on */
    eng_midi_rt(0xFC);                                                    /* stop: the ticks go on */
    ticks_render(1, 1);
    held = eng_seqp.pos;
    paused = eng_keyed() == 0 && eng_seqp.n == 0;
    { uint32_t s = eng_seqp.steps; ticks_render(60, 1); paused &= eng_seqp.steps == s && eng_keyed() == 0 && eng_seqp.pos == held; }
    eng_midi_rt(0xFB);
    cont = until_step(1) && eng_seqp.cur == held;
    ticks_render(20, 1);
    eng_midi_rt(0xFA);                                                    /* start again: step 1 */
    restart = until_step(1) && eng_seqp.cur == 0;
    ticks_render(20, 1);
    any = 0;
    for (int b = 0; b < 200; b++) { render(BLK); if (b > 50) any |= (uint32_t)eng_keyed(); }   /* the clock gone */
    gone = !any && eng_seqp.n == 0 && !eng_clk_run && eng_seq_state == 2;
    eng_seq_play(0);
    render(BLK);
    printf("seq_ext_clock (mode %d): armed and silent without a clock %d; 0xFA: step 1 %d; 0xFC: paused, released, step "
           "%u held %d; 0xFB: on from it %d; 0xFA again: step 1 %d; the clock gone: silent %d; MIDI out hanging %d\n",
           mode, armed, from1, (unsigned)held + 1u, paused, cont, restart, gone, ring_hanging());
    CHECK(armed && from1 && paused && cont && restart && gone && ring_hanging() == 0 && r_dup == 0u);
}

/* transpose: middle C adds nothing; +5; a note sounding keeps its pitch (and its note-off); out of range: dropped, the
 * step's other notes play; clamped; through the engine with Unison on */
static void seq_transpose_test(void)
{
    int center, up, kept, drop, clamp, uni;
    seq_clear(&Q);
    step_of(&Q, 1, 60, 0, 0);
    step_of(&Q, 2, 108, 60, 0);
    fresh();
    ring_reset(&A);
    seq_play(&P, 1);
    P.step_len = STEP;
    seq_transpose(&P, 60 - SEQ_XP_CENTER);                                /* middle C */
    seq_step(&P, &S);
    center = P.note[0] == 60 && keyed_note(&S, 60);
    seq_run(&P, &S, STEP);
    seq_step(&P, &S);                                                     /* (108 + 60) */
    seq_transpose(&P, 65 - SEQ_XP_CENTER);                                /* F4: +5, while 108 and 60 sound */
    kept = keyed_note(&S, 108) && keyed_note(&S, 60);
    seq_run(&P, &S, STEP);
    kept &= keyed(&S) == 0;
    seq_step(&P, &S);                                                     /* step 1 again: 65 */
    up = P.n == 1 && P.note[0] == 65 && keyed_note(&S, 65) && keyed(&S) == 1;
    seq_run(&P, &S, STEP);
    seq_transpose(&P, 30);
    seq_step(&P, &S);                                                     /* step 1: 90 */
    seq_run(&P, &S, STEP);
    seq_step(&P, &S);                                                     /* step 2: 138 dropped, 90 plays */
    drop = P.n == 1 && P.note[0] == 90 && keyed(&S) == 1;
    seq_silence(&P, &S);
    ring_read(&A);
    seq_transpose(&P, -200);
    clamp = P.xp == -127;
    seq_transpose(&P, 200);
    clamp &= P.xp == 127;
    printf("seq_transpose: middle C = none %d; +5 from the next step %d; sounding notes keep their pitch %d; out of range "
           "dropped %d; clamped %d; MIDI out paired %d\n", center, up, kept, drop, clamp, ring_hanging() == 0 && r_dup == 0u);
    CHECK(center && up && kept && drop && clamp && ring_hanging() == 0 && r_dup == 0u && r_stray == 0u);

    eng_fresh(&Q);                                                        /* Unison on: the transposed note, mono */
    eng_param(P_UNISON_ON, 1);
    eng_arp_sync(0);
    eng_seq_transpose(72 - SEQ_XP_CENTER);
    eng_seq_play(1);
    render(2u * BLK);
    uni = eng_synth.voice[0].busy && eng_synth.voice[0].note == 72 && keyed_note(&eng_synth, 72);
    eng_seq_play(0);
    render(2u * BLK);
    uni &= eng_keyed() == 0;
    printf("seq_transpose: Unison on, transposed +12: the unison voices play 72 %d\n", uni);
    CHECK(uni && ring_hanging() == 0);
}

/* turning one on turns the other off, both ways, its notes released (the producer's copy follows) */
static void seq_arp_exclusive(void)
{
    int seq_on, arp_on;
    demo(&Q);
    eng_fresh(&Q);
    eng_arp_sync(0);
    eng_param(P_ARP_ON, 1);
    eng_note(50, 100);
    render(4u * BLK);
    CHECK(eng_arp.note == 50);
    eng_seq_play(1);                                                      /* SEQ PLAY: the arp off */
    seq_on = eng_edit.v[P_ARP_ON] == 0 && eng_seq_playing();
    render(4u * BLK);
    seq_on &= !eng_arp.on && eng_arp.note == -1 && eng_seqp.on && eng_seqp.steps > 0u && keyed_note(&eng_synth, 60);
    eng_note(50, 0);
    render(BLK);
    eng_param(P_ARP_ON, 1);                                               /* ARP ON: the sequencer off */
    arp_on = !eng_seq_playing();
    render(BLK);
    arp_on &= eng_arp.on && !eng_seqp.on && eng_seqp.n == 0 && eng_arp.n == 0 && eng_keyed() == 0 && eng_seq_state == 0;
    render(100u * BLK);
    arp_on &= eng_seqp.steps == 1u && eng_keyed() == 0;
    printf("seq_arp_exclusive: SEQ PLAY turns the arp off (its note released, the sequence plays) %d; ARP ON stops the "
           "sequence (released, the arp has no keys from it) %d; MIDI out hanging %d\n", seq_on, arp_on, ring_hanging());
    CHECK(seq_on && arp_on && ring_hanging() == 0 && r_dup == 0u);
}

/* no note left on, here or at the receiver, whatever stops the sequence while 6-note chords and ties sound */
static void chords(seq_t *q)
{
    static const uint8_t a[6] = {48, 52, 55, 60, 64, 67}, b[6] = {50, 53, 57, 62, 65, 69}, v[6] = {100, 100, 100, 100, 100, 100};
    seq_clear(q);
    (void)seq_rec_step(q, a, v, 6);
    (void)seq_rec_tie(q);
    (void)seq_rec_step(q, b, v, 6);
    (void)seq_rec_rest(q);
}
static int sounding_seq(void)                    /* render until the sequence has notes on (at most 400 blocks) */
{
    for (int k = 0; k < 400; k++) { if (eng_seqp.n) return 1; render(BLK); }
    return 0;
}
enum { ST_STOP, ST_PROG, ST_ARP, ST_ALLOFF, ST_PANIC, ST_EXTSTOP, ST_GONE, ST_SHED, ST_N };
static const char *const ST_NAME[ST_N] = {"SEQ PLAY off", "program load", "ARP ON", "all notes off", "panic",
                                          "0xFC", "clock gone", "voice shed"};
static void seq_no_stuck_notes(void)
{
    int all = 1;
    program_t p;
    chords(&Q);
    for (int t = 0; t < ST_N; t++) {
        int ok, sounded;
        eng_fresh(&Q);
        eng_arp_sync(0);
        eng_param(P_BPM, 250);
        eng_param(P_CLK_DIV, CLK_DIV_16TH);
        if (t == ST_EXTSTOP || t == ST_GONE) {
            eng_clock_mode(CLK_MIDI_IN);
            tick_t = tick_next = 0;
            eng_midi_rt(0xFA);
        }
        eng_seq_play(1);
        if (t == ST_EXTSTOP || t == ST_GONE) { for (int k = 0; k < 400 && !eng_seqp.n; k++) ticks_render(1, 1); sounded = eng_seqp.n > 0; }
        else sounded = sounding_seq();
        switch (t) {
        case ST_STOP: eng_seq_play(0); break;
        case ST_PROG: program_init(&p); eng_program(&p); break;
        case ST_ARP: eng_param(P_ARP_ON, 1); break;
        case ST_ALLOFF: eng_all_notes_off(); break;
        case ST_PANIC: eng_panic(); break;
        case ST_EXTSTOP: eng_midi_rt(0xFC); break;
        case ST_GONE: break;
        case ST_SHED: eng_fake_per_voice = 4000000u; break;               /* (every block over budget: sheds) */
        }
        if (t == ST_EXTSTOP) ticks_render(4, 1);
        else if (t == ST_GONE) render(60u * BLK);                         /* > 0.25 s without ticks */
        else if (t == ST_SHED) render(40u * BLK);
        else render(4u * BLK);
        if (t == ST_ALLOFF) {                                             /* released at once; the sequence plays on */
            ok = sounded;
            render(200u * BLK);
            ok &= eng_seqp.on && eng_seqp.steps > 4u;
            eng_seq_play(0);
            render(4u * BLK);
        } else if (t == ST_SHED) {
            ok = sounded && eng_synth.sheds > 0u;
            eng_fake_per_voice = 0;
            eng_seq_play(0);
            render(4u * BLK);
        } else ok = sounded && !eng_seqp.on == (t != ST_EXTSTOP && t != ST_GONE);
        ok &= eng_seqp.n == 0 && eng_keyed() == 0 && ring_hanging() == 0 && r_dup == 0u && r_on > 0u;
        printf("seq_no_stuck_notes: %-14s nothing held here or at the receiver %d (MIDI on %u off %u)\n", ST_NAME[t], ok,
               (unsigned)r_on, (unsigned)r_off);
        all &= ok;
    }
    eng_fake_per_voice = 0;
    CHECK(all);
}

/* the ring: a step's note-offs before its note-ons, every on paired with an off (transposed included), the tie and the
 * rest sending nothing */
static void seq_midi_out_paired(void)
{
    uint32_t w0, w1, w2;
    int order, tie, rest, pairs;
    chords(&Q);
    fresh();
    ring_reset(&A);
    seq_play(&P, 1);
    P.step_len = STEP;
    seq_step(&P, &S);                                                     /* 6 on */
    w0 = A.out_w;
    seq_run(&P, &S, STEP);                                                /* (held: a tie next) */
    seq_step(&P, &S);                                                     /* the tie: nothing */
    w1 = A.out_w;
    tie = w1 == w0 && w0 == 6u;
    seq_run(&P, &S, STEP);                                                /* 6 off at the gate */
    seq_transpose(&P, -2);
    seq_step(&P, &S);                                                     /* 6 on (transposed) */
    w2 = A.out_w;
    order = w2 - w1 == 12u && A.out[w1][1] == 0 && A.out[w1 + 6u][1] == 100 && A.out[w1 + 6u][0] == 48;
    seq_run(&P, &S, STEP);
    w0 = A.out_w;
    seq_step(&P, &S);                                                     /* the rest: nothing (offs went at the gate) */
    rest = A.out_w == w0 && w0 == w2 + 6u;
    seq_run(&P, &S, STEP);
    seq_step(&P, &S);
    seq_silence(&P, &S);
    seq_play(&P, 0);
    ring_read(&A);
    pairs = ring_hanging() == 0 && r_dup == 0u && r_stray == 0u && r_on == r_off && r_on == 18u;   /* (chord, tie, chord, rest, chord: 3 x 6) */
    printf("seq_midi_out_paired: the tie sends nothing %d; a step's offs before its ons %d; the rest sends nothing %d; "
           "%u on, %u off, all paired %d\n", tie, order, rest, (unsigned)r_on, (unsigned)r_off, pairs);
    CHECK(tie && order && rest && pairs);
}

/* a sequence handed over while it plays: followed (a shorter one wraps to step 1, never past its end); empty: silent */
static void seq_change_under(void)
{
    static seq_t q2;
    int shorter, empty;
    seq_clear(&Q);
    for (int i = 0; i < 8; i++) step_of(&Q, 1, 60 + i, 0, 0);
    eng_fresh(&Q);
    eng_arp_sync(0);
    eng_param(P_BPM, 250);
    eng_param(P_CLK_DIV, CLK_DIV_32ND);
    eng_seq_play(1);
    for (int k = 0; k < 400 && eng_seqp.cur != 6; k++) render(BLK);
    seq_clear(&q2);
    step_of(&q2, 1, 40, 0, 0), step_of(&q2, 1, 41, 0, 0);
    CHECK(eng_seq_load(&q2) == 0);
    render(BLK);                                                          /* (drained: the step shown is not past 2) */
    CHECK(eng_seq_cur == SEQ_NO_STEP || eng_seq_cur < 2u);
    { uint32_t s = eng_seqp.steps; for (int k = 0; k < 400 && eng_seqp.steps == s; k++) render(BLK); }
    shorter = eng_seqp.cur == 0 && eng_seqp.note[0] == 40 && eng_seqp.pos == 1;
    render(40u * BLK);
    shorter &= eng_seqp.cur < 2 && eng_seqp.pos < 2;
    seq_clear(&q2);
    CHECK(eng_seq_load(&q2) == 0);
    render(20u * BLK);
    empty = eng_seqp.n == 0 && eng_keyed() == 0 && eng_seqp.on;
    eng_seq_play(0);
    render(BLK);
    printf("seq_change_under: a shorter sequence followed from step 1 %d; an empty one silent %d; MIDI out hanging %d\n",
           shorter, empty, ring_hanging());
    CHECK(shorter && empty && ring_hanging() == 0);
}


/* ties at the loop point: a tie on the last step holds across the wrap into step 1 only when step 1 is itself a tie (a
 * hand-made sequence: the recorder never makes one); a last-step tie releases at its gate and step 1 retriggers */
static void seq_ties_wrap(void)
{
    int last, wrap;
    uint32_t w;
    seq_clear(&Q);                                                        /* C4 | E4 | tie */
    step_of(&Q, 1, 60, 0, 0), step_of(&Q, 1, 64, 0, 0);
    (void)seq_rec_tie(&Q);
    fresh();
    seq_play(&P, 1);
    P.step_len = STEP;
    seq_step(&P, &S), seq_run(&P, &S, STEP);
    seq_step(&P, &S);                                                     /* E4, held (a tie next) */
    last = seq_until(&P) == SEQ_NONE;
    seq_run(&P, &S, STEP);
    w = A.out_w;
    seq_step(&P, &S);                                                     /* the last step, a tie: no retrigger */
    last &= A.out_w == w && keyed_note(&S, 64) && seq_until(&P) == 500u;   /* (step 1 is no tie: the gate) */
    seq_run(&P, &S, 500);
    last &= keyed(&S) == 0;
    seq_run(&P, &S, 500);
    seq_step(&P, &S);                                                     /* the wrap: step 1, C4 */
    last &= P.cur == 0 && keyed_note(&S, 60) && keyed(&S) == 1;
    seq_silence(&P, &S);

    seq_clear(&Q);                                                        /* hand-made: step 1 a tie (of C4), D4 */
    step_of(&Q, 1, 60, 0, 0), step_of(&Q, 1, 62, 0, 0);
    Q.step[0].tie = 1;
    fresh();
    seq_play(&P, 1);
    P.step_len = STEP;
    seq_step(&P, &S);                                                     /* step 1, nothing sounding: its copy plays */
    wrap = keyed_note(&S, 60) && seq_until(&P) == 500u;
    seq_run(&P, &S, STEP);
    seq_step(&P, &S);                                                     /* D4, held: step 1 (next) is a tie */
    wrap &= keyed_note(&S, 62) && seq_until(&P) == SEQ_NONE;
    seq_run(&P, &S, STEP);
    w = A.out_w;
    seq_step(&P, &S);                                                     /* the wrap onto the tie: D4 held on */
    wrap &= A.out_w == w && keyed_note(&S, 62) && keyed(&S) == 1 && P.cur == 0 && seq_until(&P) == 500u;
    seq_run(&P, &S, 500);
    wrap &= keyed(&S) == 0 && P.n == 0;
    printf("seq_ties_wrap: a last-step tie: held, released at its gate, step 1 retriggers %d; a wrap onto a tie (step 1): "
           "the last notes held on, no retrigger %d\n", last, wrap);
    CHECK(last && wrap);
}

/* an external Continue landing on a tie: the pause released the notes, so the tie plays its carried copy */
static void seq_continue_on_tie(void)
{
    int ok;
    seq_clear(&Q);                                                        /* C4 | E4 | tie | G4 */
    step_of(&Q, 1, 60, 0, 0), step_of(&Q, 1, 64, 0, 0);
    (void)seq_rec_tie(&Q);
    step_of(&Q, 1, 67, 0, 0);
    eng_fresh(&Q);
    eng_clock_mode(CLK_MIDI_IN);
    eng_param(P_CLK_DIV, CLK_DIV_16TH);
    eng_seq_play(1);
    tick_t = tick_next = 0;
    eng_midi_rt(0xFA);
    for (int k = 0; k < 400 && eng_seqp.cur != 1; k++) ticks_render(1, 1);   /* E4 sounding, held */
    ok = eng_seqp.cur == 1 && keyed_note(&eng_synth, 64);
    eng_midi_rt(0xFC);
    ticks_render(4, 1);
    ok &= eng_keyed() == 0 && eng_seqp.pos == 2;                          /* paused on the tie */
    eng_midi_rt(0xFB);
    ok &= until_step(1) && eng_seqp.cur == 2 && keyed_note(&eng_synth, 64) && eng_seqp.n == 1;
    eng_seq_play(0);
    render(BLK);
    ok &= eng_keyed() == 0 && ring_hanging() == 0 && r_dup == 0u;
    printf("seq_continue_on_tie: 0xFB onto the tie: its notes play (E4), paired on MIDI out %d\n", ok);
    CHECK(ok);
}

/* HOLD latches the live keys only: the sequence's notes never pile up latched, nothing of theirs sounds on after a
 * stop or ALL NOTES OFF, a live key let go under HOLD stays latched beside it; with Unison the line follows the steps */
static int seq_latched(int except)               /* voices of the sequence latched, or still keyed (except a note) */
{
    int n = 0;
    for (int i = 0; i < NVOICE; i++)
        n += eng_synth.voice[i].busy && eng_synth.voice[i].latched && eng_synth.voice[i].note != except;
    return n;
}
static void seq_hold_never_latches(void)
{
    static const uint8_t a[3] = {48, 52, 55}, b[3] = {50, 53, 57}, v[3] = {100, 100, 100};
    int piled = 0, stop, live, alloff, uni = 1, cnt = 0;
    seq_clear(&Q);
    (void)seq_rec_step(&Q, a, v, 3), (void)seq_rec_tie(&Q), (void)seq_rec_step(&Q, b, v, 3), (void)seq_rec_rest(&Q);
    eng_fresh(&Q);
    eng_arp_sync(0);
    eng_param(P_BPM, 250);
    eng_param(P_CLK_DIV, CLK_DIV_16TH);
    eng_hold(1);
    eng_note(40, 100), eng_note(40, 0);                                   /* a live key under HOLD: latched */
    render(BLK);
    eng_seq_play(1);
    for (int k = 0; k < 300; k++) { render(BLK); piled |= seq_latched(40) != 0; }   /* ~25 steps */
    live = keyed_note(&eng_synth, 40);
    eng_seq_play(0);
    render(2u * BLK);
    stop = seq_latched(40) == 0 && eng_keyed() == 1 && keyed_note(&eng_synth, 40);
    eng_seq_play(1);
    render(30u * BLK);
    eng_all_notes_off();                                                  /* (releases the live latch too: CC 123) */
    render(BLK);
    alloff = eng_seqp.n == 0 || eng_seqp.steps > 0u;
    eng_seq_play(0);
    render(2u * BLK);
    alloff &= eng_keyed() == 0;
    eng_hold(0);
    render(BLK);
    printf("seq_hold_never_latches: HOLD on: no sequence note latched over 25 steps %d; the live key stays latched %d; "
           "SEQ PLAY off: only the live key left %d; all notes off: nothing %d\n", !piled, live, stop, alloff);
    CHECK(!piled && live && stop && alloff && ring_hanging() == 0);

    seq_clear(&Q);                                                        /* Unison + HOLD: the line follows */
    for (int i = 0; i < 4; i++) step_of(&Q, 1, 60 + 2 * i, 0, 0);
    eng_fresh(&Q);
    eng_arp_sync(0);
    eng_param(P_UNISON_ON, 1);
    eng_param(P_BPM, 250);
    eng_param(P_CLK_DIV, CLK_DIV_16TH);
    eng_hold(1);
    eng_seq_play(1);
    for (int k = 0; k < 300; k++) {
        render(BLK);
        if (eng_seqp.n) { uni &= eng_synth.mono == eng_seqp.note[0]; cnt++; }
        else uni &= eng_synth.mono == -1;
    }
    eng_seq_play(0);
    render(2u * BLK);
    uni &= eng_synth.mono == -1 && eng_keyed() == 0 && cnt > 50;
    eng_hold(0);
    printf("seq_hold_never_latches: Unison + HOLD: the unison line follows every step and stops with it %d\n", uni);
    CHECK(uni);
}

/* (the engine's producer calls this test does not make: referenced, so -Werror's unused-function stays quiet) */
__attribute__((unused)) static void eng_rest(void)
{
    (void)eng_bend, (void)eng_wheel, (void)eng_pressure, (void)eng_volume, (void)eng_tune, (void)eng_fade, (void)eng_faded;
    (void)eng_chord, (void)eng_flush, (void)eng_active;
}

int main(void)
{
    seq_play_timing();
    seq_start_rules();
    seq_ext_clock(CLK_MIDI_IN);
    seq_ext_clock(CLK_MIDI_THRU);
    seq_transpose_test();
    seq_arp_exclusive();
    seq_no_stuck_notes();
    seq_midi_out_paired();
    seq_change_under();
    seq_ties_wrap();
    seq_continue_on_tie();
    seq_hold_never_latches();
    if (fails) { printf("test_seqplay: %d FAILED\n", fails); return 1; }
    printf("test_seqplay: all passed\n");
    return 0;
}
