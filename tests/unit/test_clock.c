/* SPDX-License-Identifier: GPL-3.0-only */
/* The clock (firmware/src/synth/clock.c, Phase 2 Task 5): step timing at 30 / 120 / 250 BPM (and 97, not a whole number
 * of frames a step) within 1 frame for every step over many bars, no drift; tap tempo (the average of the last 4
 * intervals, 4 taps first, a gap > 2 s restarts); the division table (the reference's rates); swing timing; a BPM
 * change keeps the position; MIDI clock in at 120 BPM delivered at block starts as engine.c does: locked within 2
 * beats, stopped by 0xFC and by the clock going away, not running without it; the ticks counted for MIDI out; the LFO's
 * quarter-note increment. Built a second time with the host's transport compiled in (test_clock_host.c, CLOCK_HOST):
 * the same tests, the same results, and the host's: transport_start_bar, transport_mid_bar (a count-in too),
 * transport_loop (a wrap inside a block, a loop from inside a step, a wrap at a block's start), transport_locate,
 * transport_tempo_ramp, transport_stop_internal, bpm_param_greyed. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "synth/clock.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define BLOCK 256u

/* runs c for `frames` in blocks, the step events' absolute frames into ev (up to max); returns how many */
static uint32_t run(clk_t *c, uint32_t *t0, uint32_t frames, uint64_t *ev, uint32_t max)
{
    uint32_t n = 0;
    while (frames) {
        uint32_t b = frames < BLOCK ? frames : BLOCK;
        clock_tick(c, b);
        for (uint32_t i = 0; i < c->n; i++) if (n < max) ev[n++] = (uint64_t)*t0 + c->at[i];
        *t0 += b, frames -= b;
    }
    return n;
}

/* exact: step k (of equal length `ticks`) at ceil(k x ticks x 110250 / bpm) frames */
static void bpm_timing_one(int bpm, int div, int bars)
{
    static uint64_t ev[4096];
    clk_t c;
    uint32_t t = 0, n, ticks = CLOCK_DIV_TICKS[div][0], want_n;
    int worst = 0, order = 1;
    uint64_t frames = (uint64_t)bars * 4u * 24u * 110250u / (uint64_t)bpm;
    clock_init(&c);
    clock_set_bpm(&c, bpm);
    clock_set_div(&c, div);
    n = run(&c, &t, (uint32_t)frames, ev, 4096);
    want_n = (uint32_t)((uint64_t)bars * 96u / ticks);          /* boundaries 0 .. strictly before the end */
    for (uint32_t k = 0; k < n; k++) {
        uint64_t num = (uint64_t)k * ticks * 110250u, want = (num + (uint64_t)bpm - 1u) / (uint64_t)bpm;
        int d = (int)((int64_t)ev[k] - (int64_t)want);
        if (d < 0) d = -d;
        if (d > worst) worst = d;
        if (k && ev[k] <= ev[k - 1]) order = 0;
    }
    printf("bpm_timing: %3d BPM, %d bars of %s: %u steps (want %u), worst %d frame(s) off, last at %llu\n", bpm, bars,
           div == CLK_DIV_QTR ? "quarters" : "16ths", n, want_n, worst, (unsigned long long)(n ? ev[n - 1] : 0));
    CHECK(n == want_n);
    CHECK(worst <= 1);
    CHECK(order);
}
static void bpm_timing(void)
{
    bpm_timing_one(30, CLK_DIV_QTR, 8);
    bpm_timing_one(120, CLK_DIV_QTR, 64);
    bpm_timing_one(250, CLK_DIV_QTR, 64);
    bpm_timing_one(250, CLK_DIV_16TH, 32);
    bpm_timing_one(97, CLK_DIV_16TH, 32);                       /* 6 ticks = 6819.6 frames: never a whole number */
    {                                                          /* a bar = 4 x 22050 at 120: exact, bar after bar */
        static uint64_t ev[2048];
        clk_t c;
        uint32_t t = 0, n, bad = 0;
        clock_init(&c);
        n = run(&c, &t, 400u * 88200u + 1u, ev, 2048);           /* 400 bars, one block at a time */
        for (uint32_t k = 0; k < n; k++) bad += ev[k] != (uint64_t)k * 22050u;
        printf("bpm_timing: 120 BPM over 400 bars: %u quarters, %u not on k x 22050\n", n, bad);
        CHECK(n == 1601u && bad == 0);
    }
}

static void tap_tempo(void)
{
    clk_t c;
    int r1, r2, r3, r4, r5, r6, gap1, gap2, gap3, gap4, slow, fast;
    clock_init(&c);
    r1 = clock_tap(&c, 1000);
    r2 = clock_tap(&c, 1500);
    r3 = clock_tap(&c, 2000);
    r4 = clock_tap(&c, 2500);                   /* 4 taps, 3 intervals of 500 ms: 120 */
    r5 = clock_tap(&c, 2900);                   /* + 400: (500 + 500 + 500 + 400) / 4 = 475 ms: 126.3 -> 126 */
    r6 = clock_tap(&c, 3300);                   /* last 4: 500 500 400 400 = 450 ms: 133.3 -> 133 */
    printf("tap_tempo: taps 1..3 -> %d %d %d, 4th -> %d, 5th -> %d, 6th -> %d (bpm now %d)\n", r1, r2, r3, r4, r5, r6, c.bpm);
    CHECK(r1 == 0 && r2 == 0 && r3 == 0 && r4 == 120 && r5 == 126 && r6 == 133 && c.bpm == 133);
    gap1 = clock_tap(&c, 6000);                 /* > 2 s later: a new count */
    gap2 = clock_tap(&c, 6600);
    gap3 = clock_tap(&c, 7200);
    gap4 = clock_tap(&c, 7800);                 /* 600 ms: 100 */
    printf("tap_tempo: after a 2.7 s gap -> %d %d %d %d\n", gap1, gap2, gap3, gap4);
    CHECK(gap1 == 0 && gap2 == 0 && gap3 == 0 && gap4 == 100);
    clock_init(&c);
    clock_tap(&c, 0), clock_tap(&c, 1990), clock_tap(&c, 3980);
    slow = clock_tap(&c, 5970);                 /* 1990 ms (under the gap): 30.15 -> 30 */
    clock_init(&c);
    clock_tap(&c, 0), clock_tap(&c, 100), clock_tap(&c, 200);
    fast = clock_tap(&c, 300);                  /* 600 BPM: clamped to 250 */
    printf("tap_tempo: 1990 ms -> %d, 100 ms -> %d\n", slow, fast);
    CHECK(slow == 30 && fast == 250);
}

