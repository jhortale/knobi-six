/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX step entry (seq_rec.h: the rules). Included by app.c after synth/seq.c; tests/unit/test_seqrec.c builds it
 * with seq.c alone. */
#ifndef KNOBI_SEQ_REC_C
#define KNOBI_SEQ_REC_C
#ifdef DINGHY_HOST
#include <string.h>
#endif                                  /* (the device: memset from libc.c, through platform.c) */
#include "seq_rec.h"

static seq_t *seqrec_seq(seqrec_t *r)            /* the sequence, to change (cleared first if fresh) */
{
    seq_t *s = r->mut();
    if (r->fresh) { seq_clear(s); r->fresh = 0; }
    return s;
}
static void seqrec_full(seqrec_t *r, const seq_t *s)   /* 64 steps: recording stops */
{
    if (s->len >= SEQ_MAX_STEPS) r->on = 0, r->n = 0;
}
static int seqrec_commit(seqrec_t *r)            /* the chord being entered, as a step */
{
    seq_t *s;
    int rc;
    if (!r->n) return 0;
    s = seqrec_seq(r);
    rc = seq_rec_step(s, r->notes, r->vels, r->n);
    r->kept = r->n, r->at = s->len, r->n = 0;    /* (its notes stay in notes[]: DEC may give them back) */
    seqrec_full(r, s);
    return rc < 0 ? -1 : 1;
}

void seqrec_start(seqrec_t *r, seqrec_mut_t mut)
{
    memset(r, 0, sizeof *r);
    r->mut = mut;
    r->on = 1, r->fresh = 1;
}
int seqrec_stop(seqrec_t *r)
{
    int rc = 0;
    if (r->on && !r->in) rc = seqrec_commit(r) > 0;
    seqrec_abort(r);
    return rc;
}
void seqrec_abort(seqrec_t *r)
{
    r->on = 0, r->n = 0, r->in = 0, r->fresh = 0;
    memset(r->held, 0, sizeof r->held);
}
int seqrec_held(const seqrec_t *r)
{
    int c = 0;
    for (unsigned i = 0; i < 4u; i++)
        for (uint32_t m = r->held[i]; m; m &= m - 1u) c++;
    return c;
}
int seqrec_fresh(const seqrec_t *r) { return r->on && r->fresh; }
int seqrec_note(seqrec_t *r, int note, int vel)
{
    uint32_t *m, bit;
    if (!r->on || note < 0 || note > 127) return 0;
    m = &r->held[(uint32_t)note >> 5], bit = 1u << ((uint32_t)note & 31u);
    if (vel > 0) {
        unsigned k;
        if (r->in) r->in = 0, r->n = 0;          /* (the chord held was tied in: a new note starts the next one) */
        *m |= bit;
        if (note < SEQ_NOTE_MIN || note > SEQ_NOTE_MAX || r->n >= SEQ_MAX_NOTES) return 0;
        for (k = 0; k < r->n; k++) if (r->notes[k] == note) return 0;
        r->notes[r->n] = (uint8_t)note, r->vels[r->n] = (uint8_t)(vel > 127 ? 127 : vel);
        r->n++;
        return 0;
    }
    if (!(*m & bit)) return 0;                   /* (held since before the start) */
    *m &= ~bit;
    if (seqrec_held(r)) return 0;                /* some still held: the chord goes on */
    if (r->in) { r->in = 0; return 0; }          /* (tied in already) */
    return seqrec_commit(r);
}
int seqrec_inc(seqrec_t *r)
{
    seq_t *s;
    int rc;
    if (!r->on) return 0;
    if (!r->in && !r->n) {                       /* nothing to extend: a rest */
        s = seqrec_seq(r);
        rc = seq_rec_rest(s);
        seqrec_full(r, s);
        return rc < 0 ? -1 : 1;
    }
    if (!r->in) {                                /* the chord held: in now, then tied */
        if (seqrec_commit(r) < 0) return -1;
        if (!r->on) return 1;                    /* (it was the 64th) */
        r->in = 1;
    }
    s = seqrec_seq(r);
    rc = seq_rec_tie(s);
    seqrec_full(r, s);
    return rc < 0 ? -1 : 1;
}
int seqrec_dec(seqrec_t *r)
{
    seq_t *s;
    int rc;
    if (!r->on) return 0;
    if (r->fresh) return 0;                      /* nothing recorded yet: nothing to erase, and the old sequence stays
                                                  * (the first step, rest or tie replaces it, not a DEC) */
    s = seqrec_seq(r);
    rc = seq_rec_back(s);
    if (r->in && s->len < r->at) r->in = 0, r->n = r->kept;   /* the tied-in chord's own step erased: it is being
                                                  * entered again (still held: its release, or INC, records it anew) */
    return rc < 0 ? -1 : 1;
}

#endif
