/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX: a program and its sequence, paired (prog_seq.h: the rules). Included by app.c after params/store_prog.c
 * (prog_load / prog_save / prog_factory, and the factory bank header it includes) and params/seq_store.c; on the host
 * tests/unit/test_pairing.c includes it the same way. Main loop only; integers only.
 *
 * The edit buffer's sequence and where it came from:
 *   es_prog   the program it belongs to (loaded from, restored from, or written to), -1 none
 *   es_gen    the stored entry it is (1..65535), 0 none
 *   es_fb     it is the factory fallback of es_prog (the record named no entry, or RESTORE FACTORY): a WRITE of it
 *             unedited to es_prog keeps generation 0, so no entry is spent on a copy of the factory sequence
 *   es_miss   es_gen is what the record names but the store did not give it (the buffer holds an empty sequence in
 *             its place): an unedited WRITE keeps naming es_gen and releases nothing, so the entry is not given up
 *   es_dirty  changed since (edit_seq_mut): a WRITE puts a new entry
 *   es_sum    a hash of es_seq when es_dirty was last cleared: a change that bypassed edit_seq_mut (a cast of
 *             edit_seq()) is still seen as an edit at WRITE, never stored under the old generation
 * A WRITE stores a new entry unless the sequence is es_prog's own, unedited, and written to es_prog (prog_seq.h). */
#ifndef KNOBI_PROG_SEQ_C
#define KNOBI_PROG_SEQ_C
#include "prog_seq.h"

#ifdef KNOBI_CORE
#include "../core/state/prog_seq.h"   /* knobi_core: per instance (tools/core_state.py) */
#else
#ifdef DINGHY_HOST
static seq_t es_seq;
#else
static seq_t es_seq __attribute__((section(".pool")));   /* (897 B; the pool: not in .bss) */
#endif
static int32_t es_prog = -1;
static uint16_t es_gen;
static uint8_t es_fb, es_dirty, es_miss;
static uint32_t es_sum;
static uint32_t es_ver;                         /* edit_seq_ver: bumped by every change (or a pointer that may make one) */
static program_t prog_wr;                       /* the record WRITE stores: the program, its generation */
#endif

static uint32_t es_hash(void)                   /* FNV-1a over the whole buffer */
{
    const uint8_t *b = (const uint8_t *)&es_seq;
    uint32_t h = 2166136261u, i;
    for (i = 0; i < sizeof es_seq; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}
static void es_set(int32_t n, uint16_t g, int fb, int miss)   /* the source, unedited */
{
    es_prog = n, es_gen = g, es_fb = (uint8_t)fb, es_miss = (uint8_t)miss, es_dirty = 0;
    es_sum = es_hash();
    es_ver++;
}

void prog_seq_boot(void)
{
    (void)seqst_init();
    seq_clear(&es_seq);
    es_set(-1, 0, 0, 0);
}

int factory_seq(uint16_t prog, seq_t *out)
{
#if defined FACTORY_SEQ_COUNT
    if (prog < FACTORY_SEQ_COUNT) {
        uint32_t a = factory_seq_at[prog], b = factory_seq_at[prog + 1u];
        if (b > a && seq_unpack(out, factory_seq_data + a, b - a) == 0 && seq_packed_size(out) == b - a) return 0;
    }
#else
    (void)prog;
#endif
    seq_clear(out);
    return -1;
}

const seq_t *edit_seq(void) { return &es_seq; }
seq_t *edit_seq_mut(void) { es_dirty = 1, es_ver++; return &es_seq; }
uint32_t edit_seq_ver(void) { return es_ver; }

int prog_seq_open(uint16_t n, int load_rc, const program_t *p)
{
    uint16_t g = load_rc == 0 ? program_seq_gen(p) : 0;
    if (load_rc == -2) { seq_clear(&es_seq); es_set(n, 0, 0, 0); return -2; }   /* a broken record: empty, never the
                                                 * factory sequence */
    if (g) {
        if (seqst_get(n, g, &es_seq) == 1 || seqst_get(n, g, &es_seq) == 1) { es_set(n, g, 0, 0); return 0; }   /* (a
                                                 * read that failed once: once more) */
        seq_clear(&es_seq);                     /* named, not there: empty in its place, the name kept (es_miss) */
        es_set(n, g, 0, 1);
        return -3;
    }
    (void)factory_seq(n, &es_seq);              /* no generation, never written: the factory sequence (or empty) */
    es_set(n, 0, 1, 0);
    return 1;
}

void prog_seq_factory(uint16_t n)
{
    (void)factory_seq(n, &es_seq);
    es_set(n, 0, 1, 0);
}

int prog_open(uint16_t n, program_t *p)
{
    int rc = prog_load(n, p);
    (void)prog_seq_open(n, rc, p);
    return rc;
}

int prog_write(uint16_t n, const program_t *p)
{
    uint16_t g = 0;
    int own, miss = 0, r;
    if (n >= PROG_COUNT) return PROG_ERR_FLASH;
    if (!es_dirty && es_hash() != es_sum) es_dirty = 1;   /* (changed behind edit_seq_mut's back: an edit all the same) */
    own = !es_dirty && es_prog == (int32_t)n;
    if (!seqst_total()) g = 0;                  /* (no sequence store on this board: the program only) */
    else if (own && es_gen) g = es_gen, miss = es_miss;   /* the entry it already is (or names: missing, kept) */
    else if (own && es_fb) g = 0;               /* the factory sequence, unedited: no entry */
    else {                                      /* a new entry, its generation newer than the one n's record names
                                                 * now (the store may have lost that one): until the record is
                                                 * written, that name must never resolve to this entry */
        uint16_t floor = prog_load(n, &prog_wr) == 0 ? program_seq_gen(&prog_wr) : 0;
        if ((r = seqst_put_above(n, &es_seq, floor, &g)) != 0)   /* nothing written: full (refused before any flash
                                                 * operation), or a flash error that leaves the store as it was */
            return r == -1 ? PROG_ERR_SEQ_FULL : PROG_ERR_FLASH;
    }
    prog_wr = *p;
    program_set_seq_gen(&prog_wr, g);
    if (prog_save(n, &prog_wr)) return PROG_ERR_FLASH;   /* the old record still holds (and names the old entry) */
    if (miss) {}                                /* (the missing entry stays named and nothing is released: it may read
                                                 * again after a reboot) */
    else if (g) seqst_release(n, g);            /* the older entries reclaimable */
    else seqst_drop(n);                         /* every entry of n reclaimable */
    es_set(n, g, !g, miss);
    return 0;
}

#endif /* KNOBI_PROG_SEQ_C */