/* the reference's rates: steps a quarter, x2 per pair (Half 0.5 .. 32nd 8) */
static void divisions(void)
{
    static const uint8_t pair_rate_x2[CLK_DIV_COUNT] = {1, 2, 4, 4, 4, 6, 8, 8, 12, 16};   /* steps a quarter x 2 */
    int ok = 1;
    for (int d = 0; d < CLK_DIV_COUNT; d++) {
        uint32_t pair = CLOCK_DIV_TICKS[d][0] + CLOCK_DIV_TICKS[d][1];
        ok &= 2u * 24u * 2u == pair * pair_rate_x2[d];            /* 2 steps take 2 x 24 / rate ticks */
        ok &= CLOCK_DIV_TICKS[d][0] >= CLOCK_DIV_TICKS[d][1];     /* the long one first */
    }
    printf("divisions: rates %s; 8th D %u+%u, 8th S %u+%u, 16th S %u+%u, 8th T %u, 16th T %u, 32nd %u ticks\n",
           ok ? "as the reference" : "WRONG", CLOCK_DIV_TICKS[3][0], CLOCK_DIV_TICKS[3][1], CLOCK_DIV_TICKS[4][0],
           CLOCK_DIV_TICKS[4][1], CLOCK_DIV_TICKS[7][0], CLOCK_DIV_TICKS[7][1], CLOCK_DIV_TICKS[5][0],
           CLOCK_DIV_TICKS[8][0], CLOCK_DIV_TICKS[9][0]);
    CHECK(ok);
    CHECK(CLOCK_DIV_TICKS[CLK_DIV_HALF][0] == 48 && CLOCK_DIV_TICKS[CLK_DIV_QTR][0] == 24);
    CHECK(CLOCK_DIV_TICKS[CLK_DIV_8TH_S][0] != CLOCK_DIV_TICKS[CLK_DIV_8TH_S][1]);   /* a swing is uneven */
    CHECK(CLOCK_DIV_TICKS[CLK_DIV_16TH_S][0] != CLOCK_DIV_TICKS[CLK_DIV_16TH_S][1]);
    CHECK(CLOCK_DIV_TICKS[CLK_DIV_8TH_T][0] == 8 && CLOCK_DIV_TICKS[CLK_DIV_16TH_T][0] == 4);
}

/* swing at 120 BPM (918.75 frames a tick): 8th S long 16 ticks, short 8: steps at 0, 14700, 22050, 36750, 44100 ... */
static void swing_timing(void)
{
    static uint64_t ev[64];
    clk_t c;
    uint32_t t = 0, n, bad = 0, k;
    clock_init(&c);
    clock_set_div(&c, CLK_DIV_8TH_S);
    n = run(&c, &t, 8u * 22050u, ev, 64);        /* 8 beats: 16 steps */
    for (k = 0; k < n; k++) {
        uint64_t want = (uint64_t)(k / 2u) * 22050u + (k & 1u ? 14700u : 0u);
        bad += ev[k] != want;
    }
    printf("swing_timing: 8th S at 120: %u steps, %u off; steps 0..3 at %llu %llu %llu %llu\n", n, bad,
           (unsigned long long)ev[0], (unsigned long long)ev[1], (unsigned long long)ev[2], (unsigned long long)ev[3]);
    CHECK(n == 16u && bad == 0);
    clock_init(&c);                              /* 16th S at 250 BPM: 8 + 4 ticks of 441 frames */
    clock_set_bpm(&c, 250);
    clock_set_div(&c, CLK_DIV_16TH_S);
    t = 0;
    n = run(&c, &t, 2u * 10584u, ev, 64);
    for (k = 0, bad = 0; k < n; k++) bad += ev[k] != (uint64_t)(k / 2u) * 5292u + (k & 1u ? 3528u : 0u);
    printf("swing_timing: 16th S at 250: %u steps, %u off\n", n, bad);
    CHECK(n == 8u && bad == 0);                  /* 2 beats of 16ths */
    clock_init(&c);                              /* 8th D: dotted pairs, 18 + 6 ticks */
    clock_set_div(&c, CLK_DIV_8TH_D);
    t = 0;
    n = run(&c, &t, 2u * 22050u, ev, 64);
    for (k = 0, bad = 0; k < n; k++) bad += ev[k] != (uint64_t)(k / 2u) * 22050u + (k & 1u ? 16538u : 0u);   /* ceil(18 x 918.75) */
    printf("swing_timing: 8th D at 120: %u steps, %u off\n", n, bad);
    CHECK(n == 4u && bad == 0);
}

/* a BPM change mid-step keeps the position: the step after it comes at the new rate from where it was */
static void bpm_change_and_restart(void)
{
    static uint64_t ev[16];
    clk_t c;
    uint32_t t = 0, n, k;
    clock_init(&c);
    n = run(&c, &t, 11264u, ev, 16);             /* 44 blocks = 11264 frames into the first quarter (of 22050) */
    clock_set_bpm(&c, 60);                       /* the rest of it, 10786 x (120 / 60) frames: next at 11264 + 21572 */
    k = run(&c, &t, 40000u, ev + n, 16);
    printf("bpm_change: steps %u then %u, the next at %llu (want 32836)\n", n, k, (unsigned long long)ev[n]);
    CHECK(n == 1u && k == 1u && ev[1] == 32836u);
    clock_restart(&c);                           /* step 0 now */
    clock_tick(&c, BLOCK);
    printf("restart: %u step(s), at %u, step no %u\n", c.n, c.at[0], c.step[0]);
    CHECK(c.n == 1u && c.at[0] == 0u && c.step[0] == 0u);
}

/* MIDI clock in at 120 BPM: a tick every 918.75 frames; engine.c drains the queue at each block's start, so a tick is
 * seen at the first block start at or after it was sent */
