/* SPDX-License-Identifier: GPL-3.0-only */
/* Written by tools/core_state.py from firmware/src/engine.c (a state block): do not edit. The
 * firmware's state there, per knobi_core instance: the struct, the initial values, its slot and a macro per
 * variable (KC_ST: the block of the instance this thread runs, knobi_core.c kc_bind). Not for the device build. */
#ifndef KC_STATE_ENGINE_H
#define KC_STATE_ENGINE_H
#define ENG_FADE_ONE 32768                /* the fade's gain, Q15 */
#define ENG_FADE_MS 10
#define ENG_FADE_STEP (ENG_FADE_ONE / (ENG_FADE_MS * 441 / 10))   /* a frame: 74 -> 443 frames = 10.0 ms */
#define ENG_FADE_QUIET 2u                 /* whole silent blocks before eng_faded() */
#define ENG_LOAD_MAX 999u
struct kc_st_engine {
    synth_t eng_synth;
    eng_ev_t eng_q[ENG_QLEN];
    volatile uint32_t eng_qw;
    volatile uint32_t eng_qr;
    volatile uint32_t eng_drops;
    program_t eng_pbuf;
    program_t eng_edit;
    volatile uint32_t eng_pw;
    volatile uint32_t eng_pr;
    volatile uint8_t eng_ready;
    volatile uint8_t eng_voices;
    int32_t eng_last_bend;
    int32_t eng_last_wheel;
    int32_t eng_last_press;
    int32_t eng_last_hold;
    int32_t eng_last_vol;
    int32_t eng_last_tune;
    int32_t eng_last_clkmode;
    int32_t eng_last_arpsync;
    arp_t eng_arp;
    uint8_t eng_arp_sync_on;
    seqp_t eng_seqp;
    seq_t eng_seqd;
    seq_t eng_sbuf;
    volatile uint32_t eng_sw;
    volatile uint32_t eng_sr;
    int32_t eng_last_seqplay;
    int32_t eng_last_seqxp;
    volatile uint8_t eng_seq_state;
    volatile uint8_t eng_seq_cur;
    clk_t eng_clk;
    volatile uint32_t eng_clk_ticks;
    volatile uint32_t eng_clk_beats;
    volatile uint32_t eng_clk_starts;
    volatile uint32_t eng_clk_start_ticks;
    volatile uint8_t eng_clk_run;
    volatile uint8_t eng_clk_alive;
    volatile uint8_t eng_clk_mode;
    volatile uint16_t eng_clk_bpm10;
    volatile uint8_t eng_fade_out;
    volatile int32_t eng_fade_g;
    volatile uint8_t eng_fade_quiet;
    volatile uint16_t eng_load;
    volatile uint16_t eng_peak;
    volatile uint8_t eng_vlimit;
    volatile uint32_t eng_sheds;
    uint32_t eng_load_q3;
    uint32_t eng_peak_q8;
    uint32_t eng_off_pend[4];
    uint32_t eng_par_pend[(P_COUNT_RESERVED + 31u) / 32u];
    uint8_t eng_panic_pend;
    uint8_t eng_alloff_pend;
    uint8_t eng_chord_pend;
};
#define KC_IX_engine 2
static void kc_st_engine_init(struct kc_st_engine *s)
{
    memset((void *)s, 0, sizeof *s);
    s->eng_last_bend = 8192;
    s->eng_last_vol = 127;
    s->eng_seq_cur = SEQ_NO_STEP;
    s->eng_fade_g = ENG_FADE_ONE;
    s->eng_vlimit = NVOICE;
}
#define eng_synth (KC_ST(engine)->eng_synth)
#define eng_q (KC_ST(engine)->eng_q)
#define eng_qw (KC_ST(engine)->eng_qw)
#define eng_qr (KC_ST(engine)->eng_qr)
#define eng_drops (KC_ST(engine)->eng_drops)
#define eng_pbuf (KC_ST(engine)->eng_pbuf)
#define eng_edit (KC_ST(engine)->eng_edit)
#define eng_pw (KC_ST(engine)->eng_pw)
#define eng_pr (KC_ST(engine)->eng_pr)
#define eng_ready (KC_ST(engine)->eng_ready)
#define eng_voices (KC_ST(engine)->eng_voices)
#define eng_last_bend (KC_ST(engine)->eng_last_bend)
#define eng_last_wheel (KC_ST(engine)->eng_last_wheel)
#define eng_last_press (KC_ST(engine)->eng_last_press)
#define eng_last_hold (KC_ST(engine)->eng_last_hold)
#define eng_last_vol (KC_ST(engine)->eng_last_vol)
#define eng_last_tune (KC_ST(engine)->eng_last_tune)
#define eng_last_clkmode (KC_ST(engine)->eng_last_clkmode)
#define eng_last_arpsync (KC_ST(engine)->eng_last_arpsync)
#define eng_arp (KC_ST(engine)->eng_arp)
#define eng_arp_sync_on (KC_ST(engine)->eng_arp_sync_on)
#define eng_seqp (KC_ST(engine)->eng_seqp)
#define eng_seqd (KC_ST(engine)->eng_seqd)
#define eng_sbuf (KC_ST(engine)->eng_sbuf)
#define eng_sw (KC_ST(engine)->eng_sw)
#define eng_sr (KC_ST(engine)->eng_sr)
#define eng_last_seqplay (KC_ST(engine)->eng_last_seqplay)
#define eng_last_seqxp (KC_ST(engine)->eng_last_seqxp)
#define eng_seq_state (KC_ST(engine)->eng_seq_state)
#define eng_seq_cur (KC_ST(engine)->eng_seq_cur)
#define eng_clk (KC_ST(engine)->eng_clk)
#define eng_clk_ticks (KC_ST(engine)->eng_clk_ticks)
#define eng_clk_beats (KC_ST(engine)->eng_clk_beats)
#define eng_clk_starts (KC_ST(engine)->eng_clk_starts)
#define eng_clk_start_ticks (KC_ST(engine)->eng_clk_start_ticks)
#define eng_clk_run (KC_ST(engine)->eng_clk_run)
#define eng_clk_alive (KC_ST(engine)->eng_clk_alive)
#define eng_clk_mode (KC_ST(engine)->eng_clk_mode)
#define eng_clk_bpm10 (KC_ST(engine)->eng_clk_bpm10)
#define eng_fade_out (KC_ST(engine)->eng_fade_out)
#define eng_fade_g (KC_ST(engine)->eng_fade_g)
#define eng_fade_quiet (KC_ST(engine)->eng_fade_quiet)
#define eng_load (KC_ST(engine)->eng_load)
#define eng_peak (KC_ST(engine)->eng_peak)
#define eng_vlimit (KC_ST(engine)->eng_vlimit)
#define eng_sheds (KC_ST(engine)->eng_sheds)
#define eng_load_q3 (KC_ST(engine)->eng_load_q3)
#define eng_peak_q8 (KC_ST(engine)->eng_peak_q8)
#define eng_off_pend (KC_ST(engine)->eng_off_pend)
#define eng_par_pend (KC_ST(engine)->eng_par_pend)
#define eng_panic_pend (KC_ST(engine)->eng_panic_pend)
#define eng_alloff_pend (KC_ST(engine)->eng_alloff_pend)
#define eng_chord_pend (KC_ST(engine)->eng_chord_pend)
#endif
