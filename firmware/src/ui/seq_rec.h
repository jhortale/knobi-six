/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX step entry: the sequencer's recorder (Phase 3 Task 5; docs/ob6-reference.md 5 "Recording"; the spec's
 * Behaviour, Recording). Pure: it edits the seq_t it is given through the model's ops (synth/seq.h seq_rec_*), nothing
 * else; ui/controls.c owns one (ctl_rec) and hands it the edit buffer's sequence (params/prog_seq.h edit_seq_mut).
 * Main loop only; integers only. Unit-tested on the host (tests/unit/test_seqrec.c).
 *
 * The notes come from the keys (the note after the octave buttons' shift: the shift is baked into the stored note) and
 * from MIDI in, as note on (vel 1..127) / note off (vel 0); one set of notes held for both, by note number. Unison and
 * chord memory are applied at playback (as live playing), never recorded: the caller passes the note played.
 *
 * The state machine:
 *   off        seqrec_start: the sequence cleared (recording replaces it, from step 1), on.
 *   on         a note on adds the note to the chord being entered (notes outside 12..108 are held but not added; a
 *              note already in it is not added twice; at most 6). The chord is recorded as one step (each note at its
 *              own velocity) when every note held is up: so a chord is entered by keeping at least one key held while
 *              adding notes (the OB-6's rule); a note pressed and released alone is a step of one note.
 *              INC with notes held: the chord held is recorded now (if it was not yet) and a tie is added after it; each
 *              further INC while they stay held adds one more tie (the step held on, extended). Their release then adds
 *              nothing (the chord is in). A new note pressed after that, while some are still held, starts the next
 *              chord (recorded when every note is up).
 *              INC with nothing held (or only notes that are not recorded): a rest.
 *              DEC: the last step recorded is erased (seq_rec_back); a chord being entered is not touched.
 *              A note off for a note not held (held since before the start) is ignored.
 *   full       the step that makes the 64th ends the recording (on = 0): recording stops at 64 steps.
 *   seqrec_stop: a chord still being entered is recorded first (if there is room), then off. seqrec_abort: off, the
 *              chord being entered forgotten (the sequence is being replaced: a program change).
 * Ops while off do nothing and return 0. Returns: 1 the sequence changed, 0 not, -1 refused (DEC on an empty sequence,
 * a step that cannot be recorded). */
#ifndef KNOBI_SEQ_REC_H
#define KNOBI_SEQ_REC_H
#include <stdint.h>
#include "../synth/seq.h"

typedef struct seqrec {
    uint8_t on;                          /* recording */
    uint8_t n;                           /* the chord being entered: notes so far (in range, not yet recorded) */
    uint8_t notes[SEQ_MAX_NOTES], vels[SEQ_MAX_NOTES];
    uint8_t in;                          /* the chord held is recorded already (INC tied it): its release adds nothing */
    uint32_t held[4];                    /* notes down since the start (keys and MIDI), a bit a note */
} seqrec_t;

void seqrec_start(seqrec_t *r, seq_t *s);                    /* arm: s cleared, recording from step 1 */
int  seqrec_stop(seqrec_t *r, seq_t *s);                     /* end (a chord still held recorded); 1 s changed */
void seqrec_abort(seqrec_t *r);                              /* end, nothing more recorded */
int  seqrec_note(seqrec_t *r, seq_t *s, int note, int vel);  /* a note on (vel > 0) / off (0) */
int  seqrec_inc(seqrec_t *r, seq_t *s);                      /* INC: rest, or tie while notes are held */
int  seqrec_dec(seqrec_t *r, seq_t *s);                      /* DEC: the last step erased */
int  seqrec_held(const seqrec_t *r);                         /* notes held (recorded ones and not) */

#endif