typedef struct { clk_t c; uint32_t t; uint64_t next_tick; uint32_t sent; } sim_t;   /* next_tick: x 4 (exact) */
static uint32_t sim_run(sim_t *s, uint32_t frames, int send, uint64_t *ev, uint32_t max, uint32_t *n)
{
    uint32_t steps = 0;
    while (frames) {
        uint32_t b = frames < BLOCK ? frames : BLOCK;
        while (send && s->next_tick <= (uint64_t)s->t * 4u) { clock_midi(&s->c, 0xF8, s->t); s->next_tick += 3675u; s->sent++; }
        if (!send) while (s->next_tick <= (uint64_t)s->t * 4u) s->next_tick += 3675u;   /* (the master's time goes on) */
        clock_tick(&s->c, b);
        for (uint32_t i = 0; i < s->c.n; i++) { if (*n < max) ev[(*n)++] = (uint64_t)s->t + s->c.at[i]; steps++; }
        s->t += b, frames -= b;
    }
    return steps;
}
static void midi_clock_in(void)
{
    static uint64_t ev[256];
    sim_t s;
    uint32_t n = 0, k, early = 0, late = 0, gaps = 0, none, after_stop, after_gone, nostart, nss;
    int bpm2, run_before, run_on, run_stop, run_gone, run_back;
    clock_init(&s.c);
    clock_set_bpm(&s.c, 77);                     /* (the program's BPM: ignored while following) */
    clock_set_mode(&s.c, CLK_MIDI_IN);
    s.t = 0, s.next_tick = 4u * 1000u, s.sent = 0;
    none = sim_run(&s, 44100u, 0, ev, 256, &n);  /* 1 s: no clock at all */
    run_before = clock_running(&s.c);
    clock_midi(&s.c, 0xFA, s.t);                 /* start, then the ticks: the first is the downbeat */
    s.next_tick = (uint64_t)s.t * 4u + 4u * 100u;
    {
        uint64_t t0 = s.next_tick;               /* (x 4) the downbeat */
        sim_run(&s, 2u * 22050u + 600u, 1, ev, 256, &n);   /* 2 beats (the 49th tick, beat 3, is in) */
        bpm2 = clock_bpm_x10(&s.c);
        run_on = clock_running(&s.c);
        sim_run(&s, 6u * 22050u, 1, ev, 256, &n);   /* 6 more beats */
        for (k = 0; k < n; k++) {                /* step k (quarters) at the k x 24th tick, seen at a block start */
            uint64_t sent = (t0 + (uint64_t)k * 24u * 3675u + 3u) / 4u;
            if (ev[k] < sent) early++;
            if (ev[k] > sent + BLOCK) late++;
            if (k && (ev[k] - ev[k - 1] < 22050u - BLOCK || ev[k] - ev[k - 1] > 22050u + BLOCK)) gaps++;
        }
    }
    printf("midi_clock_in: no clock 1 s: %u steps, running %d; started: after 2 beats %d.%d BPM, running %d; %u steps "
           "in 8 beats, %u early, %u late by more than a block, %u spacings off\n", none, run_before, bpm2 / 10, bpm2 % 10,
           run_on, n, early, late, gaps);
    CHECK(none == 0 && !run_before);
    CHECK(bpm2 >= 1185 && bpm2 <= 1215);
    CHECK(run_on && n == 9u && early == 0 && late == 0 && gaps == 0);
    clock_midi(&s.c, 0xFC, s.t);                 /* stop: the ticks go on, the steps do not */
    after_stop = sim_run(&s, 2u * 22050u, 1, ev, 256, &n);
    run_stop = clock_running(&s.c);
    clock_midi(&s.c, 0xFB, s.t);                 /* continue */
    sim_run(&s, 22050u, 1, ev, 256, &n);
    run_back = clock_running(&s.c);
    after_gone = sim_run(&s, 22050u, 0, ev, 256, &n);   /* the clock goes away (no stop) */
    after_gone = sim_run(&s, 22050u, 0, ev, 256, &n);   /* a beat later: nothing */
    run_gone = clock_running(&s.c);
    printf("midi_clock_in: 0xFC -> %u steps in 2 beats, running %d; 0xFB -> running %d; clock gone -> %u steps in the "
           "2nd beat after, running %d\n", after_stop, run_stop, run_back, after_gone, run_gone);
    CHECK(after_stop == 0 && !run_stop && run_back && after_gone == 0 && !run_gone);
    clock_init(&s.c);                            /* IN: ticks without a start do not run it */
    clock_set_mode(&s.c, CLK_MIDI_IN);
    s.t = 0, s.next_tick = 0, n = 0;
    nostart = sim_run(&s, 44100u, 1, ev, 256, &n);
    clock_init(&s.c);                            /* IN-no-start-stop: they do; 0xFC ignored */
    clock_set_mode(&s.c, CLK_MIDI_IN_NSS);
    s.t = 0, s.next_tick = 0, n = 0;
    nss = sim_run(&s, 44100u, 1, ev, 256, &n);
    clock_midi(&s.c, 0xFC, s.t);
    nss += sim_run(&s, 44100u, 1, ev, 256, &n);
    printf("midi_clock_in: IN without 0xFA %u steps in 2 beats; IN-no-start-stop %u in 4 beats (a stop between)\n",
           nostart, nss);
    CHECK(nostart == 0 && nss == 4u);
    clock_init(&s.c);                            /* an internal clock ignores MIDI clock */
    clock_midi(&s.c, 0xFC, 0);
    CHECK(clock_running(&s.c));
}

/* MIDI clock out counts 24 ticks a quarter; the LFO's increment: Q32 quarters a frame */
static void ticks_and_lfo(void)
{
    clk_t c;
    uint32_t t = 0, q;
    uint64_t ev[8];
    clock_init(&c);
    clock_set_mode(&c, CLK_MIDI_OUT);
    run(&c, &t, 4u * 22050u - 1u, ev, 0);        /* 4 beats less a frame: ticks 0..95 entered, beats 0..3 */
    q = clock_quarter_inc(&c);                   /* 2 quarters a second / 44100: 194783.9 */
    printf("ticks_and_lfo: OUT then 4 beats -> %u ticks entered (tick 0 at the start, then 1..95; 96 is the next "
           "block's frame 0), %u beats; quarter inc at 120 %u (want 194784)\n", c.ticks, c.beats, q);
    CHECK(c.ticks == 96u && c.beats == 4u);
    CHECK(q >= 194780u && q <= 194788u);
    clock_set_mode(&c, CLK_MIDI_IN);
    CHECK(clock_quarter_inc(&c) == 0u && clock_bpm_x10(&c) == 0);   /* following, no tempo known yet */
}

