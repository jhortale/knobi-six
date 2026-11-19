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
 * (released, HOLD unchanged), MIDI volume, master tune (the globals TRANSPOSE + TUNE, cents), MIDI clock in (the
 * realtime bytes 0xF8 / FA / FB / FC) and the MIDI CLOCK global's mode.
 *
 * The clock (synth/clock.h; Task 5): eng_clk, the consumer's, on CPU0 in the audio interrupt, never on CPU1. Its events
 * drain in order (P_BPM / P_CLK_DIV edits and program loads set its tempo and division, EV_RT a MIDI clock byte at the
 * block's start, EV_CLKMODE the mode); then eng_render ticks it once per block, before the synth renders, and gives the
 * synth its tempo for LFO SYNC (synth_clock). The block's step events stay in eng_clk (eng_clk.n, .at[], .step[]) for
 * the arpeggiator, which runs there too (synth/arp.h; eng_arp, CPU0, never CPU1): with ARP ON a key event goes to
 * arp_key, not to the synth; eng_render splits the block at each step's frame (arp_step: the note starts on that sample)
 * and at the arp note's gate end (arp_run); a pattern start with ARP BEAT SYNC off (EV_ARPSYNC, the global) restarts the
 * clock (never under MIDI CLOCK OUT: eng_arp_start); the clock not running (an external clock absent or stopped) silences it; HOLD, ALL NOTES OFF, PANIC and program
 * loads reach it too. Its MIDI out ring (eng_arp.out[], out_w) is read by the main loop (ui/midi_map.c).
 * The sequencer's player (synth/seq.h "The player"; eng_seqp, CPU0, never CPU1) runs beside it on the same step events
 * and the same split (eng_render_steps: seq_step at each step's frame, seq_run to its gate's end), and writes the same
 * MIDI out ring. It plays eng_seqd, the engine's copy of the edit buffer's sequence: eng_seq_load copies a sequence into
 * eng_sbuf and queues EV_SEQ (one at a time, as program loads: eng_sw / eng_sr), the consumer copies it into eng_seqd as
 * the event drains, so the interrupt never reads the main loop's buffer. SEQ PLAY (EV_SEQPLAY) starts it under the
 * arp's start rules (eng_arp_start) and turns the arp off; ARP ON, a program load and PANIC stop it (eng_seq_stop: its
 * notes released first, so ARP ON never takes them for keys); ALL NOTES OFF releases its notes (it plays on); the clock
 * not running silences it (the step kept); a MIDI clock's 0xFA / 0xFB / 0xFC reach it (seq_ext; not under IN NO S/S).
 * Published for the main loop: eng_seq_state (0 stopped, 1 playing, 2 armed: on, the clock not running), eng_seq_cur
 * (the step played last). The producer's view of SEQ PLAY: eng_seq_playing().
 * The main loop reads what the clock publishes, one aligned word each:
 * eng_clk_ticks (ticks entered, 24 a quarter, never going back), eng_clk_starts / eng_clk_start_ticks (restarts to
 * step 0, the tick count before the latest) and eng_clk_mode (the mode applied): MIDI clock out (clock.h
 * clock_out_poll; written ticks, start_ticks, then starts, so a reader that sees starts unchanged around its reads has a
 * consistent set); eng_clk_beats (quarter notes: TAP's LED), eng_clk_run (clock_running), eng_clk_alive (an external
 * clock's ticks arriving, started or not) and eng_clk_bpm10 (the tempo followed, x 10).
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
 * The audio side does nothing until eng_init has set the synth up (eng_ready), so app_audio may start first.
 *
 * The output fade (WRITE's deliberate dropout, ui/controls.c): eng_fade(1) asks the consumer to take the output to
 * silence along a straight line of ENG_FADE_MS (10 ms), eng_fade(0) back to full; the synth plays on underneath.
 * eng_faded() is true once ENG_FADE_QUIET whole blocks have gone out silent (both halves of the audio DMA's double
 * buffer hold zeros): a flash erase that holds the interrupts off then stalls on silence, not mid-waveform. The fade
 * scales what eng_render added into lr (app_audio's buffer starts at zero). */
#ifndef KNOBI_ENGINE_C
#define KNOBI_ENGINE_C
#include <stdint.h>
#include "synth/clock.c"                  /* (the clock: a pure module, the engine's) */
#include "synth/arp.c"                    /* (the arpeggiator: the engine's, after voices.c) */
#include "synth/seqplay.c"                /* (the sequencer's player: the engine's, after arp.c: it shares its ring) */

#define ENG_QLEN 256u                     /* events; a power of two */
#define ENG_RESERVE 64u                   /* the last slots: note-offs, HOLD and PANIC only */
#define ENG_BARRIER() __asm__ volatile("" ::: "memory")   /* a compiler barrier (as platform.c RING_PUBLISH) */

enum { EV_NOTE, EV_PARAM, EV_BEND, EV_WHEEL, EV_PRESSURE, EV_HOLD, EV_PANIC, EV_PROGRAM, EV_CHORD, EV_ALLOFF, EV_VOLUME,
       EV_TUNE, EV_RT, EV_CLKMODE, EV_ARPSYNC, EV_SEQPLAY, EV_SEQXP, EV_SEQ };
typedef struct { uint8_t type, a; int16_t b; } eng_ev_t;   /* NOTE a note b vel; PARAM a id b value; BEND b 0..16383;
                                                              CHORD a slot 0..5 b note (0 = empty); RT a status
                                                              (0xF8..0xFC); CLKMODE b the MIDI CLOCK global;
                                                              ARPSYNC b the ARP BEAT SYNC global; SEQPLAY b
                                                              PLAY on; SEQXP b transpose; SEQ a sequence
                                                              handed over (eng_sbuf) */
_Static_assert(P_COUNT_RESERVED <= 256, "a live id fits eng_ev_t.a");

#ifdef KNOBI_CORE
#include "core/state/engine.h"   /* knobi_core: per instance (tools/core_state.py) */
#else
static synth_t eng_synth;                 /* the consumer's (and eng_init's, before eng_ready) */
static eng_ev_t eng_q[ENG_QLEN];
static volatile uint32_t eng_qw, eng_qr, eng_drops;
static program_t eng_pbuf, eng_edit;
static volatile uint32_t eng_pw, eng_pr;
static volatile uint8_t eng_ready, eng_voices;
static int32_t eng_last_bend = 8192, eng_last_wheel, eng_last_press, eng_last_hold, eng_last_vol = 127, eng_last_tune;
static int32_t eng_last_clkmode;          /* the MIDI CLOCK mode sent (producer) */
static int32_t eng_last_arpsync;          /* the ARP BEAT SYNC global sent (producer) */
static arp_t eng_arp;                     /* the arpeggiator (consumer; eng_init before eng_ready) */
static uint8_t eng_arp_sync_on;           /* ARP BEAT SYNC as applied (consumer) */
static seqp_t eng_seqp;                   /* the sequencer's player (consumer; eng_init before eng_ready) */
#ifdef DINGHY_HOST
static seq_t eng_seqd, eng_sbuf;          /* the sequence played (consumer); the hand-over buffer (eng_seq_load) */
#else
static seq_t eng_seqd __attribute__((section(".pool"))), eng_sbuf __attribute__((section(".pool")));   /* (897 B each) */
#endif
static volatile uint32_t eng_sw, eng_sr;  /* sequences handed over / taken (as eng_pw / eng_pr) */
static int32_t eng_last_seqplay, eng_last_seqxp;   /* SEQ PLAY and the transpose sent (producer) */
static volatile uint8_t eng_seq_state;    /* published: 0 stopped, 1 playing, 2 armed (PLAY on, the clock not running) */
static volatile uint8_t eng_seq_cur = SEQ_NO_STEP;   /* published: the step played last (SEQ_NO_STEP none since PLAY) */
static clk_t eng_clk;                     /* the clock (consumer; eng_init before eng_ready) */
static volatile uint32_t eng_clk_ticks, eng_clk_beats, eng_clk_starts, eng_clk_start_ticks;   /* published by the
                                                                                          consumer (see above) */
static volatile uint8_t eng_clk_run, eng_clk_alive, eng_clk_mode;
static volatile uint16_t eng_clk_bpm10;
#define ENG_FADE_ONE 32768                /* the fade's gain, Q15 */
#define ENG_FADE_MS 10
#define ENG_FADE_STEP (ENG_FADE_ONE / (ENG_FADE_MS * 441 / 10))   /* a frame: 74 -> 443 frames = 10.0 ms */
#define ENG_FADE_QUIET 2u                 /* whole silent blocks before eng_faded() */
static volatile uint8_t eng_fade_out;     /* producer: 1 = to silence */
static volatile int32_t eng_fade_g = ENG_FADE_ONE;   /* consumer: the gain now */
static volatile uint8_t eng_fade_quiet;   /* consumer: whole blocks gone out at gain 0 (saturates) */

#define ENG_LOAD_MAX 999u
static volatile uint16_t eng_load, eng_peak;   /* the load (eng_cpu) */
static volatile uint8_t eng_vlimit = NVOICE;
static volatile uint32_t eng_sheds;
static uint32_t eng_load_q3, eng_peak_q8;   /* (consumer only) */
static uint32_t eng_off_pend[4];          /* note-offs waiting for room: bit n = note n (producer only) */
static uint32_t eng_par_pend[(P_COUNT_RESERVED + 31u) / 32u];   /* live ids whose latest eng_edit value is not queued */
static uint8_t eng_panic_pend, eng_alloff_pend, eng_chord_pend;
#endif

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
    eng_last_bend = 8192, eng_last_wheel = eng_last_press = eng_last_hold = 0, eng_last_vol = 127, eng_last_tune = 0;
    eng_last_clkmode = CLK_MIDI_OFF, eng_last_arpsync = 0, eng_arp_sync_on = 0;
    arp_init(&eng_arp, 0x41525036u);
    arp_program(&eng_arp, &eng_synth, eng_edit.v);
    seq_clear(&eng_seqd);
    seqp_init(&eng_seqp, &eng_seqd, &eng_arp);
    eng_sw = eng_sr = 0, eng_last_seqplay = eng_last_seqxp = 0, eng_seq_state = 0, eng_seq_cur = SEQ_NO_STEP;
    clock_init(&eng_clk);
    clock_set_bpm(&eng_clk, eng_edit.v[P_BPM]);
    clock_set_div(&eng_clk, eng_edit.v[P_CLK_DIV]);
    eng_clk_ticks = eng_clk_beats = eng_clk_starts = eng_clk_start_ticks = 0, eng_clk_run = 1, eng_clk_alive = 0;
    eng_clk_mode = CLK_MIDI_OFF, eng_clk_bpm10 = (uint16_t)clock_bpm_x10(&eng_clk);
    eng_fade_out = 0, eng_fade_g = ENG_FADE_ONE, eng_fade_quiet = 0;
    eng_load = eng_peak = 0, eng_load_q3 = eng_peak_q8 = 0, eng_vlimit = NVOICE, eng_sheds = 0;
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
static int eng_param(uint32_t id, int32_t v)   /* 0 queued (or unchanged), 1 kept for eng_flush, -1 not a live id */
{
    int16_t c;
    if (id > 0xFFFFu || !P_IS_LIVE(id)) return -1;
    c = param_clamp((uint16_t)id, v);
    if (eng_edit.v[id] == c && !ENG_BIT(eng_par_pend, id)) return 0;
    eng_edit.v[id] = c;
    if (id == P_ARP_ON && c) eng_last_seqplay = 0;   /* (ARP ON stops the sequencer: the consumer does it as it drains) */
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
static int eng_tune(int32_t cents) { return eng_level(EV_TUNE, &eng_last_tune, cents < -1250 ? -1250 : cents > 1250 ? 1250 : cents, ENG_RESERVE); }
static int eng_arp_sync(int on) { return eng_level(EV_ARPSYNC, &eng_last_arpsync, on != 0, 0); }   /* ARP BEAT SYNC */
static int eng_clock_mode(int32_t mode) { return eng_level(EV_CLKMODE, &eng_last_clkmode, mode < 0 ? 0 : mode >= CLK_MIDI_COUNT ? CLK_MIDI_COUNT - 1 : mode, 0); }
/* a MIDI clock byte in (0xF8 clock, FA start, FB continue, FC stop): -1 refused (the queue full). A clock tick keeps the
 * reserve free (one lost is one tick late); start / stop / continue may use it */
static int eng_midi_rt(uint32_t st)
{
    if (st != 0xF8u && st != 0xFAu && st != 0xFBu && st != 0xFCu) return -1;
    if (eng_push(EV_RT, st, 0, st == 0xF8u ? ENG_RESERVE : 0)) { eng_drops = eng_drops + 1u; return -1; }
    return 0;
}
static void eng_fade(int out) { eng_fade_out = (uint8_t)(out != 0); }
static int eng_faded(void) { return eng_fade_out && eng_fade_quiet >= ENG_FADE_QUIET; }
static int eng_panic(void)                /* 0 queued, 1 kept for eng_flush */
{
    eng_last_hold = 0;                    /* (panic clears HOLD) */
    eng_last_seqplay = 0;                 /* (and stops the sequencer) */
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
/* the main loop, every pass: what was kept, in order; returns what is left. A kept param edit goes out here, after
 * whatever was queued since, so it may land after a later note-on: harmless (eng_edit has the value already; the
 * note starts on the value before it, and the edit then applies, smoothed, as any edit under a sounding note) */
static int eng_flush(void)
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
    for (i = 0; i < P_COUNT_RESERVED; i++)
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
    eng_last_seqplay = eng_last_seqxp = 0;   /* (a program load stops the sequencer, its transpose back to none) */
    for (unsigned i = 0; i < P_COUNT_RESERVED; i++) eng_edit.v[i] = param_clamp((uint16_t)i, p->v[i]);
    for (unsigned i = 0; i < sizeof eng_par_pend / sizeof eng_par_pend[0]; i++) eng_par_pend[i] = 0;   /* its values win */
    eng_chord_pend = 0;
    return 0;
}
static int eng_active(void) { return eng_voices; }   /* voices sounding after the last block */
/* ---- the sequencer (producer; synth/seq.h "The player"). SEQ PLAY: on arms it (from step 1; the arp's start rules,
 * eng_arp_start), off stops it (its notes released, back to step 1). Turning it on turns the arp off (ARP ON, eng_edit's
 * too); ARP ON (eng_param), a program load (eng_program) and PANIC turn it off: eng_seq_playing() follows all of them.
 * -1: refused (the queue full): the caller tries again on its next pass. Not stored with the program */
static int eng_seq_play(int on)
{
    on = on != 0;
    if (on != eng_last_seqplay) {
        if (eng_push(EV_SEQPLAY, 0, on, on ? ENG_RESERVE : 0)) { eng_drops = eng_drops + 1u; return -1; }
        eng_last_seqplay = on;
    }
    if (on && eng_edit.v[P_ARP_ON]) eng_param(P_ARP_ON, 0);   /* (only once PLAY is queued: a refused one leaves the
                                                 * arp as it was. The consumer turns the arp off as it drains anyway) */
    return 0;
}
static int eng_seq_playing(void) { return eng_last_seqplay; }   /* SEQ PLAY as sent (producer's view) */
/* transpose, semitones (the UI: the key pressed - SEQ_XP_CENTER); from the next step; a program load resets it */
static int eng_seq_transpose(int semis)
{
    return eng_level(EV_SEQXP, &eng_last_seqxp, semis < -127 ? -127 : semis > 127 ? 127 : semis, ENG_RESERVE);
}
/* the sequence the player plays (the edit buffer's, params/prog_seq.h edit_seq: the main loop hands every change
 * over, app.c). Copied into eng_sbuf, which the consumer copies into its own (eng_seqd) as the event drains, so the
 * interrupt never reads a sequence being written. -1: one still pending (or the queue full): try again later */
static int eng_seq_load(const seq_t *q)
{
    if (eng_sw != eng_sr) return -1;
    eng_sbuf = *q;
    ENG_BARRIER();
    eng_sw = eng_sw + 1u;
    if (eng_push(EV_SEQ, 0, 0, ENG_RESERVE)) { eng_sw = eng_sw - 1u; return -1; }
    return 0;
}

/* ---- consumer (audio interrupt) */
/* an arp pattern starts (a key into an empty one, a relatch, ARP ON with keys held): ARP BEAT SYNC off, the clock
 * restarts (step 0 at this block's frame 0: the events drain before clock_tick; an external clock: ignored, the next
 * step); on, the next step of the division. Under MIDI CLOCK = OUT never a restart (final review 1): it would send the
 * slaves 0xFA and jump a DAW or drum machine to its bar start on every chord; the arp waits for the next step of the
 * division, as with BEAT SYNC on (the internal grid, the slaves' and the TAP LED untouched) */
static void eng_arp_start(void) { if (!eng_arp_sync_on && eng_clk.mode != CLK_MIDI_OUT) clock_restart(&eng_clk); }
static void eng_seq_stop(void)            /* the sequencer off, its notes released (here and in the MIDI ring) */
{
    seq_silence(&eng_seqp, &eng_synth);
    seq_play(&eng_seqp, 0);
}
static void eng_drain(void)
{
    uint32_t r = eng_qr, w = eng_qw;
    ENG_BARRIER();
    for (; r != w; r++) {
        const eng_ev_t *e = &eng_q[r & (ENG_QLEN - 1u)];
        switch (e->type) {
        case EV_NOTE:                     /* (ARP ON: the arp's, never the synth's directly) */
            if (!eng_arp.on) synth_note(&eng_synth, e->a, e->b);
            else if (arp_key(&eng_arp, e->a, e->b)) eng_arp_start();
            break;
        case EV_PARAM:
            if (e->a >= P_ARP_ON && e->a <= P_ARP_HOLD) {
                if (e->a == P_ARP_ON && e->b) eng_seq_stop();   /* (exclusive; first: ARP ON takes the synth's keys
                                                                 * down, the sequence's must not be among them) */
                eng_synth.prog.v[e->a] = e->b;
                if (arp_set(&eng_arp, &eng_synth, e->a, e->b)) eng_arp_start();
            } else if (e->a == P_BPM) clock_set_bpm(&eng_clk, e->b), eng_synth.prog.v[P_BPM] = e->b;
            else if (e->a == P_CLK_DIV) clock_set_div(&eng_clk, e->b), eng_synth.prog.v[P_CLK_DIV] = e->b;
            else synth_param(&eng_synth, e->a, e->b);
            break;
        case EV_RT:                       /* (at this block's start) */
            clock_midi(&eng_clk, e->a, eng_clk.now);
            if (CLOCK_EXTERNAL(eng_clk.mode) && eng_clk.mode != CLK_MIDI_IN_NSS) seq_ext(&eng_seqp, e->a);
            break;
        case EV_CLKMODE: clock_set_mode(&eng_clk, e->b); break;
        case EV_ARPSYNC: eng_arp_sync_on = (uint8_t)(e->b != 0); break;
        case EV_BEND: synth_bend(&eng_synth, e->b); break;
        case EV_WHEEL: synth_wheel(&eng_synth, e->b); break;
        case EV_PRESSURE: synth_pressure(&eng_synth, e->b); break;
        case EV_HOLD:                     /* (ARP ON: the arp latches; the synth's HOLD stays off) */
            arp_hold(&eng_arp, e->b);
            if (!eng_arp.on) synth_hold(&eng_synth, e->b);
            break;
        case EV_PANIC: eng_seq_stop(); arp_clear(&eng_arp, &eng_synth); synth_panic(&eng_synth); arp_hold(&eng_arp, 0); break;
        case EV_ALLOFF:                   /* (the sequence's notes released; it plays on at its next step) */
            seq_silence(&eng_seqp, &eng_synth);
            arp_clear(&eng_arp, &eng_synth);
            synth_all_notes_off(&eng_synth);
            break;
        case EV_SEQPLAY:
            if (!e->b) { eng_seq_stop(); break; }
            if (eng_arp.on) arp_set(&eng_arp, &eng_synth, P_ARP_ON, 0), eng_synth.prog.v[P_ARP_ON] = 0;   /* exclusive */
            if (seq_play(&eng_seqp, 1)) eng_arp_start();   /* (the arp's start rules) */
            break;
        case EV_SEQXP: seq_transpose(&eng_seqp, e->b); break;
        case EV_SEQ:                      /* a new sequence: the player follows it (its notes sounding are its own) */
            eng_seqd = eng_sbuf;
            if (eng_seqp.cur != SEQ_NO_STEP && eng_seqp.cur >= eng_seqd.len) eng_seqp.cur = SEQ_NO_STEP;   /* (the step
                                                 * shown never past the new length) */
            eng_seq_cur = eng_seqp.cur;
            ENG_BARRIER();
            eng_sr = eng_sr + 1u;         /* the buffer is the producer's again */
            break;
        case EV_VOLUME: synth_volume(&eng_synth, e->b); break;
        case EV_TUNE: synth_tune(&eng_synth, e->b); break;
        case EV_CHORD: if (e->a < 6u) eng_synth.prog.chord[e->a] = (uint8_t)e->b; break;
        case EV_PROGRAM:
            eng_seq_stop();               /* (the sequencer stops; its new sequence follows: app.c) */
            seq_transpose(&eng_seqp, 0);
            synth_program(&eng_synth, &eng_pbuf);
            arp_program(&eng_arp, &eng_synth, eng_pbuf.v);   /* (its keys forgotten, as the synth's) */
            clock_set_bpm(&eng_clk, eng_pbuf.v[P_BPM]);   /* (the program's tempo and division: the position kept) */
            clock_set_div(&eng_clk, eng_pbuf.v[P_CLK_DIV]);
            ENG_BARRIER();
            eng_pr = eng_pr + 1u;         /* the buffer is the producer's again */
            break;
        }
    }
    ENG_BARRIER();
    eng_qr = r;
}
/* The CPU guard's clock (voices.h synth_guard): the platform's tick counter (TIMER4, FM1_TICKS_PER_US); on the host a
 * fake clock that each render advances by eng_fake_per_voice ticks per voice sounding (the tests set it). */
#define ENG_TICKS_PER_FRAME_X10 5442u      /* 24 ticks per us x 10 / 44.1 kHz */
#ifdef DINGHY_HOST
#ifndef KNOBI_CORE                         /* (knobi_core: per instance, knobi_core.c) */
uint32_t eng_fake_ticks, eng_fake_per_voice;
#endif
static uint32_t eng_ticks(void) { return eng_fake_ticks; }
#else
#if FM1_TICKS_PER_US != 24
#error "ENG_TICKS_PER_FRAME_X10 is computed for 24 ticks per microsecond"
#endif
static uint32_t eng_ticks(void) { return fm1_ticks(); }
#endif

#if KNOBI_CPU1
/* ---- the second core (a KNOBI_CPU1 build with the DUAL CORE global on; docs/API.md "Second core"): voices.c's
 * SYNTH_SPLIT is eng_split (app.c defines it before voices.c). Each control period, inside the audio interrupt: the
 * busy voices split (synth_balance: CPU0 the smaller half), set B posted to CPU1 (cpu1_job_voices into its own bus),
 * set A rendered here, the join, CPU1's bus added (int32 sums: bit-exact with one core in any split). Fewer than two
 * voices busy, DUAL CORE off, or CPU1 off: every voice here, as the default build. A join that fails (the job late or
 * lost: the HAL has retired CPU1, held until power-off) renders set B here in the same period, so the block is whole
 * (a late job may have run part of it: one block can glitch), and from then on one core (eng_c1_failed; Globals shows
 * "C1 FAIL"). The job runs from the flash: every job is joined before the interrupt returns, so a flash erase or
 * write (the main loop's, interrupts off) always finds CPU1 idle in its RAM loop (hal/fm1_flash_access.h).
 * Cache lines: 32 bytes on the AC791N (the SDK's include_lib/driver/cpu/wl82/asm/cache.h CACHE_LINE_COUNT, Melodee's
 * and felucca-bsp's mailbox lines). voice_t is a whole number of lines and synth_t.voice[] starts on one
 * (SYNTH_VOICE_ALIGN), so a voice of set A and one of set B never share a line; CPU1's bus and its time counter are
 * on lines of their own, away from CPU0's words. (The cores share the caches, and internal SRAM needs no cache
 * maintenance, only the HAL's csync for the order: the alignment keeps them off each other's lines all the same.)
 * The CPU guard (synth_guard) keeps timing CPU0's whole render, the joins' waits included: the time the block takes.
 * CPU1's own job time (TIMER4 on CPU1) is eng_c1_load, % of the block, for the Globals line ("C1 ON 40%"). */
#define ENG_C1_LINE 32u                   /* bytes: the AC791N's cache line */
#define ENG_C1_JOIN_TICKS (1500u * FM1_TICKS_PER_US)   /* a period's join, from its post: a 16-frame period is 363 us of
                                           * audio; set B's share of a heavy period takes ~100..200 us in the emulator */
/* One deadline per audio block (final review 3): a block holds 16 periods (and the arpeggiator's sub-blocks), so a CPU1
 * slow but not hung (jobs just under ENG_C1_JOIN_TICKS) would stretch every block to ~16 x 1.5 ms and never be
 * retired. eng_c1_block() (eng_render's start, or a test render's) opens the block; each join then waits at most
 * min(ENG_C1_JOIN_TICKS from its post, what is left to the block's deadline ENG_C1_BLOCK_TICKS after its start, what
 * is left of ENG_C1_WAIT_MAX, the joins' total wait a block may take: 50 %). A join that runs out is a timeout like
 * any other (the HAL retires CPU1, LATE; set B rendered here; one core from then on): a slow CPU1 is retired within
 * the first block it is slow in, which overruns by at most the wait it was allowed, once. A CPU1 whose joins wait
 * less than half a block in all is kept (it still saves CPU0 its share). The CPU1 probe's dual renders
 * (cpu1_probe.c, after eng_render in the same interrupt) count against the same block. */
#define ENG_C1_BLOCK_TICKS (5805u * FM1_TICKS_PER_US)   /* one audio block: 256 frames at 44.1 kHz */
#define ENG_C1_WAIT_MAX (ENG_C1_BLOCK_TICKS / 2u)        /* the joins' total wait in a block: 50 % of it */
_Static_assert(sizeof(voice_t) % ENG_C1_LINE == 0u, "voice_t: a whole number of cache lines (SYNTH_VOICE_ALIGN)");
_Static_assert(__builtin_offsetof(synth_t, voice) % ENG_C1_LINE == 0u, "synth_t.voice on a cache line");
static struct eng_c1 {
    synth_t *s;                           /* CPU0's, written before each post */
    uint32_t mask, n;
    int32_t bus[2u * VOICE_CTL] __attribute__((aligned(32)));   /* CPU1's: set B's period */
    uint32_t ticks __attribute__((aligned(32)));                /* CPU1's: its jobs' time, summed (TIMER4 ticks) */
} eng_c1 __attribute__((aligned(32)));
static volatile uint8_t eng_dual;         /* producer (main loop): the DUAL CORE global */
static volatile uint8_t eng_c1_failed;    /* consumer: a join failed: one core until power-off */
static volatile uint32_t eng_c1_jobs;     /* consumer: periods set B rendered on CPU1 */
static volatile uint16_t eng_c1_load;     /* consumer: CPU1's job time, % of the block, smoothed as eng_load */
static uint32_t eng_c1_load_q3, eng_c1_seen;
static uint32_t eng_c1_t0, eng_c1_wait;   /* consumer: the block's start (eng_ticks), its joins' wait so far */
static void eng_c1_block(void) { eng_c1_t0 = eng_ticks(), eng_c1_wait = 0; }   /* a block starts (eng_render) */
#if KNOBI_CPU1_TEST == 7 || KNOBI_CPU1_TEST == 9 || KNOBI_CPU1_TEST == 10
#define ENG_C1_HANG_AT 1000u              /* (fm1-emu cases dualfail / dualmid: the 1000th voice job never returns; slow:
                                           * from the 1000th on, every job is slow) */
static uint32_t eng_c1_test_jobs;
static volatile uint32_t eng_c1_test_spin;
#endif
#if KNOBI_CPU1_TEST == 10
#define ENG_C1_SLOW_TICKS (1200u * FM1_TICKS_PER_US)   /* (slow: a job spins this long from its start, then renders:
                                           * ~1.3 ms in all, under ENG_C1_JOIN_TICKS: slow, never hung) */
static volatile uint8_t eng_c1_test_slow; /* (CPU1's: a slow job has started) */
#endif

static void cpu1_job_voices(void *arg)    /* on CPU1 (from the flash): set B of this period into its own bus */
{
    struct eng_c1 *j = arg;
    uint32_t t0 = eng_ticks(), n = j->n;
#if KNOBI_CPU1_TEST == 7
    if (++eng_c1_test_jobs == ENG_C1_HANG_AT)
        for (;;) eng_c1_test_spin++;      /* (before it touches a voice: the fallback's output stays exact) */
#endif
#if KNOBI_CPU1_TEST == 10
    if (++eng_c1_test_jobs >= ENG_C1_HANG_AT) {   /* (slow: before it touches a voice, so a join that gives up on it
                                                   * leaves the output exact. fm1-emu does not hold a retired core
                                                   * (C1_CON): the job stops itself once CPU0 has retired it, as the
                                                   * chip's hold would have stopped it there) */
        eng_c1_test_slow = 1;
        while (eng_ticks() - t0 < ENG_C1_SLOW_TICKS) {
            fm1_cpu1_sync();
            if (!fm1_cpu1_on) return;
            eng_c1_test_spin++;
        }
    }
#endif
    for (uint32_t i = 0; i < 2u * n; i++) j->bus[i] = 0;
#if KNOBI_CPU1_TEST == 9
    if (++eng_c1_test_jobs == ENG_C1_HANG_AT) {   /* (dualmid: one voice of set B rendered, then the hang: CPU0 renders
                                                   * set B again from that voice's advanced state) */
        synth_render_set(j->s, j->mask & (0u - j->mask), j->bus, n);
        for (;;) eng_c1_test_spin++;
    }
#endif
    synth_render_set(j->s, j->mask, j->bus, n);
    j->ticks += eng_ticks() - t0;
}
#if KNOBI_CPU1_PROBE
/* the CPU1 probe build (cpu1_probe.c): its own test renders force one core (0) or two (1) whatever DUAL CORE says;
 * -1 (outside them): DUAL CORE decides, as in any KNOBI_CPU1 build */
static volatile int8_t eng_split_force = -1;
static uint32_t eng_probe_ticks;          /* the probe's own time in the interrupt last block: the guard counts it too */
#define ENG_DUAL_NOW() (eng_split_force < 0 ? eng_dual : (uint8_t)eng_split_force)
#else
#define ENG_DUAL_NOW() eng_dual
#endif
static void eng_split(synth_t *s, int32_t *bus, uint32_t n)
{
    uint32_t a, b;
    if (ENG_DUAL_NOW() && !eng_c1_failed && synth_balance(s, &a, &b) >= 2u) {
        eng_c1.s = s, eng_c1.mask = b, eng_c1.n = n;
        if (cpu1_post(cpu1_job_voices, &eng_c1) == 0) {
            uint32_t tp = eng_ticks(), tj, el, left, lim;
            int r;
            synth_render_set(s, a, bus, n);
            tj = eng_ticks();                 /* the join's limit, from the post (the HAL counts from its own post,
                                               * a little earlier: never later than this) */
            el = tj - eng_c1_t0;
            left = el >= ENG_C1_BLOCK_TICKS ? 0u : ENG_C1_BLOCK_TICKS - el;   /* to the block's deadline */
            if (eng_c1_wait >= ENG_C1_WAIT_MAX) left = 0;
            else if (ENG_C1_WAIT_MAX - eng_c1_wait < left) left = ENG_C1_WAIT_MAX - eng_c1_wait;   /* the wait left */
            lim = tj - tp + left;
            if (lim > ENG_C1_JOIN_TICKS) lim = ENG_C1_JOIN_TICKS;
            r = cpu1_join(lim);
            eng_c1_wait += eng_ticks() - tj;
            if (r < 0) {                      /* late or lost: set B here, one core from now on. A job that
                                           * stopped part way (late) left some of set B advanced by this period: they
                                           * render it again from there, a jump of up to 16 frames in their waveform,
                                           * once (fm1-emu case dualmid: the step stays within the clean run's) */
                eng_c1_failed = 1;
                synth_render_set(s, b, bus, n);
                return;
            }
            fm1_cpu1_sync();              /* (CPU1's writes, its bus and set B's voices, before CPU0 reads them) */
            for (uint32_t i = 0; i < 2u * n; i++) bus[i] += eng_c1.bus[i];
            eng_c1_jobs = eng_c1_jobs + 1u;
            return;
        }
    }
    synth_render_set(s, SYNTH_ALL_VOICES, bus, n);
}
static void eng_c1_cpu(uint32_t budget)   /* (consumer, after the block's joins: CPU1 is idle) */
{
    uint32_t t = eng_c1.ticks, d = t - eng_c1_seen, pct;
    eng_c1_seen = t;
    pct = d >= budget * 10u ? ENG_LOAD_MAX : d * 100u / budget;
    if (pct > ENG_LOAD_MAX) pct = ENG_LOAD_MAX;
    eng_c1_load_q3 += pct - (eng_c1_load_q3 >> 3);
    eng_c1_load = (uint16_t)(eng_c1_load_q3 >> 3);
}
/* main loop: 0 off (DUAL CORE off), 1 on (CPU1 renders), 2 FAIL (on, but CPU1 retired or never started) */
static int eng_c1_status(void)
{
    int st;
    if (!eng_dual) return 0;              /* (off is OFF, whatever happened to CPU1) */
    st = cpu1_state();
    return eng_c1_failed || st == CPU1_FAIL || st == CPU1_OFF ? 2 : 1;
}
#endif

/* The load, for the Globals screen's diagnostics line (consumer writes, main loop reads; each a single aligned word or
 * byte): eng_load the block's load in % of its time (used x 100 / budget, up to 999), smoothed (a one-pole of 8
 * blocks, ~46 ms); eng_peak the highest block's, decaying toward the load with a time constant of 128 blocks
 * (~0.74 s: a peak is gone in about a second); eng_vlimit the CPU guard's voice limit; eng_sheds its sheds since
 * eng_init. */
static void eng_cpu(uint32_t used, uint32_t budget)
{
    uint32_t pct;
    if (!budget) return;
    pct = used >= budget * 10u ? ENG_LOAD_MAX : used * 100u / budget;   /* (32-bit: used < 10 budget, ~1.4 M ticks) */
    if (pct > ENG_LOAD_MAX) pct = ENG_LOAD_MAX;
    eng_load_q3 += pct - (eng_load_q3 >> 3);
    if (pct << 8 >= eng_peak_q8) eng_peak_q8 = pct << 8;
    else eng_peak_q8 -= (eng_peak_q8 - (pct << 8)) >> 7;
    eng_load = (uint16_t)(eng_load_q3 >> 3);
    eng_peak = (uint16_t)((eng_peak_q8 + 128u) >> 8);
    eng_vlimit = eng_synth.vlimit;
    eng_sheds = eng_synth.sheds;
}

/* the block's render with the arpeggiator and the sequencer (one at a time: exclusive): split at each clock step's
 * frame (arp_step / seq_step there: their notes start on that sample) and at the end of their notes' gate (arp_run /
 * seq_run: the note-offs on that sample). synth_render takes any frame count and keeps its control period across the
 * calls. With both off and no note of their own: one render, as before */
static void eng_render_steps(int32_t *lr, uint32_t frames)
{
    uint32_t pos = 0, k = 0, n = eng_clk.n;
    if (!eng_arp.on && eng_arp.note < 0 && !eng_seqp.on && !eng_seqp.n) { synth_render(&eng_synth, lr, frames); return; }
    for (;;) {
        uint32_t end = frames, g;
        while (k < n && eng_clk.at[k] <= pos) {   /* a step here: the step's length for the gate (50 %) */
            uint32_t ticks = CLOCK_DIV_TICKS[eng_clk.div][eng_clk.step[k] & 1u], rate = eng_clk.rate;
            eng_arp.step_len = eng_seqp.step_len = rate ? ticks * CLOCK_U / rate : 0u;   /* (48 x CLOCK_U < 2^29) */
            arp_step(&eng_arp, &eng_synth);
            seq_step(&eng_seqp, &eng_synth);
            k++;
        }
        if (pos >= frames) break;
        if (k < n && eng_clk.at[k] < end) end = eng_clk.at[k];
        g = arp_until(&eng_arp);
        if (g < end - pos) end = pos + g;
        g = seq_until(&eng_seqp);
        if (g < end - pos) end = pos + g;
        synth_render(&eng_synth, lr + 2u * pos, end - pos);
        arp_run(&eng_arp, &eng_synth, end - pos);
        seq_run(&eng_seqp, &eng_synth, end - pos);
        pos = end;
    }
}

/* eng_render's 'used' is wall-clock ticks around the render (eng_render_steps): it includes the time of any interrupt
 * that preempted it (USB, the key scan), which is time the block did not have either, so the guard counts it too. */
static void eng_render(int32_t *lr, uint32_t frames)   /* app_audio: adds into L R pairs, Q15 (then the fade) */
{
    int32_t g = eng_fade_g, tgt;
    if (!eng_ready) return;
    eng_drain();
    clock_tick(&eng_clk, frames);              /* this block's steps (eng_clk.n, .at[]: the arpeggiator's) */
    eng_clk_ticks = eng_clk.ticks, eng_clk_start_ticks = eng_clk.start_ticks, eng_clk_beats = eng_clk.beats;
    eng_clk_mode = eng_clk.mode;
    ENG_BARRIER();
    eng_clk_starts = eng_clk.starts;           /* (last: the main loop's consistency check) */
    eng_clk_run = (uint8_t)clock_running(&eng_clk), eng_clk_alive = eng_clk.alive;
    eng_clk_bpm10 = (uint16_t)clock_bpm_x10(&eng_clk);
    synth_clock(&eng_synth, clock_quarter_inc(&eng_clk));   /* LFO SYNC's tempo (a compare when unchanged) */
    if (!eng_clk_run) {                        /* (reference 5: no clock, no arp, no sequence; no note left held. The
                                                * sequence keeps its position: an external Continue goes on from it) */
        arp_silence(&eng_arp, &eng_synth);
        seq_silence(&eng_seqp, &eng_synth);
    }
    uint32_t t0 = eng_ticks();
#if KNOBI_CPU1
    eng_c1_block();                            /* (the joins' deadline: this block's) */
#endif
    eng_render_steps(lr, frames);
    eng_seq_state = (uint8_t)(!eng_seqp.on ? 0 : eng_clk_run ? 1 : 2);
    eng_seq_cur = eng_seqp.cur;
#ifdef DINGHY_HOST
    eng_fake_ticks += eng_fake_per_voice * (uint32_t)synth_active(&eng_synth);
#endif
    uint32_t used = eng_ticks() - t0, budget = frames * ENG_TICKS_PER_FRAME_X10 / 10u;
#if KNOBI_CPU1_PROBE
    used += eng_probe_ticks;               /* (the CPU1 probe's checks of the block before: in the CPU line and the
                                            * guard, so a crackle from the probe's cost sheds voices, as any load) */
    eng_probe_ticks = 0;
#endif
    synth_guard(&eng_synth, used, budget);
    eng_cpu(used, budget);
#if KNOBI_CPU1
    eng_c1_cpu(budget);
#endif
    eng_voices = (uint8_t)synth_active(&eng_synth);
    tgt = eng_fade_out ? 0 : ENG_FADE_ONE;
    if (g == ENG_FADE_ONE && tgt == ENG_FADE_ONE) return;   /* (the usual case: nothing to do) */
    if (g == 0 && tgt == 0) {                  /* silent the whole block */
        for (uint32_t i = 0; i < 2u * frames; i++) lr[i] = 0;
        if (eng_fade_quiet < 255u) eng_fade_quiet = (uint8_t)(eng_fade_quiet + 1u);
        return;
    }
    eng_fade_quiet = 0;
    for (uint32_t i = 0; i < frames; i++) {    /* a straight line to the target, a step a frame */
        g = g < tgt ? (g + ENG_FADE_STEP > tgt ? tgt : g + ENG_FADE_STEP) : g > tgt ? (g - ENG_FADE_STEP < tgt ? tgt : g - ENG_FADE_STEP) : g;
        lr[2u * i] = (int32_t)((int64_t)lr[2u * i] * g >> 15);   /* (64-bit: whatever lr holds, no overflow) */
        lr[2u * i + 1u] = (int32_t)((int64_t)lr[2u * i + 1u] * g >> 15);
    }
    eng_fade_g = g;
}
#endif
