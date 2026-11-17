/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX engine: the one synth (voices.h) between the main loop and the audio interrupt. synth_* is not
 * thread-safe, so the main loop never calls it while audio may run: it calls eng_* (producer), which queue events;
 * app_audio calls eng_render (consumer), which applies every queued event, in order, then renders the block.
 * Included by app.c after params.c and the synth/ modules (unity build), and by tests/unit/test_render.c, the same way.
 *
 * The queue: a single-producer / single-consumer ring of ENG_QLEN (256, a power of two) 4-byte events, with free
 * running indices eng_qw (written only by the producer) and eng_qr (written only by the consumer); the producer
 * stores the event, then (compiler barrier) advances eng_qw; the consumer reads eng_qw once, applies up to it, then
 * advances eng_qr. One core: an interrupt sees either the old index or the new one, never half an event. Events:
 * note on/off, param edit, bend, mod wheel, pressure, HOLD, panic, program load, chord memory slot, all notes off
 * (released, HOLD unchanged), MIDI volume.
 * Overflow policy: the consumer empties the queue at every block (5.8 ms), so it fills only if more than 256 events
 * arrive within one block (37 keys pressed and released together are 74). Nothing that would leave the synth wrong is
 * ever lost:
 *  - the last ENG_RESERVE (64) slots take only note-offs, HOLD and PANIC: a note-on (or a bend, wheel, pressure, program
 *    or param event) that would eat into them is refused, counted in eng_drops, and eng_note returns -1 (a refused
 *    note-on just does not play);
 *  - a note-off that finds even the reserve full is kept in eng_off_pend (a bit per note) and eng_flush, which the main
 *    loop calls on every pass, queues it as soon as there is room; a note-on of a note whose off is still pending
 *    queues the off first (or is refused), so the order is kept. eng_note returns 1 for such a deferred off;
 *  - a param edit that does not fit is kept: eng_edit has the new value at once (the screens show it), the id is
 *    marked in eng_par_pend, and eng_flush queues the id's latest value when there is room (several edits meanwhile
 *    send one event). eng_param returns 1 then. A program load supersedes pending edits;
 *  - PANIC and ALL NOTES OFF that do not fit are kept (eng_panic_pend, eng_alloff_pend) and flushed first, and a
 *    note-on queues them before itself (or is refused), so a later note is never cut by an earlier PANIC; chord memory (eng_chord) always goes through
 *    eng_flush (six EV_CHORD events, all or none);
 *  - bend, wheel, pressure, MIDI volume and HOLD are levels: refused, they are not marked sent (eng_last_*), so the caller's next
 *    call (the main loop sends them every pass) tries again.
 * Bend, wheel and pressure are queued only when their value changes, param edits only when the value changes
 * (eng_edit holds the values sent), so a fader or wheel cannot flood it.
 * Program loads: a 220-byte program_t does not go through the ring; eng_program copies it into one buffer (eng_pbuf)
 * and queues a PROGRAM event that points at it (in order with the other events). The buffer is the producer's only
 * while no load is pending (eng_pw == eng_pr: eng_pw counts loads queued, written by the producer; eng_pr counts loads
 * applied, written by the consumer after synth_program has copied the buffer). A second load while one is pending
 * returns -1 and the caller tries again on its next pass (the main loop's work is never held up waiting for audio).
 *
 * Main-loop side: eng_edit is the producer's copy of the program in play (what the queue will have applied once it
 * drains): screens and probes read it, never the synth. eng_voices: voices sounding after the last block (consumer).
 * The audio side does nothing until eng_init has set the synth up (eng_ready), so app_audio may start first. */
#ifndef KNOBI_ENGINE_C
#define KNOBI_ENGINE_C
#include <stdint.h>

#define ENG_QLEN 256u                     /* events; a power of two */
#define ENG_RESERVE 64u                   /* the last slots: note-offs, HOLD and PANIC only */
#define ENG_BARRIER() __asm__ volatile("" ::: "memory")   /* a compiler barrier (as platform.c RING_PUBLISH) */

enum { EV_NOTE, EV_PARAM, EV_BEND, EV_WHEEL, EV_PRESSURE, EV_HOLD, EV_PANIC, EV_PROGRAM, EV_CHORD, EV_ALLOFF, EV_VOLUME };
typedef struct { uint8_t type, a; int16_t b; } eng_ev_t;   /* NOTE a note b vel; PARAM a id b value; BEND b 0..16383;
                                                              CHORD a slot 0..5 b note (0 = empty) */

static synth_t eng_synth;                 /* the consumer's (and eng_init's, before eng_ready) */
static eng_ev_t eng_q[ENG_QLEN];
static volatile uint32_t eng_qw, eng_qr, eng_drops;
static program_t eng_pbuf, eng_edit;
static volatile uint32_t eng_pw, eng_pr;
static volatile uint8_t eng_ready, eng_voices;
static int32_t eng_last_bend = 8192, eng_last_wheel, eng_last_press, eng_last_hold, eng_last_vol = 127;