/* Review fix 1: MIDI clock out in phase with the steps. A sender (OUT) and a receiver (IN: this same clock) as the
 * firmware runs them: the sender ticks a block, the main loop polls clock_out_poll with what the engine publishes
 * (mode applied, starts, start_ticks, ticks) and the bytes reach the receiver at its next block's start. 0xFA comes
 * with the first 0xF8 (tick 0, step 0); each receiver step lands within one block after the sender's same step (not a
 * tick later); a clock_restart under OUT re-sends 0xFA with the new tick 0 and the receiver follows; leaving OUT sends
 * 0xFC, and the receiver stops */
typedef struct { clk_t c; clk_out_t o; } snd_t;
static uint32_t snd_poll(snd_t *s, int mode, uint8_t *b)
{
    return clock_out_poll(&s->o, mode, s->c.mode, s->c.starts, s->c.start_ticks, s->c.ticks, b, 16);
}
static void midi_clock_out(void)
{
    static uint64_t se[256], re[256];
    static uint32_t sst[256], rst[256];
    snd_t s;
    clk_t r;
    uint8_t q[64], b[16];
    uint32_t qn = 0, sn = 0, rn = 0, t = 0, k, n, fa = 0, f8 = 0, fc = 0, first_two = 0, bad = 0, polls = 0;
    int mode = CLK_MIDI_OFF, restart_done = 0, stop_done = 0, r_after_stop = 0;
    clock_init(&s.c);
    clock_out_init(&s.o);
    clock_init(&r);
    clock_set_mode(&r, CLK_MIDI_IN);
    clock_set_div(&s.c, CLK_DIV_16TH), clock_set_div(&r, CLK_DIV_16TH);
    for (uint32_t blk = 0; blk < 1200u; blk++, t += BLOCK) {          /* ~7 s */
        for (k = 0; k < qn; k++) clock_midi(&r, q[k], r.now);         /* last poll's bytes, at this block's start */
        qn = 0;
        if (blk == 10u) mode = CLK_MIDI_OUT, clock_set_mode(&s.c, mode);   /* (the engine applies it at a block start) */
        if (blk == 600u) clock_restart(&s.c), restart_done = 1;        /* Task 6's arp start, BEAT SYNC off */
        if (blk == 1000u) mode = CLK_MIDI_OFF, clock_set_mode(&s.c, mode), stop_done = 1;
        clock_tick(&s.c, BLOCK);
        for (k = 0; k < s.c.n; k++) if (sn < 256u && mode == CLK_MIDI_OUT) se[sn] = t + s.c.at[k], sst[sn++] = s.c.step[k];
        clock_tick(&r, BLOCK);
        for (k = 0; k < r.n; k++) {
            if (rn < 256u) re[rn] = t + r.at[k], rst[rn++] = r.step[k];
            if (stop_done) r_after_stop++;
        }
        n = snd_poll(&s, mode, b);
        polls += n != 0;
        for (k = 0; k < n; k++) {
            fa += b[k] == 0xFA, f8 += b[k] == 0xF8, fc += b[k] == 0xFC;
            if (b[k] == 0xFA) first_two += k + 1u < n && b[k + 1u] == 0xF8;   /* FA then at once tick 0's F8 */
            if (qn < 64u) q[qn++] = b[k];
        }
    }
    /* every sender step has the receiver's same step within (0, BLOCK] frames after it (the restart included) */
    for (k = 0; k < sn && k < rn; k++)
        if (re[k] < se[k] || re[k] - se[k] > BLOCK || rst[k] != sst[k]) bad++;
    printf("midi_clock_out: sent FA %u (FA then F8 at once %u), F8 %u, FC %u; sender %u steps, receiver %u, %u not "
           "within a block after or a different step number; restart re-sent FA %d; receiver steps after FC %d\n",
           fa, first_two, f8, fc, sn, rn, bad, restart_done && fa == 2u, r_after_stop);
    CHECK(fa == 2u && first_two == 2u && fc == 1u);
    CHECK(sn == rn && sn > 40u && bad == 0);
    CHECK(r_after_stop == 0);
    (void)polls;
    {   /* the old fault, measured: the 1st F8 after FA is tick 0 (the downbeat), so its step 0 is our step 0 */
        snd_t z;
        clock_init(&z.c), clock_out_init(&z.o);
        clock_set_mode(&z.c, CLK_MIDI_OUT);
        clock_tick(&z.c, BLOCK);                 /* step 0 at frame 0 */
        n = snd_poll(&z, CLK_MIDI_OUT, b);
        printf("midi_clock_out: the first block in OUT (step 0 at frame %u) sends %u byte(s): %02X %02X\n", z.c.at[0], n,
               b[0], n > 1 ? b[1] : 0);
        CHECK(z.c.n == 1u && n == 2u && b[0] == 0xFA && b[1] == 0xF8);
    }
}

#ifdef CLOCK_HOST
/* ---- the host's transport (clock.h CLOCK_HOST; built by test_clock_host.c: the tests above run there too, the clock
 * with the host code compiled in but no anchor given: the firmware's behaviour). A host at 44.1 kHz (its samples are
 * the clock's frames): before each host block, its transport as an anchor at the block's first sample (as the plugin's
 * CoreRunner gives it, plugin plan Task 5), then the clock ticked in the firmware's 256-frame blocks up to the last
 * whole one the host block covers (the core runs behind the host by up to a block). Every event kept: frame, step
 * number, kind, gate */
