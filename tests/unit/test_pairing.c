/* SPDX-License-Identifier: GPL-3.0-only */
/* Program <-> sequence pairing (firmware/src/params/prog_seq.c, Phase 3 Task 3): WRITE = sequence entry, then the
 * program record naming its generation, then the release; load = the record's generation, else the factory sequence,
 * else empty. Both stores against one power model: the program slots as test_store.c fakes them (a save = erase and
 * the bytes, then the header: two operations) and the sequence store's 20 sectors as NOR flash as
 * test_seq_store.c models it (an erase or a page program: one operation each). f_cut_at makes the power go at
 * operation n (left half done, or with f_cut_full done and then the power goes); nothing works until power_cycle().
 * With the synthetic bank tests/unit/factory_test_seq.h (3 programs, sequences for 0 and 1); test_pairing_nobank.c
 * builds the same tests without a bank (PAIRING_NO_BANK).
 *   write_pairs             WRITE, reload (and power cycles): the same program and sequence, 400 WRITEs of random
 *                           edits (new sequence, cleared, unedited, RESTORE FACTORY, to another program)
 *   write_powercut_pairs    a cut at every flash operation of a WRITE (both kinds of cut), the store near full so
 *                           compactions run inside WRITEs: reload gives old program + old sequence or new + new, the
 *                           other programs untouched, the next WRITE works
 *   write_full_refuses_whole  store full: PROG_ERR_SEQ_FULL, no flash operation, the record byte for byte; clearing two
 *                           sequences frees the room
 *   load_fallbacks          v1 / v2 records -> the factory sequence; no bank / no sequence -> empty; an explicit empty
 *                           entry beats the factory one; RESTORE FACTORY restores it into the edit buffer and its WRITE
 *                           frees the entries; a broken record or a missing generation -> empty
 *   edit_lost_without_write a program change (or a power cut) drops the edit buffer's sequence
 *   record_v3_compat        v2 records load with generation 0; v3 packs within the stride; 1..65535 round trip */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BOARD_SMK37 1
#include "store.h"
#include "params/params.c"
#include "synth/seq.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- the power model: one operation counter for both stores */
static uint32_t f_ops, f_erases, f_guard_bad, f_overwrite;
static int32_t f_cut_at = -1;
static int f_dead, f_cut_full;
static uint32_t frs = 0x2545F491u;
static uint32_t frnd(void) { frs ^= frs << 13; frs ^= frs >> 17; frs ^= frs << 5; return frs; }
static int f_cut_now(void) { return f_cut_at >= 0 && f_ops == (uint32_t)f_cut_at; }

/* the program slots (test_store.c's fake, with the operations counted) */
static uint8_t f_slot[NSLOT][SLOT_SECT * 0x1000u];
static uint32_t f_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++) c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}
static void f_put32(uint8_t *a, uint32_t v) { a[0] = (uint8_t)v, a[1] = (uint8_t)(v >> 8), a[2] = (uint8_t)(v >> 16), a[3] = (uint8_t)(v >> 24); }
static uint32_t f_get32(const uint8_t *a) { return (uint32_t)a[0] | (uint32_t)a[1] << 8 | (uint32_t)a[2] << 16 | (uint32_t)a[3] << 24; }
static int slot_load(uint32_t i, void *p, uint32_t n)
{
    const uint8_t *a = f_slot[i];
    if (i >= NSLOT || n > SLOT_MAX || f_get32(a) != 0x4B4E4236u || f_get32(a + 4) != n) return -1;
    memcpy(p, a + 12, n);
    return f_crc32(p, n) == f_get32(a + 8) ? 0 : -1;
}
static int slot_save(uint32_t i, const void *p, uint32_t n)
{
    uint8_t *a = f_slot[i];
    if (i >= NSLOT || n > SLOT_MAX || f_dead) return -1;
    f_ops++;                                    /* 1: erase, the bytes */
    memset(a, 0xFF, SLOT_MAX + 12u);
    if (f_cut_now() && !f_cut_full) { memcpy(a + 12, p, frnd() % (n + 1u)); f_dead = 1; return -1; }
    memcpy(a + 12, p, n);
    if (f_cut_now()) { f_dead = 1; return -1; }
    f_ops++;                                    /* 2: the header (a cut one: not there) */
    if (f_cut_now() && !f_cut_full) { f_dead = 1; return -1; }
    f_put32(a, 0x4B4E4236u), f_put32(a + 4, n), f_put32(a + 8, f_crc32(p, n));
    if (f_cut_now()) { f_dead = 1; return -1; }
    return 0;
}

