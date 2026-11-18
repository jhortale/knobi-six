/* SPDX-License-Identifier: GPL-3.0-only */
/* The sequencer's step entry (firmware/src/ui/seq_rec.c, Phase 3 Task 5): the brief's rec_keys_and_midi (a chord by
 * keeping a note held; notes from two sources, keys and MIDI, by note number), rec_octave_baked (the note given is the
 * note stored: the caller passes it after the octave shift; out of 12..108 not recorded), rec_inc_dec (rest, tie by
 * repeated INC while holding, DEC back), rec_stop_at_64, plus start / stop / abort. The whole path (keys, MIDI in, the
 * buttons, WRITE, a power cycle) is tests/scripts/seq_rec.txt. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "synth/seq.c"
#include "ui/seq_rec.c"

static int fails;
static seq_t the_seq;
static int muts;                                           /* edit_seq_mut's calls (each marks the sequence edited) */
static seq_t *mut(void) { muts++; return &the_seq; }
#define s the_seq
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int has(const seq_step_t *st, int note, int vel)
{
    for (int k = 0; k < st->count; k++) if (st->notes[k] == note && st->vels[k] == vel) return 1;
    return 0;
}

/* the lazy clear (controller ruling, fix round 1): arming touches nothing; a recording that records nothing leaves the
 * sequence as it was and unedited (mut never called); the first op that records replaces it */
static void rec_lazy_clear(void)
{
    seqrec_t r;
    static const uint8_t n[1] = {60}, v[1] = {100};
    memset(&r, 0, sizeof r);
    seq_clear(&s);
    seq_rec_step(&s, n, v, 1), seq_rec_step(&s, n, v, 1);
    CHECK(seqrec_note(&r, 62, 90) == 0 && s.len == 2);     /* off: nothing */
    CHECK(seqrec_inc(&r) == 0 && seqrec_dec(&r) == 0 && s.len == 2);
    muts = 0;
    seqrec_start(&r, mut);
    CHECK(r.on == 1 && seqrec_fresh(&r) && s.len == 2 && muts == 0);   /* armed: untouched */
    CHECK(seqrec_stop(&r) == 0 && !r.on && s.len == 2 && muts == 0);    /* SEQ REC twice: unchanged, not edited */
    seqrec_start(&r, mut);
    seqrec_note(&r, 64, 80);                               /* a note held: nothing recorded yet */
    CHECK(s.len == 2 && muts == 0 && seqrec_fresh(&r));
    seqrec_note(&r, 64, 0);                                /* its release: the first step replaces the sequence */
    CHECK(s.len == 1 && s.step[0].notes[0] == 64 && muts == 1 && !seqrec_fresh(&r));
    seqrec_abort(&r);
    seq_rec_step(&s, n, v, 1);
    seqrec_start(&r, mut);
    CHECK(seqrec_inc(&r) == 1 && s.len == 1 && s.step[0].count == 0);   /* INC first: a rest, from step 1 */
    seqrec_abort(&r);
    seqrec_start(&r, mut);
    CHECK(seqrec_dec(&r) == -1 && s.len == 0);             /* DEC first: replaced (empty), nothing to erase */
    seqrec_abort(&r);
    seq_rec_step(&s, n, v, 1);
    seqrec_start(&r, mut);
    seqrec_note(&r, 67, 100);
    CHECK(seqrec_stop(&r) == 1 && s.len == 1 && s.step[0].notes[0] == 67);   /* a chord held at the end: replaces */
    printf("rec_lazy_clear: ok\n");
}

/* fix round 1, minor 5: DEC that erases the step of a chord tied in and still held gives the chord back as being
 * entered: its release records it anew (never lost), INC records and ties it anew (never a tie onto an earlier step) */
static void rec_dec_after_tie(void)
{
    seqrec_t r;
    memset(&r, 0, sizeof r);
    seqrec_start(&r, mut);
    seqrec_note(&r, 48, 100), seqrec_note(&r, 48, 0);     /* step 1: C3 */
    seqrec_note(&r, 60, 90);                               /* C4 held */
    seqrec_inc(&r);                                        /* C4, tie */
    CHECK(s.len == 3 && s.step[1].notes[0] == 60 && s.step[2].tie);
    seqrec_dec(&r);                                        /* the tie */
    CHECK(s.len == 2 && r.in);
    seqrec_dec(&r);                                        /* C4's own step */
    CHECK(s.len == 1 && !r.in && r.n == 1);
    CHECK(seqrec_inc(&r) == 1 && s.len == 3 && s.step[1].count == 1 && s.step[1].notes[0] == 60 &&
          s.step[1].vels[0] == 90 && !s.step[1].tie && s.step[2].tie && s.step[2].notes[0] == 60);
    seqrec_dec(&r), seqrec_dec(&r);                        /* back to C3 alone, C4 still held */
    CHECK(s.len == 1);
    seqrec_note(&r, 60, 0);                                /* its release records it */
    CHECK(s.len == 2 && s.step[1].notes[0] == 60 && !s.step[1].tie);
    seqrec_note(&r, 62, 100), seqrec_inc(&r);              /* D4 tied, then DEC x3 (tie, D4, C4): C3 */
    seqrec_dec(&r), seqrec_dec(&r), seqrec_dec(&r);
    CHECK(s.len == 1 && s.step[0].notes[0] == 48);
    seqrec_note(&r, 62, 0);
    CHECK(s.len == 2 && s.step[1].notes[0] == 62);         /* D4 back at its release */
    printf("rec_dec_after_tie: len %d\n", s.len);
}

