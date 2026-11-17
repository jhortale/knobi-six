/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX programs in flash: the 8 slots of the base's store (store.h, docs/API.md "Storage"), each holding
 * PROG_PER_SLOT records of PROG_STRIDE bytes. The stride is PROGRAM_PACKED_MAX, the largest record any version may
 * have (96 values), so a later phase's bigger record still fits where the phase-1 ones are. 7 logical slots of 74
 * records live in the 8 physical slots, one always spare: a save writes the whole slot into a spare physical slot and
 * only then is the old copy free, so a save cut by a power loss leaves the previous copy (store_prog.c). The count is
 * whole banks of 100 (the OB-6's addressing: bank / tens / ones): 7 x 74 = 518 -> 5 banks, programs 0..499.
 * Main loop only: a save erases the slot's 4 sectors (the interrupts held off: save when silent, or fade out first). */
#ifndef KNOBI_STORE_PROG_H
#define KNOBI_STORE_PROG_H
#include <stdint.h>
#include "params.h"
#include "../store.h"

#define PROG_STRIDE PROGRAM_PACKED_MAX                 /* bytes a record in the slot (zero-padded) */
#define PROG_TAG_BYTES 9u                              /* a slot image ends with its tag: magic, logical slot, seq */
#define PROG_PER_SLOT ((SLOT_MAX - PROG_TAG_BYTES) / PROG_STRIDE)   /* 74 */
#define PROG_LSLOTS (NSLOT - 1u)                       /* 7 logical slots; one physical slot always spare */
#define PROG_RAW (PROG_LSLOTS * PROG_PER_SLOT)         /* 518 records */
#define PROG_BANKS (PROG_RAW / 100u)                   /* 5 banks of 100 */
#define PROG_COUNT (PROG_BANKS * 100u)                 /* 500 programs */
_Static_assert(PROG_COUNT >= 100u, "at least 100 programs");
_Static_assert(PROG_BANKS <= 10u, "one bank digit");

int prog_load(uint32_t n, program_t *p);         /* 0 loaded; never written (or n past the end): 1 = the factory
                                                  * program n (store_prog.c), -1 = the init program (no factory bank,
                                                  * or n past it); -2 = a broken or unknown-version record: the init
                                                  * program */
int prog_factory(uint32_t n, program_t *p);      /* 0 = factory program n; -1 none (no bank, n past it): init */
uint32_t prog_factory_count(void);               /* factory programs in this build (0: none) */
int prog_save(uint32_t n, const program_t *p);   /* 0 saved; -1 failed (n past the end, the flash refused) */
uint32_t prog_count(void);                       /* PROG_COUNT */

#endif
