/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX step entry (seq_rec.h: the rules). Included by app.c after synth/seq.c; tests/unit/test_seqrec.c builds it
 * with seq.c alone. */
#ifndef KNOBI_SEQ_REC_C
#define KNOBI_SEQ_REC_C
#include <string.h>
#include "seq_rec.h"

static void seqrec_full(seqrec_t *r, const seq_t *s)   /* 64 steps: recording stops */
{
    if (s->len >= SEQ_MAX_STEPS) r->on = 0, r->n = 0;
}
static int seqrec_commit(seqrec_t *r, seq_t *s)  /* the chord being entered, as a step */
{
    int rc;
    if (!r->n) return 0;
    rc = seq_rec_step(s, r->notes, r->vels, r->n);
    r->n = 0;
    seqrec_full(r, s);
    return rc < 0 ? -1 : 1;
}

void seqrec_start(seqrec_t *r, seq_t *s)
{
    memset(r, 0, sizeof *r);
    seq_clear(s);
    r->on = 1;
}
int seqrec_stop(seqrec_t *r, seq_t *s)
{
    int rc = 0;
    if (r->on && !r->in) rc = seqrec_commit(r, s) > 0;
    seqrec_abort(r);
    return rc;
}
void seqrec_abort(seqrec_t *r)
{
    r->on = 0, r->n = 0, r->in = 0;
    memset(r->held, 0, sizeof r->held);
}
int seqrec_held(const seqrec_t *r)
{
    int c = 0;
    for (unsigned i = 0; i < 4u; i++)
        for (uint32_t m = r->held[i]; m; m &= m - 1u) c++;
    return c;
}
int seqrec_note(seqrec_t *r, seq_t *s, int note, int vel)
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
    return seqrec_commit(r, s);
}
int seqrec_inc(seqrec_t *r, seq_t *s)
{
    int rc;
    if (!r->on) return 0;
    if (!r->in && !r->n) {                       /* nothing to extend: a rest */
        rc = seq_rec_rest(s);
        seqrec_full(r, s);
        return rc < 0 ? -1 : 1;
    }
    if (!r->in) {                                /* the chord held: in now, then tied */
        if (seqrec_commit(r, s) < 0) return -1;
        if (!r->on) return 1;                    /* (it was the 64th) */
        r->in = 1;
    }
    rc = seq_rec_tie(s);
    seqrec_full(r, s);
    return rc < 0 ? -1 : 1;
}
int seqrec_dec(seqrec_t *r, seq_t *s)
{
    if (!r->on) return 0;
    return seq_rec_back(s) < 0 ? -1 : 1;         /* (a chord tied in stays "in": INC ties again onto the step now last,
                                                  * refused if that is a rest or none; its release adds nothing) */
}

#endif
