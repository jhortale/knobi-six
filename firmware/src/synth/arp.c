/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX arpeggiator (arp.h: the behaviour and the rulings). Included by engine.c (after voices.c: it plays through
 * synth_note); also built alone by tests/unit/test_arp.c. Integers only; the one division is 32-bit (RANDOM's index). */
#ifndef KNOBI_ARP_C
#define KNOBI_ARP_C
#include "arp.h"

#define ARP_BARRIER() __asm__ volatile("" ::: "memory")

void arp_init(arp_t *a, uint32_t seed)
{
    uint8_t *b = (uint8_t *)a;
    for (unsigned i = 0; i < sizeof *a; i++) b[i] = 0;
    a->note = -1;
    a->rnd = seed ? seed : 1u;
}

static uint32_t arp_rand(arp_t *a)
{
    uint32_t x = a->rnd;
    x ^= x << 13, x ^= x >> 17, x ^= x << 5;
    return a->rnd = x;
}

static int arp_holding(const arp_t *a) { return a->hold_ext || a->hold_prog; }

static void arp_out(arp_t *a, int note, int vel)   /* (MIDI out: the entry, then the index) */
{
    uint32_t w = a->out_w;
    a->out[w & (ARP_OUT_N - 1u)][0] = (uint8_t)note;
    a->out[w & (ARP_OUT_N - 1u)][1] = (uint8_t)vel;
    ARP_BARRIER();
    a->out_w = w + 1u;
}

static void arp_note_off(arp_t *a, synth_t *s)
{
    if (a->note < 0) return;
    synth_note(s, a->note, 0);
    arp_out(a, a->note, 0);
    a->note = -1, a->gate = 0;
}

static void arp_remove(arp_t *a, unsigned i)
{
    for (; i + 1u < a->n; i++) a->key[i] = a->key[i + 1u], a->vel[i] = a->vel[i + 1u], a->down[i] = a->down[i + 1u];
    a->n--;
}

static void arp_drop_up(arp_t *a)                   /* HOLD gone: the latched keys leave */
{
    for (unsigned i = a->n; i-- > 0;) if (!a->down[i]) arp_remove(a, i);
    if (!a->n) a->pos = 0;
}

int arp_key(arp_t *a, int note, int vel)
{
    unsigned i, held = 0;
    int start = 0;
    if (note < 0 || note > 127) return 0;
    for (i = 0; i < a->n && a->key[i] != note; i++) {}
    if (vel <= 0) {
        if (i == a->n) return 0;
        if (arp_holding(a)) a->down[i] = 0;          /* latched */
        else arp_remove(a, i);
        if (!a->n) a->pos = 0;
        return 0;
    }
    for (unsigned k = 0; k < a->n; k++) held += a->down[k];
    if (a->n && !held && arp_holding(a)) a->n = 0, i = 0;   /* relatch: the new chord replaces the latched one */
    if (!a->n) start = 1, a->pos = 0;
    if (i < a->n) { a->vel[i] = (uint8_t)vel, a->down[i] = 1; return start; }   /* (a latched key pressed again) */
    if (a->n == ARP_KEYS) arp_remove(a, 0);          /* full: the oldest goes */
    a->key[a->n] = (uint8_t)note, a->vel[a->n] = (uint8_t)(vel > 127 ? 127 : vel), a->down[a->n] = 1;
    a->n++;
    return start;
}

/* the cycle: UP's (sorted, octaves up), DOWN (reversed), UP+DN (up, then down without the ends), ASSIGN (played order,
 * octaves up); RANDOM: UP's (arp_step picks in it) */
uint32_t arp_cycle(const arp_t *a, uint8_t *notes, uint8_t *vels)
{
    uint8_t bk[ARP_KEYS], bv[ARP_KEYS];
    uint32_t n = a->n, m = 0, i, j;
    for (i = 0; i < n; i++) {                        /* the base: played order, or sorted (insertion) */
        uint8_t k = a->key[i], v = a->vel[i];
        j = i;
        if (a->mode != ARP_ASSIGN)
            while (j > 0 && bk[j - 1u] > k) bk[j] = bk[j - 1u], bv[j] = bv[j - 1u], j--;
        bk[j] = k, bv[j] = v;
    }
    for (uint32_t o = 0; o <= a->range && o < 3u; o++)
        for (i = 0; i < n; i++)
            if (bk[i] + 12u * o <= 127u) notes[m] = (uint8_t)(bk[i] + 12u * o), vels[m] = bv[i], m++;
    if (a->mode == ARP_DOWN)
        for (i = 0; i < m / 2u; i++) {
            uint8_t t = notes[i]; notes[i] = notes[m - 1u - i], notes[m - 1u - i] = t;
            t = vels[i]; vels[i] = vels[m - 1u - i], vels[m - 1u - i] = t;
        }
    else if (a->mode == ARP_UPDOWN && m > 2u)
        for (i = m - 2u, j = m; i > 0; i--, j++) notes[j] = notes[i], vels[j] = vels[i];   /* back down, no ends */
    if (a->mode == ARP_UPDOWN && m > 2u) m = 2u * m - 2u;
    return m;
}

void arp_step(arp_t *a, synth_t *s)
{
    uint8_t notes[ARP_SEQ], vels[ARP_SEQ];
    uint32_t len, i;
    arp_note_off(a, s);
    if (!a->on || !a->n) { a->pos = 0; return; }
    len = arp_cycle(a, notes, vels);
    if (!len) return;                                /* (every note over 127) */
    if (a->mode == ARP_RANDOM) i = arp_rand(a) % len;
    else {
        if (a->pos >= len) a->pos = 0;
        i = a->pos++;
    }
    a->note = notes[i];
    synth_note(s, notes[i], vels[i]);
    arp_out(a, notes[i], vels[i]);
    a->gate = a->step_len / 2u;                      /* 50 % of the step (0: unknown, until the next step) */
    a->steps++;
}

void arp_run(arp_t *a, synth_t *s, uint32_t frames)
{
    if (a->note < 0 || !a->gate) return;
    if (frames >= a->gate) arp_note_off(a, s);
    else a->gate -= frames;
}

uint32_t arp_until(const arp_t *a) { return a->note >= 0 && a->gate ? a->gate : ARP_NONE; }

void arp_silence(arp_t *a, synth_t *s)
{
    arp_note_off(a, s);
    a->pos = 0;
}

void arp_clear(arp_t *a, synth_t *s)
{
    arp_silence(a, s);
    a->n = 0;
}

void arp_hold(arp_t *a, int on)
{
    int was = arp_holding(a);
    a->hold_ext = (uint8_t)(on != 0);
    if (was && !arp_holding(a)) arp_drop_up(a);
}

/* ARP ON: the keys the synth has held (down, in press order) become the pattern and stop sounding; the synth's HOLD
 * off (the arp latches). ARP OFF: the arp's note off, the keys still held handed to the synth (they sound as played),
 * the synth's HOLD back to the panel's */
static int arp_turn(arp_t *a, synth_t *s, int on)
{
    int start = 0;
    if (on == a->on) return 0;
    if (on) {
        a->n = 0, a->pos = 0, a->on = 1;
        for (unsigned i = 0; i < s->nstack; i++)
            if (s->sdown[i]) start |= arp_key(a, s->stack[i], s->svel[i]);
        synth_hold(s, 0);
        synth_all_notes_off(s);
        return start;
    }
    arp_note_off(a, s);
    a->on = 0;
    synth_hold(s, a->hold_ext);
    for (unsigned i = 0; i < a->n; i++)
        if (a->down[i]) synth_note(s, a->key[i], a->vel[i]);
    a->n = 0, a->pos = 0;
    return 0;
}

int arp_set(arp_t *a, synth_t *s, uint16_t id, int v)
{
    switch (id) {
    case P_ARP_ON: return arp_turn(a, s, v != 0);
    case P_ARP_MODE: a->mode = (uint8_t)(v < 0 ? 0 : v >= ARP_MODES ? ARP_MODES - 1 : v); break;
    case P_ARP_RANGE: a->range = (uint8_t)(v < 0 ? 0 : v > 2 ? 2 : v); break;
    case P_ARP_HOLD: {
        int was = arp_holding(a);
        a->hold_prog = (uint8_t)(v != 0);
        if (was && !arp_holding(a)) arp_drop_up(a);
        break;
    }
    }
    return 0;
}

/* after synth_program (which has released every voice and forgotten its keys): the arp forgets its keys too (as the
 * synth does), takes the program's values; the synth's HOLD as the arp being on wants it */
void arp_program(arp_t *a, synth_t *s, const int16_t *v)
{
    arp_clear(a, s);
    a->on = (uint8_t)(v[P_ARP_ON] != 0);
    arp_set(a, s, P_ARP_MODE, v[P_ARP_MODE]);
    arp_set(a, s, P_ARP_RANGE, v[P_ARP_RANGE]);
    a->hold_prog = (uint8_t)(v[P_ARP_HOLD] != 0);
    synth_hold(s, a->on ? 0 : a->hold_ext);
}
#endif
