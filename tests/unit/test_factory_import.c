/* SPDX-License-Identifier: GPL-3.0-only */
/* The sequences tools/ob6_import.py emits (docs/FACTORY_BANK.md "Sequences"), against the firmware's own seq_unpack:
 * tests/unit/factory_test_import.h is the importer's header for made-up OB-6 dumps (tests/ob6_import_test.py SEQ_SYX,
 * written with --write; no OB-6 data). Every span must unpack to exactly its own length (what factory_seq,
 * params/prog_seq.c, requires), the empty ones read as none, and the steps are the ones the dumps hold. */
#include <stdio.h>
#include <string.h>
#include "synth/seq.c"
#include "factory_test_import.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    seq_t s;
    CHECK(FACTORY_COUNT == 4u && FACTORY_SEQ_COUNT == 4u && factory_bank[0][0] == 2u);
    for (uint32_t p = 0; p < FACTORY_SEQ_COUNT; p++) {
        uint32_t a = factory_seq_at[p], b = factory_seq_at[p + 1u];
        CHECK(b >= a);
        if (b == a) continue;
        CHECK(seq_unpack(&s, factory_seq_data + a, b - a) == 0);
        CHECK(seq_packed_size(&s) == b - a);
        uint8_t buf[SEQ_PACKED_MAX];
        CHECK(seq_pack(&s, buf, sizeof buf) == (int)(b - a) && memcmp(buf, factory_seq_data + a, b - a) == 0);
    }
    CHECK(factory_seq_at[1] == factory_seq_at[2] && factory_seq_at[2] == factory_seq_at[3]);   /* 1 empty, 2 a gap */
    /* program 0: a note, a rest, a 3-note chord, that chord tied */
    CHECK(seq_unpack(&s, factory_seq_data, factory_seq_at[1]) == 0);
    CHECK(s.len == 4 && s.step[0].count == 1 && s.step[0].notes[0] == 60 && s.step[0].vels[0] == 100);
    CHECK(s.step[1].count == 0 && !s.step[1].tie);
    CHECK(s.step[2].count == 3 && s.step[2].notes[2] == 67 && s.step[2].vels[2] == 92 && !s.step[2].tie);
    CHECK(s.step[3].count == 3 && s.step[3].tie && memcmp(s.step[3].notes, s.step[2].notes, 3) == 0);
    /* program 3: a note tied twice (the second tie marker), then a rest */
    CHECK(seq_unpack(&s, factory_seq_data + factory_seq_at[3], factory_seq_at[4] - factory_seq_at[3]) == 0);
    CHECK(s.len == 4 && s.step[1].tie && s.step[2].tie && s.step[2].notes[0] == 36 && s.step[3].count == 0);
    printf("test_factory_import: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
