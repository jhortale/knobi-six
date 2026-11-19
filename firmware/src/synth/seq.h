/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX step sequencer: the sequence model, its recording ops and its packed encoding (docs/ob6-reference.md 5;
 * the Phase 3 spec's Encoding and Recording). Integers only; no allocation, no recursion, no 64-bit division, no
 * libgcc (the player added later runs in the audio interrupt). Unit-tested on the host (tests/unit/test_seq.c).
 *
 * The model (seq_t, 897 bytes: it lives in the edit buffer and its COMPARE copy): up to SEQ_MAX_STEPS (64) steps; each
 * step has count 0..6 notes (0 = a rest), a tie flag, notes[6] (12..108) and vels[6] (0..127), the first count used.
 *   rest  count 0, tie 0.
 *   tie   the previous step's notes held on into this step: the step carries the same notes and velocities (a copy)
 *         with tie set, so the player extends the gate instead of retriggering. A tie needs a previous step with
 *         count > 0 (a tie of a tie is fine, the chain is carried); after a rest or at step 1 seq_rec_tie refuses.
 *         A tie step always has count > 0.
 *   Velocity 0 is a legal stored value (the API stores what it is given; a recorded note-on never has it, that is a
 *   note-off, so the caller must not pass it).
 *
 * Recording ops return the new length, or -1 when refused (the sequence is then unchanged): full (64 steps); seq_rec_step
 * with no note left after notes outside 12..108 are dropped (not recorded as a rest; use seq_rec_rest), or a NULL
 * array or n < 1; seq_rec_tie with nothing to extend; seq_rec_back with nothing to erase. Notes are dropped before
 * counting; only the first 6 in-range notes are kept; a velocity above 127 is clamped to 127.
 *
 * Packed encoding (seq_pack / seq_unpack; the flash store's payload):
 *   byte 0       length L, 0..64
 *   then L steps, each: a header byte = count | tie << 3 (count 0..6; bits 4..7 reserved, must be 0), then count
 *                pairs of bytes (note, velocity)
 * An empty sequence is the one byte 00. A step takes 1 + 2 * count bytes: rest 1, a 6-note chord 13. The worst case
 * is SEQ_PACKED_MAX = 1 + 64 * 13 = 833 bytes. seq_unpack reads exactly what the length byte and the headers say and
 * ignores any bytes after (a padded buffer is fine); it rejects, returning -1 and leaving *s empty (length 0): a
 * buffer shorter than it needs (n = 0 included), length > 64, count > 6, a reserved header bit, a tie on a rest or
 * with no previous non-rest step, a note outside 12..108, a velocity above 127. */
#ifndef KNOBI_SEQ_H
#define KNOBI_SEQ_H
#include <stdint.h>

#define SEQ_MAX_STEPS 64u
#define SEQ_MAX_NOTES 6u
#define SEQ_NOTE_MIN 12
#define SEQ_NOTE_MAX 108
#define SEQ_PACKED_MAX (1u + SEQ_MAX_STEPS * (1u + 2u * SEQ_MAX_NOTES))

typedef struct seq_step {
    uint8_t count;                       /* notes in the step, 0..6 (0 = rest) */
    uint8_t tie;                         /* the previous step's notes held on into this one (count > 0) */
    uint8_t notes[SEQ_MAX_NOTES];        /* 12..108 */
    uint8_t vels[SEQ_MAX_NOTES];         /* 0..127 */
} seq_step_t;

typedef struct seq {
    uint8_t len;                         /* steps recorded, 0..64 */
    seq_step_t step[SEQ_MAX_STEPS];
} seq_t;

void seq_clear(seq_t *s);                                                    /* length 0, every step zeroed */
int  seq_rec_step(seq_t *s, const uint8_t *notes, const uint8_t *vels, int n);   /* a step of n notes; new length / -1 */
int  seq_rec_rest(seq_t *s);                                                 /* a rest; new length / -1 */
int  seq_rec_tie(seq_t *s);                                                  /* tie: the previous step held on; new length / -1 */
int  seq_rec_back(seq_t *s);                                                 /* erase the last step; new length / -1 */
uint32_t seq_packed_size(const seq_t *s);                                    /* bytes seq_pack writes (1 for empty) */
int  seq_pack(const seq_t *s, uint8_t *buf, uint32_t max);                   /* bytes written, -1: no room / invalid */
int  seq_unpack(seq_t *s, const uint8_t *buf, uint32_t n);                   /* 0 / -1 (see above) */

/* ---- The player (synth/seqplay.c; Phase 3 Task 4). It plays through synth_note, so it lives with the engine: engine.c
 * includes seqplay.c after arp.c (this model, seq.c, stays pure: test_seq.c builds it alone). Where it runs: engine.c
 * owns one seqp_t (eng_seqp) on CPU0, in the audio interrupt, like the arp (synth/arp.h): never on CPU1. It plays the
 * engine's own copy of the edit buffer's sequence (eng_seqd: the main loop hands every change over through the event
 * queue, engine.c eng_seq_load; the interrupt never reads the main loop's edit_seq()), so a sequence changing under it
 * is never read half written.
 *
 * Steps: the clock's step events (clk_t .n / .at[] / .step[]) call seq_step at their frame in the block (engine.c
 * splits the render there, as for the arp: the notes start sample-accurately), and seq_run counts the gate down between
 * them (the render is split at the gate's end too: the note-offs are sample-accurate as well). One clock step = one
 * sequence step; it loops the recorded length (after the last step, step 1).
 *   a step       the notes sounding off, the step's notes on (velocity 0, a legal stored value, plays as 1: a note-on
 *                of velocity 0 would be a note-off); a note twice in a step plays once.
 *   rest         the notes sounding off, nothing on.
 *   note-offs    synth_note_release, never synth_note(.., 0): HOLD (the pad, sustain) latches the live keys only, never
 *                the sequence's notes (controller ruling, Task 4 fix 1), so they neither pile up latched nor sound on
 *                after a stop; in unison the line follows the steps.
 *   tie          the notes sounding stay on (no retrigger, no MIDI out); the step's own notes are ignored. With nothing
 *                sounding (a Continue that lands on a tie, its notes released at the pause) the tie step plays its
 *                notes (it carries a copy of them).
 *   gate         50 % of the step (step_len, frames, set by the caller before seq_step: engine.c from the clock's tempo
 *                and the division's step, as the arp's; 0 = unknown: until the next step). A step followed by a tie
 *                keeps its notes on until the next step (gate 0), so a tied chain is one note from its first step to
 *                50 % of its last tie step.
 * The position (pos, the next step) never indexes past the sequence's length: a sequence that got shorter under the
 * player (a new one handed over while it plays) is followed, the position wrapping to step 1 when it is past the end;
 * an empty sequence plays nothing (its notes released at the next step). The notes sounding are the player's own list
 * (note[], n), so every note-on gets its note-off, locally and in the MIDI ring, whatever the sequence became.
 *
 * Transpose (seq_transpose): semitones added to every note from the next non-tie step (notes sounding keep their pitch
 * and their note-offs; a tied chain keeps its pitch to its end). Clamped to -127..+127. A transposed note outside the
 * MIDI range 0..127 is dropped (the step's other notes still play); notes in range are played as they are, even below
 * 12 or above 108 (the recording range, not the synth's: the synth plays 0..127). The UI's mapping: transpose = the
 * key pressed - SEQ_XP_CENTER (middle C, 60 = none). Not stored; engine.c resets it on a program load.
 *
 * Play (seq_play): on arms (from step 1: it returns 1 = a start, the caller applies the arp's start rules: engine.c
 * eng_arp_start); off stops and puts the position back to step 1 (the caller releases the notes first: seq_silence).
 * seq_silence releases the notes sounding now and keeps the position (the clock paused or gone: a Continue goes on from
 * the held step). seq_ext takes an external clock's realtime bytes: 0xFA = from step 1, 0xFB = on from the held step,
 * 0xFC = pause (the clock stops, engine.c silences; the position kept).
 *
 * MIDI out: every note-on / off the player makes goes into the arpeggiator's ring (out: arp.c arp_out; one ring, one
 * reader, ui/midi_map.c ctl_arp_out, one pairing: PARAM TX and LOCAL ON, each note-off on its note-on's channel). The
 * arp and the sequencer never play together (engine.c: turning one on turns the other off). out may be NULL (no MIDI). */
#define SEQ_XP_CENTER 60                 /* middle C: the transpose key that adds nothing */
#define SEQ_NONE 0xFFFFFFFFu             /* seq_until: no gate running */
#define SEQ_NO_STEP 0xFFu                /* seqp_t.cur: no step played since the start */
struct synth;
struct arp;
typedef struct seqp {
    const seq_t *seq;                    /* the sequence played (the engine's copy) */
    struct arp *out;                     /* the MIDI out ring (the arp's), NULL none */
    uint8_t on;                          /* PLAY on (armed: it steps whenever the clock does) */
    uint8_t pos;                         /* the next step to play, < seq->len (wrapped at seq_step) */
    uint8_t cur;                         /* the step played last (the screen's), SEQ_NO_STEP none since the start */
    int8_t xp;                           /* transpose, semitones */
    uint8_t n;                           /* notes sounding */
    uint8_t note[SEQ_MAX_NOTES];         /* their notes (transposed: as sent) */
    uint32_t gate;                       /* frames left of them (0: until the next step) */
    uint32_t step_len;                   /* frames of the step about to start (the caller's; 0 unknown) */
    uint32_t steps;                      /* steps played (tests, probes) */
} seqp_t;

void seqp_init(seqp_t *p, const seq_t *seq, struct arp *out);   /* stopped, step 1, no transpose */
int  seq_play(seqp_t *p, int on);                    /* PLAY on / off; 1 = a start (it was off) */
void seq_step(seqp_t *p, struct synth *s);           /* a clock step: the step's notes (see above) */
void seq_run(seqp_t *p, struct synth *s, uint32_t frames);   /* time passes: the gate's end releases the notes */
uint32_t seq_until(const seqp_t *p);                 /* frames until the gate ends (SEQ_NONE: none running) */
void seq_silence(seqp_t *p, struct synth *s);        /* the notes off now; the position kept */
void seq_transpose(seqp_t *p, int semis);            /* from the next step; -127..127 */
void seq_ext(seqp_t *p, uint8_t status);             /* 0xFA from step 1, 0xFB on, 0xFC pause */

#endif
