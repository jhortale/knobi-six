/* SPDX-License-Identifier: GPL-3.0-only */
/* Written by tools/core_state.py from firmware/src/ui/midi_map.c (a state block): do not edit. The
 * firmware's state there, per knobi_core instance: the struct, the initial values, the thread's binding (the
 * instance it runs: knobi_core.c kc_bind) and a macro per variable. Not for the device build. */
#ifndef KC_STATE_MIDI_CC_H
#define KC_STATE_MIDI_CC_H
struct kc_st_midi_cc {
    uint8_t MIDI_CC[P_COUNT_PHASE1];
    uint8_t midi_cc_id[128];
};
static _Thread_local struct kc_st_midi_cc *kc_tls_midi_cc;
static void kc_st_midi_cc_init(struct kc_st_midi_cc *s)
{
    memset((void *)s, 0, sizeof *s);
}
#define MIDI_CC (kc_tls_midi_cc->MIDI_CC)
#define midi_cc_id (kc_tls_midi_cc->midi_cc_id)
#endif
