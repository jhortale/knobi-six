/* SPDX-License-Identifier: GPL-3.0-only */
/* The voice-set render seam (voices.h: synth_render_set, synth_balance, SYNTH_SPLIT), Phase 2 Task 1.
 * split_render_bitexact: random patches, notes and edits over random block sizes, rendered by the pre-change render
 * (ref_render below: the Phase 1 synth_render, frozen here) and by synth_render with set A / set B forced to each of
 * the 64 partitions of the 6 voices (and with synth_balance's own split): the output must match sample for sample,
 * and so must the whole synth state after every block. split_balance: the masks synth_balance hands out.
 * split_threads_tsan (built with -DSPLIT_THREADS, `make -C tests tsan`: -fsanitize=thread): set B rendered on a
 * second host thread every control period through a lock-free mailbox (C11 atomics, as CPU1's will be), set A on the
 * calling thread at the same time: bit-exact, and ThreadSanitizer finds no race, i.e. synth_render_set writes nothing
 * the other set reads or writes. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef SPLIT_THREADS
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#endif

/* the seam under test: how a control period's voices are split (before voices.c, which uses it) */
struct synth;
static void test_split(struct synth *s, int32_t *bus, uint32_t n);
#define SYNTH_SPLIT(s, bus, n) test_split((s), (bus), (n))

#include "params/params.c"
#include "synth/osc.c"
#include "synth/filter.c"
#include "synth/env.c"
#include "synth/mod.c"
#include "synth/voice.c"
#include "synth/voices.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define ALL SYNTH_ALL_VOICES

/* how test_split splits: SPLIT_DEFAULT the one-core build's own (every voice as one set); 0..ALL set A = that mask,
 * set B = the rest; SPLIT_BALANCE synth_balance's masks; SPLIT_THREAD_* set B on the worker thread (synth_balance's
 * masks, or a random partition every period) */
enum { SPLIT_DEFAULT = -1, SPLIT_BALANCE = 1000, SPLIT_THREAD_BALANCE, SPLIT_THREAD_RANDOM };
static int split_mode = SPLIT_DEFAULT;
static uint32_t split_rng = 1, split_periods, split_b_busy;

static uint32_t xs(uint32_t *r) { uint32_t x = *r; x ^= x << 13, x ^= x >> 17, x ^= x << 5; return *r = x; }

#ifdef SPLIT_THREADS
/* the mailbox: 0 idle, 1 posted (job filled, worker's), 2 done (bus filled, main's), -1 quit */
static struct {
    struct synth *s;
    uint32_t mask, n;
    int32_t bus[2 * VOICE_CTL];
    atomic_int state;
} job;
static void *worker(void *arg)
{
    (void)arg;
    for (;;) {
        int st;
        while ((st = atomic_load_explicit(&job.state, memory_order_acquire)) != 1)
            if (st < 0) return 0; else sched_yield();
        for (uint32_t i = 0; i < 2 * job.n; i++) job.bus[i] = 0;
        synth_render_set(job.s, job.mask, job.bus, job.n);
        atomic_store_explicit(&job.state, 2, memory_order_release);
    }
}
#endif

static void test_split(struct synth *s, int32_t *bus, uint32_t n)
{
    split_periods++;
    if (split_mode == SPLIT_DEFAULT) {
        synth_render_set(s, SYNTH_ALL_VOICES, bus, n);   /* (voices.h's own SYNTH_SPLIT) */
        return;
    }
    uint32_t a, b;
    synth_balance(s, &a, &b);
    if (split_mode >= 0 && split_mode <= (int)ALL) a = (uint32_t)split_mode, b = ALL & ~a;
    else if (split_mode == SPLIT_THREAD_RANDOM) a = xs(&split_rng) & ALL, b = ALL & ~a;
    for (int i = 0; i < NVOICE; i++) split_b_busy += (b >> i & 1) && s->voice[i].busy;
#ifdef SPLIT_THREADS
    if (split_mode == SPLIT_THREAD_BALANCE || split_mode == SPLIT_THREAD_RANDOM) {
        job.s = s, job.mask = b, job.n = n;
        atomic_store_explicit(&job.state, 1, memory_order_release);
        synth_render_set(s, a, bus, n);   /* set A here while set B renders there */
        while (atomic_load_explicit(&job.state, memory_order_acquire) != 2) sched_yield();
        for (uint32_t i = 0; i < 2 * n; i++) bus[i] += job.bus[i];
        atomic_store_explicit(&job.state, 0, memory_order_relaxed);
        return;
    }
#endif
    synth_render_set(s, a, bus, n), synth_render_set(s, b, bus, n);
}

