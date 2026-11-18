/* SPDX-License-Identifier: GPL-3.0-only */
/* The arpeggiator (firmware/src/synth/arp.c, Phase 2 Task 6), alone on a synth and through the engine (engine.c) as
 * app.c runs it: every mode's cycle for a C-E-G chord at 1, 2 and 3 octaves (Up+Down without repeating the ends,
 * Assign in the order played, Random seeded: the same seed the same run); the gate (50 % of the step) and the played
 * key's velocity; HOLD: latch, relatch (a new chord after every key is up replaces the old), append (a key added while
 * one is held); HOLD off, ARP off, all notes off, a program load and panic leave no note held; a key never sounds
 * directly while the arp is on; an external clock absent or stopped keeps the arp silent (Review Focus 4) and its
 * note released; arp + unison; arp notes start at the clock step's frame (sample-accurate), ARP BEAT SYNC; the MIDI
 * out ring. */
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

static synth_t S;
static arp_t A;

static void fresh(int mode, int range)
{
    synth_init(&S);
    arp_init(&A, 12345u);
    arp_set(&A, &S, P_ARP_ON, 1);
    arp_set(&A, &S, P_ARP_MODE, mode);
    arp_set(&A, &S, P_ARP_RANGE, range);
}
/* n steps of STEP frames: the note each step started (-1 none) */
static void steps(int n, int *out)
{
    for (int i = 0; i < n; i++) {
        A.step_len = STEP;
        arp_step(&A, &S);
        out[i] = A.note;
        arp_run(&A, &S, STEP);
    }
}
/* voices with their key down (a note held, not released) */
static int keyed(const synth_t *s)
{
    int n = 0;
    for (int i = 0; i < NVOICE; i++) n += s->voice[i].busy && (s->voice[i].key || s->voice[i].latched);
    return n;
}
static int same(const int *a, const int *b, int n)
{
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}
static void show(const char *what, const int *v, int n)
{
    printf("  %-22s", what);
    for (int i = 0; i < n; i++) printf(" %d", v[i]);
    printf("\n");
}

