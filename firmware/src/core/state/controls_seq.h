/* SPDX-License-Identifier: GPL-3.0-only */
/* Written by tools/core_state.py from firmware/src/ui/controls.c (a state block): do not edit. The
 * firmware's state there, per knobi_core instance: the struct, the initial values, its slot and a macro per
 * variable (KC_ST: the block of the instance this thread runs, knobi_core.c kc_bind). Not for the device build. */
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
#define KC_IX_controls_seq 4
static void kc_st_controls_seq_init(struct kc_st_controls_seq *s)
{
    memset((void *)s, 0, sizeof *s);
    s->ctl_seq_want = -1;
}
#define ctl_rec (KC_ST(controls_seq)->ctl_rec)
#define ctl_seq_want (KC_ST(controls_seq)->ctl_seq_want)
#define ctl_rec_arm (KC_ST(controls_seq)->ctl_rec_arm)
#define ctl_rec_xhold (KC_ST(controls_seq)->ctl_rec_xhold)
#define ctl_rec_xused (KC_ST(controls_seq)->ctl_rec_xused)
#define ctl_xp_want (KC_ST(controls_seq)->ctl_xp_want)
#define ctl_xp_pend (KC_ST(controls_seq)->ctl_xp_pend)
#endif