static void rec_keys_and_midi(void)
{
    seqrec_t r;
    memset(&r, 0, sizeof r);
    seq_clear(&s);
    seqrec_start(&r, mut);
    /* a single note: pressed, released: one step */
    CHECK(seqrec_note(&r, 60, 100) == 0 && s.len == 0); /* (recorded at its release) */
    CHECK(seqrec_note(&r, 60, 0) == 1 && s.len == 1);
    CHECK(s.step[0].count == 1 && has(&s.step[0], 60, 100) && !s.step[0].tie);
    /* a chord: C held, E added, C let go (E still held), G added (from MIDI: same path), all up: one step of 3 */
    seqrec_note(&r, 48, 90);
    seqrec_note(&r, 52, 80);
    CHECK(seqrec_note(&r, 48, 0) == 0 && s.len == 1);   /* one still held: not yet */
    seqrec_note(&r, 55, 70);
    CHECK(seqrec_held(&r) == 2);
    seqrec_note(&r, 52, 0);
    CHECK(s.len == 1);
    CHECK(seqrec_note(&r, 55, 0) == 1 && s.len == 2);
    CHECK(s.step[1].count == 3 && has(&s.step[1], 48, 90) && has(&s.step[1], 52, 80) && has(&s.step[1], 55, 70));
    /* the same note twice in a chord (a key and a MIDI note on the same number): once */
    seqrec_note(&r, 64, 100);
    seqrec_note(&r, 64, 50);
    seqrec_note(&r, 64, 0);
    CHECK(s.len == 3 && s.step[2].count == 1 && has(&s.step[2], 64, 100));
    /* seven notes held: the first six recorded */
    for (int k = 0; k < 7; k++) seqrec_note(&r, 60 + k, 100 - k);
    for (int k = 0; k < 7; k++) seqrec_note(&r, 60 + k, 0);
    CHECK(s.len == 4 && s.step[3].count == 6 && !has(&s.step[3], 66, 94) && has(&s.step[3], 65, 95));
    /* a note off for a note held since before the start: ignored */
    CHECK(seqrec_note(&r, 30, 0) == 0 && s.len == 4);
    printf("rec_keys_and_midi: len %d, step 2 count %d, step 4 count %d\n", s.len, s.step[1].count, s.step[3].count);
}

static void rec_octave_baked(void)
{
    seqrec_t r;
    memset(&r, 0, sizeof r);
    seq_clear(&s);
    seqrec_start(&r, mut);
    /* the caller's note (F3 at OCTAVE +2 = 53 + 24 = 77) is the note stored */
    seqrec_note(&r, 77, 100), seqrec_note(&r, 77, 0);
    CHECK(s.len == 1 && s.step[0].notes[0] == 77);
    /* outside 12..108: held, not recorded; a step of only such notes is not a step */
    seqrec_note(&r, 11, 100), seqrec_note(&r, 109, 100);
    CHECK(seqrec_held(&r) == 2);
    seqrec_note(&r, 11, 0), seqrec_note(&r, 109, 0);
    CHECK(s.len == 1 && seqrec_held(&r) == 0);
    /* mixed: the in-range ones */
    seqrec_note(&r, 10, 100), seqrec_note(&r, 108, 90), seqrec_note(&r, 12, 80);
    seqrec_note(&r, 10, 0), seqrec_note(&r, 108, 0), seqrec_note(&r, 12, 0);
    CHECK(s.len == 2 && s.step[1].count == 2 && has(&s.step[1], 108, 90) && has(&s.step[1], 12, 80));
    printf("rec_octave_baked: %d %d steps\n", s.step[0].notes[0], s.len);
}

