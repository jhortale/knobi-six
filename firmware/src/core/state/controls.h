/* SPDX-License-Identifier: GPL-3.0-only */
/* Written by tools/core_state.py from firmware/src/ui/controls.c (a state block): do not edit. The
 * firmware's state there, per knobi_core instance: the struct, the initial values, its slot and a macro per
 * variable (KC_ST: the block of the instance this thread runs, knobi_core.c kc_bind). Not for the device build. */
#ifndef KC_STATE_CONTROLS_H
#define KC_STATE_CONTROLS_H
#define CTL_BOOT_MS 1000u                              /* the boot screen, then Play (or at the first button) */
#define CTL_BEAT_MS 90u                                /* TAP lit this long on each beat */
#define CTL_RESTORE_MS 2000u
#define CTL_FADE_WAIT_MS 100u                          /* the fade-out not confirmed (no audio running): save anyway */
struct kc_st_controls {
    int8_t ctl_glob[G_COUNT];
    clk_t ctl_tapclk;
    uint32_t ctl_beat_seen;
    uint32_t ctl_beat_ms;
    uint8_t ctl_row[SCR_COUNT];
    int ctl_last_edit;
    int ctl_before_glob;
    int ctl_before_seq;
    uint8_t ctl_hold;
    uint8_t ctl_sustain;
    uint8_t ctl_midi_at;
    uint8_t ctl_midi_wheel;
    uint8_t ctl_midi_vol;
    int32_t ctl_midi_bend;
    uint32_t ctl_pressed;
    uint32_t ctl_released;
    int32_t ctl_enc_pos[8];
    uint32_t ctl_enc_ms[8];
    uint8_t ctl_fpos[4];
    ctl_fader_t ctl_fad[4];
    uint8_t ctl_chord_on;
    uint8_t ctl_chord_n;
    uint8_t ctl_chord_cap[6];
    int32_t ctl_prg_bank;
    int32_t ctl_prg_req;
    int32_t ctl_bank_sel;
    int32_t ctl_prog;
    int32_t ctl_prog_want;
    int32_t ctl_wr_dest;
    uint8_t ctl_manual;
    uint8_t ctl_wr;
    uint8_t ctl_want_factory;
    uint32_t ctl_restores;
    uint8_t ctl_notice;
    int32_t ctl_restore_ask;
    uint32_t ctl_restore_t;
    int8_t ctl_wr_rc;
    uint32_t ctl_wr_t;
    uint32_t ctl_writes;
    uint16_t ctl_last_id;
    int32_t ctl_last_v;
    uint32_t ctl_edits;
    uint32_t ctl_scr_n;
    uint32_t ctl_led[16];
};
#define KC_IX_controls 3
static void kc_st_controls_init(struct kc_st_controls *s)
{
    memset((void *)s, 0, sizeof *s);
    { static const int8_t v[G_COUNT] = {0, POT_RELATIVE, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0}; memcpy((void *)s->ctl_glob, v, sizeof v); }
    s->ctl_last_edit = SCR_VCO;
    s->ctl_before_glob = SCR_PLAY;
    s->ctl_before_seq = SCR_PLAY;
    s->ctl_midi_vol = 127;
    s->ctl_prg_bank = -1;
    s->ctl_prg_req = -1;
    s->ctl_bank_sel = -1;
    s->ctl_prog_want = -1;
    s->ctl_restore_ask = -1;
    s->ctl_last_id = B_NONE;
}
#define ctl_glob (KC_ST(controls)->ctl_glob)
#define ctl_tapclk (KC_ST(controls)->ctl_tapclk)
#define ctl_beat_seen (KC_ST(controls)->ctl_beat_seen)
#define ctl_beat_ms (KC_ST(controls)->ctl_beat_ms)
#define ctl_row (KC_ST(controls)->ctl_row)
#define ctl_last_edit (KC_ST(controls)->ctl_last_edit)
#define ctl_before_glob (KC_ST(controls)->ctl_before_glob)
#define ctl_before_seq (KC_ST(controls)->ctl_before_seq)
#define ctl_hold (KC_ST(controls)->ctl_hold)
#define ctl_sustain (KC_ST(controls)->ctl_sustain)
#define ctl_midi_at (KC_ST(controls)->ctl_midi_at)
#define ctl_midi_wheel (KC_ST(controls)->ctl_midi_wheel)
#define ctl_midi_vol (KC_ST(controls)->ctl_midi_vol)
#define ctl_midi_bend (KC_ST(controls)->ctl_midi_bend)
#define ctl_pressed (KC_ST(controls)->ctl_pressed)
#define ctl_released (KC_ST(controls)->ctl_released)
#define ctl_enc_pos (KC_ST(controls)->ctl_enc_pos)
#define ctl_enc_ms (KC_ST(controls)->ctl_enc_ms)
#define ctl_fpos (KC_ST(controls)->ctl_fpos)
#define ctl_fad (KC_ST(controls)->ctl_fad)
#define ctl_chord_on (KC_ST(controls)->ctl_chord_on)
#define ctl_chord_n (KC_ST(controls)->ctl_chord_n)
#define ctl_chord_cap (KC_ST(controls)->ctl_chord_cap)
#define ctl_prg_bank (KC_ST(controls)->ctl_prg_bank)
#define ctl_prg_req (KC_ST(controls)->ctl_prg_req)
#define ctl_bank_sel (KC_ST(controls)->ctl_bank_sel)
#define ctl_prog (KC_ST(controls)->ctl_prog)
#define ctl_prog_want (KC_ST(controls)->ctl_prog_want)
#define ctl_wr_dest (KC_ST(controls)->ctl_wr_dest)
#define ctl_manual (KC_ST(controls)->ctl_manual)
#define ctl_wr (KC_ST(controls)->ctl_wr)
#define ctl_want_factory (KC_ST(controls)->ctl_want_factory)
#define ctl_restores (KC_ST(controls)->ctl_restores)
#define ctl_notice (KC_ST(controls)->ctl_notice)
#define ctl_restore_ask (KC_ST(controls)->ctl_restore_ask)
#define ctl_restore_t (KC_ST(controls)->ctl_restore_t)
#define ctl_wr_rc (KC_ST(controls)->ctl_wr_rc)
#define ctl_wr_t (KC_ST(controls)->ctl_wr_t)
#define ctl_writes (KC_ST(controls)->ctl_writes)
#define ctl_last_id (KC_ST(controls)->ctl_last_id)
#define ctl_last_v (KC_ST(controls)->ctl_last_v)
#define ctl_edits (KC_ST(controls)->ctl_edits)
#define ctl_scr_n (KC_ST(controls)->ctl_scr_n)
#define ctl_led (KC_ST(controls)->ctl_led)
#endif
