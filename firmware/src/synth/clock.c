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
#ifdef CLOCK_HOST
static void clk_host_grid(clk_t *c);
static uint32_t clk_host_tick(clk_t *c, uint32_t frames);
#endif

static void clk_zero(clk_t *c)                 /* the position back to step 0 (the counters ticks / beats go on) */
{
    c->pos = c->frac = c->last = c->next = c->steps = 0;
    c->sub = 0;
}
/* a start: step 0 now, and tick 0 and beat 0 entered (counted, as clk_advance counts each tick it enters): MIDI clock
 * out sends 0xFA then this tick's 0xF8, the downbeat, with step 0 (start_ticks: the count before it) */
static void clk_start(clk_t *c)
{
    clk_zero(c);
    c->start_ticks = c->ticks;
    c->ticks++, c->beats++, c->starts++;
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
#ifdef CLOCK_HOST
    if (c->host) return;                        /* (following the host: kept for its stop) */
#endif
    if (!CLOCK_EXTERNAL(c->mode)) c->rate = (uint32_t)c->bpm * CLOCK_RATE_Q;
}

void clock_set_div(clk_t *c, int div)
{
    if (div < 0 || div >= CLK_DIV_COUNT || div == c->div) return;
    c->div = (uint8_t)div;
#ifdef CLOCK_HOST
    if (c->host) { clk_host_grid(c); return; }   /* (the host's grid: the step under way, no step now) */
#endif
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
#ifdef CLOCK_HOST
    if (c->host) { c->mode = (uint8_t)mode; return; }   /* (applied at the host's stop) */
#endif
    c->mode = (uint8_t)mode;
    if (CLOCK_EXTERNAL(mode)) clk_ext_reset(c);
    else c->rate = (uint32_t)c->bpm * CLOCK_RATE_Q;   /* internal: on from where it is, at the program's tempo */
    if (mode == CLK_MIDI_OUT) clk_start(c);      /* OUT: from a downbeat (the main loop sends 0xFA with it) */
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
#ifdef CLOCK_HOST
    if (c->host) return;
#endif
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
            clk_start(c);
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
#ifdef CLOCK_HOST
    if (c->host) return 1;
#endif
    return !CLOCK_EXTERNAL(c->mode) || (c->go && c->alive);
}

void clock_restart(clk_t *c)
{
    if (CLOCK_EXTERNAL(c->mode)) return;
#ifdef CLOCK_HOST
    if (c->host) return;                        /* (the host owns the phase) */
#endif
    clk_start(c);                               /* (under OUT: the slaves get 0xFA and tick 0 again, clock_out_poll) */
}

/* the position on by a counts (a < 2^31); ticks and beats count the ticks (beats) entered */
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
#ifdef CLOCK_HOST
    at += c->hoff;                              /* (a part of a block cut at an anchor: the block's frame) */
#endif
    while ((int32_t)(c->pos - c->next) >= 0) {
#ifdef CLOCK_HOST
        if (c->n < CLOCK_EV_MAX) c->kind[c->n] = (uint8_t)(c->host ? CLK_EV_HOSTED | CLK_EV_STEP : CLK_EV_STEP), c->gate[c->n] = 0;
#endif
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
#ifdef CLOCK_HOST
    if (!c->hpart && c->hq_n) return clk_host_tick(c, frames);   /* (anchors waiting: the block in parts) */
    if (c->host) ext = 0;
    if (!c->hpart)
#endif
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
#ifdef CLOCK_HOST
    if (c->host) return (int)((c->rate * 10u + CLOCK_RATE_Q / 2u) / CLOCK_RATE_Q);
#endif
    if (!CLOCK_EXTERNAL(c->mode)) return c->bpm * 10;
    return (int)((c->rate * 10u + CLOCK_RATE_Q / 2u) / CLOCK_RATE_Q);
}

