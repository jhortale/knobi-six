/* SPDX-License-Identifier: GPL-3.0-only */
/* The sequence store (firmware/src/params/seq_store.c, Phase 3 Task 2) against a model of the NOR flash: the 20
 * sectors of store.h's SEQST_* list, an erase sets a sector to 0xFF, a program can only clear bits (and it is counted
 * when it would have to set one: a bug, writing over written bytes). Every erase and every 256-byte page program is
 * one operation; f_cut_at makes the power go at operation n: an erase is left half done (bytes towards 0xFF, the rest
 * of the sector garbage), a program done up to a random byte, that byte half programmed; nothing works after it until
 * power_on(). Every access must pass store.h's FL_GUARD (the allow-list the device's driver enforces). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BOARD_SMK37 1
#include "store.h"
#include "synth/seq.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- the flash model */
static uint8_t F[SEQST_SECTORS][0x1000u];
static uint32_t f_erases[SEQST_SECTORS], f_ops, f_overwrite, f_guard_bad, f_reads;
static int f_flip_next;                         /* the next program leaves one bit of its bytes 0 (a marginal cell) */
static int32_t f_stuck = -1;                    /* a sector whose programs change nothing (reported done) */
static int f_stuck_erase;                       /* ... and whose erases are refused too */
static int32_t f_cut_at = -1;                   /* the operation (1..) the power goes at; -1 never */
static int f_dead, f_cut_full;                  /* f_cut_full: the cut operation completes, then the power goes */
static uint32_t frs = 0x2545F491u;
static uint32_t frnd(void) { frs ^= frs << 13; frs ^= frs >> 17; frs ^= frs << 5; return frs; }

static uint8_t *f_at(uint32_t off, uint32_t n)
{
    uint32_t i;
    if (!FL_GUARD(off, n)) { f_guard_bad++; return NULL; }
    for (i = 0; i < SEQST_SECTORS; i++)
        if (off >= SEQST_SECTOR_AT(i) && off + n <= SEQST_SECTOR_AT(i) + 0x1000u) return &F[i][off - SEQST_SECTOR_AT(i)];
    f_guard_bad++;                              /* allowed, but not the sequence store's */
    return NULL;
}
static int f_cut_now(void) { return f_cut_at >= 0 && f_ops == (uint32_t)f_cut_at; }
static int seqst_fl_read(uint32_t off, void *p, uint32_t n)
{
    const uint8_t *a = f_at(off, n);
    if (!a) return -1;
    memcpy(p, a, n);
    f_reads++;
    return 0;
}
static int seqst_fl_erase(uint32_t off)
{
    uint8_t *a = f_at(off, 0x1000u);
    uint32_t i, k;
    if (!a || (off & 0xFFFu) || f_dead) return -1;
    if (f_stuck >= 0 && a == F[f_stuck] && f_stuck_erase) return -1;
    f_ops++;
    for (i = 0; i < SEQST_SECTORS; i++) if (a == F[i]) f_erases[i]++;
    if (f_cut_now() && !f_cut_full) {           /* half erased: some bytes 0xFF, the rest going there */
        k = frnd() % 0x1000u;
        memset(a, 0xFF, k);
        for (i = k; i < 0x1000u; i++) a[i] |= (uint8_t)frnd();
        f_dead = 1;
        return -1;
    }
    memset(a, 0xFF, 0x1000u);
    if (f_cut_now()) { f_dead = 1; return -1; }
    return 0;
}
static int seqst_fl_prog(uint32_t off, const void *p, uint32_t n)
{
    const uint8_t *s = p;
    uint8_t *a = f_at(off, n);
    if (!a || f_dead) return -1;
    if (f_stuck >= 0 && a >= F[f_stuck] && a < F[f_stuck] + 0x1000u) return 0;   /* (silently not programmed) */
    while (n) {                                 /* page by page, as the driver */
        uint32_t chunk = 256u - (off & 0xFFu), i, k;
        if (chunk > n) chunk = n;
        f_ops++;
        k = f_cut_now() && !f_cut_full ? frnd() % (chunk + 1u) : chunk;
        for (i = 0; i < k; i++) {
            if ((a[i] & s[i]) != s[i]) f_overwrite++;
            a[i] &= s[i];
        }
        if (k < chunk) a[k] &= (uint8_t)(s[k] | frnd());   /* the byte it was on: half programmed */
        if (f_flip_next && chunk > 13u) {       /* a weak cell reads 0 where 1 was meant (past the tag) */
            for (i = 12; i < chunk && !(a[i] & 0x10u); i++) {}
            if (i < chunk) a[i] &= (uint8_t)~0x10u, f_flip_next = 0;
        }
        if (f_cut_now()) { f_dead = 1; return -1; }
        off += chunk, a += chunk, s += chunk, n -= chunk;
    }
    return 0;
}

#include "params/seq_store.c"

static void f_blank(void) { memset(F, 0xFF, sizeof F); memset(f_erases, 0, sizeof f_erases); }
static void power_on(void) { f_dead = 0, f_cut_at = -1, f_cut_full = 0; CHECK(seqst_init() == 0); }

