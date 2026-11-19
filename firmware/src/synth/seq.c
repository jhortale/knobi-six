/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX step sequencer model (seq.h: the semantics and the packed encoding). Included by app.c (unity build);
 * also built alone by tests/unit/test_seq.c. Integers only, no allocation, no recursion. */
#ifndef KNOBI_SEQ_C
#define KNOBI_SEQ_C
#ifdef DINGHY_HOST
#include <string.h>
#endif                                  /* (the device: memset / memcpy from libc.c, through platform.c) */
#include "seq.h"

void seq_clear(seq_t *s)
{
    memset(s, 0, sizeof *s);
}

static void seq_put(seq_t *s, const seq_step_t *st)
{
    s->step[s->len] = *st;
    s->len++;
}

int seq_rec_step(seq_t *s, const uint8_t *notes, const uint8_t *vels, int n)
{
    if (s->len >= SEQ_MAX_STEPS || !notes || !vels || n < 1) return -1;
    seq_step_t st;
    memset(&st, 0, sizeof st);
    for (int i = 0; i < n && st.count < SEQ_MAX_NOTES; i++) {
        if (notes[i] < SEQ_NOTE_MIN || notes[i] > SEQ_NOTE_MAX) continue;       /* dropped before counting */
        st.notes[st.count] = notes[i];
        st.vels[st.count] = vels[i] > 127 ? 127 : vels[i];
        st.count++;
    }
    if (!st.count) return -1;
    seq_put(s, &st);
    return s->len;
}

int seq_rec_rest(seq_t *s)
{
    if (s->len >= SEQ_MAX_STEPS) return -1;
    seq_step_t st;
    memset(&st, 0, sizeof st);
    seq_put(s, &st);
    return s->len;
}

int seq_rec_tie(seq_t *s)
{
    if (s->len >= SEQ_MAX_STEPS || s->len == 0 || s->step[s->len - 1].count == 0) return -1;
    seq_step_t st = s->step[s->len - 1];
    st.tie = 1;
    seq_put(s, &st);
    return s->len;
}

int seq_rec_back(seq_t *s)
{
    if (s->len == 0) return -1;
    s->len--;
    memset(&s->step[s->len], 0, sizeof s->step[0]);
    return s->len;
}

uint32_t seq_packed_size(const seq_t *s)
{
    uint32_t n = 1;
    for (uint32_t i = 0; i < s->len && i < SEQ_MAX_STEPS; i++) n += 1u + 2u * s->step[i].count;
    return n;
}

int seq_pack(const seq_t *s, uint8_t *buf, uint32_t max)
{
    if (s->len > SEQ_MAX_STEPS) return -1;
    for (uint32_t i = 0; i < s->len; i++) if (s->step[i].count > SEQ_MAX_NOTES) return -1;
    uint32_t need = seq_packed_size(s);
    if (!buf || max < need) return -1;
    uint32_t o = 0;
    buf[o++] = s->len;
    for (uint32_t i = 0; i < s->len; i++) {
        const seq_step_t *st = &s->step[i];
        buf[o++] = (uint8_t)(st->count | (st->tie ? 8u : 0u));
        for (uint32_t k = 0; k < st->count; k++) {
            buf[o++] = st->notes[k];
            buf[o++] = st->vels[k];
        }
    }
    return (int)o;
}

int seq_unpack(seq_t *s, const uint8_t *buf, uint32_t n)
{
    seq_clear(s);
    if (!buf || n < 1 || buf[0] > SEQ_MAX_STEPS) return -1;
    uint32_t len = buf[0], o = 1;
    for (uint32_t i = 0; i < len; i++) {
        if (o >= n) goto bad;
        uint8_t h = buf[o++];
        uint32_t c = h & 7u, tie = (h >> 3) & 1u;
        if ((h & 0xF0u) || c > SEQ_MAX_NOTES) goto bad;
        if (tie && (c == 0 || i == 0 || s->step[i - 1].count == 0)) goto bad;
        if (n - o < 2u * c) goto bad;
        seq_step_t *st = &s->step[i];
        st->count = (uint8_t)c;
        st->tie = (uint8_t)tie;
        for (uint32_t k = 0; k < c; k++) {
            uint8_t note = buf[o++], vel = buf[o++];
            if (note < SEQ_NOTE_MIN || note > SEQ_NOTE_MAX || vel > 127) goto bad;
            st->notes[k] = note;
            st->vels[k] = vel;
        }
    }
    s->len = (uint8_t)len;
    return 0;
bad:
    seq_clear(s);
    return -1;
}
#endif
