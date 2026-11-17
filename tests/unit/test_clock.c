/* SPDX-License-Identifier: GPL-3.0-only */
/* The clock (firmware/src/synth/clock.c, Phase 2 Task 5): step timing at 30 / 120 / 250 BPM (and 97, not a whole number
 * of frames a step) within 1 frame for every step over many bars, no drift; tap tempo (the average of the last 4
 * intervals, 4 taps first, a gap > 2 s restarts); the division table (the reference's rates); swing timing; a BPM
 * change keeps the position; MIDI clock in at 120 BPM delivered at block starts as engine.c does: locked within 2
 * beats, stopped by 0xFC and by the clock going away, not running without it; the ticks counted for MIDI out; the LFO's
 * quarter-note increment. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
    if (fails) printf("test_clock: %d FAILED\n", fails);
    else printf("test_clock: all passed\n");
    return fails != 0;
}
