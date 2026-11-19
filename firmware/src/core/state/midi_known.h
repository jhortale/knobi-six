/* SPDX-License-Identifier: GPL-3.0-only */
/* Written by tools/core_state.py from firmware/src/ui/midi_map.c (a state block): do not edit. The
 * firmware's state there, per knobi_core instance: the struct, the initial values, the thread's binding (the
 * instance it runs: knobi_core.c kc_bind) and a macro per variable. Not for the device build. */
#ifndef KC_STATE_MIDI_KNOWN_H
#define KC_STATE_MIDI_KNOWN_H
struct kc_st_midi_known {
    int16_t midi_cc_known[128];
    int16_t midi_cc_sent[128];
};
static _Thread_local struct kc_st_midi_known *kc_tls_midi_known;
static void kc_st_midi_known_init(struct kc_st_midi_known *s)
{
    memset((void *)s, 0, sizeof *s);
}
#define midi_cc_known (kc_tls_midi_known->midi_cc_known)
#define midi_cc_sent (kc_tls_midi_known->midi_cc_sent)
#endif
