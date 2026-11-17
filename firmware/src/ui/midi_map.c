/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX MIDI mapping (docs/MIDI.md): our own CC map, one CC per continuous (knob) parameter, in and out, and the
 * channel messages. Included by app.c after ui/controls.c; tests/unit/test_controls.c includes the map with
 * CONTROLS_PURE.
 *
 * In (controls_midi, main loop; the channel: global MIDI CH, 0 = ALL): notes (velocity; note-off velocity ignored),
 * channel pressure (aftertouch, with the bottom pads': the higher), pitch bend (added to the pitch wheel's), CC 1 mod
 * wheel (with the panel's: the higher), CC 7 volume (times PROGRAM VOLUME, smoothed; master volume after), CC 64
 * sustain (= HOLD while down; the global SUSTAIN N.C. reads it upside down), CC 32 bank select (0..PROG_BANKS-1, the
 * next program change's bank; CC 0 is ignored: one bank byte is all 5 banks need, as the OB-6 receives CC 32) and
 * program change (0..99 in that bank, else the current program's bank; ignored in MANUAL), CC 120
 * all sound off (PANIC), CC 123 all notes off (every note released, HOLD unchanged), CC 121 reset controllers (not
 * volume, as MIDI's RP-015), CC 4 (foot controller) ignored in phase 1 (the reference lists it, not its effect), and with
 * PARAM RX = CC the map's CCs (0..127 over the param's range, bipolar 64 = 0). A CC received is not sent back.
 * Out (midi_cc_send): a panel edit, with PARAM TX = CC, as the map's CC on the MIDI channel (ALL = 1), only when its
 * 7-bit value differs from the last one either side had (a 0..255 param moving one step may stay on the same CC
 * value). Switches and selects have no CC (NRPN later). */
#include "controls.h"

#define MIDI_CC_NONE 0xFFu
static const uint8_t MIDI_CC_MAP[][2] = {   /* param, CC; the free CCs, the General MIDI sound controllers where they fit */
    {P_VCO1_FREQ, 14}, {P_VCO1_SHAPE, 15}, {P_VCO1_PW, 16},
    {P_VCO2_FREQ, 17}, {P_VCO2_FINE, 18}, {P_VCO2_SHAPE, 19}, {P_VCO2_PW, 20},
    {P_MIX_VCO1, 21}, {P_MIX_VCO2, 22}, {P_MIX_SUB, 23}, {P_MIX_NOISE, 24},
    {P_FLT_FREQ, 74}, {P_FLT_RESO, 71}, {P_FLT_MODE, 25}, {P_FLT_ENV_AMT, 26},
    {P_FENV_A, 27}, {P_FENV_D, 28}, {P_FENV_S, 29}, {P_FENV_R, 30},
    {P_LENV_A, 73}, {P_LENV_D, 75}, {P_LENV_S, 31}, {P_LENV_R, 72}, {P_LENV_AMT, 102},
    {P_LFO_FREQ, 76}, {P_LFO_AMT, 77},
    {P_XM_FENV, 103}, {P_XM_VCO2, 104},
    {P_AT_AMT, 105},
    {P_DETUNE, 94}, {P_PAN_SPREAD, 106}, {P_BEND_RANGE, 107}, {P_PRG_VOL, 108}, {P_PORTA_RATE, 5},
};
static uint8_t MIDI_CC[P_COUNT_PHASE1];   /* param -> CC (MIDI_CC_NONE); midi_map_init */
static uint8_t midi_cc_id[128];           /* CC -> param + 1 (0 none) */

static void midi_map_init(void)
{
    unsigned i;
    for (i = 0; i < P_COUNT_PHASE1; i++) MIDI_CC[i] = MIDI_CC_NONE;
    for (i = 0; i < 128u; i++) midi_cc_id[i] = 0;
    for (i = 0; i < sizeof MIDI_CC_MAP / sizeof MIDI_CC_MAP[0]; i++) {
        MIDI_CC[MIDI_CC_MAP[i][0]] = MIDI_CC_MAP[i][1];
        midi_cc_id[MIDI_CC_MAP[i][1] & 127u] = (uint8_t)(MIDI_CC_MAP[i][0] + 1u);
    }
}
static int midi_cc_param(uint32_t cc) { return cc < 128u ? (int)midi_cc_id[cc] - 1 : -1; }

#ifndef CONTROLS_PURE
static int16_t midi_cc_known[128], midi_cc_sent[128];   /* the last value either side had / we sent (-1 none) */
static void midi_map_reset(void)
{
    midi_map_init();
    for (unsigned i = 0; i < 128u; i++) midi_cc_known[i] = midi_cc_sent[i] = -1;
}
static uint32_t controls_tx_ch(void) { return ctl_glob[G_MIDI_CH] ? ctl_glob[G_MIDI_CH] - 1u : 0u; }

static void midi_cc_send(uint16_t id, int32_t v)
{
    uint32_t cc, x;
    if (id >= P_COUNT_PHASE1 || (cc = MIDI_CC[id]) == MIDI_CC_NONE) return;
    x = ctl_to7(id, v);
    if (midi_cc_known[cc] == (int16_t)x) return;
    midi_cc_known[cc] = midi_cc_sent[cc] = (int16_t)x;
    midi_send(0xB0u | controls_tx_ch(), cc, x);
}

/* program change: program prog (0..99) of bank (CC 32's, -1: the current program's). Ignored under MANUAL and while a
 * WRITE is armed or committing (ctl_wr != 0): the edit buffer is what WRITE stores, a load must not replace it */
static void ctl_program_change(int32_t bank, int32_t prog)
{
    ctl_prg_bank = bank, ctl_prg_req = prog;
    if (bank < 0) bank = ctl_prog / 100;
    if (ctl_manual || ctl_wr != 0 || bank >= (int32_t)PROG_BANKS || prog < 0 || prog > 99) return;
    ctl_prog_select(bank * 100 + prog);
}

void controls_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t ty = st & 0xF0u;
    int id;
    if (st >= 0xF0u || st < 0x80u) return;              /* (system messages: clock is phase 2) */
    if (ctl_glob[G_MIDI_CH] && (st & 15u) != ctl_glob[G_MIDI_CH] - 1u) return;
    d1 &= 127u, d2 &= 127u;
    switch (ty) {
    case 0x90u:
        if (d2) { eng_note((int)d1, (int)d2); controls_key((int)d1); }
        else eng_note((int)d1, 0);                    /* (velocity 0: a note-off) */
        break;
    case 0x80u: eng_note((int)d1, 0); break;
    case 0xD0u: ctl_midi_at = (uint8_t)d1; break;
    case 0xE0u: ctl_midi_bend = (int32_t)(d2 << 7 | d1) - 8192; break;
    case 0xC0u: ctl_program_change(ctl_bank_sel, (int32_t)d1); break;
    case 0xB0u:
        if (d1 == 32u) ctl_bank_sel = (int32_t)d2;
        else if (d1 == 0u) { }                         /* bank select MSB: ignored (CC 32 alone picks the bank) */
        else if (d1 == 1u) ctl_midi_wheel = (uint8_t)d2;
        else if (d1 == 64u) ctl_sustain = (d2 >= 64u) != (ctl_glob[G_SUSTAIN] != 0);   /* N.C.: upside down */
        else if (d1 == 7u) ctl_midi_vol = (uint8_t)d2;
        else if (d1 == 120u) { eng_panic(); ctl_hold = ctl_sustain = 0; }   /* all sound off: silence, HOLD off */
        else if (d1 == 123u) eng_all_notes_off();       /* all notes off: released; HOLD (pad, sustain) unchanged */
        else if (d1 == 121u) { ctl_midi_wheel = 0; ctl_midi_bend = 0; ctl_midi_at = 0; ctl_sustain = 0; }
        else if (ctl_glob[G_PARAM_RX] && (id = midi_cc_param(d1)) >= 0) {
            midi_cc_known[d1] = (int16_t)d2;
            ctl_set(&eng_edit, (uint16_t)id, ctl_from7((uint16_t)id, d2), 0);
        }
        break;
    }
}
#endif