#include <math.h>
#define HS_MAX 512u
typedef struct {
    clk_t c;
    uint64_t t;                                   /* frames ticked */
    uint32_t n;
    uint64_t f[HS_MAX];
    int32_t step[HS_MAX];
    uint8_t kind[HS_MAX];
    uint32_t gate[HS_MAX];
} hs_t;
static void hs_init(hs_t *h, int div)
{
    memset(h, 0, sizeof *h);
    clock_init(&h->c);
    clock_set_div(&h->c, div);
}
static void hs_tick_to(hs_t *h, uint64_t target)
{
    while (h->t < target) {
        uint32_t b = (uint32_t)(BLOCK - h->t % BLOCK);
        if (b > target - h->t) b = (uint32_t)(target - h->t);
        clock_tick(&h->c, b);
        for (uint32_t i = 0; i < h->c.n; i++)
            if (h->n < HS_MAX) {
                h->f[h->n] = h->t + h->c.at[i], h->step[h->n] = (int32_t)h->c.step[i];
                h->kind[h->n] = h->c.kind[i], h->gate[h->n] = h->c.gate[i], h->n++;
            }
        h->t += b;
    }
}
/* the host's transport at frame `at` (>= the frames ticked): ppq to ticks + counts, bpm to counts a frame */
static void hs_anchor(hs_t *h, uint64_t at, int playing, double bpm, double ppq)
{
    double ticks = ppq * 24.0, whole = floor(ticks);
    uint32_t frac = (uint32_t)llround((ticks - whole) * (double)CLOCK_U);
    if (frac >= CLOCK_U) frac = CLOCK_U - 1u;
    clock_host(&h->c, (uint32_t)(at - h->t), playing, (uint32_t)llround(bpm * CLOCK_RATE_Q), (uint32_t)(int32_t)(int64_t)whole,
               frac);
}
/* a host block of n samples from S: its anchor, then the clock up to the last whole firmware block it covers */
static void hs_block(hs_t *h, uint64_t S, uint32_t n, int playing, double bpm, double ppq)
{
    hs_anchor(h, S, playing, bpm, ppq);
    hs_tick_to(h, (S + n) / BLOCK * BLOCK);
}
/* a steady host from frame S0 at ppq p0: blocks of hb up to frame end (the ppq of each block from the tempo) */
static void hs_play(hs_t *h, uint64_t S0, uint64_t end, uint32_t hb, double bpm, double p0)
{
    for (uint64_t S = S0; S < end; S += hb) {
        uint32_t n = end - S < hb ? (uint32_t)(end - S) : hb;
        hs_block(h, S, n, 1, bpm, p0 + (double)(S - S0) * bpm / 60.0 / 44100.0);
    }
    hs_tick_to(h, end);                           /* (the stretch's last frames: the clock on at the tempo) */
}
/* from event index i on: the events at or after frame f (the internal clock ran before a start: its steps dropped) */
static void hs_drop_before(hs_t *h, uint64_t f)
{
    uint32_t j = 0;
    for (uint32_t i = 0; i < h->n; i++)
        if (h->f[i] >= f) h->f[j] = h->f[i], h->step[j] = h->step[i], h->kind[j] = h->kind[i], h->gate[j] = h->gate[i], j++;
    h->n = j;
}
static int hs_find(const hs_t *h, uint64_t f)     /* the event at frame f, -1 none */
{
    for (uint32_t i = 0; i < h->n; i++) if (h->f[i] == f) return (int)i;
    return -1;
}
/* exact frame of ppq p from frame S0 at ppq p0 and a steady bpm: ceil (the first frame at or past it) */
static uint64_t hs_frame(uint64_t S0, double p0, double bpm, double p)
{
    return S0 + (uint64_t)ceil((p - p0) * 60.0 * 44100.0 / bpm - 1e-9);
}

/* play from bar 1 (ppq 0) at frame 0, 120 BPM: step 0 at frame 0 (a jump: from stopped), then every quarter exactly
 * at k x 22050, numbered 0, 1, 2 ...; 16ths the same; from bar 2 (ppq 4) at frame 1000 after a stopped stretch: step 4
 * there. Host blocks of 512 and 1000 */
static void transport_start_bar(void)
{
    static hs_t h;
    uint32_t bad = 0;
    hs_init(&h, CLK_DIV_QTR);
    hs_play(&h, 0, 8u * 22050u + 10u, 512, 120.0, 0.0);
    for (uint32_t i = 0; i < h.n; i++)
        bad += h.f[i] != (uint64_t)i * 22050u || h.step[i] != (int32_t)i ||
               h.kind[i] != (CLK_EV_HOSTED | (i ? CLK_EV_STEP : CLK_EV_JUMP));
    printf("transport_start_bar: quarters from ppq 0 at 120: %u steps, %u off the grid (first kind %02X), following %d\n",
           h.n, bad, h.n ? h.kind[0] : 0, clock_following(&h.c));
    CHECK(h.n == 9u && bad == 0 && clock_following(&h.c));
    hs_init(&h, CLK_DIV_16TH);
    hs_play(&h, 0, 16u * 5512u, 1000, 120.0, 0.0);
    bad = 0;
    for (uint32_t i = 0; i < h.n; i++) bad += h.f[i] != ((uint64_t)i * 11025u + 1u) / 2u || h.step[i] != (int32_t)i;
    printf("transport_start_bar: 16ths, host blocks of 1000: %u steps, %u off\n", h.n, bad);
    CHECK(h.n == 16u && bad == 0);
    hs_init(&h, CLK_DIV_QTR);
    for (uint64_t S = 0; S < 1000u; S += 500u) hs_block(&h, S, 500, 0, 120.0, 0.0);   /* stopped: the internal clock */
    hs_play(&h, 1000, 1000u + 2u * 22050u + 1u, 512, 120.0, 4.0);
    {
        int a = hs_find(&h, 1000), b = hs_find(&h, 1000u + 22050u);
        printf("transport_start_bar: from bar 2 at frame 1000: step %d (kind %02X) there, step %d one quarter on\n",
               a >= 0 ? h.step[a] : -99, a >= 0 ? h.kind[a] : 0, b >= 0 ? h.step[b] : -99);
        CHECK(a >= 0 && h.step[a] == 4 && h.kind[a] == (CLK_EV_HOSTED | CLK_EV_JUMP) && b >= 0 && h.step[b] == 5);
        CHECK(h.n >= 1u && h.f[h.n - 1u] == 1000u + 2u * 22050u && h.step[h.n - 1u] == 6);
    }
}

/* start mid-bar: on a 16th boundary (ppq 1.5 = beat 2.5: step 6 at once, a step), inside a quarter's first half (ppq
 * 1.25: step 1 resumed, its gate to ppq 1.5 = 5513 frames; step 2 at ppq 2), after it (ppq 1.75: released, no step;
 * step 2 at ppq 2); a count-in (ppq -1: step -1, then 0) */