/* every mode's cycle, C E G (60 64 67) played E C G (for ASSIGN's order), 1..3 octaves */
static void modes_cycles(void)
{
    static const int UP[3][20] = {
        {60, 64, 67, 60, 64, 67, 60, 64, 67, 60, 64, 67, 60, 64, 67, 60, 64, 67, 60, 64},
        {60, 64, 67, 72, 76, 79, 60, 64, 67, 72, 76, 79, 60, 64, 67, 72, 76, 79, 60, 64},
        {60, 64, 67, 72, 76, 79, 84, 88, 91, 60, 64, 67, 72, 76, 79, 84, 88, 91, 60, 64}};
    static const int DN[3][20] = {
        {67, 64, 60, 67, 64, 60, 67, 64, 60, 67, 64, 60, 67, 64, 60, 67, 64, 60, 67, 64},
        {79, 76, 72, 67, 64, 60, 79, 76, 72, 67, 64, 60, 79, 76, 72, 67, 64, 60, 79, 76},
        {91, 88, 84, 79, 76, 72, 67, 64, 60, 91, 88, 84, 79, 76, 72, 67, 64, 60, 91, 88}};
    static const int UD[3][20] = {
        {60, 64, 67, 64, 60, 64, 67, 64, 60, 64, 67, 64, 60, 64, 67, 64, 60, 64, 67, 64},
        {60, 64, 67, 72, 76, 79, 76, 72, 67, 64, 60, 64, 67, 72, 76, 79, 76, 72, 67, 64},
        {60, 64, 67, 72, 76, 79, 84, 88, 91, 88, 84, 79, 76, 72, 67, 64, 60, 64, 67, 72}};
    static const int AS[3][20] = {
        {64, 60, 67, 64, 60, 67, 64, 60, 67, 64, 60, 67, 64, 60, 67, 64, 60, 67, 64, 60},
        {64, 60, 67, 76, 72, 79, 64, 60, 67, 76, 72, 79, 64, 60, 67, 76, 72, 79, 64, 60},
        {64, 60, 67, 76, 72, 79, 88, 84, 91, 64, 60, 67, 76, 72, 79, 88, 84, 91, 64, 60}};
    static const char *const NAME[ARP_MODES] = {"UP", "DOWN", "UP+DOWN", "RANDOM", "ASSIGN"};
    int got[20], bad = 0;
    for (int m = 0; m < ARP_MODES; m++)
        for (int r = 0; r < 3; r++) {
            const int *want = m == ARP_UP ? UP[r] : m == ARP_DOWN ? DN[r] : m == ARP_UPDOWN ? UD[r] : AS[r];
            char nm[32];
            fresh(m, r);
            arp_key(&A, 64, 100), arp_key(&A, 60, 90), arp_key(&A, 67, 80);
            steps(20, got);
            snprintf(nm, sizeof nm, "%s %d oct:", NAME[m], r + 1);
            show(nm, got, 20);
            if (m == ARP_RANDOM) {               /* every note of UP's cycle, nothing else; seeded: run again, the same */
                int again[200], first[200], seen[128] = {0}, ok = 1, cover = 1;
                fresh(m, r);
                arp_key(&A, 64, 100), arp_key(&A, 60, 90), arp_key(&A, 67, 80);
                steps(200, first);
                fresh(m, r);
                arp_key(&A, 64, 100), arp_key(&A, 60, 90), arp_key(&A, 67, 80);
                steps(200, again);
                for (int i = 0; i < 200; i++) {
                    int in = 0;
                    for (int k = 0; k < 3 * (r + 1); k++) in |= first[i] == UP[r][k];
                    ok &= in;
                    if (first[i] >= 0 && first[i] < 128) seen[first[i]] = 1;
                }
                for (int k = 0; k < 3 * (r + 1); k++) cover &= seen[UP[r][k]];
                printf("  RANDOM %d oct: in the cycle %d, all %d notes seen %d, seeded (same twice) %d\n", r + 1, ok,
                       3 * (r + 1), cover, same(first, again, 200));
                if (!ok || !cover || !same(first, again, 200)) bad++;
            } else if (!same(got, want, 20)) {
                show("  want:", want, 20);
                bad++;
            }
        }
    printf("modes_cycles: %d mode / octave cases wrong\n", bad);
    CHECK(bad == 0);
}

/* the gate: 50 % of the step; the velocity: the key's; nothing sounds before the first step */
static void gate_and_velocity(void)
{
    int before, mid, end, vel_ok, until;
    fresh(ARP_UP, 0);
    arp_key(&A, 60, 77);
    before = synth_active(&S);
    A.step_len = 2000;
    arp_step(&A, &S);
    until = (int)arp_until(&A);
    vel_ok = S.voice[0].busy ? 0 : 1;
    for (int i = 0; i < NVOICE; i++) if (S.voice[i].busy && S.voice[i].note == 60) vel_ok = S.voice[i].vel == 77;
    arp_run(&A, &S, 999);
    mid = keyed(&S);
    arp_run(&A, &S, 1);
    end = keyed(&S);
    printf("gate_and_velocity: before the first step %d voices; gate %d frames of a 2000 step; held at 999 %d, at "
           "1000 %d; velocity 77 %d; arp_until after %u\n", before, until, mid, end, vel_ok, (unsigned)arp_until(&A));
    CHECK(before == 0 && until == 1000 && mid == 1 && end == 0 && vel_ok && arp_until(&A) == ARP_NONE);
}

/* HOLD: latch (keys let go stay), relatch (a new chord after every key is up replaces it), append (a key added while
 * one is still held); HOLD off: the latched keys go, no note left held */
