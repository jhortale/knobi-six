/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/unit/factory_test_seq.h: the synthetic factory bank (factory_test_bank.h: 3 programs, no OB-6 data) plus
 * factory sequences in the format docs/FACTORY_BANK.md "Sequences" and params/prog_seq.h define (written by hand;
 * tools/ob6_import.py emits the same symbols from Task 6 on). Program 0: 4 steps (a note, a rest, a chord, that chord
 * tied); program 1: a 6-note chord then a rest; program 2: an empty span (no factory sequence). Used by
 * tests/unit/test_pairing.c. */
#ifndef KNOBI_FACTORY_TEST_SEQ_H
#define KNOBI_FACTORY_TEST_SEQ_H
#include "factory_test_bank.h"
#define FACTORY_SEQ_COUNT 3u
static const uint32_t factory_seq_at[FACTORY_SEQ_COUNT + 1u] = {0u, 19u, 34u, 34u};
static const uint8_t factory_seq_data[34] = {
    /* 000 */ 4, 0x01, 60, 100, 0x00, 0x03, 60, 90, 64, 91, 67, 92, 0x0B, 60, 90, 64, 91, 67, 92,
    /* 001 */ 2, 0x06, 12, 0, 24, 1, 36, 2, 48, 3, 108, 126, 100, 127, 0x00,
    /* 002: none */
};
#endif