static void transport_mid_bar(void)
{
    static hs_t h;
    const uint64_t S = 3000;
    hs_init(&h, CLK_DIV_16TH);
    hs_play(&h, S, S + 3u * 5513u, 512, 120.0, 1.5);
    hs_drop_before(&h, S);
    printf("transport_mid_bar: 16ths from ppq 1.5: step %d kind %02X at %llu, step %d at %llu (want 7 at %llu)\n",
           h.n ? h.step[0] : -99, h.n ? h.kind[0] : 0, (unsigned long long)(h.n ? h.f[0] : 0), h.n > 1 ? h.step[1] : -99,
           (unsigned long long)(h.n > 1 ? h.f[1] : 0), (unsigned long long)hs_frame(S, 1.5, 120.0, 1.75));
    CHECK(h.n >= 2u && h.f[0] == S && h.step[0] == 6 && h.kind[0] == (CLK_EV_HOSTED | CLK_EV_JUMP));
    CHECK(h.n >= 2u && h.f[1] == hs_frame(S, 1.5, 120.0, 1.75) && h.step[1] == 7 && h.kind[1] == (CLK_EV_HOSTED | CLK_EV_STEP));
    hs_init(&h, CLK_DIV_QTR);
    hs_play(&h, S, S + 22050u, 512, 120.0, 1.25);
    hs_drop_before(&h, S);
    printf("transport_mid_bar: quarters from ppq 1.25: step %d kind %02X gate %u at %llu; step %d at %llu (want %llu)\n",
           h.n ? h.step[0] : -99, h.n ? h.kind[0] : 0, h.n ? h.gate[0] : 0, (unsigned long long)(h.n ? h.f[0] : 0),
           h.n > 1 ? h.step[1] : -99, (unsigned long long)(h.n > 1 ? h.f[1] : 0),
           (unsigned long long)hs_frame(S, 1.25, 120.0, 2.0));
    CHECK(h.n == 2u && h.f[0] == S && h.step[0] == 1 && h.kind[0] == (CLK_EV_HOSTED | CLK_EV_RESUME) &&
          (h.gate[0] == 5512u || h.gate[0] == 5513u));
    CHECK(h.n == 2u && h.step[1] == 2 && h.f[1] == hs_frame(S, 1.25, 120.0, 2.0) && h.kind[1] == (CLK_EV_HOSTED | CLK_EV_STEP));
    hs_init(&h, CLK_DIV_QTR);
    hs_play(&h, S, S + 22050u, 512, 120.0, 1.75);
    hs_drop_before(&h, S);
    printf("transport_mid_bar: quarters from ppq 1.75: kind %02X at %llu, then step %d at %llu (want %llu)\n",
           h.n ? h.kind[0] : 0, (unsigned long long)(h.n ? h.f[0] : 0), h.n > 1 ? h.step[1] : -99,
           (unsigned long long)(h.n > 1 ? h.f[1] : 0), (unsigned long long)hs_frame(S, 1.75, 120.0, 2.0));
    CHECK(h.n == 2u && h.f[0] == S && (h.kind[0] & CLK_EV_KIND) == CLK_EV_RELEASE);
    CHECK(h.n == 2u && h.step[1] == 2 && h.f[1] == hs_frame(S, 1.75, 120.0, 2.0));
    hs_init(&h, CLK_DIV_QTR);
    hs_play(&h, 0, 22050u + 1u, 512, 120.0, -1.0);
    printf("transport_mid_bar: a count-in from ppq -1: steps %d, %d at %llu, %llu\n", h.n ? h.step[0] : -99,
           h.n > 1 ? h.step[1] : -99, (unsigned long long)(h.n ? h.f[0] : 0), (unsigned long long)(h.n > 1 ? h.f[1] : 0));
    CHECK(h.n == 2u && h.step[0] == -1 && h.f[0] == 0u && h.step[1] == 0 && h.f[1] == 22050u);
}

/* a 4-bar loop [0, 16) at 120 BPM, host blocks of 512 (the loop's end, 352800, inside a block): the plugin gives an
 * anchor at the wrap's frame (ppq 0 there) besides the blocks' own. Quarters 0..15, then 0..15 again, each exactly on
 * k x 22050 from its pass's start: no step doubled or skipped at the wrap (one event at 352800: step 0, a jump). A loop
 * starting inside a step ([1.25, 5.25)): the wrap resumes step 1 */