static void hold_latch_relatch_append(void)
{
    int got[6], start_relatch, start_append, latch_ok, relatch_ok, append_ok, off_ok, pat_after_off;
    static const int L[6] = {60, 64, 67, 60, 64, 67}, R[6] = {62, 62, 62, 62, 62, 62}, P[6] = {62, 65, 69, 62, 65, 69};
    fresh(ARP_UP, 0);
    arp_hold(&A, 1);
    arp_key(&A, 60, 100), arp_key(&A, 64, 100), arp_key(&A, 67, 100);
    arp_key(&A, 60, 0), arp_key(&A, 64, 0), arp_key(&A, 67, 0);          /* all let go: latched */
    steps(6, got);
    show("latched C E G:", got, 6);
    latch_ok = same(got, L, 6) && A.n == 3;
    start_relatch = arp_key(&A, 62, 100);                                 /* none held: a new pattern */
    steps(6, got);
    show("relatch D:", got, 6);
    relatch_ok = same(got, R, 6) && A.n == 1;
    start_append = arp_key(&A, 65, 100);                                  /* D still held: appended */
    arp_key(&A, 62, 0);
    arp_key(&A, 69, 100);                                                 /* F still held: appended */
    arp_key(&A, 65, 0), arp_key(&A, 69, 0);
    A.pos = 0;                                                            /* (from the cycle's start, to compare) */
    steps(6, got);
    show("append D F A:", got, 6);
    append_ok = same(got, P, 6) && A.n == 3;
    arp_hold(&A, 0);                                                      /* HOLD off: every key is up */
    pat_after_off = A.n;
    steps(2, got);
    off_ok = A.n == 0 && got[0] == -1 && got[1] == -1 && keyed(&S) == 0;
    printf("hold_latch_relatch_append: latch %d, relatch %d (start %d), append %d (start %d), HOLD off: pattern %d, "
           "silent %d\n", latch_ok, relatch_ok, start_relatch, append_ok, start_append, pat_after_off, off_ok);
    CHECK(latch_ok && relatch_ok && start_relatch == 1 && append_ok && start_append == 0 && off_ok);
    /* the program's ARP HOLD latches the same way */
    fresh(ARP_UP, 0);
    arp_set(&A, &S, P_ARP_HOLD, 1);
    arp_key(&A, 60, 100), arp_key(&A, 60, 0);
    steps(2, got);
    latch_ok = got[0] == 60 && got[1] == 60;
    arp_set(&A, &S, P_ARP_HOLD, 0);
    steps(1, got);
    printf("hold_latch_relatch_append: the program's ARP HOLD latches %d, off releases %d\n", latch_ok, got[0] == -1);
    CHECK(latch_ok && got[0] == -1 && keyed(&S) == 0);
}

/* without HOLD: a key let go leaves the pattern; the last one let go stops it (the note sounding ends at its gate) */
static void keys_leave(void)
{
    int got[4], ok;
    fresh(ARP_UP, 0);
    arp_key(&A, 60, 100), arp_key(&A, 64, 100);
    steps(1, got);
    arp_key(&A, 60, 0);
    steps(2, got + 1);
    arp_key(&A, 64, 0);
    steps(1, got + 3);
    ok = got[0] == 60 && got[1] == 64 && got[2] == 64 && got[3] == -1 && keyed(&S) == 0;
    show("keys_leave:", got, 4);
    CHECK(ok);
}

/* ARP off: the arp's note released, the keys still held handed to the synth (they sound as played); ARP on again
 * takes them back (the synth's stop); a key while the arp is on never sounds before a step */
static void arp_off_on(void)
{
    int direct, after_off, held_sound, on_again, started;
    fresh(ARP_UP, 0);
    arp_key(&A, 60, 100);
    direct = synth_active(&S);
    A.step_len = STEP;
    arp_step(&A, &S);
    arp_key(&A, 64, 100);
    arp_set(&A, &S, P_ARP_ON, 0);
    after_off = A.note == -1 && A.n == 0;
    held_sound = 0;
    for (int i = 0; i < NVOICE; i++) held_sound += S.voice[i].busy && S.voice[i].key && (S.voice[i].note == 60 || S.voice[i].note == 64);
    started = arp_set(&A, &S, P_ARP_ON, 1);
    on_again = keyed(&S) == 0 && A.n == 2;
    printf("arp_off_on: a key with the arp on sounds directly %d; off: arp note released, pattern emptied %d, the 2 keys "
           "held sound %d; on again: taken back %d, a start %d\n", direct, after_off, held_sound, on_again, started);
    CHECK(direct == 0 && after_off && held_sound == 2 && on_again && started == 1);
    arp_clear(&A, &S);
    CHECK(keyed(&S) == 0 && A.n == 0 && A.note == -1);
}

