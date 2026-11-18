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

#endif