static void transport_loop_run(hs_t *h, double ls, double le, uint32_t passes, int div)
{
    const double bpm = 120.0, fpb = 44100.0 * 60.0 / bpm;   /* frames a beat */
    const uint32_t hb = 512;
    double len = (le - ls) * fpb;                 /* frames a pass (whole here) */
    uint64_t end = (uint64_t)(len * passes);
    hs_init(h, div);
    for (uint64_t S = 0; S < end; S += hb) {
        double ppq = ls + fmod((double)S, len) / fpb;
        uint64_t wrap = (uint64_t)(len * (double)((uint64_t)((double)S / len) + 1u));   /* the next pass's start */
        hs_anchor(h, S, 1, bpm, ppq);
        if (wrap < S + hb && wrap < end) hs_anchor(h, wrap, 1, bpm, ls);   /* (CoreRunner: the wrap inside the block) */
        hs_tick_to(h, (S + hb) / BLOCK * BLOCK < end ? (S + hb) / BLOCK * BLOCK : end);
    }
    hs_tick_to(h, end);
}
static void transport_loop(void)
{
    static hs_t h;
    uint32_t bad = 0;
    transport_loop_run(&h, 0.0, 16.0, 3, CLK_DIV_QTR);
    for (uint32_t i = 0; i < h.n; i++) {
        uint32_t pass = i / 16u, k = i % 16u;
        bad += h.f[i] != (uint64_t)pass * 352800u + (uint64_t)k * 22050u || h.step[i] != (int32_t)k ||
               (h.kind[i] & CLK_EV_KIND) != (k == 0 && pass ? CLK_EV_JUMP : i ? CLK_EV_STEP : CLK_EV_JUMP);
    }
    for (uint32_t i = 0; i < h.n; i++) {
        uint32_t pass = i / 16u, k = i % 16u;
        if (h.f[i] != (uint64_t)pass * 352800u + (uint64_t)k * 22050u || h.step[i] != (int32_t)k)
            printf("  [%u] at %llu step %d kind %02X\n", i, (unsigned long long)h.f[i], h.step[i], h.kind[i]);
    }
    printf("transport_loop: 4 bars x 3 passes, quarters: %u steps (want 48), %u off (steps at the wraps: %d, %d)\n", h.n,
           bad, h.n > 16u ? h.step[16] : -99, h.n > 32u ? h.step[32] : -99);
    CHECK(h.n == 48u && bad == 0);
    transport_loop_run(&h, 1.25, 5.25, 2, CLK_DIV_QTR);
    {                                             /* pass: resume 1 (gate), 2, 3, 4, 5; then again */
        uint32_t ok = h.n == 10u;
        for (uint32_t i = 0; ok && i < h.n; i++) {
            uint32_t k = i % 5u;
            ok = h.step[i] == (int32_t)(k + 1u) && (h.kind[i] & CLK_EV_KIND) == (k ? CLK_EV_STEP : CLK_EV_RESUME) &&
                 h.f[i] == (uint64_t)(i / 5u) * 88200u + (k ? (uint64_t)k * 22050u - 5512u : 0u);   /* (ceil of k x 22050 - 5512.5) */
        }
        printf("transport_loop: a loop from ppq 1.25: %u steps, the wraps resume step 1: %s\n", h.n, ok ? "yes" : "no");
        CHECK(ok);
    }
    {                                             /* the loop's end at a host block's start: the block's anchor (ppq 4,
                                                   * the old timeline) and the wrap's (ppq 0) at one frame: the newest
                                                   * wins, one step there (0, a jump), not step 4 as well */
        uint32_t at = 0;
        hs_init(&h, CLK_DIV_QTR);
        hs_play(&h, 0, 88200, 441, 120.0, 0.0);
        hs_anchor(&h, 88200, 1, 120.0, 4.0);
        hs_anchor(&h, 88200, 1, 120.0, 0.0);
        hs_tick_to(&h, 88200u + 22051u);
        for (uint32_t i = 0; i < h.n; i++) at += h.f[i] == 88200u;
        printf("transport_loop: the wrap at a block's start: %u step(s) there (step %d kind %02X), then step %d at %llu\n",
               at, h.n > 4u ? h.step[4] : -99, h.n > 4u ? h.kind[4] : 0, h.n > 5u ? h.step[5] : -99,
               (unsigned long long)(h.n > 5u ? h.f[5] : 0));
        CHECK(at == 1u && h.n == 6u && h.step[4] == 0 && h.kind[4] == (CLK_EV_HOSTED | CLK_EV_JUMP) && h.step[5] == 1 &&
              h.f[5] == 88200u + 22050u);
    }
}

/* locate while playing (blocks of 512, the jump at a block's start): to ppq 10 (a quarter's boundary): step 10 there,
 * 11 a quarter on; backwards to ppq 2 + a 16th: step 2 resumed; with 16ths to ppq 10.5: step 42 */
static void transport_locate(void)
{
    static hs_t h;
    const uint64_t J = 512u * 30u;                /* 15360: inside the first quarter */
    int a, b;
    hs_init(&h, CLK_DIV_QTR);
    hs_play(&h, 0, J, 512, 120.0, 0.0);
    hs_play(&h, J, J + 22050u + 1u, 512, 120.0, 10.0);
    a = hs_find(&h, J), b = hs_find(&h, J + 22050u);
    printf("transport_locate: at %llu to ppq 10: step %d kind %02X, then step %d a quarter on; %u steps in all\n",
           (unsigned long long)J, a >= 0 ? h.step[a] : -99, a >= 0 ? h.kind[a] : 0, b >= 0 ? h.step[b] : -99, h.n);
    CHECK(h.n == 3u && h.step[0] == 0 && a == 1 && h.step[a] == 10 && (h.kind[a] & CLK_EV_KIND) == CLK_EV_JUMP && b == 2 &&
          h.step[b] == 11);
    hs_init(&h, CLK_DIV_QTR);
    hs_play(&h, 0, J, 512, 120.0, 20.0);
    hs_play(&h, J, J + 22050u, 512, 120.0, 2.25);
    printf("transport_locate: back to ppq 2.25: step %d kind %02X at %llu, then step %d at %llu (want %llu)\n",
           h.n > 1 ? h.step[1] : -99, h.n > 1 ? h.kind[1] : 0, (unsigned long long)(h.n > 1 ? h.f[1] : 0),
           h.n > 2 ? h.step[2] : -99, (unsigned long long)(h.n > 2 ? h.f[2] : 0),
           (unsigned long long)hs_frame(J, 2.25, 120.0, 3.0));
    CHECK(h.n == 3u && h.step[1] == 2 && h.f[1] == J && (h.kind[1] & CLK_EV_KIND) == CLK_EV_RESUME &&
          h.step[2] == 3 && h.f[2] == hs_frame(J, 2.25, 120.0, 3.0));
    hs_init(&h, CLK_DIV_16TH);
    hs_play(&h, 0, J, 512, 120.0, 0.0);
    hs_play(&h, J, J + 5514u, 512, 120.0, 10.5);
    a = hs_find(&h, J);
    printf("transport_locate: 16ths to ppq 10.5: step %d kind %02X\n", a >= 0 ? h.step[a] : -99, a >= 0 ? h.kind[a] : 0);
    CHECK(a >= 0 && h.step[a] == 42 && (h.kind[a] & CLK_EV_KIND) == CLK_EV_JUMP && h.step[a + 1] == 43 &&
          h.f[a + 1] == J + 5513u);
}

/* tempo automation 100 -> 140 BPM over 8 s (linear in time), then 140: the host gives each block's tempo at its start
 * and its position (exact: the integral). Every quarter within 1 frame of the ideal grid (the first frame at or past
 * the time the position reaches it) */