/* Q32 quarter notes a frame = rate x 2^32 / (CLOCK_U x 24) = rate x 25.36246...: rate x 25 + rate x 23754 / 65536
 * (rate <= CLOCK_RATE_MAX: 19200 x 23754 < 2^29) */
uint32_t clock_quarter_inc(const clk_t *c)
{
    return c->rate * 25u + (c->rate * 23754u >> 16);
}
void clock_out_init(clk_out_t *o) { o->mode = -1, o->started = 0, o->starts = o->sent = 0; }

uint32_t clock_out_poll(clk_out_t *o, int mode, int applied, uint32_t starts, uint32_t start_ticks, uint32_t ticks,
                        uint8_t *bytes, uint32_t max)
{
    uint32_t n = 0;
    if (max < 3u) return 0;
    if (mode != o->mode) {                      /* the global changed: leaving OUT stops the slaves */
        if (o->mode == CLK_MIDI_OUT && o->started) bytes[n++] = 0xFC;
        o->mode = (int8_t)mode, o->started = 0;
    }
    if (mode != CLK_MIDI_OUT || applied != CLK_MIDI_OUT) return n;   /* (OUT asked, not applied yet: its restart has
                                                 * not happened) */
    if (!o->started || starts != o->starts) {   /* a start (OUT chosen, or clock_restart): 0xFA, then from its tick 0 */
        bytes[n++] = 0xFA;
        o->started = 1, o->starts = starts, o->sent = start_ticks;
    }
    if (ticks - o->sent > CLOCK_OUT_OWED) o->sent = ticks - CLOCK_OUT_OWED;   /* (a long stall: the oldest dropped) */
    for (; o->sent != ticks && n < max; o->sent++) bytes[n++] = 0xF8;
    return n;
}
#ifdef CLOCK_HOST
/* ---- the host's transport (clock.h). Host code only: 64-bit arithmetic. Positions are signed tick counts (pos as an
 * int32_t: a count-in is negative); the grid starts at position 0 */
