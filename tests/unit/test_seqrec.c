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
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int has(const seq_step_t *st, int note, int vel)
{
    for (int k = 0; k < st->count; k++) if (st->notes[k] == note && st->vels[k] == vel) return 1;
    return 0;
}

static void rec_start_clears(void)
{
    seq_t s;
    seqrec_t r;
    static const uint8_t n[1] = {60}, v[1] = {100};
    memset(&r, 0, sizeof r);
    seq_clear(&s);
    seq_rec_step(&s, n, v, 1), seq_rec_step(&s, n, v, 1);
    CHECK(seqrec_note(&r, &s, 62, 90) == 0 && s.len == 2);   /* off: nothing */
    CHECK(seqrec_inc(&r, &s) == 0 && seqrec_dec(&r, &s) == 0 && s.len == 2);
    seqrec_start(&r, &s);
    CHECK(r.on == 1 && s.len == 0);                          /* replaces it, from step 1 */
    printf("rec_start_clears: on %d len %d\n", r.on, s.len);
}

static void rec_keys_and_midi(void)
{
    seq_t s;
    seqrec_t r;
    memset(&r, 0, sizeof r);
    seq_clear(&s);
    seqrec_start(&r, &s);
    /* a single note: pressed, released: one step */
    CHECK(seqrec_note(&r, &s, 60, 100) == 0 && s.len == 0); /* (recorded at its release) */
    CHECK(seqrec_note(&r, &s, 60, 0) == 1 && s.len == 1);
    CHECK(s.step[0].count == 1 && has(&s.step[0], 60, 100) && !s.step[0].tie);
    /* a chord: C held, E added, C let go (E still held), G added (from MIDI: same path), all up: one step of 3 */
    seqrec_note(&r, &s, 48, 90);
    seqrec_note(&r, &s, 52, 80);
    CHECK(seqrec_note(&r, &s, 48, 0) == 0 && s.len == 1);   /* one still held: not yet */
    seqrec_note(&r, &s, 55, 70);
    CHECK(seqrec_held(&r) == 2);
    seqrec_note(&r, &s, 52, 0);
    CHECK(s.len == 1);
    CHECK(seqrec_note(&r, &s, 55, 0) == 1 && s.len == 2);
    CHECK(s.step[1].count == 3 && has(&s.step[1], 48, 90) && has(&s.step[1], 52, 80) && has(&s.step[1], 55, 70));
    /* the same note twice in a chord (a key and a MIDI note on the same number): once */
    seqrec_note(&r, &s, 64, 100);
    seqrec_note(&r, &s, 64, 50);
    seqrec_note(&r, &s, 64, 0);
    CHECK(s.len == 3 && s.step[2].count == 1 && has(&s.step[2], 64, 100));
    /* seven notes held: the first six recorded */
    for (int k = 0; k < 7; k++) seqrec_note(&r, &s, 60 + k, 100 - k);
    for (int k = 0; k < 7; k++) seqrec_note(&r, &s, 60 + k, 0);
    CHECK(s.len == 4 && s.step[3].count == 6 && !has(&s.step[3], 66, 94) && has(&s.step[3], 65, 95));
    /* a note off for a note held since before the start: ignored */
    CHECK(seqrec_note(&r, &s, 30, 0) == 0 && s.len == 4);
    printf("rec_keys_and_midi: len %d, step 2 count %d, step 4 count %d\n", s.len, s.step[1].count, s.step[3].count);
}

static void rec_octave_baked(void)
{
    seq_t s;
    seqrec_t r;
    memset(&r, 0, sizeof r);
    seq_clear(&s);
    seqrec_start(&r, &s);
    /* the caller's note (F3 at OCTAVE +2 = 53 + 24 = 77) is the note stored */
    seqrec_note(&r, &s, 77, 100), seqrec_note(&r, &s, 77, 0);
    CHECK(s.len == 1 && s.step[0].notes[0] == 77);
    /* outside 12..108: held, not recorded; a step of only such notes is not a step */
    seqrec_note(&r, &s, 11, 100), seqrec_note(&r, &s, 109, 100);
    CHECK(seqrec_held(&r) == 2);
    seqrec_note(&r, &s, 11, 0), seqrec_note(&r, &s, 109, 0);
    CHECK(s.len == 1 && seqrec_held(&r) == 0);
    /* mixed: the in-range ones */
    seqrec_note(&r, &s, 10, 100), seqrec_note(&r, &s, 108, 90), seqrec_note(&r, &s, 12, 80);
    seqrec_note(&r, &s, 10, 0), seqrec_note(&r, &s, 108, 0), seqrec_note(&r, &s, 12, 0);
    CHECK(s.len == 2 && s.step[1].count == 2 && has(&s.step[1], 108, 90) && has(&s.step[1], 12, 80));
    printf("rec_octave_baked: %d %d steps\n", s.step[0].notes[0], s.len);
}