/* arp + unison: each arp note plays the whole stack (6 voices), released at the gate */
static void arp_unison(void)
{
    int stack_ok = 1, rel_ok = 1, notes[4];
    fresh(ARP_UP, 0);
    synth_param(&S, P_UNISON_ON, 1);
    synth_param(&S, P_UNISON_VOICES, 5);
    arp_key(&A, 60, 100), arp_key(&A, 64, 100);
    for (int k = 0; k < 4; k++) {
        int on = 0;
        A.step_len = STEP;
        arp_step(&A, &S);
        notes[k] = A.note;
        for (int i = 0; i < NVOICE; i++) on += S.voice[i].busy && S.voice[i].key && S.voice[i].note == A.note;
        stack_ok &= on == NVOICE && S.mono == A.note;
        arp_run(&A, &S, STEP);
        rel_ok &= keyed(&S) == 0 && S.mono == -1;
    }
    printf("arp_unison: notes %d %d %d %d; each on the 6-voice stack %d, released at the gate %d\n", notes[0], notes[1],
           notes[2], notes[3], stack_ok, rel_ok);
    CHECK(stack_ok && rel_ok && notes[0] == 60 && notes[1] == 64 && notes[2] == 60);
}

/* ---- through the engine (engine.c), 256-frame blocks as app_audio */
static int32_t OUT[2u * 44100u * 4u];
static void render_at(uint32_t f0, uint32_t frames)
{
    for (uint32_t f = f0; f < f0 + frames; f += BLK) {
        memset(&OUT[2u * f], 0, 2u * BLK * sizeof OUT[0]);
        eng_render(&OUT[2u * f], BLK);
    }
}
static void render(uint32_t frames) { render_at(0, frames); }
static int eng_keyed(void) { return keyed(&eng_synth); }

/* Review Focus 4: an external clock (MIDI CLOCK IN) absent: the arp is silent; started: it plays; stopped (0xFC) and
 * gone (no ticks): silent, its note released; no note stuck */
static void ext_clock_silent(void)
{
    int none, playing, stop_rel, gone_rel, any_none = 0, any_stop = 0, any_gone = 0;
    uint32_t t, next;
    eng_init();
    eng_clock_mode(CLK_MIDI_IN);
    eng_param(P_ARP_ON, 1);
    eng_param(P_CLK_DIV, CLK_DIV_16TH);
    eng_note(60, 100), eng_note(64, 100);
    for (int b = 0; b < 200; b++) { render(BLK); any_none |= eng_active() != 0; }   /* ~1.2 s, no clock */
    none = !any_none && eng_arp.note == -1;
    eng_midi_rt(0xFA);
    for (t = 0, next = 0; t < 120u * BLK; t += BLK) {                     /* 120 BPM: a tick every 918.75 frames */
        while (next <= t * 4u) eng_midi_rt(0xF8), next += 3675u;
        render(BLK);
    }
    playing = eng_arp.steps > 4u;
    eng_midi_rt(0xFC);                                                    /* stopped: the ticks go on */
    for (int b = 0; b < 100; b++) {
        while (next <= t * 4u) eng_midi_rt(0xF8), next += 3675u;
        render(BLK), t += BLK;
        if (b > 0) any_stop |= eng_keyed() != 0;
    }
    stop_rel = !any_stop && eng_arp.note == -1;
    eng_midi_rt(0xFB);                                                    /* continue, then the clock goes away */
    for (int b = 0; b < 40; b++) {
        while (next <= t * 4u) eng_midi_rt(0xF8), next += 3675u;
        render(BLK), t += BLK;
    }
    for (int b = 0; b < 200; b++) { render(BLK); if (b > 50) any_gone |= eng_keyed() != 0; }   /* > 0.25 s: gone */
    gone_rel = !any_gone && eng_arp.note == -1 && !eng_clk_run;
    printf("ext_clock_silent: no clock: silent %d; 0xFA + clock: %u notes played %d; 0xFC: released and silent %d; clock "
           "gone: released and silent %d\n", none, (unsigned)eng_arp.steps, playing, stop_rel, gone_rel);
    CHECK(none && playing && stop_rel && gone_rel);
}

