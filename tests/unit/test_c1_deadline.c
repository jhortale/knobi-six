/* SPDX-License-Identifier: GPL-3.0-only */
/* The second core's join deadline (engine.c eng_split, a KNOBI_CPU1 build; final review 3), on the host with a fake
 * CPU1: a job posted runs when it is joined, if its time (c1_job_us) fits the join's timeout counted from the post;
 * else the join fails and CPU1 is retired (as the HAL's fm1_cpu1_wait does: LATE). Time is engine.c's fake clock
 * (eng_fake_ticks): the fake job's time moves it; CPU0's own share costs nothing here (so a join waits the whole job).
 *
 * slow_cpu1_retired_within_a_block: a CPU1 that is slow but never hung (every job 1400 us, under the 1500 us a join
 * may take) used to be joined 16 times a block, ~22 ms for a 5.8 ms block, every block, and never retired. With one
 * deadline per block (the joins' total wait at most ENG_C1_WAIT_MAX, half a block) it is retired in the first slow
 * block, which takes no more than one block; the next block is one core (no job, no wait). The output stays bit-exact
 * with the one-core render through the retirement (the job that missed never ran: CPU0 renders its set). A fast CPU1
 * (50 us jobs) is kept. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KNOBI_CPU1 1
#define FM1_TICKS_PER_US 24u
enum { CPU1_OFF = 0, CPU1_IDLE = 1, CPU1_BUSY = 2, CPU1_FAIL = 3 };
extern uint32_t eng_fake_ticks;                 /* (engine.c, below) */
static uint32_t c1_job_us = 50u;                /* the fake job's time */
static int c1_state = CPU1_IDLE, c1_out;
static void (*c1_fn)(void *);
static void *c1_arg;
static uint32_t c1_post_at, c1_posts, c1_late;
static int cpu1_post(void (*job)(void *), void *arg)
{
    if (c1_state != CPU1_IDLE || c1_out) return -1;
    c1_fn = job, c1_arg = arg, c1_post_at = eng_fake_ticks, c1_out = 1, c1_posts++;
    return 0;
}
static int cpu1_join(uint32_t timeout_ticks)
{
    uint32_t need = c1_job_us * FM1_TICKS_PER_US;
    if (!c1_out) return 0;
    c1_out = 0;
    if (need <= timeout_ticks) {                /* back in time: it ran, the clock at its end (or later) */
        c1_fn(c1_arg);
        if (eng_fake_ticks - c1_post_at < need) eng_fake_ticks = c1_post_at + need;
        return 0;
    }
    if (eng_fake_ticks - c1_post_at < timeout_ticks) eng_fake_ticks = c1_post_at + timeout_ticks;
    c1_state = CPU1_FAIL, c1_late++;            /* (the HAL: retired, LATE; the job dropped, never run) */
    return -1;
}
static int cpu1_state(void) { return c1_state; }
static void fm1_cpu1_sync(void) {}

struct synth;
static void eng_split(struct synth *s, int32_t *bus, uint32_t n);
#define SYNTH_SPLIT(s, bus, n) eng_split((s), (bus), (n))
#define SYNTH_VOICE_ALIGN __attribute__((aligned(32)))
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
#define NBLK 8u
static int32_t REF[2u * BLK * NBLK], OUT[2u * BLK * NBLK];
static const uint8_t NOTES[6] = {48, 55, 60, 64, 67, 72};

static void start(int dual)
{
    eng_init();
    eng_c1_failed = 0, eng_c1_jobs = 0;
    c1_state = CPU1_IDLE, c1_out = 0, c1_posts = c1_late = 0, c1_job_us = 50u;
    eng_fake_ticks = 0, eng_fake_per_voice = 0;
    eng_dual = (uint8_t)dual;
    for (unsigned i = 0; i < 6u; i++) eng_note(NOTES[i], 100);
}
static uint32_t block(int32_t *buf, uint32_t k)   /* block k into buf; its time (us, the fake clock) */
{
    uint32_t t = eng_fake_ticks;
    memset(&buf[2u * BLK * k], 0, 2u * BLK * sizeof buf[0]);
    eng_render(&buf[2u * BLK * k], BLK);
    return (eng_fake_ticks - t) / FM1_TICKS_PER_US;
}

static void slow_cpu1_retired_within_a_block(void)
{
    uint32_t k, fast_us = 0, fast_failed, slow_us, after_us, jobs_fast, jobs_slow, jobs_after, posts_after, exact, slow_blk = 5u;
    start(0);
    for (k = 0; k < NBLK; k++) block(REF, k);
    start(1);
    for (k = 0; k < slow_blk; k++) {             /* a fast CPU1: kept */
        uint32_t us = block(OUT, k);
        if (us > fast_us) fast_us = us;
    }
    jobs_fast = eng_c1_jobs, fast_failed = eng_c1_failed;
    c1_job_us = 1400u;                           /* slow, never hung: under the 1500 us a single join may wait */
    slow_us = block(OUT, slow_blk);
    jobs_slow = eng_c1_jobs - jobs_fast;
    posts_after = c1_posts;
    after_us = block(OUT, slow_blk + 1u);
    jobs_after = eng_c1_jobs - jobs_fast - jobs_slow;
    exact = memcmp(REF, OUT, 2u * BLK * (slow_blk + 2u) * sizeof REF[0]) == 0;
    printf("slow_cpu1_retired_within_a_block: fast CPU1 (50 us jobs): %u jobs, not retired %d, longest block %u us; "
           "slow CPU1 (1400 us jobs): retired in its first block %d (LATE %u), that block %u us (a block: 5805), %u "
           "slow job(s) back first; the next block %u us, %u jobs, %u posts; bit-exact with one core %d\n",
           (unsigned)jobs_fast, !fast_failed, (unsigned)fast_us, eng_c1_failed == 1, (unsigned)c1_late,
           (unsigned)slow_us, (unsigned)jobs_slow, (unsigned)after_us, (unsigned)jobs_after,
           (unsigned)(c1_posts - posts_after), exact);
    CHECK(!fast_failed && jobs_fast == slow_blk * BLK / VOICE_CTL && fast_us < 5805u / 2u);
    CHECK(eng_c1_failed == 1 && c1_late == 1 && c1_state == CPU1_FAIL);
    CHECK(slow_us <= 5805u && jobs_slow >= 1u);
    CHECK(after_us == 0u && jobs_after == 0u && c1_posts == posts_after);
    CHECK(exact);
    CHECK(eng_c1_status() == 2);
}

/* (the engine's producer calls this test does not make: referenced, so -Werror's unused-function stays quiet) */
__attribute__((unused)) static void eng_rest(void)
{
    (void)eng_param, (void)eng_bend, (void)eng_wheel, (void)eng_pressure, (void)eng_volume, (void)eng_hold, (void)eng_tune;
    (void)eng_arp_sync, (void)eng_clock_mode, (void)eng_midi_rt, (void)eng_fade, (void)eng_faded, (void)eng_panic;
    (void)eng_all_notes_off, (void)eng_chord, (void)eng_flush, (void)eng_program, (void)eng_active;
    (void)eng_seq_play, (void)eng_seq_playing, (void)eng_seq_transpose, (void)eng_seq_load;
}

int main(void)
{
    slow_cpu1_retired_within_a_block();
    if (fails) { printf("test_c1_deadline: %d FAILED\n", fails); return 1; }
    printf("test_c1_deadline: all passed\n");
    return 0;
}