static uint32_t eng_off_pend[4];          /* note-offs waiting for room: bit n = note n (producer only) */
static uint32_t eng_par_pend[(P_COUNT_PHASE1 + 31u) / 32u];   /* param ids whose latest eng_edit value is not queued */
static uint8_t eng_panic_pend, eng_alloff_pend, eng_chord_pend;

/* keep: the free slots that must remain after this event (0 for note-offs, HOLD, PANIC; ENG_RESERVE for the rest) */
static int eng_push(uint32_t type, uint32_t a, int32_t b, uint32_t keep)
{
    uint32_t w = eng_qw;
    if (w - eng_qr + keep >= ENG_QLEN) return -1;
    eng_q[w & (ENG_QLEN - 1u)].type = (uint8_t)type;
    eng_q[w & (ENG_QLEN - 1u)].a = (uint8_t)a;
    eng_q[w & (ENG_QLEN - 1u)].b = (int16_t)b;
    ENG_BARRIER();
    eng_qw = w + 1u;
    return 0;
}

/* ---- producer (main loop) */
static void eng_init(void)                /* the init program; call before audio needs it (or again: a full reset) */
{
    eng_ready = 0;
    ENG_BARRIER();
    synth_init(&eng_synth);
    program_init(&eng_edit);
    synth_program(&eng_synth, &eng_edit);
    eng_qw = eng_qr = eng_drops = 0;
    eng_pw = eng_pr = 0;
    for (unsigned i = 0; i < 4u; i++) eng_off_pend[i] = 0;
    for (unsigned i = 0; i < sizeof eng_par_pend / sizeof eng_par_pend[0]; i++) eng_par_pend[i] = 0;
    eng_panic_pend = eng_alloff_pend = eng_chord_pend = 0;
    eng_voices = 0;
    eng_last_bend = 8192, eng_last_wheel = eng_last_press = eng_last_hold = 0, eng_last_vol = 127;
    ENG_BARRIER();
    eng_ready = 1;
}
#define ENG_BIT(m, i) ((m)[(i) >> 5] >> ((i) & 31u) & 1u)
#define ENG_SET(m, i) ((m)[(i) >> 5] |= 1u << ((i) & 31u))
#define ENG_CLR(m, i) ((m)[(i) >> 5] &= ~(1u << ((i) & 31u)))
static int eng_note(int note, int vel)    /* vel 0 = off. 0 queued, 1 an off kept for eng_flush, -1 a note-on refused */
{
    uint32_t n = (uint32_t)note;
    if (note < 0 || note > 127) return -1;
    if (vel <= 0) {
        if (eng_push(EV_NOTE, n, 0, 0)) { ENG_SET(eng_off_pend, n); return 1; }
        ENG_CLR(eng_off_pend, n);
        return 0;
    }
    if (eng_panic_pend) {                 /* a kept PANIC / ALL NOTES OFF goes before this note, not after it */
        if (eng_push(EV_PANIC, 0, 0, 0)) { eng_drops = eng_drops + 1u; return -1; }
        eng_panic_pend = 0;
    }
    if (eng_alloff_pend) {
        if (eng_push(EV_ALLOFF, 0, 0, 0)) { eng_drops = eng_drops + 1u; return -1; }
        eng_alloff_pend = 0;
    }
    if (ENG_BIT(eng_off_pend, n)) {       /* its off first */
        if (eng_push(EV_NOTE, n, 0, 0)) { eng_drops = eng_drops + 1u; return -1; }
        ENG_CLR(eng_off_pend, n);
    }
    if (eng_push(EV_NOTE, n, vel > 127 ? 127 : vel, ENG_RESERVE)) { eng_drops = eng_drops + 1u; return -1; }
    return 0;
}
static int eng_param(uint32_t id, int32_t v)   /* 0 queued (or unchanged), 1 kept for eng_flush, -1 not a param */
{
    int16_t c;
    if (id >= P_COUNT_PHASE1) return -1;
    c = param_clamp((uint16_t)id, v);
    if (eng_edit.v[id] == c && !ENG_BIT(eng_par_pend, id)) return 0;
    eng_edit.v[id] = c;
    if (eng_push(EV_PARAM, id, c, ENG_RESERVE)) { ENG_SET(eng_par_pend, id); return 1; }
    ENG_CLR(eng_par_pend, id);
    return 0;
}
static int eng_level(uint32_t type, int32_t *last, int32_t v, uint32_t keep)
{
    if (v == *last) return 0;
    if (eng_push(type, 0, v, keep)) { eng_drops = eng_drops + 1u; return -1; }
    *last = v;
    return 0;
}
static int eng_bend(int32_t bend14) { return eng_level(EV_BEND, &eng_last_bend, bend14 < 0 ? 0 : bend14 > 16383 ? 16383 : bend14, ENG_RESERVE); }
static int eng_wheel(int32_t v) { return eng_level(EV_WHEEL, &eng_last_wheel, v < 0 ? 0 : v > 127 ? 127 : v, ENG_RESERVE); }
static int eng_pressure(int32_t v) { return eng_level(EV_PRESSURE, &eng_last_press, v < 0 ? 0 : v > 127 ? 127 : v, ENG_RESERVE); }
static int eng_volume(int32_t v) { return eng_level(EV_VOLUME, &eng_last_vol, v < 0 ? 0 : v > 127 ? 127 : v, ENG_RESERVE); }
static int eng_hold(int on) { return eng_level(EV_HOLD, &eng_last_hold, on != 0, 0); }
static int eng_panic(void)                /* 0 queued, 1 kept for eng_flush */
{
    eng_last_hold = 0;                    /* (panic clears HOLD) */
    if (eng_push(EV_PANIC, 0, 0, 0)) { eng_panic_pend = 1; return 1; }
    eng_panic_pend = 0;
    return 0;
}
static int eng_all_notes_off(void)       /* every note released (MIDI CC 123); HOLD stays. 0 queued, 1 kept */
{
    if (eng_push(EV_ALLOFF, 0, 0, 0)) { eng_alloff_pend = 1; return 1; }
    eng_alloff_pend = 0;
    return 0;
}
static void eng_chord(const uint8_t *c)   /* chord memory: 6 absolute MIDI notes, 0 = empty; queued by eng_flush */
{
    for (unsigned i = 0; i < 6u; i++) eng_edit.chord[i] = c[i] > 127u ? 0 : c[i];
    eng_chord_pend = 1;
}
static int eng_flush(void)                /* the main loop, every pass: what was kept, in order; returns what is left */
{
    int left = 0;
    unsigned i;
    if (eng_panic_pend && eng_push(EV_PANIC, 0, 0, 0) == 0) eng_panic_pend = 0;
    left += eng_panic_pend;
    if (eng_alloff_pend && eng_push(EV_ALLOFF, 0, 0, 0) == 0) eng_alloff_pend = 0;
    left += eng_alloff_pend;
    for (i = 0; i < 128u; i++)
        if (ENG_BIT(eng_off_pend, i)) {
            if (eng_push(EV_NOTE, i, 0, 0)) left++;
            else ENG_CLR(eng_off_pend, i);
        }
    for (i = 0; i < P_COUNT_PHASE1; i++)
        if (ENG_BIT(eng_par_pend, i)) {
            if (eng_push(EV_PARAM, i, eng_edit.v[i], ENG_RESERVE)) left++;
            else ENG_CLR(eng_par_pend, i);
        }
    if (eng_chord_pend) {
        if (eng_qw - eng_qr + ENG_RESERVE + 6u > ENG_QLEN) left++;
        else {
            for (i = 0; i < 6u; i++) eng_push(EV_CHORD, i, eng_edit.chord[i], 0);
            eng_chord_pend = 0;
        }
    }
    return left;
}
static int eng_program(const program_t *p)   /* -1: a load is still pending (or the queue is full): try again later */
{
    if (eng_pw != eng_pr) return -1;
    eng_pbuf = *p;
    ENG_BARRIER();
    eng_pw = eng_pw + 1u;
    if (eng_push(EV_PROGRAM, 0, 0, ENG_RESERVE)) { eng_pw = eng_pw - 1u; return -1; }
    eng_edit = *p;
    for (unsigned i = 0; i < P_COUNT_PHASE1; i++) eng_edit.v[i] = param_clamp((uint16_t)i, p->v[i]);
    for (unsigned i = 0; i < sizeof eng_par_pend / sizeof eng_par_pend[0]; i++) eng_par_pend[i] = 0;   /* its values win */
    eng_chord_pend = 0;
    return 0;
}
static int eng_active(void) { return eng_voices; }   /* voices sounding after the last block */