/* The pre-change render (Phase 1's synth_render, verbatim bar the name): every busy voice in index order into one bus */
static void ref_render(synth_t *s, int32_t *lr, uint32_t frames)
{
    if (!synth_active(s)) {
        for (int i = 0; i < SM_COUNT; i++) s->sm[i] = s->tgt[i];
        lfo_tick(&s->lfo, s->prog.v[P_LFO_SHAPE], frames);
        s->ctl_left = 0;
        if (synth_bus_live(s)) synth_bus(s, 0, lr, (int)frames);
        else s->dcy[0] = s->dcy[1] = 0.0f;
        return;
    }
    while (frames) {
        if (!s->ctl_left) synth_tick(s), s->ctl_left = VOICE_CTL;
        int n = (int)(frames < s->ctl_left ? frames : s->ctl_left);
        int32_t bus[2 * VOICE_CTL];
        for (int i = 0; i < 2 * n; i++) bus[i] = 0;
        s->ctx.pos = (uint8_t)(VOICE_CTL - s->ctl_left);
        for (int i = 0; i < NVOICE; i++)
            if (s->voice[i].busy) voice_render(&s->voice[i], &s->ctx, s->prog.v, bus, n);
        synth_bus(s, bus, lr, n);
        lr += 2 * n;
        frames -= (uint32_t)n;
        s->ctl_left = (uint8_t)(s->ctl_left - n);
    }
    for (int c = 0; c < 2; c++)
        if (__builtin_fabsf(s->dcy[c]) < 1e-20f) s->dcy[c] = 0.0f;
}

/* a random patch: every Phase 1 value uniform in its range (levels and volume kept audible half the time) */
static void random_patch(synth_t *s, uint32_t *r)
{
    program_t p;
    program_init(&p);
    for (int id = 0; id < P_COUNT_PHASE1; id++)
        p.v[id] = (int16_t)(PARAMS[id].min + (int32_t)(xs(r) % (uint32_t)(PARAMS[id].max - PARAMS[id].min + 1)));
    if (xs(r) & 1) p.v[P_PRG_VOL] = 100, p.v[P_MIX_VCO1] = 100, p.v[P_LENV_S] = 100, p.v[P_LENV_AMT] = 0;
    for (int k = 0; k < 6; k++) p.chord[k] = (xs(r) & 3) ? (uint8_t)(36 + xs(r) % 48) : 0;
    synth_program(s, &p);
}

/* one random event on both synths */
static void random_event(synth_t *x, synth_t *y, uint32_t *r)
{
    uint32_t k = xs(r) % 16, a = xs(r), b = xs(r);
    synth_t *t[2] = { x, y };
    for (int i = 0; i < 2; i++) {
        synth_t *s = t[i];
        if (k < 7) synth_note(s, 36 + (int)(a % 48), (int)(1 + b % 127));            /* note on */
        else if (k < 11) synth_note(s, 36 + (int)(a % 48), 0);                        /* note off */
        else if (k == 11) {
            uint16_t id = (uint16_t)(a % P_COUNT_PHASE1);
            synth_param(s, id, (int16_t)(PARAMS[id].min + (int32_t)(b % (uint32_t)(PARAMS[id].max - PARAMS[id].min + 1))));
        } else if (k == 12) synth_bend(s, (int)(a % 16384)), synth_wheel(s, (int)(b % 128));
        else if (k == 13) synth_pressure(s, (int)(a % 128));
        else if (k == 14) synth_hold(s, (int)(a & 1));
        else if (a % 8 == 0) synth_all_notes_off(s);
    }
}

static int32_t LA[2 * 4096], LB[2 * 4096];

/* one scenario (seed) rendered by ref_render and by synth_render in the current split_mode; returns the first block
 * that differs (output or state), -1 none; *nz counts nonzero output samples */
static int scenario(uint32_t seed, int blocks, long *nz)
{
    static synth_t A, B;
    uint32_t r = seed * 2654435761u | 1u;
    synth_init(&A);
    random_patch(&A, &r);
    memcpy(&B, &A, sizeof A);   /* synth_t holds no pointers: a clone */
    for (int blk = 0; blk < blocks; blk++) {
        int ev = (int)(xs(&r) % 4);
        for (int e = 0; e < ev; e++) random_event(&A, &B, &r);
        uint32_t n = 1 + xs(&r) % (xs(&r) & 1 ? 300 : 40);   /* any length: periods split across blocks */
        if (blk % 97 == 96) n = 4096;                         /* now and then a long one (releases end, silence) */
        memset(LA, 0, 2 * n * sizeof LA[0]), memset(LB, 0, 2 * n * sizeof LB[0]);
        ref_render(&A, LA, n);
        synth_render(&B, LB, n);
        for (uint32_t i = 0; i < 2 * n; i++) *nz += LA[i] != 0;
        if (memcmp(LA, LB, 2 * n * sizeof LA[0]) || memcmp(&A, &B, sizeof A)) return blk;
    }
    return -1;
}

