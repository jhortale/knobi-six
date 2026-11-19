/* SPDX-License-Identifier: GPL-3.0-only */
/* Written by tools/core_state.py from firmware/src/ui/midi_map.c (a state block): do not edit. The
 * firmware's state there, per knobi_core instance: the struct, the initial values, its slot and a macro per
 * variable (KC_ST: the block of the instance this thread runs, knobi_core.c kc_bind). Not for the device build. */
#ifndef KC_STATE_MIDI_CC_H
#define KC_STATE_MIDI_CC_H
struct kc_st_midi_cc {
    uint8_t MIDI_CC[P_COUNT_PHASE1];
    uint8_t midi_cc_id[128];
};
#define KC_IX_midi_cc 5
static void kc_st_midi_cc_init(struct kc_st_midi_cc *s)
{
    memset((void *)s, 0, sizeof *s);
}
#define MIDI_CC (KC_ST(midi_cc)->MIDI_CC)
#define midi_cc_id (KC_ST(midi_cc)->midi_cc_id)
#endif
