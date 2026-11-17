/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX CPU1 probe (tools/build.py --probe-cpu1 -> knobi-six-cpu1probe.fwsc; KNOBI_CPU1=1 + KNOBI_CPU1_PROBE=1;
 * never the default package, never a release): the normal firmware plus checks of the second core that only the
 * hardware can answer (docs/research/second-core.md "What only the hardware can answer"; docs/SMOKE_TEST.md
 * "CPU1 probe"). app.c includes this file after engine.c; app_audio calls c1probe_audio() after each block's render,
 * inside the audio interrupt (the only place a CPU1 job may be posted: hal/fm1_cpu1.h cpu1_post), and every job it
 * posts is joined before it returns. The Globals screen shows the results (ui/screens.c W_DIAG); c1probe holds them
 * (fm1-emu reads it at the end of a run: tests/cpu1_emu/run.sh, cases probe and probeflaky).
 *
 * Per audio block (5.8 ms), while CPU1 runs (IDLE):
 *   - every PR_PING_EVERY blocks (~1 s): a ping round trip (cpu1_ping: CPU1 writes back the complement of a word);
 *     PING counts the good ones;
 *   - the block after: the FPU check, a fixed float workload (pr_fpu_work: mul, add, sub, div, compares, int
 *     conversions, a value halved through the denormals to 0) run on CPU1 (a job) and at the same time on CPU0, the
 *     two results' bits compared (they must be identical). A trap of CPU1's FPU is a CPU1 fault: the HAL holds and
 *     retires it (cpu1_why FM1_CPU1_FAULT): "FPU TRAP";
 *   - every PR_PAT_EVERY blocks: a shared-RAM pattern round over 16 KB (pr_buf): CPU0 writes pattern P, CPU1 reads it
 *     back and counts the words wrong, then writes pattern ~P, which CPU0 reads back and counts (both directions;
 *     the pattern changes each round: zeros / ones, an address hash, alternating bits, a walking bit, the sides'
 *     roles swapped every 4 rounds). MISREAD = the words read wrong by either
 *     core + the jobs CPU1's mailbox check refused (fm1_cpu1_mb.rejected: a misread of the mailbox, which retires
 *     CPU1); BIT = the bits that came back wrong (OR of the XORs; the known fault reads 0x00200000 where 0 is);
 *   - every block: one control period (16 frames) of the dual-vs-single check: a test patch (the musical stress
 *     patch, 6 notes) on a synth of its own, rendered into a scratch buffer (never to the speaker) for PR_PERIODS
 *     periods (8 blocks of audio) on one core, then again from synth_init on two (engine.c eng_split_force): the two
 *     checksums must be equal (DUAL=SINGLE OK). The same renders time CPU0 (wall clock, the join's wait included) and
 *     CPU1's job: LOAD 1C (one core), 2C (CPU0 with two), JOB (CPU1), in % of a block's time at this patch: the
 *     speedup on this unit is 1C / 2C.
 * ERR counts the failures: the probe's own operations that failed while CPU1 was running (a ping wrong or late, a
 * job not back, a post refused, a dual pass whose join failed), and CPU1's retirement for any other reason (its start,
 * a misread or a fault in its idle loop, DUAL CORE's render) once. Any failure leaves CPU1 retired (held, one core
 * until power-off; the HAL's bar), as it would be with DUAL CORE on; the probe then stops posting. The audio's DUAL
 * CORE global starts OFF at every boot of the probe (params/globals.c); the player turns it on (Globals pad 7) once
 * the checks pass.
 * Cost in the interrupt: a period of the test patch every block (1/16 of a block's render), a 16 KB round every 4th. */
#if !KNOBI_CPU1 || !KNOBI_CPU1_PROBE
#error "cpu1_probe.c: the CPU1 probe build only (tools/build.py --probe-cpu1)"
#endif

#define PR_PING_EVERY 172u                /* blocks: ~1.0 s */
#define PR_PAT_EVERY 4u                   /* blocks between pattern rounds */
#define PR_PAT_WORDS 4096u                /* 16 KB */
#define PR_PERIODS 128u                   /* control periods a dual-vs-single pass (8 blocks of audio) */
#define PR_JOIN_TICKS (2000u * FM1_TICKS_PER_US)   /* a ping's or the FPU job's deadline from its post */
#define PR_PAT_TICKS (3000u * FM1_TICKS_PER_US)    /* a pattern job's (16 KB read and written: ~0.1..0.2 ms) */
#define PR_PERIOD_BUDGET (VOICE_CTL * ENG_TICKS_PER_FRAME_X10 / 10u)   /* a period's share of the block, ticks */
#define PR_MAGIC 0x52503143u              /* "C1PR" */

/* what the probe found (the audio interrupt writes; the screen and the emulator read): one word each */
struct {
    uint32_t magic, blocks, pings, errs;
    uint32_t c0_mis, c1_mis, c0_bits, c1_bits, rounds;           /* the pattern test */
    uint32_t fpu_ok, fpu_bad, fpu_trap, fpu_c0, fpu_c1;          /* the FPU check */
    uint32_t dual_ok, dual_bad, dual_none, sum1, sum2, jobs2;    /* dual vs single (jobs2: CPU1 jobs, last pass) */
    uint32_t load1, load2, loadj, passes;                        /* % of a block: 1C, 2C, CPU1's job; passes timed */
    uint32_t state, why, rejected, mbjobs, c1_failed;            /* CPU1 now (cpu1_state, cpu1_why, the mailbox) */
} c1probe = { PR_MAGIC };

static uint32_t pr_buf[PR_PAT_WORDS] __attribute__((aligned(32)));   /* the shared RAM under test */
/* a job's argument: CPU0 writes round, CPU1 its results on a cache line of their own */
static struct pr_c1 {
    uint32_t round;
    uint32_t mis __attribute__((aligned(32))), bits, fpu;
} pr_c1 __attribute__((aligned(32)));

/* the pattern of round r, word i, as side 0 (CPU0) or 1 (CPU1) writes it: the two sides write complements, and which
 * writes which flips every 4 rounds, so each core reads zeros, ones, a hash, alternating bits and a walking bit in
 * turn. (Shifts only: no rotate, which fm1-emu does not decode) */
static uint32_t pr_pat(uint32_t r, uint32_t i, uint32_t side)
{
    uint32_t p;
    switch (r & 3u) {
    case 0: p = 0u; break;                                     /* (the known misread: 0x00200000 where 0 is) */
    case 1: p = i * 0x9E3779B1u ^ r * 0x85EBCA6Bu; break;      /* an address hash, new each round */
    case 2: p = ((i ^ r >> 2) & 1u) ? 0x55555555u : 0xAAAAAAAAu; break;   /* alternating bits */
    default: p = 1u << ((i + (r >> 2)) & 31u); break;          /* a walking one */
    }
    return (side ^ (r >> 2 & 1u)) ? ~p : p;
}
static void cpu1_job_pattern(void *arg)    /* on CPU1: CPU0's pattern read back, then its own written */
{
    struct pr_c1 *j = arg;
    uint32_t r = j->round, mis = 0, bits = 0, i, x;
    for (i = 0; i < PR_PAT_WORDS; i++)
        if ((x = pr_buf[i] ^ pr_pat(r, i, 0u)) != 0u) mis++, bits |= x;
    for (i = 0; i < PR_PAT_WORDS; i++) pr_buf[i] = pr_pat(r, i, 1u);
    j->mis = mis, j->bits = bits;
}

/* the FPU workload: the same code on both cores, the result's bits compared */
static uint32_t pr_fbits(float f)
{
    union { float f; uint32_t u; } v;
    v.f = f;
    return v.u;
}
static uint32_t __attribute__((noinline)) pr_fpu_work(void)
{
    float a = 0.70710678f, b = 1.0f, c = 1.0e-30f, d = 3.0f;
    uint32_t h = 0x811C9DC5u, i;
    for (i = 0; i < 256u; i++) {
        a = a * 1.0009765625f + b * 0.25f;
        b = b - a * 0.0078125f + c * 1.0e20f;
        if (__builtin_fabsf(a) > 64.0f) a = a / d;
        d = d + 1.0f / (d + 0.5f);
        c = c * 0.5f;                          /* (below 1.18e-38 from step 27: the denormals, then 0) */
        h = (h ^ pr_fbits(a)) * 16777619u;
        h = (h ^ pr_fbits(b)) * 16777619u;
        h = (h ^ pr_fbits(c)) * 16777619u;
        h = (h ^ pr_fbits(d)) * 16777619u;
        h = (h ^ (uint32_t)(int32_t)(b * 1000.0f)) * 16777619u;
    }
    return h;
}
static void cpu1_job_fpu(void *arg)        /* on CPU1 */
{
    struct pr_c1 *j = arg;
    j->fpu = pr_fpu_work();
}

/* the dual-vs-single check's test patch: the musical stress patch (tests/scripts/musical.txt, the CPU gate's PATCH 3),
 * 6 notes C3..C5, pressure 100 */
static const int16_t PR_PATCH[][2] = {
    {P_MIX_VCO1, 95}, {P_MIX_VCO2, 95}, {P_MIX_SUB, 95}, {P_MIX_NOISE, 95},
    {P_VCO1_SHAPE, 255}, {P_VCO1_PW, 90}, {P_VCO2_SHAPE, 254}, {P_VCO2_PW, 160}, {P_VCO1_SYNC, 1},
    {P_FLT_FREQ, 90}, {P_FLT_RESO, 201}, {P_FLT_ENV_AMT, 76}, {P_FENV_S, 40}, {P_PRG_VOL, 100},
    {P_XM_VCO2, 64}, {P_XM_D_VCO1, 1}, {P_XM_D_FFREQ, 1},
    {P_LFO_FREQ, 173}, {P_LFO_AMT, 128}, {P_LFO_D_VCO1, 1}, {P_LFO_D_FFREQ, 1},
    {P_AT_AMT, 64}, {P_AT_D_FFREQ, 1},
};
static const uint8_t PR_NOTES[6] = {48, 55, 60, 64, 67, 72};
static synth_t pr_synth;                   /* the test patch's own synth (the audio interrupt's only) */
static program_t pr_prog;
static uint32_t pr_pass, pr_k, pr_sum, pr_t, pr_tj, pr_j0, pr_lost;   /* (pr_lost: a join failed in this pass) */

static uint32_t pr_pct(uint32_t ticks)     /* ticks over a pass, % of the blocks' time they cover */
{
    if (ticks > 40000000u) ticks = 40000000u;
    return ticks * 100u / (PR_PERIODS * PR_PERIOD_BUDGET);
}
static int pr_up(void) { return cpu1_state() == CPU1_IDLE; }
static uint8_t pr_failed;                  /* CPU1's failure counted */

static void pr_dual_period(void)
{
    int32_t lr[2u * VOICE_CTL];
    uint32_t i, t0, c1t, used, dj;
    uint8_t failed = eng_c1_failed;
    if (pr_k == 0u) {                      /* a pass starts: the patch from synth_init, the notes on */
        synth_init(&pr_synth);
        program_init(&pr_prog);
        for (i = 0; i < sizeof PR_PATCH / sizeof PR_PATCH[0]; i++) pr_prog.v[PR_PATCH[i][0]] = PR_PATCH[i][1];
        synth_program(&pr_synth, &pr_prog);
        synth_pressure(&pr_synth, 100);
        for (i = 0; i < 6u; i++) synth_note(&pr_synth, PR_NOTES[i], 100);
        pr_sum = 0, pr_t = 0, pr_tj = 0, pr_j0 = eng_c1_jobs, pr_lost = 0;
    }
    for (i = 0; i < 2u * VOICE_CTL; i++) lr[i] = 0;
    eng_split_force = (int8_t)pr_pass;
    c1t = eng_c1.ticks;
    t0 = eng_ticks();
    synth_render(&pr_synth, lr, VOICE_CTL);
    used = eng_ticks() - t0;
    eng_split_force = -1;
    dj = eng_c1.ticks - c1t;               /* (CPU1's time on the test patch: kept out of the Globals' C1 ON n%) */
    eng_c1_seen += dj;
    pr_t += used, pr_tj += dj;
    for (i = 0; i < 2u * VOICE_CTL; i++) pr_sum = (pr_sum << 1 | pr_sum >> 31) ^ (uint32_t)lr[i];
    if (!failed && eng_c1_failed) c1probe.errs++, pr_lost = 1;   /* a voice job of this pass not back: CPU1 retired */
    if (++pr_k < PR_PERIODS) return;
    pr_k = 0;
    if (pr_pass == 0u) {
        c1probe.sum1 = pr_sum;
        c1probe.load1 = pr_pct(pr_t);
        pr_pass = 1;
        return;
    }
    pr_pass = 0;
    c1probe.sum2 = pr_sum;
    c1probe.jobs2 = eng_c1_jobs - pr_j0;
    if (!c1probe.jobs2 || pr_lost) { c1probe.dual_none++; return; }   /* (CPU1 not running, or lost on the way: not
                                           * compared; a lost job is an ERR already) */
    if (pr_sum == c1probe.sum1) c1probe.dual_ok++;
    else c1probe.dual_bad++;
    c1probe.load2 = pr_pct(pr_t);
    c1probe.loadj = pr_pct(pr_tj);
    c1probe.passes++;
}

static void pr_ping(void)
{
    if (cpu1_ping(PR_JOIN_TICKS) == 0) c1probe.pings++;
    else c1probe.errs++;
}

static void pr_fpu(void)
{
    uint32_t c0;
    pr_c1.fpu = 0;
    fm1_cpu1_sync();
    if (cpu1_post(cpu1_job_fpu, &pr_c1) < 0) { c1probe.errs++; return; }
    c0 = pr_fpu_work();                     /* CPU0's run, while CPU1 runs its own */
    if (cpu1_join(PR_JOIN_TICKS) < 0) {
        c1probe.errs++;
        if (cpu1_why() == FM1_CPU1_FAULT) c1probe.fpu_trap++;
        return;
    }
    fm1_cpu1_sync();
    c1probe.fpu_c0 = c0, c1probe.fpu_c1 = pr_c1.fpu;
    if (pr_c1.fpu == c0) c1probe.fpu_ok++;
    else c1probe.fpu_bad++;
}

static void pr_pattern(void)
{
    uint32_t r = c1probe.rounds, i, x, mis = 0, bits = 0;
    for (i = 0; i < PR_PAT_WORDS; i++) pr_buf[i] = pr_pat(r, i, 0u);
    pr_c1.round = r, pr_c1.mis = 0, pr_c1.bits = 0;
    fm1_cpu1_sync();
    if (cpu1_post(cpu1_job_pattern, &pr_c1) < 0) { c1probe.errs++; return; }
    if (cpu1_join(PR_PAT_TICKS) < 0) { c1probe.errs++; return; }
    fm1_cpu1_sync();
    c1probe.c1_mis += pr_c1.mis, c1probe.c1_bits |= pr_c1.bits;
    for (i = 0; i < PR_PAT_WORDS; i++)
        if ((x = pr_buf[i] ^ pr_pat(r, i, 1u)) != 0u) mis++, bits |= x;
    c1probe.c0_mis += mis, c1probe.c0_bits |= bits;
    c1probe.rounds = r + 1u;
}

/* app_audio, after the block's render (the audio interrupt; eng_render's own jobs are joined) */
static void c1probe_audio(void)
{
    uint32_t b, e0 = c1probe.errs;
    if (!eng_ready) return;
    b = c1probe.blocks++;
    fm1_cpu1_check();                       /* (a misread or a fault in its idle loop: retired now, its why set) */
    if (pr_up()) {
        if (b % PR_PING_EVERY == 0u) pr_ping();
        else if (b % PR_PING_EVERY == 1u) pr_fpu();
        else if (b % PR_PAT_EVERY == 2u) pr_pattern();
    }
    if (pr_up() || pr_k) pr_dual_period();  /* (a pass under way ends on one core if CPU1 went: dual_none) */
    c1probe.state = (uint32_t)cpu1_state();
    if (c1probe.state == CPU1_FAIL && !pr_failed) {   /* CPU1 failed (at the start, in its idle loop, in DUAL CORE's
                                           * render or in a probe job): one ERR, unless a probe job's already counts it */
        pr_failed = 1;
        if (c1probe.errs == e0) c1probe.errs++;
    }
    c1probe.why = cpu1_why();
    c1probe.rejected = fm1_cpu1_mb.rejected;
    c1probe.mbjobs = fm1_cpu1_mb.jobs;
    c1probe.c1_failed = eng_c1_failed;
}

/* the screen's numbers (main loop): MISREAD, the bits wrong, and the checks' words: 0 not run yet, 1 OK, 2 BAD, 3 TRAP */
static uint32_t c1probe_misreads(void) { return c1probe.c0_mis + c1probe.c1_mis + c1probe.rejected; }
static uint32_t c1probe_bits(void) { return c1probe.c0_bits | c1probe.c1_bits; }
static int c1probe_fpu(void)
{
    return c1probe.fpu_trap ? 3 : c1probe.fpu_bad ? 2 : c1probe.fpu_ok ? 1 : 0;
}
static int c1probe_dual(void) { return c1probe.dual_bad ? 2 : c1probe.dual_ok ? 1 : 0; }
