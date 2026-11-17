/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX programs in flash: the 8 slots of the base's store (store.h, docs/API.md "Storage"), each holding
 * PROG_PER_SLOT records of PROG_STRIDE bytes. The stride is PROGRAM_PACKED_MAX, the largest record any version may
 * have (96 values), so a later phase's bigger record still fits where the phase-1 ones are. The count is whole banks
 * of 100 (the OB-6's addressing: bank / tens / ones): 8 slots x 74 records = 592 -> 5 banks, programs 0..499.
 * Main loop only: a save erases the slot's 4 sectors (the interrupts held off: save when silent, or fade out first). */
#ifndef KNOBI_STORE_PROG_H
#define KNOBI_STORE_PROG_H
#include <stdint.h>
#include "params.h"
#include "../store.h"

#define PROG_STRIDE PROGRAM_PACKED_MAX                 /* bytes a record in the slot (zero-padded) */
#define PROG_PER_SLOT (SLOT_MAX / PROG_STRIDE)         /* 74 */
#define PROG_RAW (NSLOT * PROG_PER_SLOT)               /* 592 records the slots hold */
#define PROG_BANKS (PROG_RAW / 100u)                   /* 5 banks of 100 */
#define PROG_COUNT (PROG_BANKS * 100u)                 /* 500 programs */
_Static_assert(PROG_COUNT >= 100u, "at least 100 programs");
_Static_assert(PROG_BANKS <= 10u, "one bank digit");

int prog_load(uint32_t n, program_t *p);         /* 0 loaded; -1 nothing valid stored there (or n past the end):
                                                  * *p = the init program */
int prog_save(uint32_t n, const program_t *p);   /* 0 saved; -1 failed (n past the end, the flash refused) */
uint32_t prog_count(void);                       /* PROG_COUNT */

#endif
