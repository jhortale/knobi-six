/* SPDX-License-Identifier: GPL-3.0-only */
/* Written by tools/core_state.py from firmware/src/ui/midi_map.c (a state block): do not edit. The
 * firmware's state there, per knobi_core instance: the struct, the initial values, the thread's binding (the
 * instance it runs: knobi_core.c kc_bind) and a macro per variable. Not for the device build. */
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
static _Thread_local struct kc_st_midi_arp *kc_tls_midi_arp;
static void kc_st_midi_arp_init(struct kc_st_midi_arp *s)
{
    memset((void *)s, 0, sizeof *s);
}
#define ctl_arp_r (kc_tls_midi_arp->ctl_arp_r)
#define ctl_arp_sent (kc_tls_midi_arp->ctl_arp_sent)
#define ctl_arp_ch (kc_tls_midi_arp->ctl_arp_ch)
#define ctl_arp_outs (kc_tls_midi_arp->ctl_arp_outs)
#define ctl_arp_offs (kc_tls_midi_arp->ctl_arp_offs)
#define ctl_arp_offch (kc_tls_midi_arp->ctl_arp_offch)
#define ctl_arp_overruns (kc_tls_midi_arp->ctl_arp_overruns)
#endif
