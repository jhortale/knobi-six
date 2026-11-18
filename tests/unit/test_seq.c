/* SPDX-License-Identifier: GPL-3.0-only */
/* The sequence model and its packed encoding (firmware/src/synth/seq.c, Phase 3 Task 1): pack/unpack round trips
 * (empty, full 64 x 6, rests, ties, random), seq_packed_size, the recording ops (chord, rest, tie, back, the 64
 * limit, notes outside 12..108 dropped) and unpack's rejections (truncated, bad length, count > 6, ...). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "synth/seq.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static uint32_t rs = 0x1234567u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

static int seq_eq(const seq_t *a, const seq_t *b)
{
    if (a->len != b->len) return 0;
    for (int i = 0; i < a->len; i++) {
        const seq_step_t *x = &a->step[i], *y = &b->step[i];
        if (x->count != y->count || x->tie != y->tie) return 0;
        for (int k = 0; k < x->count; k++) if (x->notes[k] != y->notes[k] || x->vels[k] != y->vels[k]) return 0;
    }
    return 1;
}
static void rand_seq(seq_t *s)
{
    seq_clear(s);
    int len = (int)(rnd() % (SEQ_MAX_STEPS + 1));
    for (int i = 0; i < len; i++) {
        uint8_t n[6], v[6];
        int c = (int)(rnd() % 4 == 0 ? 0 : 1 + rnd() % 6);
        for (int k = 0; k < c; k++) { n[k] = (uint8_t)(12 + rnd() % 97); v[k] = (uint8_t)(rnd() % 128); }
        if (c == 0) seq_rec_rest(s);
        else if (i > 0 && s->step[i - 1].count && rnd() % 5 == 0) seq_rec_tie(s);
        else seq_rec_step(s, n, v, c);
    }
}

static void seq_roundtrip(void)
{
    static seq_t a, b;
    uint8_t buf[SEQ_PACKED_MAX];
    seq_clear(&a);                                   /* empty: a valid 1-byte "length 0" */
    CHECK(seq_pack(&a, buf, sizeof buf) == 1 && buf[0] == 0);
    memset(&b, 0xA5, sizeof b);
    CHECK(seq_unpack(&b, buf, 1) == 0 && b.len == 0 && seq_eq(&a, &b));
    uint8_t n[6] = {12, 40, 60, 80, 100, 108}, v[6] = {0, 1, 64, 100, 126, 127};
    for (int i = 0; i < 64; i++) CHECK(seq_rec_step(&a, n, v, 6) == i + 1);   /* full 64 x 6 */
    int sz = seq_pack(&a, buf, sizeof buf);
    CHECK(sz == (int)SEQ_PACKED_MAX && sz == 1 + 64 * (1 + 12));
    CHECK(seq_unpack(&b, buf, (uint32_t)sz) == 0 && seq_eq(&a, &b));
    for (int t = 0; t < 300; t++) {                  /* random, with rests and ties */
        rand_seq(&a);
        sz = seq_pack(&a, buf, sizeof buf);
        CHECK(sz > 0);
        memset(&b, 0x5A, sizeof b);
        CHECK(seq_unpack(&b, buf, (uint32_t)sz) == 0 && seq_eq(&a, &b));
    }
}

static void seq_packed_size_matches(void)
{
    static seq_t a;
    uint8_t buf[SEQ_PACKED_MAX];
    CHECK(SEQ_PACKED_MAX == 1 + 64 * 13);
    for (int t = 0; t < 200; t++) {
        rand_seq(&a);
        int sz = seq_pack(&a, buf, sizeof buf);
        CHECK(sz == (int)seq_packed_size(&a));
        CHECK(sz <= (int)SEQ_PACKED_MAX);
        if (sz > 1) CHECK(seq_pack(&a, buf, (uint32_t)sz - 1) == -1);     /* too small */
        CHECK(seq_pack(&a, buf, (uint32_t)sz) == sz);
    }
    seq_clear(&a);
    CHECK(seq_packed_size(&a) == 1);
    CHECK(seq_pack(&a, buf, 0) == -1);
}

