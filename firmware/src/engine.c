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
 * note on/off, param edit, bend, mod wheel, pressure, HOLD, panic, program load.
 * Overflow policy: the consumer empties the queue at every block (5.8 ms), so it fills only if more than 256 events
 * arrive within one block (37 keys pressed and released together are 74). A full queue drops the NEW event, counts it
 * in eng_drops and returns -1 to the caller; a lost note off would leave a note hanging until PANIC (or a key press of
 * the same note). Bend, wheel and pressure are queued only when their value changes, param edits only when the value
 * changes (eng_edit holds the values sent), so a fader or wheel cannot flood it.
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
#define ENG_BARRIER() __asm__ __volatile__("" ::: "memory")

enum { EV_NOTE, EV_PARAM, EV_BEND, EV_WHEEL, EV_PRESSURE, EV_HOLD, EV_PANIC, EV_PROGRAM };
typedef struct { uint8_t type, a; int16_t b; } eng_ev_t;   /* NOTE a note b vel; PARAM a id b value; BEND b 0..16383 */

static synth_t eng_synth;                 /* the consumer's (and eng_init's, before eng_ready) */
static eng_ev_t eng_q[ENG_QLEN];
static volatile uint32_t eng_qw, eng_qr, eng_drops;
static program_t eng_pbuf, eng_edit;
static volatile uint32_t eng_pw, eng_pr;
static volatile uint8_t eng_ready, eng_voices;
static int32_t eng_last_bend = 8192, eng_last_wheel, eng_last_press, eng_last_hold;

static int eng_push(uint32_t type, uint32_t a, int32_t b)
{
    uint32_t w = eng_qw;
    if (w - eng_qr >= ENG_QLEN) { eng_drops = eng_drops + 1u; return -1; }
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
    eng_voices = 0;
    eng_last_bend = 8192, eng_last_wheel = eng_last_press = eng_last_hold = 0;
    ENG_BARRIER();
    eng_ready = 1;
}
static int eng_note(int note, int vel)    /* vel 0 = off */
{
    if (note < 0 || note > 127) return -1;
    return eng_push(EV_NOTE, (uint32_t)note, vel < 0 ? 0 : vel > 127 ? 127 : vel);
}
static int eng_param(uint32_t id, int32_t v)
{
    int16_t c;
    if (id >= P_COUNT_PHASE1) return -1;
    c = param_clamp((uint16_t)id, v);
    if (eng_edit.v[id] == c) return 0;
    if (eng_push(EV_PARAM, id, c)) return -1;
    eng_edit.v[id] = c;
    return 0;
}
static int eng_level(uint32_t type, int32_t *last, int32_t v)
{
    if (v == *last) return 0;
    if (eng_push(type, 0, v)) return -1;
    *last = v;
    return 0;
}
static int eng_bend(int32_t bend14) { return eng_level(EV_BEND, &eng_last_bend, bend14 < 0 ? 0 : bend14 > 16383 ? 16383 : bend14); }
static int eng_wheel(int32_t v) { return eng_level(EV_WHEEL, &eng_last_wheel, v < 0 ? 0 : v > 127 ? 127 : v); }
static int eng_pressure(int32_t v) { return eng_level(EV_PRESSURE, &eng_last_press, v < 0 ? 0 : v > 127 ? 127 : v); }
static int eng_hold(int on) { return eng_level(EV_HOLD, &eng_last_hold, on != 0); }
static int eng_panic(void)
{
    if (eng_push(EV_PANIC, 0, 0)) return -1;
    eng_last_hold = 0;                    /* (panic clears HOLD) */
    return 0;
}
static int eng_program(const program_t *p)   /* -1: a load is still pending (or the queue is full): try again later */
{
    if (eng_pw != eng_pr) return -1;
    eng_pbuf = *p;
    ENG_BARRIER();
    eng_pw = eng_pw + 1u;
    if (eng_push(EV_PROGRAM, 0, 0)) { eng_pw = eng_pw - 1u; return -1; }
    eng_edit = *p;
    for (unsigned i = 0; i < P_COUNT_PHASE1; i++) eng_edit.v[i] = param_clamp((uint16_t)i, p->v[i]);
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