static double ramp_ppq(double t) { const double T = 8.0; return t < T ? (100.0 * t + 20.0 * t * t / T) / 60.0 : (100.0 * T + 20.0 * T) / 60.0 + 140.0 * (t - T) / 60.0; }
static double ramp_bpm(double t) { return t < 8.0 ? 100.0 + 40.0 * t / 8.0 : 140.0; }
static void transport_tempo_ramp(void)
{
    static hs_t h;
    const uint64_t end = 44100u * 12u;
    int worst = 0;
    uint32_t want = 0;
    hs_init(&h, CLK_DIV_QTR);
    for (uint64_t S = 0; S < end; S += 512u) {
        double t = (double)S / 44100.0;
        hs_block(&h, S, 512, 1, ramp_bpm(t), ramp_ppq(t));
    }
    for (uint32_t k = 0; ramp_ppq((double)((h.t - 1u)) / 44100.0) >= (double)k; k++) want++;
    for (uint32_t i = 0; i < h.n; i++) {          /* the ideal: bisection on the exact position */
        double lo = 0.0, hi = 12.0, ideal;
        for (int it = 0; it < 80; it++) { double m = (lo + hi) / 2.0; if (ramp_ppq(m) < (double)i) lo = m; else hi = m; }
        ideal = hi * 44100.0;
        int d = (int)fabs((double)h.f[i] - ideal) + ((double)h.f[i] - ideal > 1.0 || (double)h.f[i] - ideal < -1.0 ? 1 : 0);
        if (d > worst) worst = d;
        if (h.step[i] != (int32_t)i) worst = 99;
    }
    printf("transport_tempo_ramp: 100 -> 140 BPM: %u quarters (want %u), worst %d frame(s) off the ideal grid\n", h.n,
           want, worst);
    CHECK(h.n == want && worst <= 1);
}

/* stop: a release at the stop's frame (not a step), the clock the firmware's again: internal, at P_BPM (60 here, set
 * while playing: ignored then), on from the position (no restart): the next quarter where 60 BPM takes the rest of it */
static void transport_stop_internal(void)
{
    static hs_t h;
    const uint64_t X = 512u * 20u;                /* 10240: 10240 / 22050 of the first quarter */
    int a;
    hs_init(&h, CLK_DIV_QTR);
    clock_set_bpm(&h.c, 60);
    hs_play(&h, 0, X, 512, 120.0, 0.0);
    for (uint64_t S = X; S < X + 50000u; S += 512u) hs_block(&h, S, 512, 0, 120.0, 99.0);   /* (stopped: ppq ignored) */
    a = hs_find(&h, X);
    printf("transport_stop_internal: stop at %llu: kind %02X there, following %d, then step %d at %llu (want %llu, "
           "60 BPM), %u events\n", (unsigned long long)X, a >= 0 ? h.kind[a] : 0, clock_following(&h.c),
           h.n > 2 ? h.step[2] : -99, (unsigned long long)(h.n > 2 ? h.f[2] : 0),
           (unsigned long long)(X + 2u * (22050u - X)), h.n);
    CHECK(h.n == 3u && a == 1 && h.kind[a] == CLK_EV_RELEASE && !clock_following(&h.c));
    CHECK(h.n == 3u && h.step[2] == 1 && h.kind[2] == CLK_EV_STEP && h.f[2] == X + 2u * (22050u - X));
    CHECK(clock_bpm_x10(&h.c) == 600 && clock_running(&h.c));
    clock_restart(&h.c);                          /* (stopped: the firmware's rules again: a restart works) */
    clock_tick(&h.c, BLOCK);
    CHECK(h.c.n == 1u && h.c.step[0] == 0u && h.c.at[0] == 0u);
}

/* P_BPM while the host plays: kept, not used (the host's tempo; clock_bpm_x10 says the host's); a division change puts
 * the grid at once without a step; MIDI clock in and a restart do nothing; stopped anchors alone change nothing (the
 * firmware's timing, as bpm_timing) */
static void bpm_param_greyed(void)
{
    static hs_t h;
    uint32_t bad = 0;
    hs_init(&h, CLK_DIV_QTR);
    hs_play(&h, 0, 30000, 512, 120.0, 0.0);
    clock_set_bpm(&h.c, 60);
    clock_restart(&h.c);
    clock_midi(&h.c, 0xFA, 0);
    hs_play(&h, 30208, 4u * 22050u + 1u, 512, 120.0, 30208.0 / 22050.0);
    for (uint32_t i = 0; i < h.n; i++) bad += h.f[i] != (uint64_t)i * 22050u || h.step[i] != (int32_t)i;
    printf("bpm_param_greyed: P_BPM 60 while playing at 120: %u steps, %u off; bpm x10 %d (bpm kept %d), following %d\n",
           h.n, bad, clock_bpm_x10(&h.c), h.c.bpm, clock_following(&h.c));
    CHECK(h.n == 5u && bad == 0 && clock_bpm_x10(&h.c) == 1200 && h.c.bpm == 60 && clock_following(&h.c));
    clock_set_div(&h.c, CLK_DIV_16TH);            /* at frame 88201: ppq 4.00004; the next 16th at ppq 4.25 */
    hs_play(&h, 4u * 22050u + 1u, 4u * 22050u + 5600u, 512, 120.0, (4.0 * 22050.0 + 1.0) / 22050.0);
    printf("bpm_param_greyed: 16ths from there: step %d at %llu (want 17 at %u)\n", h.n > 5u ? h.step[5] : -99,
           (unsigned long long)(h.n > 5u ? h.f[5] : 0), 4u * 22050u + 5513u);
    CHECK(h.n == 6u && h.step[5] == 17 && h.f[5] == 4u * 22050u + 5513u);
    {                                             /* stopped anchors every block: bpm_timing's 120 BPM quarters */
        static hs_t g;
        hs_init(&g, CLK_DIV_QTR);
        for (uint64_t S = 0; S < 8u * 22050u + 1u; S += 300u) hs_block(&g, S, 300, 0, 77.0, 3.3);
        hs_tick_to(&g, 8u * 22050u + 1u);
        bad = 0;
        for (uint32_t i = 0; i < g.n; i++) bad += g.f[i] != (uint64_t)i * 22050u || g.kind[i] != CLK_EV_STEP;
        printf("bpm_param_greyed: stopped host, 120 BPM internal: %u steps, %u off\n", g.n, bad);
        CHECK(g.n == 9u && bad == 0 && !clock_following(&g.c));
    }
}

static void host_tests(void)
{
    transport_start_bar();
    transport_mid_bar();
    transport_loop();
    transport_locate();
    transport_tempo_ramp();
    transport_stop_internal();
    bpm_param_greyed();
}
#endif

int main(void)
{
    bpm_timing();
    tap_tempo();
    divisions();
    swing_timing();
    bpm_change_and_restart();
    midi_clock_in();
    ticks_and_lfo();
    midi_clock_out();
#ifdef CLOCK_HOST
    host_tests();
#endif
    if (fails) printf("test_clock: %d FAILED\n", fails);
    else printf("test_clock: all passed\n");
    return fails != 0;
}