static void seq_rec_ops(void)
{
    static seq_t s;
    seq_clear(&s);
    CHECK(s.len == 0);
    uint8_t n3[3] = {60, 64, 67}, v3[3] = {100, 90, 80};
    CHECK(seq_rec_step(&s, n3, v3, 3) == 1);                          /* a chord step */
    CHECK(s.step[0].count == 3 && s.step[0].tie == 0 && s.step[0].notes[1] == 64 && s.step[0].vels[2] == 80);
    CHECK(seq_rec_rest(&s) == 2 && s.step[1].count == 0 && s.step[1].tie == 0);
    CHECK(seq_rec_tie(&s) == -1 && s.len == 2);                       /* after a rest: nothing to extend */
    CHECK(seq_rec_back(&s) == 1);
    CHECK(seq_rec_tie(&s) == 2);                                      /* extends step 0's notes */
    CHECK(s.step[1].count == 3 && s.step[1].tie == 1 && s.step[1].notes[2] == 67 && s.step[1].vels[0] == 100);
    CHECK(seq_rec_tie(&s) == 3 && s.step[2].tie == 1 && s.step[2].count == 3);   /* tie of a tie */
    CHECK(seq_rec_back(&s) == 2 && seq_rec_back(&s) == 1 && seq_rec_back(&s) == 0);
    CHECK(seq_rec_back(&s) == -1 && s.len == 0);                      /* nothing to erase */
    CHECK(seq_rec_tie(&s) == -1);                                     /* no previous step */
    /* the erased step leaves no trace: a new step replaces it cleanly */
    uint8_t n1 = 50, v1 = 7;
    CHECK(seq_rec_step(&s, &n1, &v1, 1) == 1 && s.step[0].count == 1 && s.step[0].tie == 0 && s.step[0].notes[0] == 50);
    /* notes outside 12..108 are dropped before counting */
    seq_clear(&s);
    uint8_t no[5] = {11, 12, 60, 108, 109}, vo[5] = {1, 2, 3, 4, 5};
    CHECK(seq_rec_step(&s, no, vo, 5) == 1);
    CHECK(s.step[0].count == 3 && s.step[0].notes[0] == 12 && s.step[0].notes[1] == 60 && s.step[0].notes[2] == 108);
    CHECK(s.step[0].vels[0] == 2 && s.step[0].vels[1] == 3 && s.step[0].vels[2] == 4);
    uint8_t bad[2] = {0, 127}, vb[2] = {9, 9};
    CHECK(seq_rec_step(&s, bad, vb, 2) == -1 && s.len == 1);          /* all dropped: refused, not a rest */
    CHECK(seq_rec_step(&s, bad, vb, 0) == -1);
    CHECK(seq_rec_step(&s, NULL, NULL, 3) == -1);
    /* more than 6 notes: the first six in range */
    uint8_t big[8] = {20, 21, 22, 23, 24, 25, 26, 27}, vg[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    seq_clear(&s);
    CHECK(seq_rec_step(&s, big, vg, 8) == 1 && s.step[0].count == 6 && s.step[0].notes[5] == 25);
    /* velocity 0 is stored as given (the caller's business: a note-on never has it) */
    uint8_t z = 60, vz = 0;
    CHECK(seq_rec_step(&s, &z, &vz, 1) == 2 && s.step[1].vels[0] == 0);
    /* the 64 limit, for every op */
    seq_clear(&s);
    for (int i = 0; i < 64; i++) CHECK(seq_rec_step(&s, &z, &vz, 1) == i + 1);
    CHECK(seq_rec_step(&s, &z, &vz, 1) == -1 && seq_rec_rest(&s) == -1 && seq_rec_tie(&s) == -1 && s.len == 64);
    CHECK(seq_rec_back(&s) == 63 && seq_rec_rest(&s) == 64);
}

static void seq_unpack_rejects(void)
{
    static seq_t a, b;
    uint8_t buf[SEQ_PACKED_MAX + 4];
    uint8_t n[2] = {60, 64}, v[2] = {100, 90};
    seq_clear(&a);
    seq_rec_step(&a, n, v, 2);
    seq_rec_rest(&a);
    seq_rec_tie(&a);                                                  /* rest then tie is refused: -1, len 2 */
    seq_rec_step(&a, n, v, 2);
    seq_rec_tie(&a);
    int sz = seq_pack(&a, buf, sizeof buf);
    CHECK(sz > 0 && seq_unpack(&b, buf, (uint32_t)sz) == 0 && seq_eq(&a, &b));
    for (int cut = 0; cut < sz; cut++) CHECK(seq_unpack(&b, buf, (uint32_t)cut) == -1);   /* truncated */
    CHECK(seq_unpack(&b, buf, (uint32_t)sz + 3) == 0 && seq_eq(&a, &b));   /* trailing bytes are not read */
    CHECK(seq_unpack(&b, NULL, 1) == -1);
    uint8_t t[64];
    memcpy(t, buf, (size_t)sz);
    t[0] = 65; CHECK(seq_unpack(&b, t, (uint32_t)sz) == -1);          /* bad length */
    t[0] = 255; CHECK(seq_unpack(&b, t, (uint32_t)sz) == -1);
    memcpy(t, buf, (size_t)sz);
    t[1] = 7; CHECK(seq_unpack(&b, t, (uint32_t)sz) == -1);           /* count 7 (> 6) */
    t[1] = 0x0F; CHECK(seq_unpack(&b, t, (uint32_t)sz) == -1);
    memcpy(t, buf, (size_t)sz);
    t[1] |= 0x10; CHECK(seq_unpack(&b, t, (uint32_t)sz) == -1);       /* a reserved header bit */
    memcpy(t, buf, (size_t)sz);
    t[2] = 11; CHECK(seq_unpack(&b, t, (uint32_t)sz) == -1);          /* note out of range */
    memcpy(t, buf, (size_t)sz);
    t[3] = 128; CHECK(seq_unpack(&b, t, (uint32_t)sz) == -1);         /* velocity out of range */
    /* a tie on a rest, and a tie with nothing before it */
    uint8_t r1[3] = {2, 0x00, 0x08};
    CHECK(seq_unpack(&b, r1, 3) == -1);
    uint8_t r2[2] = {1, 0x08};
    CHECK(seq_unpack(&b, r2, 2) == -1);
    /* a failed unpack leaves the target usable (a length-0 or untouched sequence, never a half-built one) */
    memcpy(t, buf, (size_t)sz);
    t[1] = 7;
    seq_unpack(&b, t, (uint32_t)sz);
    CHECK(b.len == 0 || seq_eq(&a, &b));
}

int main(void)
{
    seq_roundtrip();
    seq_packed_size_matches();
    seq_rec_ops();
    seq_unpack_rejects();
    printf(fails ? "test_seq: %d FAILED\n" : "test_seq: ok\n", fails);
    return fails != 0;
}
