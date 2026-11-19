/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX MIDI mapping (docs/MIDI.md): our own CC map, one CC per continuous (knob) parameter, in and out, and the
 * channel messages, and MIDI clock in (ctl_midi_rt). Included by app.c after ui/controls.c; tests/unit/test_controls.c
 * includes the map with CONTROLS_PURE.
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
 * value). Switches and selects have no CC (NRPN later).
 * Arp notes out (ctl_arp_out, main loop; synth/arp.h's ring): with ARP ON, PARAM TX = CC and LOCAL ON, the notes the
 * arpeggiator plays go out on the MIDI channel (ALL = 1), as the reference has it (arp notes sent when Param Xmit is on;
 * not under LOCAL OFF, where MIDI in alone feeds the arp and a DAW's echo would feed it its own notes), and (LOCAL
 * ON: the keys feed the arp) the keys played do not (app.c: their notes stay home, so a receiver does not play the chord under the arp; a key's
 * note-off goes out only if its note-on did; a change of ARP ON, PARAM TX or LOCAL with keys held
 * sends their note-offs, or late note-ons, so the receiver's picture stays right: app.c). A note-off
 * goes out for every arp note-on sent, whatever PARAM TX has become since (nothing left hanging on the receiver).
 * Sequence notes out: the sequencer's player (synth/seq.h) writes the same ring (the arp and the sequencer never play
 * together), so its notes go out here under the same rules and pairing (PARAM TX and LOCAL ON for a note-on; every
 * note-off sent on its note-on's channel). The keys played with a sequence go out as played (ctl_keys_out: only ARP ON
 * keeps them home).
 * Sequencer recording (Phase 3 Task 5): MIDI notes in (the MIDI CH heard) are recorded as the keys are (controls_rec_note;
 * the keys only under LOCAL ON, so a DAW echoing them back under LOCAL OFF records them once). They never transpose: only
 * the keys do (SEQ REC held, or UNISON on). */
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
#ifdef KNOBI_CORE
#include "../core/state/midi_cc.h"   /* knobi_core: per instance (tools/core_state.py) */
#else
static uint8_t MIDI_CC[P_COUNT_PHASE1];   /* param -> CC (MIDI_CC_NONE); midi_map_init */
static uint8_t midi_cc_id[128];           /* CC -> param + 1 (0 none) */
#endif

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
#ifdef KNOBI_CORE
#include "../core/state/midi_known.h"   /* knobi_core: per instance (tools/core_state.py) */
#else
static int16_t midi_cc_known[128], midi_cc_sent[128];   /* the last value either side had / we sent (-1 none) */
#endif
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

#ifdef KNOBI_CORE
#include "../core/state/midi_arp.h"   /* knobi_core: per instance (tools/core_state.py) */
#else
static uint32_t ctl_arp_r;                 /* the arp ring's read index (main loop) */
static uint32_t ctl_arp_sent[4];           /* arp notes on at the receiver (a bit a note) */
static uint8_t ctl_arp_ch[128];            /* the channel each one's note-on went out on (its note-off's, final review 5:
                                            * MIDI CH may change during its gate) */
static uint32_t ctl_arp_outs;              /* arp notes sent so far (host probe "arpout") */
static uint32_t ctl_arp_offs;              /* arp note-offs sent so far (host probe "arpoff") */
static uint8_t ctl_arp_offch;              /* the channel of the last one, 0..15 (host probe "arpoffch") */
static uint32_t ctl_arp_overruns;          /* ring overruns (host probe "arpoverrun") */
#endif
/* the arp's note-ons go out with PARAM TX on and LOCAL ON only (final review 2): under LOCAL OFF the arp is fed by MIDI
 * in alone, and a DAW echoing our notes back would feed the arp its own notes (with a latch on, a loop that sustains
 * itself). Every note-on sent gets its note-off, whatever PARAM TX or LOCAL have become since (ctl_arp_sent) */
static void ctl_arp_out(void)
{
    uint32_t w = eng_arp.out_w, ch = controls_tx_ch();
    __asm__ volatile("" ::: "memory");     /* (the entries before the index: arp.c writes them first) */
    if (w - ctl_arp_r > ARP_OUT_N) {       /* a long stall: the oldest are gone, note-offs maybe among them: every note on
                                            * at the receiver gets its note-off now (on its channel), then the ring goes on
                                            * from what it still holds (an off there for a note already off: not sent) */
        for (uint32_t n = 0; n < 128u; n++)
            if (ctl_arp_sent[n >> 5] >> (n & 31u) & 1u) {
                midi_send(0x80u | ctl_arp_ch[n], n, 0);
                ctl_arp_offs++, ctl_arp_offch = ctl_arp_ch[n];
            }
        for (uint32_t i = 0; i < 4u; i++) ctl_arp_sent[i] = 0;
        ctl_arp_r = w - ARP_OUT_N;
        ctl_arp_overruns++;
    }
    for (; ctl_arp_r != w; ctl_arp_r++) {
        uint32_t note = eng_arp.out[ctl_arp_r & (ARP_OUT_N - 1u)][0] & 127u, vel = eng_arp.out[ctl_arp_r & (ARP_OUT_N - 1u)][1];
        uint32_t bit = 1u << (note & 31u), *m = &ctl_arp_sent[note >> 5];
        if (vel && ctl_glob[G_PARAM_TX] && ctl_glob[G_LOCAL]) {
            midi_send(0x90u | ch, note, vel);
            *m |= bit, ctl_arp_ch[note] = (uint8_t)ch;
            ctl_arp_outs++;
        } else if (!vel && (*m & bit)) {
            midi_send(0x80u | ctl_arp_ch[note], note, 0);
            *m &= ~bit;
            ctl_arp_offs++, ctl_arp_offch = ctl_arp_ch[note];
        }
    }
}
/* the receiver has the arp's note on (sent, its off not yet) on channel ch: the key that now goes out raw on ch takes it
 * over (returns 1; the arp's later note-off is then not sent: its ARP OFF reaches the engine after the main loop's pass).
 * On another channel (MIDI CH changed meanwhile) not: the key sends its own note-on, the arp's off goes on its own */
static int ctl_arp_take(uint32_t note, uint32_t ch)
{
    uint32_t bit = 1u << (note & 31u), *m = &ctl_arp_sent[(note & 127u) >> 5];
    if (!(*m & bit) || ctl_arp_ch[note & 127u] != ch) return 0;
    *m &= ~bit;
    return 1;
}
/* the keys' own notes go out unless they feed the local arp and the arp's go instead (LOCAL ON, ARP ON, PARAM TX on).
 * LOCAL OFF: the keys never reach the synth or the arp, so they go out raw as ever (a DAW controller) */
static int ctl_keys_out(void) { return !(ctl_glob[G_LOCAL] && eng_edit.v[P_ARP_ON] && ctl_glob[G_PARAM_TX]); }

/* MIDI clock in (0xF8 clock, FA start, FB continue, FC stop; any channel setting): to the clock when the global MIDI
 * CLOCK follows one (IN, SLAVE THRU, IN-no-start-stop), echoed out under SLAVE THRU; ignored under OFF and OUT */
static void ctl_midi_rt(uint32_t st)
{
    int m = ctl_glob[G_MIDI_CLOCK];
    if (!CLOCK_EXTERNAL(m)) return;
    eng_midi_rt(st);
    if (m == CLK_MIDI_THRU) midi_send(st, 0, 0);
}

void controls_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t ty = st & 0xF0u;
    int id;
    if (st >= 0xF8u) { ctl_midi_rt(st); return; }     /* realtime: the clock */
    if (st >= 0xF0u || st < 0x80u) return;              /* (other system messages: not ours) */
    if (ctl_glob[G_MIDI_CH] && (st & 15u) != ctl_glob[G_MIDI_CH] - 1u) return;
    d1 &= 127u, d2 &= 127u;
    switch (ty) {
    case 0x90u:
        if (d2) { eng_note((int)d1, (int)d2); controls_key((int)d1); }
        else eng_note((int)d1, 0);                    /* (velocity 0: a note-off) */
        controls_rec_note((int)d1, (int)d2);           /* (the sequencer's recorder, when recording) */
        break;
    case 0x80u: eng_note((int)d1, 0); controls_rec_note((int)d1, 0); break;
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
