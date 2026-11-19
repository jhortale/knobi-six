/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX: a WRITE with a sequence in fm1-emu (Phase 3 Task 7; tests/cpu1_emu/run.sh cases seqwrite and seqwrite1).
 * Test hook of an image built with -DKNOBI_SEQ_TEST=1, never in a real build. It works in the default build (one
 * core; no KNOBI_CPU1 code at all) and in a KNOBI_CPU1=1 build, where it turns DUAL CORE on first. app.c includes this
 * file (after the main loop's state, before app_main); check.py reads seq_test from RAM at the end of the run (--dump).
 *
 * The script, from the main loop by fm1_ms: at 400 ms the clock to 250 BPM / 1/16 (a step every 60 ms) and a sequence
 * of 8 steps (a 6-note chord, a rest, a 3-note chord, a tie, a 2-note chord, a note, a rest, a 4-note chord) recorded
 * into the edit buffer as the recorder does (edit_seq_mut + seq_rec_*); at 520 ms SEQ PLAY (eng_seq_play: the main
 * loop has handed the sequence to the player by then); WRITE (BT) at 1,200 ms and ENTER at 1,300 ms: the fade, the
 * program slot's erase and write and the sequence store's put with the sequence still playing; 300 ms after the WRITE
 * the sequence must still be playing; the program is then read back as a power cycle would (prog_load: the generation
 * the record names, seqst_get: the entry) and opened (prog_open: edit buffer = what the store gives). Recorded: the
 * audio around it (silent blocks and the largest sample-to-sample step and peak before, during and after the WRITE:
 * a fade, no click), CPU1's state in a KNOBI_CPU1 build.
 * fm1-emu does not time a flash erase (the gap's length here is the emulator's, not the chip's), and it does not turn
 * the XIP flash off: what it shows is that the flow works through the real code paths, with the sound running. */
#if KNOBI_SEQ_TEST
struct {
    uint32_t magic, done, blocks, phase;
    uint32_t len, played, wr_rc, writes, write_at, play_before, play_after, cur_a, cur_b;
    uint32_t gen, rb_len, rb_equal, open_rc, open_equal, used;
    uint32_t n[4], silent[4], step[4], peak[4];          /* blocks, silent blocks, largest step and peak by phase (index):
                                                          * 1 before the WRITE, 2 its fade and save, 3 after it */
    uint32_t dual, state, why, failed, rejected;         /* (KNOBI_CPU1 builds) */
} seq_test = { 0x54535145u };                            /* "EQST" */

static seq_t st_want;

static int st_same(const seq_t *a, const seq_t *b)
{
    if (a->len != b->len) return 0;
    for (unsigned i = 0; i < a->len; i++) {
        if (a->step[i].count != b->step[i].count || a->step[i].tie != b->step[i].tie) return 0;
        for (unsigned k = 0; k < a->step[i].count; k++)
            if (a->step[i].notes[k] != b->step[i].notes[k] || a->step[i].vels[k] != b->step[i].vels[k]) return 0;
    }
    return 1;
}

static void seq_test_audio(int32_t *out, uint32_t n)     /* app_audio, instead of eng_render alone */
{
    static int32_t last[2];
    uint32_t ph = seq_test.phase;
    uint32_t nz = 0, pk = 0, st = 0;
    if (!eng_ready) return;
    eng_render(out, n);
    for (uint32_t i = 0; i < 2u * n; i++) {
        int32_t x = out[i], d = x - last[i & 1u], a = x < 0 ? -x : x;
        if (d < 0) d = -d;
        nz |= (uint32_t)(x != 0);
        if ((uint32_t)a > pk) pk = (uint32_t)a;
        if ((uint32_t)d > st) st = (uint32_t)d;
        last[i & 1u] = x;
    }
    if (ph >= 1u && ph <= 3u) {
        seq_test.n[ph]++;
        seq_test.silent[ph] += !nz;
        if (st > seq_test.step[ph]) seq_test.step[ph] = st;
        if (pk > seq_test.peak[ph]) seq_test.peak[ph] = pk;
    }
    seq_test.blocks++;
}

static void seq_test_main(int *scr)                      /* (the main loop, every pass) */
{
    static uint32_t step, t_write;
#if KNOBI_CPU1
    seq_test.dual = eng_dual;
    seq_test.state = (uint32_t)cpu1_state();
    seq_test.why = cpu1_why();
    seq_test.failed = eng_c1_failed;
    seq_test.rejected = fm1_cpu1_mb.rejected;
#endif
    seq_test.used = seqst_used();
    if (step == 0u && fm1_ms >= 400u) {
        static const uint8_t c6[6] = {48, 55, 60, 64, 67, 72}, c3[3] = {50, 57, 65}, c2[2] = {52, 59}, one[1] = {79},
                             c4[4] = {53, 60, 65, 69};
        static const uint8_t vel[6] = {100, 96, 92, 88, 84, 80};
        seq_t *q;
#if KNOBI_CPU1
        ctl_glob[G_DUAL] = 1;                            /* (the Globals pad) */
#endif
        (void)eng_param(P_BPM, 250);
        (void)eng_param(P_CLK_DIV, CLK_DIV_16TH);
        q = edit_seq_mut();
        seq_clear(q);
        (void)seq_rec_step(q, c6, vel, 6), (void)seq_rec_rest(q), (void)seq_rec_step(q, c3, vel, 3);
        (void)seq_rec_tie(q), (void)seq_rec_step(q, c2, vel, 2), (void)seq_rec_step(q, one, vel, 1);
        (void)seq_rec_rest(q), (void)seq_rec_step(q, c4, vel, 4);
        st_want = *q;
        seq_test.len = q->len;
        step++;
    } else if (step == 1u && fm1_ms >= 520u) {
        (void)eng_seq_play(1);
        seq_test.phase = 1u;                             /* playing, before the WRITE */
        seq_test.play_before = (uint32_t)eng_seq_playing();
        step++;
    } else if (step == 2u && fm1_ms >= 1200u) {
        ctl_write_press(scr);                            /* BT: WRITE armed, the Program screen, destination = here */
        step++;
    } else if (step == 3u && fm1_ms >= 1300u) {
        ctl_enter();                                     /* ENTER: the fade out, the save, the fade in */
        seq_test.phase = 2u;
        step++;
    } else if (step == 4u && !ctl_wr && ctl_writes) {
        program_t pp;
        seq_t chk;
        seq_test.write_at = fm1_ms, seq_test.wr_rc = (uint32_t)ctl_wr_rc, seq_test.writes = ctl_writes;
        seq_test.cur_a = eng_seq_cur;
        seq_test.phase = 3u;
        t_write = fm1_ms;
        if (prog_load((uint32_t)ctl_prog, &pp) == 0) {   /* as a power cycle reloads it */
            seq_test.gen = (uint32_t)pp.v[P_SEQ_GEN];
            seq_test.rb_equal = seqst_get((uint16_t)ctl_prog, (uint16_t)pp.v[P_SEQ_GEN], &chk) && st_same(&chk, &st_want);
            seq_test.rb_len = chk.len;
        }
        seq_test.open_rc = (uint32_t)prog_open((uint16_t)ctl_prog, &pp);
        seq_test.open_equal = (uint32_t)st_same(edit_seq(), &st_want);
        step++;
    } else if (step == 5u && fm1_ms >= t_write + 300u) {
        seq_test.cur_b = eng_seq_cur;
        seq_test.play_after = (uint32_t)eng_seq_state;   /* 1: still playing */
        (void)eng_seq_play(0);
        seq_test.played = 1u;
        seq_test.phase = 0u;                             /* (the stats end with the sequence) */
        seq_test.done = 1u;
        step++;
    }
}
#endif
