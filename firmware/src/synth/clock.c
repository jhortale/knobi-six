/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX clock (clock.h: the model, the units, the rulings). Included by engine.c (and so app.c's unity build); also
 * built alone by tests/unit/test_clock.c. Integers only: the divisions are by 32-bit values (no 64-bit division, no
 * libgcc), every product fits 32 bits (the bounds next to each). */
#ifndef KNOBI_CLOCK_C
#define KNOBI_CLOCK_C
#include "clock.h"

/*                                                   HALF     QTR      8TH      8TH D   8TH S   8TH T   16TH    16TH S  16TH T  32ND */
const uint8_t CLOCK_DIV_TICKS[CLK_DIV_COUNT][2] = {{48, 48}, {24, 24}, {12, 12}, {18, 6}, {16, 8}, {8, 8}, {6, 6}, {8, 4}, {4, 4}, {3, 3}};

#define CLOCK_RATE_MAX (300u * CLOCK_RATE_Q)   /* an external tempo is taken up to 300 BPM (the products below fit) */
#define CLOCK_CHUNK 4096u                      /* frames advanced at most at once: 4096 x CLOCK_RATE_MAX < 2^27 */

static uint32_t clk_len(const clk_t *c, uint32_t step) { return CLOCK_DIV_TICKS[c->div][step & 1u]; }

static void clk_zero(clk_t *c)                 /* the position back to step 0 (the counters ticks / beats go on) */
{
    c->pos = c->frac = c->last = c->next = c->steps = 0;
    c->sub = 0;
}

void clock_init(clk_t *c)
{
    uint8_t *b = (uint8_t *)c;
    for (unsigned i = 0; i < sizeof *c; i++) b[i] = 0;
    c->div = CLK_DIV_QTR;
    c->mode = CLK_MIDI_OFF;
    clock_set_bpm(c, 120);
}

void clock_set_bpm(clk_t *c, int bpm)
{
    c->bpm = (int16_t)(bpm < CLOCK_BPM_MIN ? CLOCK_BPM_MIN : bpm > CLOCK_BPM_MAX ? CLOCK_BPM_MAX : bpm);
    if (!CLOCK_EXTERNAL(c->mode)) c->rate = (uint32_t)c->bpm * CLOCK_RATE_Q;
}

void clock_set_div(clk_t *c, int div)
{
    if (div < 0 || div >= CLK_DIV_COUNT || div == c->div) return;
    c->div = (uint8_t)div;
    if (c->steps) c->next = c->last + clk_len(c, c->steps - 1u);   /* the step under way, at the new length (passed
                                                       * already: the next one fires at once) */
}

static void clk_ext_reset(clk_t *c)
{
    c->go = c->mode == CLK_MIDI_IN_NSS, c->alive = 0, c->armed = 0;
    c->ext_pos = c->pos, c->ext_n = 0, c->ext_w = 0;
    c->rate = 0;
}

void clock_set_mode(clk_t *c, int mode)
{
    if (mode < 0 || mode >= CLK_MIDI_COUNT || mode == c->mode) return;
    c->mode = (uint8_t)mode;
    if (CLOCK_EXTERNAL(mode)) clk_ext_reset(c);
    else c->rate = (uint32_t)c->bpm * CLOCK_RATE_Q;   /* internal: on from where it is, at the program's tempo */
    if (mode == CLK_MIDI_OUT) clk_zero(c), c->beats++;   /* OUT: from a downbeat (the main loop sends 0xFA with it) */
}

int clock_tap(clk_t *c, uint32_t ms)
{
    uint32_t k, span;
    int bpm;
    if (c->tap_n && ms - c->tap_t[c->tap_n - 1u] > CLOCK_TAP_GAP_MS) c->tap_n = 0;   /* a new count */
    if (c->tap_n == 5u) {                       /* keep the last 5 taps: 4 intervals */
        for (k = 0; k < 4u; k++) c->tap_t[k] = c->tap_t[k + 1u];
        c->tap_n = 4;
    }
    c->tap_t[c->tap_n++] = ms;
    if (c->tap_n < CLOCK_TAP_MIN) return 0;
    k = c->tap_n - 1u;                          /* intervals: 3 or 4 */
    span = ms - c->tap_t[0];                    /* <= 4 x 2000 ms */
    if (!span) return 0;
    bpm = (int)((60000u * k + span / 2u) / span);
    clock_set_bpm(c, bpm);
    return c->bpm;
}

static void clk_tempo(clk_t *c)                /* an external clock's rate, from the ticks' times */
{
    uint32_t n = c->ext_n, oldest, span, fpt_q8;
    if (n < 2u) { c->rate = 0; return; }
    oldest = c->ext_t[(c->ext_w + CLOCK_EXT_N - n) % CLOCK_EXT_N];
    span = c->ext_t[(c->ext_w + CLOCK_EXT_N - 1u) % CLOCK_EXT_N] - oldest;   /* <= 24 x CLOCK_EXT_TIMEOUT */
    fpt_q8 = span * 256u / (n - 1u);            /* frames a tick, Q8 (span x 256 < 2^27) */
    if (!fpt_q8) { c->rate = CLOCK_RATE_MAX; return; }
    c->rate = (CLOCK_U / 1000u) * 256000u / fpt_q8;   /* CLOCK_U x 256 / fpt_q8 (7056 x 256000 < 2^31) */
    if (c->rate > CLOCK_RATE_MAX) c->rate = CLOCK_RATE_MAX;
}

void clock_midi(clk_t *c, uint8_t status, uint32_t ticks)
{
    int nss = c->mode == CLK_MIDI_IN_NSS;
    if (!CLOCK_EXTERNAL(c->mode)) return;
    switch (status) {
    case 0xF8: {
        int was = c->alive;
        c->ext_t[c->ext_w] = ticks;             /* the tempo: its time into the ring */
        c->ext_w = (uint8_t)((c->ext_w + 1u) % CLOCK_EXT_N);
        if (c->ext_n < CLOCK_EXT_N) c->ext_n++;
        clk_tempo(c);
        c->ext_last = ticks, c->alive = 1;
        if (c->armed || (nss && !was)) {        /* after 0xFA (or, without start / stop, after a silence): the
                                                 * downbeat, step 0 here */
            c->armed = 0, c->go = 1;
            clk_zero(c);
            c->beats++;
            c->ext_pos = 0;
        } else if (c->go) c->ext_pos++;         /* (stopped: the ticks give the tempo, the position stays) */
        break;
    }
    case 0xFA: if (!nss) c->armed = 1, c->go = 0; break;
    case 0xFB: if (!nss) c->go = 1, c->armed = 0; break;
    case 0xFC: if (!nss) c->go = 0, c->armed = 0; break;
    }
}

int clock_running(const clk_t *c)
{
    return !CLOCK_EXTERNAL(c->mode) || (c->go && c->alive);
}

void clock_restart(clk_t *c)
{
    if (CLOCK_EXTERNAL(c->mode)) return;
    clk_zero(c);
    c->beats++;
}

/* the position on by a counts (a < 2^31); ticks and beats count the ticks crossed */
static void clk_advance(clk_t *c, uint32_t a)
{
    c->frac += a;
    while (c->frac >= CLOCK_U) {
        c->frac -= CLOCK_U;
        c->pos++, c->ticks++;
        if (++c->sub == CLOCK_PPQ) c->sub = 0, c->beats++;
    }
}

/* the steps that start at the position now, at frame `at` */
static void clk_emit(clk_t *c, uint32_t at)
{
    while ((int32_t)(c->pos - c->next) >= 0) {
        if (c->n < CLOCK_EV_MAX) c->at[c->n] = (uint16_t)at, c->step[c->n] = c->steps, c->n++;
        c->last = c->next;
        c->next += clk_len(c, c->steps);
        c->steps++;
    }
}

uint32_t clock_tick(clk_t *c, uint32_t frames)
{
    uint32_t base = 0;
    int ext = CLOCK_EXTERNAL(c->mode);
    c->n = 0;
    if (ext && c->alive && c->now - c->ext_last > CLOCK_EXT_TIMEOUT) c->alive = 0, c->ext_n = 0, c->rate = 0;
    if (!clock_running(c)) { c->now += frames; return 0; }
    if (ext)                                    /* a tick came before the position got there: on to it now */
        while ((int32_t)(c->pos - c->ext_pos) < 0) clk_advance(c, CLOCK_U - c->frac);
    while (base < frames) {
        uint32_t len = frames - base < CLOCK_CHUNK ? frames - base : CLOCK_CHUNK, off = 0, rate = c->rate;
        while (off < len) {                     /* (a step at the chunk's end is the next one's frame 0) */
            uint32_t need, f, cap = 0, adv;
            clk_emit(c, base + off);
            if (!rate) { off = len; break; }     /* (an external clock with no tempo yet: the ticks move it) */
            need = (c->next - c->pos) * CLOCK_U - c->frac;   /* counts to the next step: <= 48 ticks (< 2^29) */
            if (ext) cap = (int32_t)(c->ext_pos - c->pos) < 0 ? 0u :   /* not past the next tick */
                           (c->ext_pos - c->pos) * CLOCK_U + CLOCK_U - 1u - c->frac;
            if (!ext || need <= cap) {
                f = (need + rate - 1u) / rate;   /* the first frame at or past it */
                if (f < len - off) { clk_advance(c, f * rate); off += f; continue; }
            }
            adv = (len - off) * rate;            /* the rest of the chunk (< 2^27) */
            if (ext && adv > cap) adv = cap;
            clk_advance(c, adv);
            off = len;
        }
        base += len;
    }
    c->now += frames;
    return c->n;
}

int clock_bpm_x10(const clk_t *c)
{
    if (!CLOCK_EXTERNAL(c->mode)) return c->bpm * 10;
    return (int)((c->rate * 10u + CLOCK_RATE_Q / 2u) / CLOCK_RATE_Q);
}

/* Q32 quarter notes a frame = rate x 2^32 / (CLOCK_U x 24) = rate x 25.36246...: rate x 25 + rate x 23754 / 65536
 * (rate <= CLOCK_RATE_MAX: 19200 x 23754 < 2^29) */
uint32_t clock_quarter_inc(const clk_t *c)
{
    return c->rate * 25u + (c->rate * 23754u >> 16);
}
#endif
