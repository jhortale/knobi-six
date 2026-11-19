/* SPDX-License-Identifier: GPL-3.0-only */
/* Written by tools/core_state.py from firmware/src/ui/midi_map.c (a state block): do not edit. The
 * firmware's state there, per knobi_core instance: the struct, the initial values, its slot and a macro per
 * variable (KC_ST: the block of the instance this thread runs, knobi_core.c kc_bind). Not for the device build. */
#ifndef KC_STATE_MIDI_ARP_H
#define KC_STATE_MIDI_ARP_H
struct kc_st_midi_arp {
    uint32_t ctl_arp_r;
    uint32_t ctl_arp_sent[4];
    uint8_t ctl_arp_ch[128];
    uint32_t ctl_arp_outs;
    uint32_t ctl_arp_offs;
    uint8_t ctl_arp_offch;
    uint32_t ctl_arp_overruns;
};
#define KC_IX_midi_arp 7
static void kc_st_midi_arp_init(struct kc_st_midi_arp *s)
{
    memset((void *)s, 0, sizeof *s);
}
#define ctl_arp_r (KC_ST(midi_arp)->ctl_arp_r)
#define ctl_arp_sent (KC_ST(midi_arp)->ctl_arp_sent)
#define ctl_arp_ch (KC_ST(midi_arp)->ctl_arp_ch)
#define ctl_arp_outs (KC_ST(midi_arp)->ctl_arp_outs)
#define ctl_arp_offs (KC_ST(midi_arp)->ctl_arp_offs)
#define ctl_arp_offch (KC_ST(midi_arp)->ctl_arp_offch)
#define ctl_arp_overruns (KC_ST(midi_arp)->ctl_arp_overruns)
#endif