static void rec_inc_dec(void)
{
    seqrec_t r;
    memset(&r, 0, sizeof r);
    seq_clear(&s);
    seqrec_start(&r, mut);
    CHECK(seqrec_inc(&r) == 1 && s.len == 1 && s.step[0].count == 0);   /* nothing held: a rest */
    /* C held, INC three times: C, tie, tie, tie; its release adds nothing */
    seqrec_note(&r, 60, 100);
    CHECK(seqrec_inc(&r) == 1 && s.len == 3);
    CHECK(s.step[1].count == 1 && !s.step[1].tie && s.step[2].tie && has(&s.step[2], 60, 100));
    seqrec_inc(&r), seqrec_inc(&r);
    CHECK(s.len == 5 && s.step[3].tie && s.step[4].tie);
    CHECK(seqrec_note(&r, 60, 0) == 0 && s.len == 5);
    /* a chord, tied once, then a new note while one of it is still held: the next step */
    seqrec_note(&r, 48, 100), seqrec_note(&r, 52, 100);
    seqrec_inc(&r);
    CHECK(s.len == 7 && s.step[5].count == 2 && s.step[6].tie && s.step[6].count == 2);
    seqrec_note(&r, 48, 0);
    seqrec_note(&r, 55, 90);
    CHECK(s.len == 7);
    seqrec_note(&r, 52, 0);
    CHECK(s.len == 7);                                     /* G still held */
    seqrec_note(&r, 55, 0);
    CHECK(s.len == 8 && s.step[7].count == 1 && has(&s.step[7], 55, 90) && !s.step[7].tie);
    /* only notes out of range held: INC is a rest */
    seqrec_note(&r, 110, 100);
    CHECK(seqrec_inc(&r) == 1 && s.len == 9 && s.step[8].count == 0);
    seqrec_note(&r, 110, 0);
    CHECK(s.len == 9);
    /* DEC: back and erase, to empty; then refused */
    CHECK(seqrec_dec(&r) == 1 && s.len == 8);
    CHECK(s.step[8].count == 0 && s.step[8].tie == 0);
    /* DEC while a chord is being entered: the last recorded step goes, the chord still comes in at its release */
    seqrec_note(&r, 67, 100);
    CHECK(seqrec_dec(&r) == 1 && s.len == 7);
    seqrec_note(&r, 67, 0);
    CHECK(s.len == 8 && has(&s.step[7], 67, 100));
    while (s.len) seqrec_dec(&r);
    CHECK(seqrec_dec(&r) == -1 && s.len == 0);
    /* a tie after a rest: there is nothing to extend: the held note is recorded, then tied (never a tie on a rest) */
    seqrec_inc(&r);
    seqrec_note(&r, 60, 100);
    seqrec_inc(&r);
    CHECK(s.len == 3 && s.step[0].count == 0 && s.step[1].count == 1 && s.step[2].tie);
    seqrec_note(&r, 60, 0);
    printf("rec_inc_dec: len %d\n", s.len);
}

static void rec_stop_at_64(void)
{
    seqrec_t r;
    int k;
    memset(&r, 0, sizeof r);
    seq_clear(&s);
    seqrec_start(&r, mut);
    for (k = 0; k < 63; k++) { seqrec_note(&r, 60 + k % 12, 100); seqrec_note(&r, 60 + k % 12, 0); }
    CHECK(s.len == 63 && r.on);
    seqrec_note(&r, 72, 100), seqrec_note(&r, 72, 0);
    CHECK(s.len == 64 && !r.on);                           /* the 64th ends it */
    CHECK(seqrec_note(&r, 74, 100) == 0 && seqrec_note(&r, 74, 0) == 0 && seqrec_inc(&r) == 0 && s.len == 64);
    CHECK(seqrec_dec(&r) == 0 && s.len == 64);         /* (off: DEC does nothing either) */
    /* a tie chain reaching 64: INC stops it there */
    seqrec_start(&r, mut);
    for (k = 0; k < 62; k++) seqrec_inc(&r);
    seqrec_note(&r, 60, 100);
    seqrec_inc(&r);                                    /* step 63 C, step 64 tie */
    CHECK(s.len == 64 && !r.on && s.step[63].tie);
    /* a rest as the 64th */
    seqrec_start(&r, mut);
    for (k = 0; k < 64; k++) seqrec_inc(&r);
    CHECK(s.len == 64 && !r.on);
    printf("rec_stop_at_64: len %d on %d\n", s.len, r.on);
}

static void rec_stop_abort(void)
{
    seqrec_t r;
    memset(&r, 0, sizeof r);
    seq_clear(&s);
    seqrec_start(&r, mut);
    seqrec_note(&r, 60, 100), seqrec_note(&r, 64, 90);
    CHECK(seqrec_stop(&r) == 1 && !r.on && s.len == 1 && s.step[0].count == 2);   /* the chord held: in */
    CHECK(seqrec_note(&r, 60, 0) == 0 && s.len == 1);
    seqrec_start(&r, mut);
    seqrec_note(&r, 60, 100), seqrec_note(&r, 60, 0);
    seqrec_note(&r, 62, 100);
    seqrec_abort(&r);
    CHECK(!r.on && s.len == 1);
    seqrec_start(&r, mut);
    seqrec_note(&r, 60, 100);
    seqrec_inc(&r);
    CHECK(seqrec_stop(&r) == 0 && s.len == 2);         /* (already in: nothing more) */
    printf("rec_stop_abort: ok\n");
}

int main(void)
{
    rec_lazy_clear();
    rec_dec_after_tie();
    rec_keys_and_midi();
    rec_octave_baked();
    rec_inc_dec();
    rec_stop_at_64();
    rec_stop_abort();
    if (fails) { printf("test_seqrec: %d FAILED\n", fails); return 1; }
    printf("test_seqrec: all passed\n");
    return 0;
}
