/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX: a program and its sequence, paired (Phase 3 spec, "Where sequences come from" and "One WRITE = program +
 * sequence, paired"). The edit buffer's sequence part (edit_seq), where it came from, the factory sequences, WRITE.
 * Main loop only. Included by app.c after params/store_prog.c and params/seq_store.c (the factory bank header that
 * store_prog.c includes, if any, is in scope: factory_seq reads it).
 *
 * The edit buffer: the program part is the engine's (engine.c eng_edit), the sequence part is edit_seq here. Like a
 * parameter edit, a sequence edit lives only there: a program change (prog_seq_open), RESTORE FACTORY
 * (prog_seq_factory) and a power cut replace it; only WRITE keeps it. Not autosaved.
 *
 * Where a program's sequence comes from (prog_seq_open, after prog_load):
 *   - its record (version 3) names a generation (P_SEQ_GEN, 1..65535): that entry of the sequence store (an empty
 *     entry included: the program was written with no sequence). Read twice before giving up; if the store still has
 *     no such entry (flash damage, a failed read) the sequence is empty and prog_seq_open says so (-3: the panel shows
 *     STORED SEQUENCE MISSING). The loss is not made permanent: a WRITE that does not edit the sequence keeps naming
 *     that generation and releases nothing (the entry may read again after a reboot); only an edit (or a clear)
 *     replaces it.
 *   - its record names none (0: a version 1 or 2 record, a program written with an unedited factory sequence) or it was
 *     never written: the factory sequence of the same number (factory_seq), empty without one.
 *   - its record is broken (prog_load -2: the init program): empty, never silently the factory one.
 *
 * WRITE (prog_write, under the WRITE fade: the flash holds the interrupts) to program n:
 *   1. the sequence: if the edit buffer's sequence is unedited and is program n's own (loaded from n, or RESTORE
 *      FACTORY of n), the record keeps naming what it named: the same generation (no put), or 0 when it came from the
 *      factory fallback. Otherwise seqst_put writes it as a new entry with a new generation (an empty sequence as an
 *      explicit empty entry, so the factory one does not come back); the old entries stay valid. If the store is full
 *      prog_write returns PROG_ERR_SEQ_FULL and nothing at all has changed (seqst_put refuses before any flash write;
 *      the program record is not touched); if the flash fails, PROG_ERR_FLASH, nothing changed either.
 *   2. the program record, with that generation in P_SEQ_GEN (prog_save: the slot store's own power-safe save).
 *   3. seqst_release(n, generation): the older entries become reclaimable; with generation 0, seqst_drop(n): all of
 *      them (RESTORE FACTORY then WRITE frees its room). Not when the record keeps naming a missing entry.
 *   A new entry's generation is always newer than the one n's current record names (seqst_put_above): if the store
 *   lost that one (a format, damage) it would otherwise hand the same number out again, and a cut (or a failed record
 *   save) between 1 and 2 would leave the old record naming the new entry.
 *   A power cut in 1 leaves the old record naming the old entry (the new one, if complete, is an orphan the next WRITE
 *   of n releases); in 2 the slot store gives the old record or the new one; in 3 the new pair holds and the old entries
 *   only cost room until the next WRITE of n. So a reload always gives old program + old sequence or new + new.
 * On a board without the sequence store (the FM-1: seqst_total() 0) WRITE stores the program only (generation 0).
 *
 * The factory sequences (read only, in the image; docs/FACTORY_BANK.md "Sequences"): the factory bank header may
 * define, beside factory_bank[][]:
 *   #define FACTORY_SEQ_COUNT N                           programs 0..N-1 have an entry (N need not be FACTORY_COUNT)
 *   static const uint32_t factory_seq_at[N + 1];          program p's sequence is the bytes
 *   static const uint8_t factory_seq_data[];              factory_seq_data[factory_seq_at[p] .. factory_seq_at[p + 1])
 * each sequence in seq_pack's format exactly (synth/seq.h: an empty sequence is the one byte 00); an empty span means
 * the program has none. A span that does not unpack to exactly its own length reads as none. No FACTORY_SEQ_COUNT
 * (no bank, or a bank of programs only): every factory sequence is empty. */
#ifndef KNOBI_PROG_SEQ_H
#define KNOBI_PROG_SEQ_H
#include <stdint.h>
#include "params.h"
#include "../synth/seq.h"

#define PROG_ERR_FLASH (-1)                     /* prog_write: the flash failed (or n is past the programs); nothing
                                                  * changed: the old pair still holds */
#define PROG_ERR_SEQ_FULL (-2)                  /* prog_write: the sequence store is full; nothing changed */

void prog_seq_boot(void);                        /* at boot, before the first load: the sequence store's init
                                                  * (seqst_init) and an empty edit sequence */
int factory_seq(uint16_t prog, seq_t *out);      /* 0: the factory sequence of prog (it may be empty); -1 none (no
                                                  * bank, no sequences, prog past them): *out empty */
const seq_t *edit_seq(void);                     /* the edit buffer's sequence, to read only: change it only through
                                                  * edit_seq_mut (a change through a cast of this pointer is caught at
                                                  * WRITE by a hash and stored as an edit, but do not rely on it) */
seq_t *edit_seq_mut(void);                       /* the same, to change: marks it edited (a WRITE then stores it) */
uint32_t edit_seq_ver(void);                     /* changes whenever the edit buffer's sequence may have: edit_seq_mut,
                                                  * a load, RESTORE FACTORY, WRITE (app.c hands it to the player then:
                                                  * engine.c eng_seq_load) */
int prog_seq_open(uint16_t n, int load_rc, const program_t *p);   /* program n was loaded (prog_load gave load_rc and
                                                  * *p): its sequence into the edit buffer. 0 from the store; 1 the factory
                                                  * sequence (maybe empty); -2 empty: a broken record; -3 empty: the
                                                  * record names a generation the store does not give (kept named) */
void prog_seq_factory(uint16_t n);               /* RESTORE FACTORY of n: n's factory sequence into the edit buffer */
int prog_open(uint16_t n, program_t *p);         /* prog_load + prog_seq_open: prog_load's result */
int prog_write(uint16_t n, const program_t *p);  /* WRITE: program *p and the edit buffer's sequence as program n. 0
                                                  * stored; PROG_ERR_SEQ_FULL nothing changed; PROG_ERR_FLASH the flash
                                                  * failed, nothing changed (the old pair still holds) */

#endif