static void split_balance(void)
{
    synth_t s;
    uint32_t r = 7;
    int bad = 0, checked = 0;
    for (int t = 0; t < 2000; t++) {
        synth_init(&s);
        uint32_t busy = xs(&r) & ALL;
        for (int i = 0; i < NVOICE; i++) s.voice[i].busy = (uint8_t)(busy >> i & 1);
        uint32_t a = 0xdead, b = 0xbeef, n = synth_balance(&s, &a, &b);
        int na = __builtin_popcount(a), nb = __builtin_popcount(b);
        if (n != (uint32_t)__builtin_popcount(busy) || (a & b) || (a | b) != busy || na > nb || nb - na > 1) bad++;
        checked++;
    }
    synth_init(&s);
    for (int i = 0; i < NVOICE; i++) s.voice[i].busy = 1;
    uint32_t a, b;
    synth_balance(&s, &a, &b);
    printf("split_balance: %d busy sets, %d bad (A, B disjoint, A + B = busy, |A| <= |B| <= |A| + 1); 6 busy: A %02x "
           "B %02x\n", checked, bad, a, b);
    CHECK(bad == 0 && __builtin_popcount(a) == 3 && __builtin_popcount(b) == 3);
}

#ifndef SPLIT_THREADS
static void split_render_bitexact(void)
{
    const int seeds = 3, blocks = 400;
    int bad = 0, runs = 0;
    long nz = 0;
    split_periods = split_b_busy = 0;
    for (int m = -1; m <= (int)ALL + 1; m++) {
        split_mode = m < 0 ? SPLIT_DEFAULT : m > (int)ALL ? SPLIT_BALANCE : m;
        for (uint32_t sd = 1; sd <= (uint32_t)seeds; sd++) {
            int d = scenario(sd + (uint32_t)(m + 1) * 101u, blocks, &nz);
            runs++;
            if (d >= 0) {
                if (bad < 5) printf("split_render_bitexact: mode %d seed %u differs at block %d\n", m, sd, d);
                bad++;
            }
        }
    }
    split_mode = SPLIT_DEFAULT;
    printf("split_render_bitexact: %d runs (the build's split, all %u partitions, synth_balance's) x %d blocks, %u "
           "control periods, %u voice-periods in set B, %ld nonzero samples: %d differ from the pre-change render\n",
           runs, ALL + 1, blocks, split_periods, split_b_busy, nz, bad);
    CHECK(bad == 0 && nz > 100000 && split_b_busy > 10000);
}
#else
static void split_threads_tsan(void)
{
    pthread_t th;
    atomic_init(&job.state, 0);
    CHECK(pthread_create(&th, 0, worker, 0) == 0);
    int bad = 0, runs = 0;
    long nz = 0;
    split_periods = split_b_busy = 0;
    for (int m = 0; m < 2; m++) {
        split_mode = m ? SPLIT_THREAD_RANDOM : SPLIT_THREAD_BALANCE;
        for (uint32_t sd = 1; sd <= 4; sd++) {
            int d = scenario(sd * 7919u + (uint32_t)m, 200, &nz);
            runs++;
            if (d >= 0) printf("split_threads_tsan: mode %d seed %u differs at block %d\n", m, sd, d), bad++;
        }
    }
    atomic_store_explicit(&job.state, -1, memory_order_release);
    pthread_join(th, 0);
    split_mode = SPLIT_DEFAULT;
    printf("split_threads_tsan: %d runs, set B on a second thread in %u control periods (%u voice-periods), %ld nonzero "
           "samples: %d differ from the pre-change render\n", runs, split_periods, split_b_busy, nz, bad);
    CHECK(bad == 0 && nz > 10000 && split_b_busy > 1000);
}
#endif

int main(void)
{
    split_balance();
#ifdef SPLIT_THREADS
    split_threads_tsan();
#else
    split_render_bitexact();
#endif
    if (fails) { printf("test_split: %d FAILED\n", fails); return 1; }
    printf("test_split: all passed\n");
    return 0;
}