static int64_t clk_floordiv(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
/* the step under way at position p (ticks): its number g and its first tick */
static void clk_host_at(const clk_t *c, int64_t p, int64_t *g, int64_t *start)
{
    int64_t even = CLOCK_DIV_TICKS[c->div][0], pair = even + CLOCK_DIV_TICKS[c->div][1];
    int64_t k = clk_floordiv(p, pair), r = p - k * pair;
    *g = 2 * k + (r >= even), *start = k * pair + (r >= even ? even : 0);
}
static void clk_host_pos(clk_t *c, uint32_t pos, uint32_t frac)
{
    int64_t p = (int32_t)pos;
    c->pos = pos, c->frac = frac, c->sub = (uint8_t)(p - clk_floordiv(p, CLOCK_PPQ) * CLOCK_PPQ);
}
/* the step under way (last, next, steps: the next step's number) from the position, on the host's grid */
static void clk_host_grid(clk_t *c)
{
    int64_t g, t;
    clk_host_at(c, (int32_t)c->pos, &g, &t);
    c->last = (uint32_t)t, c->next = (uint32_t)(t + (int64_t)clk_len(c, (uint32_t)g)), c->steps = (uint32_t)(g + 1);
}
static void clk_host_ev(clk_t *c, unsigned kind, uint32_t step, uint32_t gate)   /* an event at the anchor's frame */
{
    if (c->n < CLOCK_EV_MAX)
        c->at[c->n] = (uint16_t)c->hoff, c->step[c->n] = step, c->kind[c->n] = (uint8_t)kind, c->gate[c->n] = gate, c->n++;
}
static void clk_host_apply(clk_t *c, const struct clk_host *h)
{
    int64_t g, t, d, half;
    uint32_t rate = h->rate < CLOCK_RATE_Q ? CLOCK_RATE_Q : h->rate > CLOCK_HOST_RATE_MAX ? CLOCK_HOST_RATE_MAX : h->rate;
    if (!h->playing) {                          /* stopped: the firmware's clock again, from where it is */
        if (!c->host) return;
        c->host = 0;
        clk_host_ev(c, CLK_EV_RELEASE, c->steps - 1u, 0);
        if (CLOCK_EXTERNAL(c->mode)) clk_ext_reset(c);
        else c->rate = (uint32_t)c->bpm * CLOCK_RATE_Q;
        return;
    }
    if (c->host) {                              /* where the clock ran to? (within a quarter tick: no jump) */
        d = ((int64_t)(int32_t)h->pos - (int32_t)c->pos) * (int64_t)CLOCK_U + (int64_t)h->frac - (int64_t)c->frac;
        if (d <= (int64_t)CLOCK_HOST_TOL && d >= -(int64_t)CLOCK_HOST_TOL) {
            c->rate = rate;
            clk_host_pos(c, h->pos, h->frac);
            clk_emit(c, 0);                     /* (a boundary it passed: here; one fired already never again) */
            return;
        }
    }
    c->host = 1, c->rate = rate;                /* a jump: start, a loop's wrap, a locate */
    clk_host_pos(c, h->pos, h->frac);
    clk_host_at(c, (int32_t)h->pos, &g, &t);
    c->last = (uint32_t)t, c->next = (uint32_t)(t + (int64_t)clk_len(c, (uint32_t)g)), c->steps = (uint32_t)(g + 1);
    d = ((int64_t)(int32_t)h->pos - t) * (int64_t)CLOCK_U + (int64_t)h->frac;   /* into the step, counts */
    half = (int64_t)clk_len(c, (uint32_t)g) * (int64_t)CLOCK_U / 2;
    if (!d) clk_host_ev(c, CLK_EV_HOSTED | CLK_EV_JUMP, (uint32_t)g, 0);
    else if (d < half) clk_host_ev(c, CLK_EV_HOSTED | CLK_EV_RESUME, (uint32_t)g, (uint32_t)((half - d + rate - 1) / rate));
    else clk_host_ev(c, CLK_EV_HOSTED | CLK_EV_RELEASE, (uint32_t)g, 0);
}
/* a block with anchors in it: in parts, each anchor applied at its frame (the events: the block's frames) */
static uint32_t clk_host_tick(clk_t *c, uint32_t frames)
{
    uint32_t done = 0, i;
    c->n = 0, c->hpart = 1;
    while (c->hq_n && c->hq[0].at < frames) {
        struct clk_host h = c->hq[0];
        for (i = 1; i < c->hq_n; i++) c->hq[i - 1u] = c->hq[i];
        c->hq_n--;
        if (h.at > done) c->hoff = done, clock_tick(c, h.at - done), done = h.at;
        c->hoff = h.at;
        clk_host_apply(c, &h);
    }
    if (done < frames) c->hoff = done, clock_tick(c, frames - done);
    for (i = 0; i < c->hq_n; i++) c->hq[i].at -= frames;
    c->hpart = 0, c->hoff = 0;
    return c->n;
}
void clock_host(clk_t *c, uint32_t at, int playing, uint32_t rate, uint32_t pos, uint32_t frac)
{
    struct clk_host h;
    h.at = at, h.rate = rate, h.pos = pos, h.frac = frac < CLOCK_U ? frac : CLOCK_U - 1u, h.playing = (uint8_t)(playing != 0);
    if (c->hq_n && h.at <= c->hq[c->hq_n - 1u].at) {   /* (at the last one's frame, or before it: the newest wins there:
                                                 * a block's start and a loop's wrap at the same frame, one step) */
        h.at = c->hq[c->hq_n - 1u].at;
        c->hq[c->hq_n - 1u] = h;
    } else if (c->hq_n == CLOCK_HOST_Q) c->hq[CLOCK_HOST_Q - 1u] = h;   /* (full: the newest replaces the last) */
    else c->hq[c->hq_n++] = h;
}
int clock_following(const clk_t *c) { return c->host; }
#endif
#endif