static void rec_inc_dec(void)
{
    seq_t s;
    seqrec_t r;
    memset(&r, 0, sizeof r);
    seq_clear(&s);
    seqrec_start(&r, &s);
    CHECK(seqrec_inc(&r, &s) == 1 && s.len == 1 && s.step[0].count == 0);   /* nothing held: a rest */
    /* C held, INC three times: C, tie, tie, tie; its release adds nothing */
    seqrec_note(&r, &s, 60, 100);
    CHECK(seqrec_inc(&r, &s) == 1 && s.len == 3);
    CHECK(s.step[1].count == 1 && !s.step[1].tie && s.step[2].tie && has(&s.step[2], 60, 100));
    seqrec_inc(&r, &s), seqrec_inc(&r, &s);
    CHECK(s.len == 5 && s.step[3].tie && s.step[4].tie);
    CHECK(seqrec_note(&r, &s, 60, 0) == 0 && s.len == 5);
    /* a chord, tied once, then a new note while one of it is still held: the next step */
    seqrec_note(&r, &s, 48, 100), seqrec_note(&r, &s, 52, 100);
    seqrec_inc(&r, &s);
    CHECK(s.len == 7 && s.step[5].count == 2 && s.step[6].tie && s.step[6].count == 2);
    seqrec_note(&r, &s, 48, 0);
    seqrec_note(&r, &s, 55, 90);
    CHECK(s.len == 7);
    seqrec_note(&r, &s, 52, 0);
    CHECK(s.len == 7);                                     /* G still held */
    seqrec_note(&r, &s, 55, 0);
    CHECK(s.len == 8 && s.step[7].count == 1 && has(&s.step[7], 55, 90) && !s.step[7].tie);
    /* only notes out of range held: INC is a rest */
    seqrec_note(&r, &s, 110, 100);
    CHECK(seqrec_inc(&r, &s) == 1 && s.len == 9 && s.step[8].count == 0);
    seqrec_note(&r, &s, 110, 0);
    CHECK(s.len == 9);
    /* DEC: back and erase, to empty; then refused */
    CHECK(seqrec_dec(&r, &s) == 1 && s.len == 8);
    CHECK(s.step[8].count == 0 && s.step[8].tie == 0);
    /* DEC while a chord is being entered: the last recorded step goes, the chord still comes in at its release */
    seqrec_note(&r, &s, 67, 100);
    CHECK(seqrec_dec(&r, &s) == 1 && s.len == 7);
    seqrec_note(&r, &s, 67, 0);
    CHECK(s.len == 8 && has(&s.step[7], 67, 100));
    while (s.len) seqrec_dec(&r, &s);
    CHECK(seqrec_dec(&r, &s) == -1 && s.len == 0);
    /* a tie after a rest: there is nothing to extend: the held note is recorded, then tied (never a tie on a rest) */
    seqrec_inc(&r, &s);
    seqrec_note(&r, &s, 60, 100);
    seqrec_inc(&r, &s);
    CHECK(s.len == 3 && s.step[0].count == 0 && s.step[1].count == 1 && s.step[2].tie);
    seqrec_note(&r, &s, 60, 0);
    printf("rec_inc_dec: len %d\n", s.len);
}

static void rec_stop_at_64(void)
{
    seq_t s;
    seqrec_t r;
    int k;
    memset(&r, 0, sizeof r);
    seq_clear(&s);
    seqrec_start(&r, &s);
    for (k = 0; k < 63; k++) { seqrec_note(&r, &s, 60 + k % 12, 100); seqrec_note(&r, &s, 60 + k % 12, 0); }
    CHECK(s.len == 63 && r.on);
    seqrec_note(&r, &s, 72, 100), seqrec_note(&r, &s, 72, 0);
    CHECK(s.len == 64 && !r.on);                           /* the 64th ends it */
    CHECK(seqrec_note(&r, &s, 74, 100) == 0 && seqrec_note(&r, &s, 74, 0) == 0 && seqrec_inc(&r, &s) == 0 && s.len == 64);
    CHECK(seqrec_dec(&r, &s) == 0 && s.len == 64);         /* (off: DEC does nothing either) */
    /* a tie chain reaching 64: INC stops it there */
    seqrec_start(&r, &s);
    for (k = 0; k < 62; k++) seqrec_inc(&r, &s);
    seqrec_note(&r, &s, 60, 100);
    seqrec_inc(&r, &s);                                    /* step 63 C, step 64 tie */
    CHECK(s.len == 64 && !r.on && s.step[63].tie);
    /* a rest as the 64th */
    seqrec_start(&r, &s);
    for (k = 0; k < 64; k++) seqrec_inc(&r, &s);
    CHECK(s.len == 64 && !r.on);
    printf("rec_stop_at_64: len %d on %d\n", s.len, r.on);
}

static void rec_stop_abort(void)
{
    seq_t s;
    seqrec_t r;
    memset(&r, 0, sizeof r);
    seq_clear(&s);
    seqrec_start(&r, &s);
    seqrec_note(&r, &s, 60, 100), seqrec_note(&r, &s, 64, 90);
    CHECK(seqrec_stop(&r, &s) == 1 && !r.on && s.len == 1 && s.step[0].count == 2);   /* the chord held: in */
    CHECK(seqrec_note(&r, &s, 60, 0) == 0 && s.len == 1);
    seqrec_start(&r, &s);
    seqrec_note(&r, &s, 60, 100), seqrec_note(&r, &s, 60, 0);
    seqrec_note(&r, &s, 62, 100);
    seqrec_abort(&r);
    CHECK(!r.on && s.len == 1);
    seqrec_start(&r, &s);
    seqrec_note(&r, &s, 60, 100);
    seqrec_inc(&r, &s);
    CHECK(seqrec_stop(&r, &s) == 0 && s.len == 2);         /* (already in: nothing more) */
    printf("rec_stop_abort: ok\n");
}

int main(void)
{
    rec_start_clears();
    rec_keys_and_midi();
    rec_octave_baked();
    rec_inc_dec();
    rec_stop_at_64();
    rec_stop_abort();
    if (fails) { printf("test_seqrec: %d FAILED\n", fails); return 1; }
    printf("test_seqrec: all passed\n");
    return 0;
}