/* sample-accurate: with ARP BEAT SYNC on, a key at frame 2560 waits for the next quarter at 120 BPM (frame 22050) and
 * the sound starts there, not at a block's start; with it off the clock restarts: the note starts at the block's frame
 * 0 (the block the key reached the engine in); its note-off at half the step */
static void step_frame_exact(void)
{
    uint32_t first = 0, i, gate_off_ok;
    eng_init();
    eng_arp_sync(1);
    eng_param(P_ARP_ON, 1);
    render(10u * BLK);
    eng_note(60, 100);
    render_at(10u * BLK, 90u * BLK);                                      /* to frame 25600 */
    for (i = 0; i < 25600u; i++) if (OUT[2u * i] || OUT[2u * i + 1u]) { first = i; break; }
    printf("step_frame_exact: BEAT SYNC on: a key at frame 2560, the first sound at frame %u (the step at 22050)\n", first);
    CHECK(first >= 22050u && first <= 22052u);
    eng_init();                                                           /* BEAT SYNC off: the clock restarts */
    eng_arp_sync(0);
    eng_param(P_ARP_ON, 1);
    render(10u * BLK + 128u);                                             /* (the clock mid-step) */
    eng_note(60, 100);
    render_at(0, 4u * BLK);
    first = ~0u;
    for (i = 0; i < 4u * BLK; i++) if (OUT[2u * i] || OUT[2u * i + 1u]) { first = i; break; }
    render_at(0, 44u * BLK);                                              /* 11264 frames: past the gate (11025) */
    gate_off_ok = eng_keyed() == 0;
    printf("step_frame_exact: BEAT SYNC off: the first sound at frame %u of the key's block (0); released at the half "
           "step %d; clock restarts %u\n", first, gate_off_ok, (unsigned)eng_clk.starts);
    CHECK(first <= 2u && gate_off_ok);
}

/* final review 1: under MIDI CLOCK = OUT an arp start never restarts the clock (a slave would jump to its bar start on
 * every chord): no clock_restart, so no extra 0xFA from clock_out_poll; the pattern waits for the next step of the
 * division, as with ARP BEAT SYNC on (here off, the default). OUT's own start (step 0 at frame 0) is the only 0xFA */
static void out_no_restart(void)
{
    clk_out_t o;
    uint8_t b[3u + CLOCK_OUT_OWED];
    uint32_t fa = 0, starts, first = ~0u, i, n, k;
    eng_init();
    eng_arp_sync(0);
    eng_clock_mode(CLK_MIDI_OUT);
    eng_param(P_ARP_ON, 1);
    clock_out_init(&o);
    for (k = 0; k < 100u; k++) {                                          /* 100 blocks: the key at block 10 */
        if (k == 10u) eng_note(60, 100);
        memset(&OUT[2u * k * BLK], 0, 2u * BLK * sizeof OUT[0]);
        eng_render(&OUT[2u * k * BLK], BLK);
        n = clock_out_poll(&o, CLK_MIDI_OUT, eng_clk_mode, eng_clk_starts, eng_clk_start_ticks, eng_clk_ticks, b, sizeof b);
        for (i = 0; i < n; i++) fa += b[i] == 0xFAu;
    }
    starts = eng_clk.starts;
    for (i = 0; i < 100u * BLK; i++) if (OUT[2u * i] || OUT[2u * i + 1u]) { first = i; break; }
    printf("out_no_restart: MIDI CLOCK OUT, BEAT SYNC off: a key at frame 2560: clock starts %u, 0xFA sent %u, the "
           "first sound at frame %u (the next quarter at 22050)\n", (unsigned)starts, (unsigned)fa, first);
    CHECK(starts == 1u && fa == 1u);
    CHECK(first >= 22050u && first <= 22052u);
}

/* the engine's paths: a key never sounds directly with the arp on; HOLD from the panel latches the arp, not the synth;
 * all notes off, program load, panic, ARP off: nothing left held; MIDI out ring carries the arp's notes */