/* ---- sequences */
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
static void seq_of_len(seq_t *s, int len, int notes)   /* len steps of `notes` notes, contents random */
{
    seq_clear(s);
    for (int i = 0; i < len; i++) {
        uint8_t n[6], v[6];
        for (int k = 0; k < notes; k++) { n[k] = (uint8_t)(12 + rnd() % 97); v[k] = (uint8_t)(1 + rnd() % 127); }
        if (notes) seq_rec_step(s, n, v, notes); else seq_rec_rest(s);
    }
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
static uint32_t rec_size(const seq_t *s) { return (SEQST_REC_HDR + seq_packed_size(s) + 3u) & ~3u; }
static int got_is(uint16_t p, uint16_t g, const seq_t *want)
{
    static seq_t out;
    memset(&out, 0x5A, sizeof out);
    return seqst_get(p, g, &out) == 1 && seq_eq(&out, want);
}

/* ---- a model of what must read back: per program the live generations and their sequences */
#define MP 40u                                  /* programs the model follows (spread over 0..499) */
#define MG 6u
typedef struct { uint16_t prog, top, rel, n; uint16_t gen[MG]; seq_t s[MG]; } mprog_t;
static mprog_t M[MP];
static uint16_t m_prog(uint32_t i) { return (uint16_t)(i * 499u / (MP - 1u)); }   /* 0, 12, ..., 499 */
static void m_reset(void) { memset(M, 0, sizeof M); for (uint32_t i = 0; i < MP; i++) M[i].prog = m_prog(i); }
static int m_newer(uint16_t a, uint16_t b) { return b == 0 || (a && (int16_t)(a - b) > 0); }
static void m_put(uint32_t i, uint16_t g, const seq_t *s)
{
    mprog_t *m = &M[i];
    CHECK(m->n < MG);
    if (m->n >= MG) return;
    m->gen[m->n] = g, m->s[m->n] = *s, m->n++;
    m->top = g;
}
static void m_release(uint32_t i, uint16_t keep)
{
    mprog_t *m = &M[i];
    uint16_t k, j = 0;
    for (k = 0; k < m->n; k++)
        if (!m_newer(keep, m->gen[k])) m->gen[j] = m->gen[k], m->s[j] = m->s[k], j++;
    m->n = j, m->rel = keep;
}
static uint32_t m_check(const char *what)       /* every live entry of the model reads back as written */
{
    uint32_t bad = 0;
    for (uint32_t i = 0; i < MP; i++)
        for (uint32_t k = 0; k < M[i].n; k++)
            if (!got_is(M[i].prog, M[i].gen[k], &M[i].s[k])) {
                if (bad < 3) printf("  %s: program %u gen %u does not read back\n", what, M[i].prog, M[i].gen[k]);
                bad++;
            }
    return bad;
}
static uint32_t m_used(void)                    /* what seqst_used() must say: live entries + the newest releases */
{
    uint32_t u = 0;
    for (uint32_t i = 0; i < MP; i++) {
        for (uint32_t k = 0; k < M[i].n; k++) u += rec_size(&M[i].s[k]);
        if (M[i].rel) u += SEQST_REC_HDR;
    }
    return u;
}
static int m_write(uint32_t i, const seq_t *s)  /* Task 3's WRITE: put, (the program record), release */
{
    uint16_t g = 0;
    if (seqst_put(M[i].prog, s, &g)) return -1;
    CHECK(g != 0 && m_newer(g, M[i].top));
    m_put(i, g, s);
    seqst_release(M[i].prog, g);
    m_release(i, g);
    return 0;
}

/* ---- the tests */
static void store_put_get(void)
{
    static seq_t a, b, c;
    uint16_t ga = 0, gb = 0;
    f_blank();
    power_on();
    CHECK(seqst_total() == (SEQST_SECTORS - 1u) * SEQST_ROOM && seqst_used() == 0);
    rand_seq(&a);
    seq_of_len(&b, 64, 6);
    CHECK(seqst_put(0, &a, &ga) == 0 && ga != 0);
    CHECK(seqst_put(499, &b, &gb) == 0 && gb != 0);
    CHECK(got_is(0, ga, &a) && got_is(499, gb, &b));
    CHECK(seqst_used() == rec_size(&a) + rec_size(&b));
    CHECK(seqst_get(1, ga, &c) == 0 && seqst_get(0, (uint16_t)(ga + 5u), &c) == 0 && seqst_get(0, 0, &c) == 0);
    CHECK(seqst_put(500, &a, &ga) == -1 && seqst_get(500, 1, &c) == 0);   /* past the programs */
    power_on();                                 /* a power cycle: all from the flash */
    CHECK(got_is(0, ga, &a) && got_is(499, gb, &b));
    CHECK(seqst_used() == rec_size(&a) + rec_size(&b));
    CHECK(f_overwrite == 0 && f_guard_bad == 0);
    printf("store_put_get: gens %u %u, used %u of %u B\n", ga, gb, seqst_used(), seqst_total());
}

static void store_empty_entry(void)
{
    static seq_t e, out;
    uint16_t g = 0;
    f_blank();
    power_on();
    seq_clear(&e);
    CHECK(seqst_put(42, &e, &g) == 0 && g);
    out.len = 7;
    CHECK(seqst_get(42, g, &out) == 1 && out.len == 0);   /* found, empty: not "never written" */
    CHECK(seqst_get(43, g, &out) == 0);
    CHECK(seqst_used() == 16u);
    power_on();
    out.len = 7;
    CHECK(seqst_get(42, g, &out) == 1 && out.len == 0);
    printf("store_empty_entry: an empty entry takes %u B\n", seqst_used());
}

static void store_generation_select(void)
{
    static seq_t a, b, c;
    uint16_t g1 = 0, g2 = 0, g3 = 0;
    f_blank();
    power_on();
    rand_seq(&a);
    seq_of_len(&b, 10, 2);
    CHECK(seqst_put(7, &a, &g1) == 0 && seqst_put(7, &b, &g2) == 0);
    CHECK(g1 && g2 && g1 != g2 && (int16_t)(g2 - g1) > 0);
    CHECK(got_is(7, g1, &a) && got_is(7, g2, &b));       /* both, each by its generation */
    power_on();
    CHECK(got_is(7, g1, &a) && got_is(7, g2, &b));
    seqst_release(7, g1);                       /* the record still names g1 (the g2 WRITE was cut): both stay */
    power_on();
    CHECK(got_is(7, g1, &a) && got_is(7, g2, &b));
    seqst_release(7, g2);
    CHECK(got_is(7, g2, &b) && seqst_used() == rec_size(&b) + SEQST_REC_HDR);   /* g1 reclaimable */
    power_on();
    CHECK(got_is(7, g2, &b) && seqst_used() == rec_size(&b) + SEQST_REC_HDR);
    seq_of_len(&c, 3, 1);
    CHECK(seqst_put(7, &c, &g3) == 0 && (int16_t)(g3 - g2) > 0 && g3 != g1);   /* never handed out again */
    CHECK(got_is(7, g3, &c) && got_is(7, g2, &b));
    printf("store_generation_select: gens %u %u %u\n", g1, g2, g3);
}

static void store_full_refuses(void)
{
    static seq_t big, e, out;
    uint16_t g[SEQST_PROGS], ge = 0, gx = 0x7777;
    uint32_t n = 0, ops, used, i, bad = 0;
    f_blank();
    power_on();
    seq_of_len(&big, 64, 6);                    /* 848 B records */
    for (n = 0; n < SEQST_PROGS; n++) if (seqst_put((uint16_t)n, &big, &g[n])) break;
    CHECK(n > 0 && n < SEQST_PROGS);
    used = seqst_used(), ops = f_ops;
    CHECK(seqst_put((uint16_t)n, &big, &gx) == -1 && gx == 0x7777);   /* refused: nothing written, nothing erased */
    CHECK(f_ops == ops && seqst_used() == used);
    for (i = 0; i < n; i++) bad += !got_is((uint16_t)i, g[i], &big);
    CHECK(bad == 0);
    power_on();
    CHECK(seqst_put((uint16_t)n, &big, &gx) == -1 && seqst_used() == used);
    for (i = 0, bad = 0; i < n; i++) bad += !got_is((uint16_t)i, g[i], &big);
    CHECK(bad == 0);
    /* a full store can still be cleared: an empty entry for program 0, then its release frees the big one */
    seq_clear(&e);
    CHECK(seqst_put(0, &e, &ge) == 0);
    seqst_release(0, ge);
    CHECK(seqst_used() == used - rec_size(&big) + 16u + SEQST_REC_HDR);
    CHECK(seqst_get(0, ge, &out) == 1 && out.len == 0);
    power_on();
    CHECK(seqst_get(0, ge, &out) == 1 && out.len == 0 && got_is(1, g[1], &big));
    CHECK(f_overwrite == 0 && f_guard_bad == 0);
    printf("store_full_refuses: %u full-size sequences (%u B), used %u of %u B, then refused; cleared one\n", n,
           rec_size(&big), used, seqst_total());
    /* small entries fill it to the reserve: a non-empty put is refused there, an empty one (and its release) not */
    f_blank();
    power_on();
    seq_of_len(&big, 30, 1);                    /* 104 B records, never released: many generations a program */
    for (n = 0; n < 2000u; n++) if (seqst_put((uint16_t)(n % SEQST_PROGS), &big, &gx)) break;
    used = seqst_used();
    CHECK(n < 2000u && used + rec_size(&big) > seqst_total() - SEQST_RESERVE && used <= seqst_total() - SEQST_RESERVE);
    ops = f_ops;
    CHECK(seqst_put(1, &big, &gx) == -1 && f_ops == ops);
    CHECK(seqst_put(0, &e, &ge) == 0);
    seqst_release(0, ge);                       /* every older generation of program 0 goes */
    CHECK(seqst_used() < used && seqst_get(0, ge, &out) == 1 && out.len == 0);
    power_on();
    CHECK(seqst_get(0, ge, &out) == 1 && out.len == 0 && seqst_used() < used);
    printf("store_full_refuses: %u small entries, used %u B: refused at the reserve; an empty WRITE frees room\n", n, used);
}

static void compact_keeps_live(void)
{
    static seq_t s;
    uint32_t round, i, erases = 0, bad, reclaimed = 0, orphans = 0;
    f_blank();
    m_reset();
    power_on();
    for (round = 0; round < 60; round++)
        for (i = 0; i < MP; i++) {
            seq_of_len(&s, (int)(rnd() % 40u), (int)(rnd() % 7u));
            if (i % 10u == 3u && round % 20u == 5u && M[i].n < MG - 1u) {   /* a put whose record was never written */
                uint16_t g;
                CHECK(seqst_put(M[i].prog, &s, &g) == 0);
                m_put(i, g, &s);
                orphans++;
                continue;
            }
            CHECK(m_write(i, &s) == 0);
        }
    for (i = 0; i < SEQST_SECTORS; i++) erases += f_erases[i];
    bad = m_check("compact_keeps_live");
    CHECK(bad == 0 && erases > SEQST_SECTORS);  /* compaction ran, many times */
    CHECK(seqst_used() == m_used());            /* only the dead were reclaimed, all of them counted */
    power_on();
    CHECK(m_check("after a power cycle") == 0 && seqst_used() == m_used());
    for (i = 0; i < MP; i++) {                  /* old generations are gone (reclaimed), the next one is new */
        static seq_t o;
        if (M[i].rel > 20u && seqst_get(M[i].prog, (uint16_t)(M[i].rel - 20u), &o) == 0) reclaimed++;
        seq_of_len(&s, 2, 1);
        CHECK(m_write(i, &s) == 0);
    }
    CHECK(reclaimed > MP / 2u && m_check("after more writes") == 0);
    CHECK(f_overwrite == 0 && f_guard_bad == 0);
    printf("compact_keeps_live: %u writes + %u orphans over %u programs, %u erases, used %u B, %u old gens gone\n",
           60u * MP, orphans, MP, erases, seqst_used(), reclaimed);
}

static void store_wear(void)
{
    static seq_t s;
    uint32_t N = 3000, i, k, total = 0, mx = 0, mn = 0xFFFFFFFFu, per;
    f_blank();
    m_reset();
    power_on();
    seq_of_len(&s, 16, 6);                      /* 212 B records + a 12 B release each */
    k = SEQST_ROOM / (rec_size(&s) + SEQST_REC_HDR);   /* saves a sector holds */
    for (i = 0; i < N; i++) {
        seq_of_len(&s, 16, 6);
        CHECK(m_write(i % 8u, &s) == 0);
    }
    for (i = 0; i < SEQST_SECTORS; i++) {
        total += f_erases[i];
        if (f_erases[i] > mx) mx = f_erases[i];
        if (f_erases[i] < mn) mn = f_erases[i];
    }
    per = (N + k * (SEQST_SECTORS - 1u) - 1u) / (k * (SEQST_SECTORS - 1u));
    printf("store_wear: %u saves, %u a sector: %u erases (%u..%u a sector, bound %u)\n", N, k, total, mn, mx, per + 2u);
    CHECK(total <= (N + k - 1u) / k + SEQST_SECTORS);   /* about one erase per sector's worth of saves */
    CHECK(mx <= per + 2u && mx - mn <= 2u);     /* and spread evenly */
    CHECK(m_check("store_wear") == 0);
}

static void store_crc_rejects(void)
{
    static seq_t a, b, c, x;
    uint16_t ga = 0, gb = 0, gc = 0, gx = 0, gb2;
    uint32_t sb, ob, sc, oc;
    f_blank();
    power_on();
    seq_of_len(&a, 8, 3), seq_of_len(&b, 8, 3), seq_of_len(&c, 8, 3);
    CHECK(seqst_put(3, &a, &ga) == 0 && seqst_put(4, &b, &gb) == 0 && seqst_put(5, &c, &gc) == 0);
    sb = seqst_loc_top[4] >> 10, ob = (seqst_loc_top[4] & 0x3FFu) << 2;
    sc = seqst_loc_top[5] >> 10, oc = (seqst_loc_top[5] & 0x3FFu) << 2;
    F[sb][ob + SEQST_REC_HDR + 5u] ^= 0x10u;    /* a bit of b's payload rots */
    CHECK(seqst_get(4, gb, &x) == 0);           /* not trusted, without a power cycle too */
    F[sc][oc + 4u] ^= 0x01u;                    /* c's generation field */
    power_on();
    CHECK(got_is(3, ga, &a) && seqst_get(4, gb, &x) == 0);
    CHECK(seqst_get(5, gc, &x) == 0 && seqst_get(5, (uint16_t)(gc ^ 1u), &x) == 0);
    CHECK(seqst_put(4, &b, &gx) == 0 && got_is(4, gx, &b));   /* the store goes on after them */
    power_on();
    CHECK(got_is(4, gx, &b) && got_is(3, ga, &a));
    gb2 = gx;
    /* a stray programmed byte past the end of the head's log (not a clean cut: a part that programs out of order):
     * the sector is closed, never appended over */
    {
        uint32_t h = (uint32_t)seqst_head, ov = f_overwrite, k;
        F[h][seqst_fill[h] + 40u] = 0x00u;
        power_on();
        CHECK(seqst_fill[h] == 0x1000u);
        for (k = 0; k < 8u; k++) { seq_of_len(&x, 4, 1); CHECK(seqst_put(9, &x, &gx) == 0 && got_is(9, gx, &x)); }
        CHECK(f_overwrite == ov && got_is(4, gb2, &b));
    }
    /* a sector of garbage (another firmware's data) is not ours: free, erased before use */
    {
        uint32_t i, j, used = seqst_used();
        for (i = 0; i < SEQST_SECTORS; i++) if (seqst_state[i] == 0) break;
        for (j = 0; j < 0x1000u; j++) F[i][j] = (uint8_t)rnd();
        power_on();
        CHECK(seqst_used() == used && got_is(4, gb2, &b));
        for (j = 0; j < 200u; j++) {            /* past the garbage sector: puts, compactions */
            seq_of_len(&x, 20, 6);
            if (seqst_put((uint16_t)(100u + j % 50u), &x, &gx) == 0) seqst_release((uint16_t)(100u + j % 50u), gx);
        }
        CHECK(got_is(3, ga, &a) && got_is(4, gb2, &b) && f_overwrite == 0);
    }
    printf("store_crc_rejects: a broken entry reads as none, the rest as written\n");
}

/* a cut at every flash operation of each WRITE (put, then release), compactions included */
static void store_powercut_every_write(void)
{
    static uint8_t snap[sizeof F];
    static mprog_t msnap[MP];
    static seq_t s, t;
    uint32_t op, cut, i, cuts = 0, bad = 0, done, nested = 0, complete = 0, absent = 0, gcs = 0;
    f_blank();
    m_reset();
    power_on();
    while (seqst_nfree() > 1u) {                /* fill up to the spare: the WRITEs below compact */
        rand_seq(&s);
        CHECK(m_write(rnd() % MP, &s) == 0);
    }
    for (op = 0; op < 400; op++) {
        uint32_t pi = rnd() % MP;
        uint16_t want = (uint16_t)(M[pi].top + 1u), g = 0;
        if (!want) want = 1u;
        if (op % 2u) seq_of_len(&s, 32 + (int)(rnd() % 33u), 3 + (int)(rnd() % 4u));   /* big ones: compactions */
        else rand_seq(&s);
        if (op % 7u == 3u) seq_clear(&s);       /* empty entries too */
        memcpy(snap, F, sizeof F), memcpy(msnap, M, sizeof M);
        for (cut = 2, done = 0; !done; cut++) { /* cut / 2: the operation; odd: it completes, then the power goes */
            uint32_t erases0 = 0, erases1 = 0;
            int put_ok, found;
            if (cut > 4000u) { bad++; printf("  op %u: never completes\n", op); break; }
            memcpy(F, snap, sizeof F), memcpy(M, msnap, sizeof M);
            power_on();
            for (i = 0; i < SEQST_SECTORS; i++) erases0 += f_erases[i];
            f_ops = 0, f_cut_at = (int32_t)(cut / 2u), f_cut_full = (int)(cut & 1u);
            put_ok = seqst_put(M[pi].prog, &s, &g) == 0;
            if (put_ok) {
                CHECK(g == want);
                m_put(pi, g, &s);
                seqst_release(M[pi].prog, g);
                m_release(pi, g);               /* (cut in the release: the older ones may go, or stay) */
                done = !f_dead;
            }
            for (i = 0; i < SEQST_SECTORS; i++) erases1 += f_erases[i];
            if (done) {
                if (erases1 > erases0) gcs++;
                break;
            }
            cuts++;
            if (cut % 3u == 0u) {               /* the power goes again while init tidies up */
                f_dead = 0, f_ops = 0, f_cut_at = 1, f_cut_full = 0;
                (void)seqst_init();
                nested += f_dead;
            }
            power_on();
            /* the put in flight: complete with its generation, or absent */
            memset(&t, 0, sizeof t);
            found = seqst_get(M[pi].prog, want, &t) == 1;
            if (found && !seq_eq(&t, &s)) { bad++; printf("  op %u cut %u: the put in flight reads back wrong\n", op, cut); }
            if (put_ok && !found) { bad++; printf("  op %u cut %u: a put that returned is gone\n", op, cut); }
            if (found && !put_ok) m_put(pi, want, &s);   /* complete: a live (unreleased) entry now */
            complete += found, absent += !found;
            if (m_check("after a cut")) { bad++; printf("  op %u cut %u lost an entry\n", op, cut); }
            /* and the store goes on: a WRITE after the cut works and survives a power cycle */
            seq_of_len(&t, 5, 2);
            if (m_write(pi, &t) != 0) { bad++; printf("  op %u cut %u: no WRITE after it\n", op, cut); }
            power_on();
            if (m_check("WRITE after a cut") || seqst_used() != m_used()) {
                bad++;
                printf("  op %u cut %u: used %u, want %u\n", op, cut, seqst_used(), m_used());
            }
        }
        /* keep the uncut result (F as the op left it, M updated above) for the next op */
        memcpy(snap, F, sizeof F);
        if (bad > 10 || cut > 4000u) break;
    }
    CHECK(bad == 0 && f_guard_bad == 0);
    CHECK(gcs > 25u && complete > 0 && absent > 0 && nested > 0);
    printf("store_powercut_every_write: 400 WRITEs, %u with a compaction, %u cuts (%u in init): in flight %u complete, %u absent; %u bad\n",
           gcs, cuts, nested, complete, absent, bad);
    printf("store_powercut_every_write: programs over written bytes: %u (cuts leave half-programmed bytes; never after)\n",
           f_overwrite);
}


/* near full, every WRITE must not erase sector after sector: at ~86 % (the reviewer's case) <= 2 erases a WRITE, ~1 on
 * average; static data (programs written once) still gets erased in turn (no sector left out of the wear) */
#define NF_P 230u
static void store_wear_near_full(void)
{
    static seq_t s, want[NF_P];
    static uint16_t gen[NF_P];
    uint32_t i, p, e0, e1, worst = 0, sum = 0, base[SEQST_SECTORS], mn = 0xFFFFFFFFu, mx = 0, bad = 0, N = 3000, fill, nf, full = 0;
    f_blank();
    power_on();
    for (p = 0; p < NF_P && seqst_used() < seqst_total() * 86u / 100u; p++) {
        seq_of_len(&want[p], 40 + (int)(rnd() % 25u), 1 + (int)(rnd() % 6u));
        CHECK(seqst_put((uint16_t)p, &want[p], &gen[p]) == 0);
        seqst_release((uint16_t)p, gen[p]);
    }
    nf = p;
    fill = seqst_used() * 100u / seqst_total();
    for (i = 0; i < SEQST_SECTORS; i++) base[i] = f_erases[i];
    for (i = 0; i < N; i++) {
        p = rnd() % nf;
        seq_of_len(&s, 40 + (int)(rnd() % 25u), 1 + (int)(rnd() % 6u));
        for (e0 = 0, e1 = 0; e1 < SEQST_SECTORS; e1++) e0 += f_erases[e1];
        if (seqst_put((uint16_t)p, &s, &gen[p]) == 0) { seqst_release((uint16_t)p, gen[p]); want[p] = s; } else full++;
        for (e1 = 0, p = 0; p < SEQST_SECTORS; p++) e1 += f_erases[p];
        sum += e1 - e0;
        if (e1 - e0 > worst) worst = e1 - e0;
    }
    for (i = 0; i < SEQST_SECTORS; i++) {
        uint32_t e = f_erases[i] - base[i];
        if (e < mn) mn = e;
        if (e > mx) mx = e;
    }
    for (p = 0; p < nf; p++) bad += !got_is((uint16_t)p, gen[p], &want[p]);
    printf("store_wear_near_full: %u %% full, %u WRITEs: %u.%02u erases a WRITE, worst %u; %u..%u a sector; %u SEQ FULL, %u bad\n",
           fill, N, sum / N, sum * 100u / N % 100u, worst, mn, mx, full, bad);
    CHECK(fill >= 84u && bad == 0 && full <= N / 100u && worst <= 2u && sum <= N && mx <= 2u * mn + 4u);
    /* static data: 150 programs written once, 30 rewritten: every sector still erased in turn */
    f_blank();
    power_on();
    for (p = 0; p < 180u; p++) {
        seq_of_len(&want[p], 40 + (int)(rnd() % 25u), 2);
        CHECK(seqst_put((uint16_t)p, &want[p], &gen[p]) == 0);
        seqst_release((uint16_t)p, gen[p]);
    }
    for (i = 0; i < SEQST_SECTORS; i++) base[i] = f_erases[i];
    for (i = 0, worst = 0, sum = 0; i < N; i++) {
        p = 150u + rnd() % 30u;
        seq_of_len(&s, 40 + (int)(rnd() % 25u), 2);
        for (e0 = 0, e1 = 0; e1 < SEQST_SECTORS; e1++) e0 += f_erases[e1];
        CHECK(seqst_put((uint16_t)p, &s, &gen[p]) == 0);
        seqst_release((uint16_t)p, gen[p]), want[p] = s;
        for (e1 = 0, p = 0; p < SEQST_SECTORS; p++) e1 += f_erases[p];
        sum += e1 - e0;
        if (e1 - e0 > worst) worst = e1 - e0;
    }
    for (i = 0, mn = 0xFFFFFFFFu, mx = 0; i < SEQST_SECTORS; i++) {
        uint32_t e = f_erases[i] - base[i];
        if (e < mn) mn = e;
        if (e > mx) mx = e;
    }
    for (p = 0, bad = 0; p < 180u; p++) bad += !got_is((uint16_t)p, gen[p], &want[p]);
    printf("store_wear_near_full: static 150 + hot 30, %u WRITEs: %u.%02u erases a WRITE, worst %u; %u..%u a sector\n", N,
           sum / N, sum * 100u / N % 100u, worst, mn, mx);
    CHECK(bad == 0 && worst <= 2u && mn >= 1u && mx <= 4u * mn + 8u);
}

/* format (the owner tag cleared): everything gone, full capacity back, also after a power cycle */
static void store_format(void)
{
    static seq_t big, s, out;
    uint16_t g = 0;
    uint32_t n0, n1;
    f_blank();
    power_on();
    seq_of_len(&big, 64, 6);
    for (n0 = 0; n0 < SEQST_PROGS; n0++) if (seqst_put((uint16_t)n0, &big, &g)) break;
    seq_of_len(&s, 3, 1);
    CHECK(seqst_format() == 0);
    CHECK(seqst_used() == 0 && seqst_get(0, 1, &out) == 0 && seqst_get(5, 1, &out) == 0);
    power_on();
    CHECK(seqst_used() == 0 && seqst_get(0, 1, &out) == 0 && seqst_get(5, 1, &out) == 0);
    for (n1 = 0; n1 < SEQST_PROGS; n1++) if (seqst_put((uint16_t)n1, &big, &g)) break;
    CHECK(n1 == n0 && got_is(0, 1, &big));      /* the same capacity as a fresh store; generations start again */
    power_on();
    CHECK(got_is(0, 1, &big) && got_is((uint16_t)(n1 - 1u), 1, &big));
    seqst_ready = 0;                            /* format before init (at boot, the owner tag just cleared) */
    CHECK(seqst_format() == 0 && seqst_used() == 0);
    CHECK(seqst_put(3, &s, &g) == 0 && g == 1u && got_is(3, 1, &s));
    CHECK(f_overwrite == 0 && f_guard_bad == 0);
    printf("store_format: %u full-size before, %u after; empty after a power cycle\n", n0, n1);
}

/* format checks its kills: a sector whose magic will not program is erased instead; one that can be neither
 * programmed nor erased makes seqst_format say so (-1) */
static void store_format_verifies(void)
{
    static seq_t big, out;
    uint16_t g = 0;
    uint32_t n, i, k, blank;
    int rc;
    f_blank();
    power_on();
    seq_of_len(&big, 64, 6);
    for (n = 0; n < 40u; n++) CHECK(seqst_put((uint16_t)n, &big, &g) == 0);
    for (i = 0; i < SEQST_SECTORS && seqst_state[i] != SEQST_USED; i++) {}   /* a sector with entries */
    f_stuck = (int32_t)i, f_stuck_erase = 0;
    rc = seqst_format();
    f_stuck = -1;
    for (k = 0, blank = 1; k < 0x1000u; k++) blank &= F[i][k] == 0xFFu;
    CHECK(rc == 0 && blank);                    /* (killed by an erase: nothing of the old store left in it) */
    power_on();
    CHECK(seqst_used() == 0 && seqst_get(0, 1, &out) == 0);
    for (n = 0; n < 40u; n++) CHECK(seqst_put((uint16_t)n, &big, &g) == 0);
    for (i = 0; i < SEQST_SECTORS && seqst_state[i] != SEQST_USED; i++) {}
    f_stuck = (int32_t)i, f_stuck_erase = 1;
    rc = seqst_format();
    CHECK(rc == -1);                            /* a stale sector could not be killed: said */
    f_stuck = -1, f_stuck_erase = 0;
    power_on();                                 /* the next init kills it (the format sector is still there) */
    CHECK(seqst_used() == 0 && seqst_get(0, 1, &out) == 0);
    for (k = 0, blank = 1; k < 0x1000u; k++) blank &= F[i][k] == 0xFFu;
    CHECK(seqst_get32(F[i]) != SEQST_MAGIC);
    printf("store_format_verifies: a kill that does not take is erased; one that cannot be: -1 (%d)\n", rc);
}

/* a put the flash refuses says so (-2), unlike a full store (-1); nothing that counts is written */
static void store_put_flash_error(void)
{
    static seq_t a, out;
    uint16_t g = 0x4321;
    f_blank();
    power_on();
    seq_of_len(&a, 5, 2);
    f_dead = 1;                                 /* (every program and erase refused from here) */
    CHECK(seqst_put(7, &a, &g) == -2 && g == 0x4321);
    power_on();
    CHECK(seqst_get(7, 1, &out) == 0 && seqst_used() == 0 && seqst_put(7, &a, &g) == 0 && got_is(7, g, &a));
    printf("store_put_flash_error: a refused put -> -2 (full stays -1)\n");
}

/* drop: every generation of a program reclaimable (RESTORE FACTORY written: the record names none) */
static void store_drop(void)
{
    static seq_t a, b, c;
    uint16_t g1 = 0, g2 = 0, g3 = 0, g4 = 0;
    uint32_t used;
    f_blank();
    power_on();
    seq_of_len(&a, 20, 3), seq_of_len(&b, 30, 2), seq_of_len(&c, 4, 1);
    CHECK(seqst_put(11, &a, &g1) == 0 && seqst_put(11, &b, &g2) == 0 && seqst_put(12, &c, &g4) == 0);
    seqst_release(11, g1);                      /* g1 named, g2 an orphan */
    used = seqst_used();
    seqst_drop(11);
    CHECK(seqst_get(11, g1, &a) == 0 && seqst_get(11, g2, &a) == 0 && got_is(12, g4, &c));
    CHECK(seqst_used() == used - rec_size(&a) - rec_size(&b) - SEQST_REC_HDR + SEQST_REC_HDR);
    power_on();
    CHECK(seqst_get(11, g1, &a) == 0 && seqst_get(11, g2, &a) == 0 && got_is(12, g4, &c));
    CHECK(seqst_used() == rec_size(&c) + SEQST_REC_HDR);
    seq_of_len(&a, 5, 5);
    CHECK(seqst_put(11, &a, &g3) == 0 && (int16_t)(g3 - g2) > 0 && g3 != g1 && g3 != g2 && got_is(11, g3, &a));
    seqst_drop(13);                             /* never written: nothing to drop, nothing written */
    CHECK(seqst_used() == rec_size(&c) + SEQST_REC_HDR + rec_size(&a));
    printf("store_drop: gens %u %u dropped, next %u\n", g1, g2, g3);
}

/* a cut put whose header made it (CRC fails) burns its generation: if the weak bytes later read whole, the
 * generation still names only that sequence */
static void store_cut_gen_burned(void)
{
    static uint8_t snap[sizeof F], rec[SEQST_REC_MAX];
    static seq_t a, b, x;
    uint16_t g = 0, g2 = 0;
    uint32_t h, at, n;
    f_blank();
    power_on();
    seq_of_len(&x, 2, 1);
    CHECK(seqst_put(20, &x, &g) == 0);
    seq_of_len(&a, 30, 4);
    memcpy(snap, F, sizeof F);
    h = (uint32_t)seqst_head, at = seqst_fill[h];
    CHECK(seqst_put(21, &a, &g) == 0);
    n = SEQST_REC_HDR + seq_packed_size(&a);
    memcpy(rec, &F[h][at], n);                  /* the record as it should be */
    memcpy(F, snap, sizeof F);
    memcpy(&F[h][at], rec, SEQST_REC_HDR + 7u); /* the cut: the header and a few payload bytes */
    power_on();
    CHECK(seqst_get(21, g, &x) == 0);
    seq_of_len(&b, 6, 2);
    CHECK(seqst_put(21, &b, &g2) == 0 && g2 != g && got_is(21, g2, &b));
    memcpy(&F[h][at], rec, n);                  /* the weak bytes read as meant after all */
    power_on();
    CHECK(got_is(21, g2, &b));
    printf("store_cut_gen_burned: the cut put's gen %u not handed out again (%u)\n", g, g2);
}

/* a program that does not read back is not trusted: the put is redone; a sector read as blank after a boot is erased
 * before its first use */
static void store_readback(void)
{
    static seq_t a, b;
    uint16_t g = 0, g2 = 0;
    uint32_t i, e = 0;
    f_blank();
    power_on();
    seq_of_len(&b, 3, 1);
    CHECK(seqst_put(1, &b, &g) == 0);
    for (i = 0; i < SEQST_SECTORS; i++) e += f_erases[i];
    CHECK(e == 1u);                             /* blank by reading, still erased before use */
    seq_of_len(&a, 20, 3);
    f_flip_next = 1;
    CHECK(seqst_put(2, &a, &g2) == 0 && f_flip_next == 0 && got_is(2, g2, &a));
    power_on();
    CHECK(got_is(2, g2, &a) && got_is(1, g, &b));
    printf("store_readback: a weak program redone (gen %u)\n", g2);
}

/* before seqst_init nothing touches the flash: get finds none, put refuses */
static void store_needs_init(void)
{
    static seq_t a;
    uint16_t g = 0x1234;
    uint32_t ops = f_ops, reads;
    f_blank();
    power_on();
    seq_of_len(&a, 3, 1);
    CHECK(seqst_put(1, &a, &g) == 0);
    seqst_ready = 0;
    ops = f_ops, reads = f_reads, g = 0x1234;
    CHECK(seqst_get(1, 1, &a) == 0 && seqst_put(1, &a, &g) == -1 && g == 0x1234);
    seqst_release(1, 1), seqst_drop(1);
    CHECK(f_ops == ops && f_reads == reads);
    power_on();
    CHECK(got_is(1, 1, &a));
    printf("store_needs_init: no flash before seqst_init\n");
}

/* a cut at every flash operation of a drop and of a format */
static void store_powercut_drop_format(void)
{
    static uint8_t snap[sizeof F];
    static mprog_t msnap[MP];
    static seq_t s, t;
    uint32_t op, cut, i, cuts = 0, bad = 0, done, formats = 0, drops = 0, f_yes = 0, f_no = 0;
    f_blank();
    m_reset();
    power_on();
    while (seqst_nfree() > 1u) { rand_seq(&s); CHECK(m_write(rnd() % MP, &s) == 0); }
    for (op = 0; op < 60; op++) {
        uint32_t pi = rnd() % MP, fmt = op % 15u == 14u;
        uint16_t oldtop = M[pi].top;
        memcpy(snap, F, sizeof F), memcpy(msnap, M, sizeof M);
        for (cut = 2, done = 0; !done; cut++) {
            int formatted = 0;
            if (cut > 4000u) { bad++; break; }
            memcpy(F, snap, sizeof F), memcpy(M, msnap, sizeof M);
            power_on();
            f_ops = 0, f_cut_at = (int32_t)(cut / 2u), f_cut_full = (int)(cut & 1u);
            if (fmt) (void)seqst_format(); else seqst_drop(M[pi].prog);
            if (!f_dead) {                      /* done: the model follows */
                if (fmt) m_reset(), formats++;
                else {                          /* (nothing live: nothing written) */
                    if (M[pi].n) M[pi].n = 0, M[pi].top = M[pi].rel = (uint16_t)(oldtop + 1u ? oldtop + 1u : 1u);
                    drops++;
                }
                break;
            }
            cuts++;
            power_on();
            if (fmt) {                          /* all, or nothing */
                formatted = seqst_used() == 0;
                if (formatted) m_reset();
                f_yes += (uint32_t)formatted, f_no += (uint32_t)!formatted;
                if (m_check("format cut") || seqst_used() != m_used()) { bad++; printf("  format op %u cut %u: half\n", op, cut); }
            } else {
                M[pi].n = 0;                    /* its entries: either way; the others: all there */
                if (m_check("drop cut")) { bad++; printf("  drop op %u cut %u lost another\n", op, cut); }
            }
            seq_of_len(&t, 4, 2);
            {
                uint16_t g;
                if (seqst_put(M[pi].prog, &t, &g) != 0) bad++;
                else if (!formatted && oldtop && !m_newer(g, oldtop)) { bad++; printf("  op %u cut %u: gen %u reused\n", op, cut, g); }
                seqst_release(M[pi].prog, g);
                M[pi].top = g, M[pi].n = 0;
                m_put(pi, g, &t), m_release(pi, g);
            }
            power_on();
            if (m_check("after") || seqst_used() != m_used()) { bad++; printf("  op %u cut %u: used %u want %u\n", op, cut, seqst_used(), m_used()); }
        }
        f_cut_at = -1;
        for (i = 0; i < 3u; i++) { rand_seq(&s); CHECK(m_write(rnd() % MP, &s) == 0); }   /* (busy again) */
        CHECK(m_check("drop/format, then WRITEs") == 0 && seqst_used() == m_used());
        if (bad > 10) break;
    }
    CHECK(bad == 0 && formats == 4u && drops == 56u && f_guard_bad == 0 && f_yes > 0 && f_no > 0);
    printf("store_powercut_drop_format: %u drops, %u formats, %u cuts (a cut format: %u done, %u not); %u bad\n", drops,
           formats, cuts, f_yes, f_no, bad);
}

int main(void)
{
    (void)smk_owner_tag;                        /* (store.h's, for platform.c) */
    store_put_get();
    store_empty_entry();
    store_generation_select();
    store_full_refuses();
    compact_keeps_live();
    store_wear();
    store_crc_rejects();
    f_overwrite = 0;
    store_powercut_every_write();
    store_wear_near_full();
    store_format();
    store_format_verifies();
    store_put_flash_error();
    store_drop();
    store_cut_gen_burned();
    store_readback();
    store_needs_init();
    store_powercut_drop_format();
    CHECK(f_overwrite == 0);
    printf("%s\n", fails ? "SEQ STORE TESTS FAILED" : "seq store tests passed");
    return fails != 0;
}
