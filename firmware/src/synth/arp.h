/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX arpeggiator (docs/ob6-reference.md 2.12 and 5; the Phase 2 spec's "Our calls"). Integers only; no 64-bit
 * division; no libgcc. Unit-tested on the host (tests/unit/test_arp.c).
 *
 * Where it runs: engine.c owns one arp_t (eng_arp) on CPU0, in the audio interrupt, like the clock (synth/clock.h):
 * never on CPU1. With ARP ON, a key (the keybed's or MIDI's: engine.c's EV_NOTE) goes to arp_key, not to the synth: it
 * never sounds directly. The clock's step events (clk_t .n / .at[]) call arp_step at their frame in the block (engine.c
 * splits the render there: arp notes start sample-accurately), and arp_run counts the gate down between them (the
 * render is split at the gate's end too: the note-off is sample-accurate as well). Every note the arp plays goes
 * through synth_note, so poly, unison (with its key modes), portamento, chord memory and the CPU guard's voice limit
 * apply to it as to a key.
 *
 * The pattern: the keys held (and latched by HOLD), at most ARP_KEYS, in the order played. The cycle a step walks:
 *   UP      the notes ascending, then the same an octave up, two octaves up (OCTAVES 1..3: P_ARP_RANGE 0..2)
 *   DOWN    UP's cycle backwards (from the top of the top octave)
 *   UP+DN   UP, then back down without repeating the ends (C E G: C E G E, C E G E ...; one note: that note)
 *   RANDOM  a note of UP's cycle at random each step (xorshift32, seeded by arp_init: the same seed, the same run)
 *   ASSIGN  the keys in the order played, then the same an octave up ...
 * Notes over 127 are left out of the cycle. A key added to a running pattern joins the cycle where it falls; the
 * position goes on (wrapping if the cycle got shorter).
 * Gate: 50 % of the step (step_len, frames, set by the caller before arp_step: engine.c from the clock's tempo and the
 * division's step; 0 = unknown, an external clock with no tempo yet: the note then lasts until the next step). The
 * velocity is the played key's (the spec's "Our calls"; the reference leaves both open).
 *
 * HOLD (the HOLD pad / sustain: arp_hold; or the program's ARP HOLD, P_ARP_HOLD): the keys let go stay in the pattern
 * (latched). A key pressed while none is held starts a new pattern (relatch: the new chord replaces the old); a key
 * pressed while at least one is still held is appended. HOLD off: the keys not held leave the pattern.
 * A start: a key into an empty pattern, or a relatch: arp_key (and arp_set turning ARP ON with keys held) return 1 and
 * the pattern plays from its first note at the next step. ARP BEAT SYNC (the global) decides when that step comes: off,
 * engine.c restarts the clock (clock_restart: step 0 at once; an external clock owns the phase: the next step; MIDI
 * CLOCK OUT: never a restart, the next step, so the slaves get no 0xFA); on, the next step of the division (the
 * reference: "quantizes arp start to the current division").
 *
 * Silence: arp_silence (the clock not running: an external clock absent or stopped, reference 5) releases the note
 * sounding now and puts the pattern back to its start, keys kept (they play again when the clock runs). arp_clear
 * (all notes off, a program load, panic) also forgets every key. ARP OFF releases the arp's note and hands the keys
 * still held to the synth (they sound as played); ARP ON takes the keys the synth has held (and stops them sounding).
 * While ARP is on the synth's own HOLD is off (the arp latches instead; a held synth would latch the arp's note-offs).
 *
 * MIDI out: every arp note-on / off goes into out[] (note, velocity; 0 = off), a ring with a free-running write index
 * out_w (written last); the main loop reads it (ui/midi_map.c ctl_arp_out) and sends the notes when PARAM TX is on. */
#ifndef KNOBI_ARP_H
#define KNOBI_ARP_H
#include <stdint.h>
#include "voices.h"

#define ARP_KEYS 16u                     /* keys in a pattern at most (more: the oldest goes) */
#define ARP_SEQ (ARP_KEYS * 3u * 2u)     /* the longest cycle: 16 keys x 3 octaves, up and down */
#define ARP_OUT_N 64u                    /* the MIDI out ring (a power of two) */
#define ARP_NONE 0xFFFFFFFFu             /* arp_until: no gate running */

enum { ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_RANDOM, ARP_ASSIGN, ARP_MODES };

typedef struct arp {
    uint8_t on, mode, range;             /* P_ARP_ON, P_ARP_MODE (ARP_*), P_ARP_RANGE (0..2: 1..3 octaves) */
    uint8_t hold_prog, hold_ext;         /* P_ARP_HOLD; the HOLD pad / sustain (arp_hold) */
    uint8_t n;                           /* keys in the pattern */
    uint8_t key[ARP_KEYS], vel[ARP_KEYS], down[ARP_KEYS];   /* in the order played; down: held (else latched) */
    uint16_t pos;                        /* the next step's place in the cycle */
    int16_t note;                        /* the note sounding (its gate running), -1 none */
    uint32_t gate;                       /* frames left of it (0: until the next step) */
    uint32_t step_len;                   /* frames of the step about to start (the caller's; 0 unknown) */
    uint32_t rnd;                        /* RANDOM's xorshift32 */
    uint32_t steps;                      /* notes played (tests, probes) */
    uint8_t out[ARP_OUT_N][2];           /* MIDI out: note, velocity (0 = off) */
    volatile uint32_t out_w;             /* written after the entry (free running) */
} arp_t;

void arp_init(arp_t *a, uint32_t seed);              /* off, UP, 1 octave, no keys; seed: RANDOM's (0 -> 1) */
int  arp_key(arp_t *a, int note, int vel);           /* a key (vel 0: let go); 1 = a pattern starts (empty, relatch) */
void arp_step(arp_t *a, synth_t *s);                 /* a clock step: the note sounding off, the next one on */
void arp_run(arp_t *a, synth_t *s, uint32_t frames); /* time passes: the gate's end releases the note */
uint32_t arp_until(const arp_t *a);                  /* frames until the gate ends (ARP_NONE: none running) */
int  arp_set(arp_t *a, synth_t *s, uint16_t id, int v);   /* P_ARP_ON / MODE / RANGE / HOLD; 1 = a pattern starts */
void arp_hold(arp_t *a, int on);                     /* the HOLD pad / sustain (the synth's HOLD is the caller's) */
void arp_silence(arp_t *a, synth_t *s);              /* the note off now, the pattern back to its start */
void arp_clear(arp_t *a, synth_t *s);                /* arp_silence, and every key forgotten */
void arp_program(arp_t *a, synth_t *s, const int16_t *v);   /* a program loaded (after synth_program): its ARP values */
uint32_t arp_cycle(const arp_t *a, uint8_t *notes, uint8_t *vels);   /* the cycle (RANDOM: UP's); its length */

#endif
