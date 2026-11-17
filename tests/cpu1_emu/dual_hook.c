/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX dual-core voice rendering in fm1-emu (Phase 2 Task 3; tests/cpu1_emu/run.sh): test hooks of a
 * KNOBI_CPU1=1 image, never in a real build. app.c includes this file (after the main loop's state, before app_main)
 * when KNOBI_CPU1_TEST is 6, 7 or 8; check.py reads dual_test from RAM at the end of the run (--dump).
 *
 *   6 dual      the audio interrupt plays a fixed script, block by block (so both passes see the same events at the
 *               same blocks): four patches (the init chord, the musical stress patch on C3..C5 and on C5..C7, the
 *               extreme patch on C3..C5; tests/cpu_emu/harness.c's), each from synth_init: DT_ON blocks held, then
 *               released for the rest of DT_SEG. Pass 0 with DUAL CORE off, pass 1 with it on, on the same image
 *               and in the same run. Each segment's output is checksummed (as the CPU gate's harness does): pass 1
 *               must equal pass 0 for every patch, with CPU1 jobs in pass 1 only. Also recorded per segment: the
 *               ticks synth_render took on CPU0 (the joins' waits included: what the CPU guard sees) over the held
 *               blocks after the first 8, and CPU1's job load (eng_c1_load, the Globals line's "C1 ON n%").
 *               The render goes around eng_render's CPU guard (it would shed voices on its own timing, which
 *               differs between the passes: the output would not be comparable).
 *   7 dualfail  as 6, but CPU1's 1000th voice job never returns (engine.c ENG_C1_HANG_AT; in pass 1's second
 *               patch): the join fails after ENG_C1_JOIN_TICKS, CPU0 renders set B itself in that period, CPU1 is
 *               retired (LATE) and the rest runs on one core. Every checksum must still equal pass 0's (the job hung
 *               before it touched a voice: the output stays whole and exact).
 *   8 write     DUAL CORE turned on (as the Globals pad would) at 400 ms and the init chord held from 500 ms; WRITE
 *               (BT) at 900 ms and ENTER at 1000 ms: the fade, the program slot's erase and write with the voices on
 *               two cores; the notes off at 1800 ms; then the globals' autosave after 3 s of quiet (DUAL CORE and the
 *               program changed: the extended record, read back); a chord again after it. CPU1 must stay IDLE (never
 *               retired by the flash gate's wait: every erase and write found it idle in its RAM loop), the jobs go
 *               on before, between and after, and the audio runs throughout. */
#if KNOBI_CPU1_TEST == 6 || KNOBI_CPU1_TEST == 7
#define DT_SEG 40u                              /* blocks a segment (256 frames each) */
#define DT_ON 32u                               /* of them with the notes held */
#define DT_NPATCH 4u
#endif
struct {
    uint32_t magic, done, blocks;
    uint32_t sum[2][4], jobs[2][4], used[2][4], c1load[4];   /* (6, 7) per pass and patch */
    uint32_t failed, state, why, rejected, mbjobs;           /* at the end */
    uint32_t dual, wr_rc, writes, write_at, jobs_before, jobs_write, saves, save_at, jobs_save, dual_read, jobs_after,
             at_fail;                                        /* (8) */
} dual_test = { 0x54554444u };                               /* "DDUT" */

static void dual_test_note(void)
{
    dual_test.failed = eng_c1_failed;
    dual_test.state = (uint32_t)cpu1_state();
    dual_test.why = cpu1_why();
    dual_test.rejected = fm1_cpu1_mb.rejected;
    dual_test.mbjobs = fm1_cpu1_mb.jobs;
    if (dual_test.state == CPU1_FAIL && !dual_test.at_fail) dual_test.at_fail = fm1_ms;
}

#if KNOBI_CPU1_TEST == 6 || KNOBI_CPU1_TEST == 7
static const int16_t DT_MUSICAL[][2] = {   /* = tests/scripts/musical.txt = harness.c PATCH 3, 4 */
    {P_MIX_VCO1, 95}, {P_MIX_VCO2, 95}, {P_MIX_SUB, 95}, {P_MIX_NOISE, 95},
    {P_VCO1_SHAPE, 255}, {P_VCO1_PW, 90}, {P_VCO2_SHAPE, 254}, {P_VCO2_PW, 160}, {P_VCO1_SYNC, 1},
    {P_FLT_FREQ, 90}, {P_FLT_RESO, 201}, {P_FLT_ENV_AMT, 76}, {P_FENV_S, 40}, {P_PRG_VOL, 100},
    {P_XM_VCO2, 64}, {P_XM_D_VCO1, 1}, {P_XM_D_FFREQ, 1},
    {P_LFO_FREQ, 173}, {P_LFO_AMT, 128}, {P_LFO_D_VCO1, 1}, {P_LFO_D_FFREQ, 1},
    {P_AT_AMT, 64}, {P_AT_D_FFREQ, 1},
};
static const int16_t DT_EXTREME[][2] = {   /* = tests/scripts/worst.txt = harness.c PATCH 1 */
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
static const uint8_t DT_NOTES[DT_NPATCH][6] = {
    {60, 64, 67, 72, 76, 79},                   /* init chord, C4..G5 */
    {48, 55, 60, 64, 67, 72},                   /* musical, C3..C5 */
    {72, 79, 84, 88, 91, 96},                   /* musical, C5..C7 */
    {48, 55, 60, 64, 67, 72},                   /* extreme, C3..C5 */
};
static program_t dt_prog;
static void dual_test_setup(uint32_t pass, uint32_t pi)
{
    ctl_glob[G_DUAL] = (int8_t)pass;            /* (the main loop copies it into eng_dual: the same value) */
    eng_dual = (uint8_t)pass;
    synth_init(&eng_synth);
    program_init(&dt_prog);
    if (pi == 1u || pi == 2u)
        for (unsigned i = 0; i < sizeof DT_MUSICAL / sizeof DT_MUSICAL[0]; i++) dt_prog.v[DT_MUSICAL[i][0]] = DT_MUSICAL[i][1];
    if (pi == 3u)
        for (unsigned i = 0; i < sizeof DT_EXTREME / sizeof DT_EXTREME[0]; i++) dt_prog.v[DT_EXTREME[i][0]] = DT_EXTREME[i][1];
    synth_program(&eng_synth, &dt_prog);
    if (pi == 3u) synth_wheel(&eng_synth, 127), synth_pressure(&eng_synth, 127);
    else if (pi) synth_pressure(&eng_synth, 100);
    for (unsigned i = 0; i < 6u; i++) synth_note(&eng_synth, DT_NOTES[pi][i], pi == 3u ? 127 : 100);
}
/* app_audio, instead of eng_render: the script, then (done) the usual render */
static void dual_test_audio(int32_t *out, uint32_t n)
{
    static uint32_t blk;
    uint32_t seg, k, pass, pi, j0, t0, used, sum;
    if (!eng_ready) return;
    seg = blk / DT_SEG, k = blk % DT_SEG;
    if (seg >= 2u * DT_NPATCH) {
        dual_test.done = 1;
        dual_test_note();
        eng_render(out, n);
        return;
    }
    pass = seg / DT_NPATCH, pi = seg % DT_NPATCH;
    if (k == 0u) dual_test_setup(pass, pi);
    if (k == DT_ON)
        for (unsigned i = 0; i < 6u; i++) synth_note(&eng_synth, DT_NOTES[pi][i], 0);
    j0 = eng_c1_jobs;
    t0 = eng_ticks();
    synth_render(&eng_synth, out, n);
    used = eng_ticks() - t0;
    eng_c1_cpu(n * ENG_TICKS_PER_FRAME_X10 / 10u);
    sum = dual_test.sum[pass][pi];
    for (uint32_t i = 0; i < 2u * n; i++) sum = (sum << 1 | sum >> 31) ^ (uint32_t)out[i];
    dual_test.sum[pass][pi] = sum;
    dual_test.jobs[pass][pi] += eng_c1_jobs - j0;
    if (k >= 8u && k < DT_ON) dual_test.used[pass][pi] += used;
    if (pass && k == DT_ON - 1u) dual_test.c1load[pi] = eng_c1_load;
    dual_test.blocks = ++blk;
    dual_test_note();
}
static void dual_test_main(int *scr) { (void)scr; }
#else
static void dual_test_audio(int32_t *out, uint32_t n) { eng_render(out, n); }
static const uint8_t DW_NOTES[6] = {60, 64, 67, 72, 76, 79};
static void dual_test_main(int *scr)            /* (8) the main loop, every pass */
{
    static uint32_t step;
    unsigned i;
    dual_test_note();
    dual_test.dual = eng_dual;
    dual_test.saves = set_saves;
    if (step == 0u && fm1_ms >= 400u) ctl_glob[G_DUAL] = 1, step++;             /* the Globals pad */
    else if (step == 1u && fm1_ms >= 500u) {
        for (i = 0; i < 6u; i++) eng_note(DW_NOTES[i], 100);
        step++;
    } else if (step == 2u && fm1_ms >= 900u) {
        dual_test.jobs_before = eng_c1_jobs;
        ctl_write_press(scr);                    /* BT: WRITE armed, the Program screen, destination = this program */
        step++;
    } else if (step == 3u && fm1_ms >= 1000u) ctl_enter(), step++;               /* ENTER: fade, save, fade in */
    else if (step == 4u && !ctl_wr && ctl_writes) {
        dual_test.write_at = fm1_ms, dual_test.wr_rc = (uint32_t)ctl_wr_rc, dual_test.writes = ctl_writes;
        dual_test.jobs_write = eng_c1_jobs;
        step++;
    } else if (step == 5u && fm1_ms >= 1800u) {
        for (i = 0; i < 6u; i++) eng_note(DW_NOTES[i], 0);
        step++;
    } else if (step == 6u && set_saves) {       /* the autosave (3 s of quiet): DUAL CORE on, so the extended record */
        globals_t g;
        globals_load(&g);
        dual_test.save_at = fm1_ms, dual_test.jobs_save = eng_c1_jobs, dual_test.dual_read = (uint32_t)g.v[G_DUAL];
        step++;
    } else if (step == 7u && fm1_ms >= dual_test.save_at + 200u) {
        for (i = 0; i < 6u; i++) eng_note(DW_NOTES[i], 100);
        step++;
    } else if (step == 8u && fm1_ms >= dual_test.save_at + 700u) {
        dual_test.jobs_after = eng_c1_jobs;
        for (i = 0; i < 6u; i++) eng_note(DW_NOTES[i], 0);
        dual_test.done = 1;
        step++;
    }
}
#endif
