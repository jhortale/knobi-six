/* SPDX-License-Identifier: GPL-3.0-only */
/* Written by tools/core_state.py from firmware/src/ui/controls.c (a state block): do not edit. The
 * firmware's state there, per knobi_core instance: the struct, the initial values, the thread's binding (the
 * instance it runs: knobi_core.c kc_bind) and a macro per variable. Not for the device build. */
#ifndef KC_STATE_CONTROLS_SEQ_H
#define KC_STATE_CONTROLS_SEQ_H
struct kc_st_controls_seq {
    seqrec_t ctl_rec;
    int8_t ctl_seq_want;
    uint8_t ctl_rec_arm;
    uint8_t ctl_rec_xhold;
    uint8_t ctl_rec_xused;
    int32_t ctl_xp_want;
    uint8_t ctl_xp_pend;
};
static _Thread_local struct kc_st_controls_seq *kc_tls_controls_seq;
static void kc_st_controls_seq_init(struct kc_st_controls_seq *s)
{
    memset((void *)s, 0, sizeof *s);
    s->ctl_seq_want = -1;
}
#define ctl_rec (kc_tls_controls_seq->ctl_rec)
#define ctl_seq_want (kc_tls_controls_seq->ctl_seq_want)
#define ctl_rec_arm (kc_tls_controls_seq->ctl_rec_arm)
#define ctl_rec_xhold (kc_tls_controls_seq->ctl_rec_xhold)
#define ctl_rec_xused (kc_tls_controls_seq->ctl_rec_xused)
#define ctl_xp_want (kc_tls_controls_seq->ctl_xp_want)
#define ctl_xp_pend (kc_tls_controls_seq->ctl_xp_pend)
#endif
