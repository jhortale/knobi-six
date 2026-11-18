/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX step sequencer: the player (seq.h "The player": the behaviour and the rulings). Included by engine.c after
 * arp.c (it plays through synth_note and writes the arp's MIDI out ring, arp_out); tests/unit/test_seqplay.c builds it
 * through engine.c. Integers only; no division (the gate is a shift); no allocation, no recursion. */
#ifndef KNOBI_SEQPLAY_C
#define KNOBI_SEQPLAY_C
#include "seq.c"
#include "arp.c"

void seqp_init(seqp_t *p, const seq_t *seq, struct arp *out)
{
    uint8_t *b = (uint8_t *)p;
    for (unsigned i = 0; i < sizeof *p; i++) b[i] = 0;
    p->seq = seq, p->out = out, p->cur = SEQ_NO_STEP;
}

int seq_play(seqp_t *p, int on)
{
    int start = on && !p->on;
    p->on = (uint8_t)(on != 0);
    if (start || !on) p->pos = 0, p->cur = SEQ_NO_STEP;
    return start;
}

static void seq_out(seqp_t *p, int note, int vel) { if (p->out) arp_out(p->out, note, vel); }

static void seq_notes_off(seqp_t *p, struct synth *s)
{
    for (unsigned i = 0; i < p->n; i++) {
        synth_note(s, p->note[i], 0);
        seq_out(p, p->note[i], 0);
    }
    p->n = 0, p->gate = 0;
}

void seq_silence(seqp_t *p, struct synth *s) { seq_notes_off(p, s); }

void seq_step(seqp_t *p, struct synth *s)
{
    const seq_t *q = p->seq;
    uint32_t len = q ? q->len : 0u, next;
    const seq_step_t *st;
    if (len > SEQ_MAX_STEPS) len = SEQ_MAX_STEPS;
    if (!p->on || !len) { seq_notes_off(p, s); return; }
    if (p->pos >= len) p->pos = 0;                   /* (a sequence that got shorter: from step 1) */
    st = &q->step[p->pos];
    next = p->pos + 1u >= len ? 0u : p->pos + 1u;
    if (!(st->tie && p->n)) {                        /* a tie with notes sounding: they stay on, nothing else */
        uint32_t c = st->count > SEQ_MAX_NOTES ? SEQ_MAX_NOTES : st->count;
        seq_notes_off(p, s);
        for (uint32_t k = 0; k < c; k++) {
            int note = (int)st->notes[k] + p->xp, vel = st->vels[k] > 127u ? 127 : st->vels[k] ? st->vels[k] : 1;
            unsigned j;
            if (note < 0 || note > 127) continue;    /* (transposed out of the MIDI range: dropped) */
            for (j = 0; j < p->n && p->note[j] != note; j++) {}
            if (j < p->n) continue;                  /* (twice in the step: once) */
            p->note[p->n++] = (uint8_t)note;
            synth_note(s, note, vel);
            seq_out(p, note, vel);
        }
    }
    p->cur = p->pos;
    p->pos = (uint8_t)next;
    p->gate = p->n && !q->step[next].tie ? p->step_len >> 1 : 0u;   /* 50 %; a tie next: until the next step */
    p->steps++;
}

void seq_run(seqp_t *p, struct synth *s, uint32_t frames)
{
    if (!p->n || !p->gate) return;
    if (frames >= p->gate) seq_notes_off(p, s);
    else p->gate -= frames;
}

uint32_t seq_until(const seqp_t *p) { return p->n && p->gate ? p->gate : SEQ_NONE; }

void seq_transpose(seqp_t *p, int semis) { p->xp = (int8_t)(semis < -127 ? -127 : semis > 127 ? 127 : semis); }

void seq_ext(seqp_t *p, uint8_t status)
{
    if (status == 0xFAu) p->pos = 0, p->cur = SEQ_NO_STEP;   /* Start: from step 1 */
    /* 0xFB Continue: on from the held step; 0xFC Stop: the clock stops, engine.c silences (the position kept) */
}
#endif