/* ---- consumer (audio interrupt) */
static void eng_drain(void)
{
    uint32_t r = eng_qr, w = eng_qw;
    ENG_BARRIER();
    for (; r != w; r++) {
        const eng_ev_t *e = &eng_q[r & (ENG_QLEN - 1u)];
        switch (e->type) {
        case EV_NOTE: synth_note(&eng_synth, e->a, e->b); break;
        case EV_PARAM: synth_param(&eng_synth, e->a, e->b); break;
        case EV_BEND: synth_bend(&eng_synth, e->b); break;
        case EV_WHEEL: synth_wheel(&eng_synth, e->b); break;
        case EV_PRESSURE: synth_pressure(&eng_synth, e->b); break;
        case EV_HOLD: synth_hold(&eng_synth, e->b); break;
        case EV_PANIC: synth_panic(&eng_synth); break;
        case EV_ALLOFF: synth_all_notes_off(&eng_synth); break;
        case EV_VOLUME: synth_volume(&eng_synth, e->b); break;
        case EV_CHORD: if (e->a < 6u) eng_synth.prog.chord[e->a] = (uint8_t)e->b; break;
        case EV_PROGRAM:
            synth_program(&eng_synth, &eng_pbuf);
            ENG_BARRIER();
            eng_pr = eng_pr + 1u;         /* the buffer is the producer's again */
            break;
        }
    }
    ENG_BARRIER();
    eng_qr = r;
}
static void eng_render(int32_t *lr, uint32_t frames)   /* app_audio: adds into L R pairs, Q15 */
{
    if (!eng_ready) return;
    eng_drain();
    synth_render(&eng_synth, lr, frames);
    eng_voices = (uint8_t)synth_active(&eng_synth);
}
#endif