/* the sequence store's sectors (test_seq_store.c's NOR model) */
static uint8_t F[SEQST_SECTORS][0x1000u];
static uint8_t *f_at(uint32_t off, uint32_t n)
{
    uint32_t i;
    if (!FL_GUARD(off, n)) { f_guard_bad++; return NULL; }
    for (i = 0; i < SEQST_SECTORS; i++)
        if (off >= SEQST_SECTOR_AT(i) && off + n <= SEQST_SECTOR_AT(i) + 0x1000u) return &F[i][off - SEQST_SECTOR_AT(i)];
    f_guard_bad++;
    return NULL;
}
static uint32_t f_read_fail, f_reads;            /* the next f_read_fail reads of the sequence store fail */
static int f_seq_refuse;                        /* the sequence store's programs and erases are refused */
static int seqst_fl_read(uint32_t off, void *p, uint32_t n)
{
    const uint8_t *a = f_at(off, n);
    f_reads++;
    if (f_read_fail) { f_read_fail--; return -1; }
    if (!a) return -1;
    memcpy(p, a, n);
    return 0;
}
static int seqst_fl_erase(uint32_t off)
{
    uint8_t *a = f_at(off, 0x1000u);
    uint32_t i, k;
    if (!a || (off & 0xFFFu) || f_dead || f_seq_refuse) return -1;
    f_ops++, f_erases++;
    if (f_cut_now() && !f_cut_full) {
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
    if (!a || f_dead || f_seq_refuse) return -1;
    while (n) {
        uint32_t chunk = 256u - (off & 0xFFu), i, k;
        if (chunk > n) chunk = n;
        f_ops++;
        k = f_cut_now() && !f_cut_full ? frnd() % (chunk + 1u) : chunk;
        for (i = 0; i < k; i++) {
            if ((a[i] & s[i]) != s[i]) f_overwrite++;
            a[i] &= s[i];
        }
        if (k < chunk) a[k] &= (uint8_t)(s[k] | frnd());
        if (f_cut_now()) { f_dead = 1; return -1; }
        off += chunk, a += chunk, s += chunk, n -= chunk;
    }
    return 0;
}

#ifndef PAIRING_NO_BANK
#define KNOBI_FACTORY_H "../../../tests/unit/factory_test_seq.h"   /* (from firmware/src/params/) */
#endif
#include "params/store_prog.c"
#include "params/seq_store.c"
#include "params/prog_seq.c"

static void fresh(void) { memset(F, 0xFF, sizeof F); memset(f_slot, 0, sizeof f_slot); }
static void power_cycle(void)
{
    f_dead = 0, f_cut_at = -1, f_cut_full = 0;
    prog_buf_slot = -1, prog_scanned = 0, prog_seq = 0, prog_next = 0;   /* (RAM gone: the next load reads the flash) */
    prog_seq_boot();
}

/* ---- programs and sequences */
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
static void seq_full(seq_t *s, int len, int notes)
{
    seq_clear(s);
    for (int i = 0; i < len; i++) {
        uint8_t n[6], v[6];
        for (int k = 0; k < notes; k++) { n[k] = (uint8_t)(12 + rnd() % 97); v[k] = (uint8_t)(1 + rnd() % 127); }
        seq_rec_step(s, n, v, notes);
    }
}
static void rand_seq(seq_t *s)
{
    seq_clear(s);
    int len = (int)(1 + rnd() % SEQ_MAX_STEPS);
    for (int i = 0; i < len; i++) {
        uint8_t n[6], v[6];
        int c = (int)(rnd() % 4 == 0 ? 0 : 1 + rnd() % 6);
        for (int k = 0; k < c; k++) { n[k] = (uint8_t)(12 + rnd() % 97); v[k] = (uint8_t)(1 + rnd() % 127); }
        if (c == 0) seq_rec_rest(s);
        else if (i > 0 && s->step[i - 1].count && rnd() % 5 == 0) seq_rec_tie(s);
        else seq_rec_step(s, n, v, c);
    }
}
static int prog_eq(const program_t *a, const program_t *b)   /* what a program is (its record's generation aside) */
{
    return !memcmp(a->name, b->name, PROGRAM_NAME_LEN) && !memcmp(a->v, b->v, sizeof a->v[0] * P_COUNT_RESERVED) &&
           !memcmp(a->chord, b->chord, 6);
}
static void edit_params(program_t *p)
{
    for (int k = 0; k < 4; k++) {
        uint16_t id = (uint16_t)(rnd() % P_COUNT_PHASE1);
        p->v[id] = param_clamp(id, PARAMS[id].min + (int32_t)(rnd() % (uint32_t)(PARAMS[id].max - PARAMS[id].min + 1)));
    }
    p->name[0] = (char)('A' + rnd() % 26);
}

/* the model: what each program followed must reload as */
#define MP 12u
static const uint16_t MPROG[MP] = {0, 1, 2, 3, 4, 73, 74, 75, 148, 299, 430, 499};
static program_t mp[MP];
static seq_t ms[MP];
static int m_index(uint16_t n) { for (unsigned i = 0; i < MP; i++) if (MPROG[i] == n) return (int)i; return -1; }
static void model_from_flash(void)
{
    for (unsigned i = 0; i < MP; i++) { (void)prog_open(MPROG[i], &mp[i]); ms[i] = *edit_seq(); }
}
static int model_holds(unsigned i)
{
    static program_t p;
    (void)prog_open(MPROG[i], &p);
    return prog_eq(&p, &mp[i]) && seq_eq(edit_seq(), &ms[i]);
}
static int model_all(void) { int ok = 1; for (unsigned i = 0; i < MP; i++) ok &= model_holds(i); return ok; }

/* one WRITE, prepared the same way every time it is replayed: open `from`, the edit (action), WRITE to `to` */
enum { A_KEEP, A_NEW, A_CLEAR, A_FACTORY, A_COUNT };
typedef struct { uint16_t from, to; int action; uint32_t seed; } wr_t;
static program_t w_p;
static seq_t w_s;
static void wr_prepare(const wr_t *w)        /* the edit buffer ready; w_p / w_s what the WRITE must store */
{
    uint32_t keep = rs;
    rs = w->seed;
    (void)prog_open(w->from, &w_p);
    if (w->action == A_FACTORY) { (void)prog_factory(w->from, &w_p); prog_seq_factory(w->from); }
    else edit_params(&w_p);
    if (w->action == A_NEW) rand_seq(edit_seq_mut());
    if (w->action == A_CLEAR) seq_clear(edit_seq_mut());
    w_s = *edit_seq();
    rs = keep;
}
static wr_t wr_random(void)
{
    wr_t w;
    w.from = MPROG[rnd() % MP];
    w.to = rnd() % 5 == 0 ? MPROG[rnd() % MP] : w.from;
    w.action = (int)(rnd() % A_COUNT);
    w.seed = rnd() | 1u;
    return w;
}
static int wr_do(const wr_t *w)             /* prepared, written, the model updated */
{
    int rc;
    wr_prepare(w);
    rc = prog_write(w->to, &w_p);
    if (rc == 0) { int i = m_index(w->to); mp[i] = w_p, ms[i] = w_s; }
    return rc;
}

/* ---- the tests */
static uint32_t gen_of(uint16_t n) { static program_t p; (void)prog_load(n, &p); return program_seq_gen(&p); }

static void write_pairs(void)
{
    int ok = 1, bad = 0, g0 = 0, reuse = 0, put = 0;
    fresh(); power_cycle(); model_from_flash();
    for (int r = 0; r < 400; r++) {
        wr_t w = wr_random();
        uint32_t before = gen_of(w.to);
        if (wr_do(&w) != 0) { ok = 0; continue; }
        uint32_t g = gen_of(w.to);
        if (!g) g0++;
        else if (g == before) reuse++;
        else put++;
        if (!model_holds((unsigned)m_index(w.to))) bad++;
        if (r % 25 == 24) { power_cycle(); if (!model_all()) bad++; }
    }
    power_cycle();
    ok &= model_all() && !bad && g0 && reuse && put && !f_overwrite && !f_guard_bad;
    printf("write_pairs: 400 WRITEs (%d gen 0, %d same generation, %d new entries), used %u B; %d bad: %d\n", g0, reuse,
           put, (unsigned)seqst_used(), bad, ok);
    CHECK(ok);
}

static uint8_t snap_F[sizeof F], snap_slot[sizeof f_slot];
static program_t snap_mp[MP];
static seq_t snap_ms[MP];
static void snap(void) { memcpy(snap_F, F, sizeof F), memcpy(snap_slot, f_slot, sizeof f_slot); memcpy(snap_mp, mp, sizeof mp), memcpy(snap_ms, ms, sizeof ms); }
static void unsnap(void) { memcpy(F, snap_F, sizeof F), memcpy(f_slot, snap_slot, sizeof f_slot); memcpy(mp, snap_mp, sizeof mp), memcpy(ms, snap_ms, sizeof ms); }

#ifndef PAIRING_NO_BANK
#define PC_ROUNDS 200                           /* WRITEs cut at every operation (fewer without a bank: the same code) */
#else
#define PC_ROUNDS 50
#endif
static void write_powercut_pairs(void)
{
    int ok = 1, bad = 0, others_bad = 0, after_bad = 0;
    uint32_t cuts = 0, got_old = 0, got_new = 0, compacting = 0, compacted = 0;
    static program_t p, oldp;
    static seq_t olds;
    fresh(); power_cycle(); model_from_flash();
    for (uint16_t k = 0; k < 70; k++) {          /* the store near full: 70 other programs with full-size sequences */
        (void)prog_open((uint16_t)(150 + k), &p);
        seq_full(edit_seq_mut(), 64, 6);
        if (prog_write((uint16_t)(150 + k), &p) != 0) break;
    }
    printf("write_powercut_pairs: store %u of %u B before\n", (unsigned)seqst_used(), (unsigned)seqst_total());
    for (int r = 0; r < PC_ROUNDS; r++) {
        wr_t w = wr_random();
        uint32_t ops0, nops, er0;
        int i = m_index(w.to);
        power_cycle();
        snap();
        oldp = mp[i], olds = ms[i];
        ops0 = f_ops, er0 = f_erases;            /* how many operations this WRITE takes */
        if (wr_do(&w) != 0) { ok = 0; break; }
        nops = f_ops - ops0;
        if (f_erases != er0) compacting++;
        if (f_erases - er0 >= 2u) compacted++;  /* (a compaction: the spare erased, then the source) */
        for (uint32_t c = 1; c <= nops; c++)
            for (int full = 0; full < 2; full++) {
                unsnap(); power_cycle();
                wr_prepare(&w);
                f_cut_at = (int32_t)(f_ops + c), f_cut_full = full;
                (void)prog_write(w.to, &w_p);
                power_cycle();
                cuts++;
                (void)prog_open(w.to, &p);
                if (prog_eq(&p, &oldp) && seq_eq(edit_seq(), &olds)) got_old++;
                else if (prog_eq(&p, &w_p) && seq_eq(edit_seq(), &w_s)) got_new++;
                else bad++;
                for (unsigned j = 0; j < MP; j++)   /* the others: as they were */
                    if ((int)j != i && (j + c) % 3 == 0 && !model_holds(j)) others_bad++;
                {                                   /* and the next WRITE works */
                    wr_t v = { w.to, w.to, A_NEW, (uint32_t)(c * 7919u + (uint32_t)full) };
                    wr_prepare(&v);
                    if (prog_write(v.to, &w_p) != 0) after_bad++;
                    else { power_cycle(); (void)prog_open(v.to, &p); if (!prog_eq(&p, &w_p) || !seq_eq(edit_seq(), &w_s)) after_bad++; }
                }
            }
        unsnap(); power_cycle();                    /* then the WRITE for real */
        if (wr_do(&w) != 0) { ok = 0; break; }
    }
    power_cycle();
    ok &= model_all() && !bad && !others_bad && !after_bad && got_old && got_new && compacted >= PC_ROUNDS / 100 && !f_overwrite && !f_guard_bad;
    printf("write_powercut_pairs: %d WRITEs (%u erasing, %u of them compacting), %u cuts: old pair %u, new pair %u, mixed %d; others %d, next "
           "WRITE %d bad: %d\n", PC_ROUNDS, (unsigned)compacting, (unsigned)compacted, (unsigned)cuts, (unsigned)got_old, (unsigned)got_new, bad, others_bad,
           after_bad, ok);
    CHECK(ok);
}

static void write_full_refuses_whole(void)
{
    int ok = 1, rc = 0;
    uint16_t n;
    static program_t p, q, p0;
    static seq_t want;
    fresh(); power_cycle();
    for (n = 100; n < 400; n++) {                /* full-size sequences until the store refuses */
        (void)prog_open(n, &p);
        edit_params(&p);
        seq_full(edit_seq_mut(), 64, 6);
        if ((rc = prog_write(n, &p)) != 0) break;
    }
    ok &= rc == PROG_ERR_SEQ_FULL;
    {
        uint32_t ops = f_ops;
        snap();                                  /* again: nothing may change, no flash operation */
        want = *edit_seq();
        rc = prog_write(n, &p);
        ok &= rc == PROG_ERR_SEQ_FULL && f_ops == ops && !memcmp(F, snap_F, sizeof F) && !memcmp(f_slot, snap_slot, sizeof f_slot);
        ok &= seq_eq(edit_seq(), &want);         /* (the edit buffer kept: WRITE again once there is room) */
        rc = prog_write((uint16_t)(n + 1u), &p); /* to another program: refused too, nothing written */
        ok &= rc == PROG_ERR_SEQ_FULL && f_ops == ops && !memcmp(f_slot, snap_slot, sizeof f_slot);
    }
    power_cycle();
    (void)prog_open(n, &q);
    program_init(&p0);
    ok &= prog_eq(&q, &p0) && edit_seq()->len == 0;   /* (the old program n: never written, init) */
    ok &= gen_of(n) == 0;
    for (uint16_t k = 100; k < 102; k++) {       /* free room: clear two sequences (empty entries use the reserve) */
        (void)prog_open(k, &q);
        seq_clear(edit_seq_mut());
        ok &= prog_write(k, &q) == 0;
    }
    (void)prog_open(n, &q);
    *edit_seq_mut() = want;
    ok &= prog_write(n, &p) == 0;
    power_cycle();
    (void)prog_open(n, &q);
    ok &= prog_eq(&q, &p) && seq_eq(edit_seq(), &want);
    printf("write_full_refuses_whole: %u full-size sequences, then SEQ FULL with no flash operation, record unchanged; "
           "after clearing 2: written: %d\n", (unsigned)(n - 100u), ok);
    CHECK(ok);
}

/* a record of an older version (params.c's layout), as a phase-1 / phase-2 firmware wrote it */
static uint32_t rec_old(unsigned ver, const program_t *p, uint8_t *b)
{
    uint32_t size = program_packed_size(ver), np = (size - 29u) / 2u, o = 0;
    b[o++] = (uint8_t)ver;
    memcpy(b + o, p->name, PROGRAM_NAME_LEN), o += PROGRAM_NAME_LEN;
    for (uint32_t i = 0; i < np; i++) b[o++] = (uint8_t)((uint16_t)p->v[i] & 0xFF), b[o++] = (uint8_t)((uint16_t)p->v[i] >> 8);
    memcpy(b + o, p->chord, 6), o += 6;
    uint16_t c = program_crc16(b, o);
    b[o++] = (uint8_t)c, b[o++] = (uint8_t)(c >> 8);
    return o;
}
static void inject(uint16_t n, const uint8_t *rec, uint32_t size)   /* record n's bytes in its slot, the slot's CRC kept */
{
    static program_t init;
    uint32_t l = n / PROG_PER_SLOT, ph, off;
    program_init(&init);
    prog_fetch(l);                              /* (the slot map read) */
    if (prog_map[l] < 0) CHECK(prog_save(n, &init) == 0);
    ph = (uint32_t)prog_map[l], off = 12u + n % PROG_PER_SLOT * PROG_STRIDE;
    memset(f_slot[ph] + off, 0, PROG_STRIDE);
    memcpy(f_slot[ph] + off, rec, size);
    f_put32(f_slot[ph] + 8, f_crc32(f_slot[ph] + 12, f_get32(f_slot[ph] + 4)));
    power_cycle();
}

static void load_fallbacks(void)
{
    int ok = 1;
    uint8_t rec[PROGRAM_PACKED_MAX];
    uint32_t size, used;
    uint16_t g = 0;
    static program_t p;
    static seq_t f0, f1, e;
    seq_clear(&e);
    fresh(); power_cycle();
#ifndef PAIRING_NO_BANK
    ok &= factory_seq(0, &f0) == 0 && f0.len == 4 && f0.step[1].count == 0 && f0.step[2].count == 3 && f0.step[3].tie;
    ok &= factory_seq(1, &f1) == 0 && f1.len == 2 && f1.step[0].count == 6 && f1.step[0].notes[4] == 108;
    ok &= factory_seq(2, &e) == -1 && e.len == 0;        /* (an empty span: none) */
    ok &= factory_seq(3, &e) == -1 && factory_seq(499, &e) == -1 && e.len == 0;
    /* never written: the factory program and its sequence */
    ok &= prog_open(0, &p) == 1 && seq_eq(edit_seq(), &f0);
    ok &= prog_seq_open(1, prog_load(1, &p), &p) == 1 && seq_eq(edit_seq(), &f1);
    ok &= prog_open(2, &p) == 1 && edit_seq()->len == 0;
    ok &= prog_open(7, &p) == -1 && edit_seq()->len == 0;   /* past the bank: init, empty */
#else
    ok &= factory_seq(0, &f0) == -1 && f0.len == 0;          /* no bank: every factory sequence empty */
    f1 = f0;
    ok &= prog_open(0, &p) == -1 && edit_seq()->len == 0;
#endif
    /* a version-2 and a version-1 record: generation 0, the factory sequence */
    program_init(&p), memcpy(p.name, "OLD V2", 6), p.v[P_BPM] = 99;
    size = rec_old(2, &p, rec), inject(0, rec, size);
    ok &= prog_open(0, &p) == 0 && p.v[P_BPM] == 99 && program_seq_gen(&p) == 0 && seq_eq(edit_seq(), &f0);
    program_init(&p), memcpy(p.name, "OLD V1", 6);
    size = rec_old(1, &p, rec), inject(1, rec, size);
    ok &= prog_open(1, &p) == 0 && !memcmp(p.name, "OLD V1", 6) && program_seq_gen(&p) == 0 && seq_eq(edit_seq(), &f1);
    /* an explicit empty entry beats the factory sequence */
    (void)prog_open(0, &p);
    seq_clear(edit_seq_mut());
    ok &= prog_write(0, &p) == 0 && (g = (uint16_t)gen_of(0)) != 0;
    power_cycle();
    ok &= prog_open(0, &p) == 0 && edit_seq()->len == 0;
    /* RESTORE FACTORY: the factory sequence back in the edit buffer; its WRITE names no entry and frees the old ones */
    (void)prog_factory(0, &p);
    prog_seq_factory(0);
    ok &= seq_eq(edit_seq(), &f0);
    used = seqst_used();
    ok &= prog_write(0, &p) == 0 && gen_of(0) == 0;
    ok &= seqst_used() < used && !seqst_get(0, g, &e);       /* (the empty entry dead: only its release counts) */
    power_cycle();
    ok &= prog_open(0, &p) == 0 && seq_eq(edit_seq(), &f0);
    /* an edited sequence written over a RESTORE FACTORY one, then restored again: back to gen 0 */
    (void)prog_open(1, &p);
    rand_seq(edit_seq_mut());
    ok &= prog_write(1, &p) == 0 && gen_of(1) != 0;
    prog_seq_factory(1);
    ok &= seq_eq(edit_seq(), &f1) && prog_write(1, &p) == 0 && gen_of(1) == 0;
    power_cycle();
    ok &= prog_open(1, &p) == 0 && seq_eq(edit_seq(), &f1);
    /* a broken record: the init program, an empty sequence (never the factory one); its WRITE stores the empty one */
    program_init(&p);
    size = (uint32_t)program_pack(&p, rec, sizeof rec);
    rec[3] ^= 0x40u;                            /* (a name byte: the record's CRC16 fails) */
    inject(0, rec, size);
    ok &= prog_open(0, &p) == -2 && edit_seq()->len == 0;
    ok &= prog_seq_open(0, -2, &p) == -2;
    ok &= prog_write(0, &p) == 0 && gen_of(0) != 0;
    power_cycle();
    ok &= prog_open(0, &p) == 0 && edit_seq()->len == 0;
    /* a record naming a generation the store does not have: empty (-3); a WRITE without a sequence edit keeps naming
     * it and frees nothing; an explicit clear stores the empty one (seq_missing_kept: the entry coming back) */
    program_init(&p), program_set_seq_gen(&p, 777);
    size = (uint32_t)program_pack(&p, rec, sizeof rec), inject(3, rec, size);
    ok &= prog_open(3, &p) == 0 && program_seq_gen(&p) == 777 && edit_seq()->len == 0;
    ok &= prog_seq_open(3, 0, &p) == -3;
    used = seqst_used();
    memcpy(p.name, "RENAMED", 7);
    ok &= prog_write(3, &p) == 0 && gen_of(3) == 777 && seqst_used() == used;
    power_cycle();
    ok &= prog_open(3, &p) == 0 && !memcmp(p.name, "RENAMED", 7) && prog_seq_open(3, 0, &p) == -3;
    seq_clear(edit_seq_mut());
    ok &= prog_write(3, &p) == 0 && gen_of(3) != 0 && gen_of(3) != 777;
    power_cycle();
    ok &= prog_open(3, &p) == 0 && prog_seq_open(3, 0, &p) == 0 && edit_seq()->len == 0;
    printf("load_fallbacks (%s): v1/v2 -> factory, explicit empty beats it, RESTORE FACTORY + WRITE frees, broken / "
           "missing -> empty: %d\n",
#ifndef PAIRING_NO_BANK
           "bank with sequences",
#else
           "no bank",
#endif
           ok);
    CHECK(ok);
}

static void edit_lost_without_write(void)
{
    int ok = 1;
    static program_t p;
    static seq_t a, b;
    fresh(); power_cycle();
    (void)prog_open(10, &p);
    rand_seq(edit_seq_mut());
    a = *edit_seq();
    ok &= prog_write(10, &p) == 0;
    (void)prog_open(10, &p);
    rand_seq(edit_seq_mut());                   /* edited, not written */
    b = *edit_seq();
    ok &= !seq_eq(&a, &b);
    (void)prog_open(11, &p);                    /* a program change: the edit is gone */
    ok &= edit_seq()->len == 0;
    (void)prog_open(10, &p);
    ok &= seq_eq(edit_seq(), &a);
    *edit_seq_mut() = b;                        /* edited again, then the power goes */
    power_cycle();
    ok &= edit_seq()->len == 0;
    (void)prog_open(10, &p);
    ok &= seq_eq(edit_seq(), &a);
    *edit_seq_mut() = b;                        /* edited, then the same program loaded again (a program change to
                                                 * itself): gone too */
    (void)prog_open(10, &p);
    ok &= seq_eq(edit_seq(), &a);
    printf("edit_lost_without_write: a program change or a power cut drops the edited sequence: %d\n", ok);
    CHECK(ok);
}

/* a stored entry that does not read (a read error): retried once; still unreadable, the panel is told (-3) and a
 * WRITE that leaves the sequence alone keeps it named and released nothing, so it reads again after a reboot */
static void seq_missing_kept(void)
{
    int ok = 1;
    uint32_t r0, k, used;
    uint16_t g = 0;
    static program_t p;
    static seq_t a, out;
    fresh(); power_cycle();
    (void)prog_open(4, &p);
    rand_seq(edit_seq_mut());
    a = *edit_seq();
    ok &= prog_write(4, &p) == 0 && (g = (uint16_t)gen_of(4)) != 0;
    power_cycle();
    r0 = f_reads, f_read_fail = 1000u;          /* how many reads one failing seqst_get makes */
    (void)seqst_get(4, g, &out);
    k = f_reads - r0, f_read_fail = k;          /* the first get fails, the retry reads it */
    ok &= prog_open(4, &p) == 0 && seq_eq(edit_seq(), &a);
    f_read_fail = 2u * k;                       /* both fail: missing */
    ok &= prog_open(4, &p) == 0 && f_read_fail == 0 && edit_seq()->len == 0;
    f_read_fail = 2u * k;
    ok &= prog_seq_open(4, 0, &p) == -3;
    used = seqst_used();
    memcpy(p.name, "KEPT", 4);
    ok &= prog_write(4, &p) == 0 && gen_of(4) == g && seqst_used() == used;   /* (no put, no release) */
    power_cycle();
    ok &= prog_open(4, &p) == 0 && !memcmp(p.name, "KEPT", 4) && seq_eq(edit_seq(), &a);   /* back */
    f_read_fail = 2u * k;                       /* missing again; this time the sequence is edited: stored */
    ok &= prog_open(4, &p) == 0 && edit_seq()->len == 0;
    rand_seq(edit_seq_mut());
    a = *edit_seq();
    ok &= prog_write(4, &p) == 0 && gen_of(4) != g;
    power_cycle();
    ok &= prog_open(4, &p) == 0 && seq_eq(edit_seq(), &a) && !seqst_get(4, g, &out);
    f_read_fail = 0;
    printf("seq_missing_kept: one failed read retried; unreadable: -3, an unedited WRITE keeps gen %u and frees nothing, "
           "back after a reboot; an edit replaces it: %d\n", (unsigned)g, ok);
    CHECK(ok);
}

/* the flash refuses the sequence's put: WRITE FAILED (not SEQ FULL), nothing changed */
static void write_flash_error(void)
{
    int ok = 1, rc;
    static program_t p, q, old;
    static seq_t a, b;
    fresh(); power_cycle();
    (void)prog_open(5, &p);
    rand_seq(edit_seq_mut());
    a = *edit_seq();
    ok &= prog_write(5, &p) == 0;
    old = p;
    edit_params(&p);
    rand_seq(edit_seq_mut());
    b = *edit_seq();
    snap();
    f_seq_refuse = 1;
    rc = prog_write(5, &p);
    f_seq_refuse = 0;
    ok &= rc == PROG_ERR_FLASH && !memcmp(f_slot, snap_slot, sizeof f_slot);
    ok &= seq_eq(edit_seq(), &b);               /* (the edit kept: WRITE again) */
    power_cycle();
    ok &= prog_open(5, &q) == 0 && prog_eq(&q, &old) && seq_eq(edit_seq(), &a);
    *edit_seq_mut() = b;
    ok &= prog_write(5, &p) == 0;
    power_cycle();
    ok &= prog_open(5, &q) == 0 && prog_eq(&q, &p) && seq_eq(edit_seq(), &b);
    printf("write_flash_error: a refused put -> PROG_ERR_FLASH (not SEQ FULL), record and sequence unchanged: %d\n", ok);
    CHECK(ok);
}

/* a change made through a cast of edit_seq() (bypassing edit_seq_mut) is still stored as an edit */
static void edit_bypass_caught(void)
{
    int ok = 1;
    uint16_t g = 0;
    static program_t p;
    static seq_t a;
    fresh(); power_cycle();
    (void)prog_open(6, &p);
    rand_seq(edit_seq_mut());
    ok &= prog_write(6, &p) == 0 && (g = (uint16_t)gen_of(6)) != 0;
    (void)prog_open(6, &p);
    seq_rec_back((seq_t *)edit_seq());          /* (what Task 5 must not do) */
    a = *edit_seq();
    ok &= prog_write(6, &p) == 0 && gen_of(6) != g;
    power_cycle();
    ok &= prog_open(6, &p) == 0 && seq_eq(edit_seq(), &a);
    printf("edit_bypass_caught: a change behind edit_seq_mut's back is stored as an edit: %d\n", ok);
    CHECK(ok);
}

static void record_v3_compat(void)
{
    int ok = 1;
    uint8_t rec[PROGRAM_PACKED_MAX];
    uint32_t size;
    static program_t p, q;
    ok &= PROGRAM_VERSION == 3 && program_packed_size(3) == 1u + PROGRAM_NAME_LEN + 2u * P_COUNT_STORED + 8u;
    ok &= program_packed_size(3) <= PROG_STRIDE && program_packed_size(2) < program_packed_size(3);
    ok &= P_SEQ_GEN == P_COUNT_RESERVED && !P_IS_LIVE(P_SEQ_GEN) && !param_get(P_SEQ_GEN);
    program_init(&p);
    ok &= program_seq_gen(&p) == 0;
    for (uint32_t g = 1; g <= 65535u; g += g < 4u ? 1u : 4093u) {   /* 1..65535 survive pack / unpack unclamped */
        program_set_seq_gen(&p, (uint16_t)g);
        ok &= program_pack(&p, rec, sizeof rec) == (int)program_packed_size(3) && program_unpack(&q, rec, program_packed_size(3)) == 0;
        ok &= program_seq_gen(&q) == g && prog_eq(&p, &q);
    }
    program_set_seq_gen(&p, 65535u);
    ok &= program_pack(&p, rec, sizeof rec) > 0 && !program_unpack(&q, rec, program_packed_size(3)) && program_seq_gen(&q) == 65535u;
    /* a version-2 record (every reserved value set) loads with generation 0, its values kept */
    program_init(&p);
    p.v[P_BPM] = 140, p.v[P_ARP_ON] = 1, p.v[P_FXB_DIV] = 7;
    size = rec_old(2, &p, rec);
    ok &= size == program_packed_size(2) && program_unpack(&q, rec, size) == 0 && program_seq_gen(&q) == 0 &&
          q.v[P_BPM] == 140 && q.v[P_ARP_ON] == 1 && q.v[P_FXB_DIV] == 7 && q.version == 3;
    /* in the store: a v2 record and a v3 one side by side in one slot */
    fresh(); power_cycle();
    inject(20, rec, size);
    program_set_seq_gen(&p, 5);
    ok &= prog_save(21, &p) == 0;
    power_cycle();
    ok &= prog_load(20, &q) == 0 && program_seq_gen(&q) == 0 && q.v[P_BPM] == 140;
    ok &= prog_load(21, &q) == 0 && program_seq_gen(&q) == 5;
    printf("record_v3_compat: v3 %u B (stride %u), v2 %u B loads with gen 0, gen 1..65535 round trip: %d\n",
           (unsigned)program_packed_size(3), (unsigned)PROG_STRIDE, (unsigned)program_packed_size(2), ok);
    CHECK(ok);
}

int main(void)
{
    (void)smk_owner_tag;                       /* (store.h: not this test's) */
    record_v3_compat();
    load_fallbacks();
    edit_lost_without_write();
    seq_missing_kept();
    write_flash_error();
    edit_bypass_caught();
    write_pairs();
    write_full_refuses_whole();
    write_powercut_pairs();
    if (fails) { printf("%d pairing test(s) failed\n", fails); return 1; }
    printf("pairing tests passed\n");
    return 0;
}