static void engine_paths(void)
{
    int direct, latched, alloff, prog, panic, off, outs_ok;
    uint32_t w0;
    program_t p;
    eng_init();
    eng_arp_sync(0);
    eng_param(P_ARP_ON, 1);
    eng_note(60, 100);
    eng_render(OUT, 1);                                                   /* (drained, the step at frame 0) */
    eng_note(64, 100);
    eng_render(OUT, 1);
    direct = 0;
    for (int i = 0; i < NVOICE; i++) direct += eng_synth.voice[i].busy && eng_synth.voice[i].note == 64;
    eng_hold(1);
    eng_note(60, 0), eng_note(64, 0);
    render(200u * BLK);
    latched = eng_arp.n == 2 && !eng_synth.hold && eng_arp.steps > 2u;
    eng_all_notes_off();
    render(4u * BLK);
    alloff = eng_keyed() == 0 && eng_arp.n == 0;
    eng_note(62, 100);
    render(30u * BLK);
    program_init(&p);
    p.v[P_ARP_ON] = 1;
    eng_program(&p);
    render(4u * BLK);
    prog = eng_keyed() == 0 && eng_arp.n == 0 && eng_arp.on;
    eng_note(65, 100);
    render(30u * BLK);
    eng_panic();
    render(4u * BLK);
    panic = eng_keyed() == 0 && eng_arp.n == 0 && !eng_arp.hold_ext;
    eng_note(67, 100);
    render(30u * BLK);
    w0 = eng_arp.out_w;
    eng_param(P_ARP_ON, 0);
    render(BLK);
    off = !eng_arp.on && eng_arp.note == -1;
    for (int i = 0; i < NVOICE; i++) off &= !(eng_synth.voice[i].busy && eng_synth.voice[i].key && eng_synth.voice[i].note != 67);
    outs_ok = eng_arp.out_w - w0 == 1u && eng_arp.out[w0 % ARP_OUT_N][0] == 67 && eng_arp.out[w0 % ARP_OUT_N][1] == 0;
    eng_note(67, 0);
    render(BLK);
    printf("engine_paths: a 2nd key sounds directly %d; HOLD latches the arp (not the synth) %d; all notes off %d; "
           "program load %d; panic %d; ARP off: the arp's note out, the held key plays %d; MIDI out: its note-off %d; "
           "after: held voices %d\n", direct, latched, alloff, prog, panic, off, outs_ok, eng_keyed());
    CHECK(direct == 0 && latched && alloff && prog && panic && off && outs_ok && eng_keyed() == 0);
}

/* the MIDI out ring: an on and an off per note, in order, with the key's velocity */
static void midi_out_ring(void)
{
    uint32_t w0;
    int ok;
    fresh(ARP_UP, 0);
    w0 = A.out_w;
    arp_key(&A, 60, 90), arp_key(&A, 64, 70);
    int got[2];
    steps(2, got);
    ok = A.out_w - w0 == 4u && A.out[w0][0] == 60 && A.out[w0][1] == 90 && A.out[w0 + 1][0] == 60 && A.out[w0 + 1][1] == 0 &&
         A.out[w0 + 2][0] == 64 && A.out[w0 + 2][1] == 70 && A.out[w0 + 3][1] == 0;
    printf("midi_out_ring: %u events, on/off pairs with the key's velocity %d\n", (unsigned)(A.out_w - w0), ok);
    CHECK(ok);
}

/* (the engine's producer calls this test does not make: referenced, so -Werror's unused-function stays quiet) */
__attribute__((unused)) static void eng_rest(void)
{
    (void)eng_bend, (void)eng_wheel, (void)eng_pressure, (void)eng_volume, (void)eng_tune, (void)eng_fade, (void)eng_faded;
    (void)eng_chord, (void)eng_flush;
}

int main(void)
{
    modes_cycles();
    gate_and_velocity();
    hold_latch_relatch_append();
    keys_leave();
    arp_off_on();
    arp_unison();
    midi_out_ring();
    ext_clock_silent();
    step_frame_exact();
    out_no_restart();
    engine_paths();
    if (fails) { printf("test_arp: %d FAILED\n", fails); return 1; }
    printf("test_arp: all passed\n");
    return 0;
}
