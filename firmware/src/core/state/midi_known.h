/* SPDX-License-Identifier: GPL-3.0-only */
/* Written by tools/core_state.py from firmware/src/ui/midi_map.c (a state block): do not edit. The
 * firmware's state there, per knobi_core instance: the struct, the initial values, its slot and a macro per
 * variable (KC_ST: the block of the instance this thread runs, knobi_core.c kc_bind). Not for the device build. */
#ifndef KC_STATE_MIDI_KNOWN_H
#define KC_STATE_MIDI_KNOWN_H
struct kc_st_midi_known {
    int16_t midi_cc_known[128];
    int16_t midi_cc_sent[128];
};
#define KC_IX_midi_known 6
static void kc_st_midi_known_init(struct kc_st_midi_known *s)
{
    memset((void *)s, 0, sizeof *s);
}
#define midi_cc_known (KC_ST(midi_known)->midi_cc_known)
#define midi_cc_sent (KC_ST(midi_known)->midi_cc_sent)
#endif
