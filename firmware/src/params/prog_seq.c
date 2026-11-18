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
 *   es_dirty  changed since (edit_seq_mut): a WRITE puts a new entry
 * A WRITE stores a new entry unless the sequence is es_prog's own, unedited, and written to es_prog (prog_seq.h). */
#ifndef KNOBI_PROG_SEQ_C
#define KNOBI_PROG_SEQ_C
#include "prog_seq.h"

#ifdef DINGHY_HOST
static seq_t es_seq;
#else
static seq_t es_seq __attribute__((section(".pool")));   /* (897 B; the pool: not in .bss) */
#endif
static int32_t es_prog = -1;
static uint16_t es_gen;
static uint8_t es_fb, es_dirty;
static program_t prog_wr;                       /* the record WRITE stores: the program, its generation */

void prog_seq_boot(void)
{
    (void)seqst_init();
    seq_clear(&es_seq);
    es_prog = -1, es_gen = 0, es_fb = 0, es_dirty = 0;
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
seq_t *edit_seq_mut(void) { es_dirty = 1; return &es_seq; }

int prog_seq_open(uint16_t n, int load_rc, const program_t *p)
{
    uint16_t g = load_rc == 0 ? program_seq_gen(p) : 0;
    es_prog = n, es_gen = 0, es_fb = 0, es_dirty = 0;
    if (load_rc == -2) { seq_clear(&es_seq); return -2; }   /* a broken record: empty, never the factory sequence */
    if (g) {
        if (seqst_get(n, g, &es_seq) == 1) { es_gen = g; return 0; }
        seq_clear(&es_seq);                     /* (named, not there: empty; es_gen 0 so a WRITE stores it) */
        return -2;
    }
    (void)factory_seq(n, &es_seq);              /* no generation, never written: the factory sequence (or empty) */
    es_fb = 1;
    return 1;
}

void prog_seq_factory(uint16_t n)
{
    (void)factory_seq(n, &es_seq);
    es_prog = n, es_gen = 0, es_fb = 1, es_dirty = 0;
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
    int own = !es_dirty && es_prog == (int32_t)n;
    if (n >= PROG_COUNT) return -1;
    if (!seqst_total()) g = 0;                  /* (no sequence store on this board: the program only) */
    else if (own && es_gen) g = es_gen;         /* the entry it already is */
    else if (own && es_fb) g = 0;               /* the factory sequence, unedited: no entry */
    else if (seqst_put(n, &es_seq, &g)) return PROG_ERR_SEQ_FULL;   /* (nothing written: refused before any flash
                                                 * operation, or a flash error that leaves the store as it was) */
    prog_wr = *p;
    program_set_seq_gen(&prog_wr, g);
    if (prog_save(n, &prog_wr)) return -1;      /* the old record still holds (and names the old entry) */
    if (g) seqst_release(n, g);                 /* the older entries reclaimable */
    else seqst_drop(n);                         /* every entry of n reclaimable */
    es_prog = n, es_gen = g, es_fb = (uint8_t)!g, es_dirty = 0;
    return 0;
}

#endif /* KNOBI_PROG_SEQ_C */
