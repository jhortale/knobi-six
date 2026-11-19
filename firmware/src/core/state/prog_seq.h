/* SPDX-License-Identifier: GPL-3.0-only */
/* Written by tools/core_state.py from firmware/src/params/prog_seq.c (a state block): do not edit. The
 * firmware's state there, per knobi_core instance: the struct, the initial values, the thread's binding (the
 * instance it runs: knobi_core.c kc_bind) and a macro per variable. Not for the device build. */
#ifndef KC_STATE_PROG_SEQ_H
#define KC_STATE_PROG_SEQ_H
struct kc_st_prog_seq {
    seq_t es_seq;
    int32_t es_prog;
    uint16_t es_gen;
    uint8_t es_fb;
    uint8_t es_dirty;
    uint8_t es_miss;
    uint32_t es_sum;
    uint32_t es_ver;
    program_t prog_wr;
};
static _Thread_local struct kc_st_prog_seq *kc_tls_prog_seq;
static void kc_st_prog_seq_init(struct kc_st_prog_seq *s)
{
    memset((void *)s, 0, sizeof *s);
    s->es_prog = -1;
}
#define es_seq (kc_tls_prog_seq->es_seq)
#define es_prog (kc_tls_prog_seq->es_prog)
#define es_gen (kc_tls_prog_seq->es_gen)
#define es_fb (kc_tls_prog_seq->es_fb)
#define es_dirty (kc_tls_prog_seq->es_dirty)
#define es_miss (kc_tls_prog_seq->es_miss)
#define es_sum (kc_tls_prog_seq->es_sum)
#define es_ver (kc_tls_prog_seq->es_ver)
#define prog_wr (kc_tls_prog_seq->prog_wr)
#endif
