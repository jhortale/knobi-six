/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX programs in flash (store_prog.h). Included by app.c after platform.c (slot_load / slot_save) and
 * params.c; tests/unit/test_store.c includes it after its own fake of those two.
 *
 * Program n is record n % PROG_PER_SLOT of logical slot n / PROG_PER_SLOT: the record (program_pack: version, name,
 * values, chord, CRC16) at offset record x PROG_STRIDE, zero-padded to the stride. A slot image is the 74 records,
 * then a tag in the last bytes: magic "KPG1", the logical slot, a sequence number (LE). The image is one record of the
 * base's store (magic, length, CRC32; the base writes its header last, so a save cut short reads as nothing).
 *
 * Power-safe saves: the 7 logical slots live in the 8 physical slots, at least one always free. A save writes the
 * whole image, with the next sequence number, into a free physical slot (round robin: the wear spreads); once it is
 * written that slot is the logical slot's, and the old copy is free (no extra erase). A cut save destroys only a free
 * slot: the previous copy still reads. The map is found on the first use by reading all 8: each logical slot is the
 * copy whose CRC32 holds with the newest sequence number (compared as a signed difference: wraps around); if no copy
 * holds, the newest readable one (its tag read although the CRC32 failed), and each record's own CRC16 decides.
 * A record that is empty, of a version this build does not know, or broken loads as the init program, as does every
 * program of a logical slot never saved. prog_buf (16 KB, in the pool) caches the logical slot last read.
 *
 * The factory bank (optional, read only, docs/FACTORY_BANK.md): firmware/gen/factory_bank.h when it exists at build
 * time (tools/ob6_import.py writes it locally; it is never committed), else none. It defines FACTORY_COUNT records of
 * FACTORY_SIZE bytes (program_pack's format) in factory_bank[][]. A program never written (its record all zeros) loads
 * factory program n (if there is one), else the init program; a broken record (or one of an unknown version) loads
 * the init program and says so (-2: the panel shows a notice); WRITE (prog_save) never touches the factory bank, and
 * prog_factory reads it for "restore factory". -DKNOBI_NO_FACTORY builds without it (the host tests, which expect the
 * init program); -DKNOBI_FACTORY_H='"file.h"' takes another bank (the unit tests' synthetic one). */
#include "store_prog.h"
#if defined KNOBI_FACTORY_H
#include KNOBI_FACTORY_H
#elif !defined KNOBI_NO_FACTORY && defined __has_include
#if __has_include("factory_bank.h")
#include "factory_bank.h"
#endif
#endif
#ifndef FACTORY_COUNT
#define FACTORY_COUNT 0u                               /* no factory bank: the init program everywhere */
#endif

#define PROG_DATA_BYTES (PROG_PER_SLOT * PROG_STRIDE)  /* 16354 B of records */
#define PROG_SLOT_BYTES SLOT_MAX                       /* the image: records, spare, the tag at the end */
#define PROG_TAG_AT (PROG_SLOT_BYTES - PROG_TAG_BYTES)
#define PROG_TAG_MAGIC 0x3147504Bu                     /* "KPG1" */
#ifdef DINGHY_HOST
static uint8_t prog_buf[PROG_SLOT_BYTES];
#else
static uint8_t prog_buf[PROG_SLOT_BYTES] __attribute__((section(".pool")));   /* (the pool: not in .bss) */
#endif
static int32_t prog_buf_slot = -1;                     /* the logical slot prog_buf holds, -1 none */
static uint8_t prog_scanned;                           /* the map is known */
static int8_t prog_map[PROG_LSLOTS];                   /* logical -> physical slot, -1 never saved */
static uint8_t prog_good[PROG_LSLOTS];                 /* its CRC32 held at the scan */
static uint32_t prog_seq, prog_next;                   /* the newest sequence number seen; where the round robin is */
_Static_assert(PROG_TAG_AT >= PROG_DATA_BYTES, "the tag after the records");

uint32_t prog_count(void) { return PROG_COUNT; }

static uint32_t prog_get32(const uint8_t *a) { return (uint32_t)a[0] | (uint32_t)a[1] << 8 | (uint32_t)a[2] << 16 | (uint32_t)a[3] << 24; }
static void prog_put32(uint8_t *a, uint32_t v) { a[0] = (uint8_t)v, a[1] = (uint8_t)(v >> 8), a[2] = (uint8_t)(v >> 16), a[3] = (uint8_t)(v >> 24); }
static int prog_newer(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }

static void prog_scan(void)                            /* read all 8 physical slots' tags: the map */
{
    uint32_t seq[PROG_LSLOTS], p, l, have = 0;
    for (l = 0; l < PROG_LSLOTS; l++) prog_map[l] = -1, prog_good[l] = 0, seq[l] = 0;
    for (p = 0; p < NSLOT; p++) {
        const uint8_t *t = prog_buf + PROG_TAG_AT;
        int ok;
        uint32_t s;
        memset(prog_buf, 0, sizeof prog_buf);
        ok = slot_load(p, prog_buf, sizeof prog_buf) == 0;   /* -1 with the tag there: read, its CRC32 failed */
        if (prog_get32(t) != PROG_TAG_MAGIC || t[4] >= PROG_LSLOTS) continue;
        l = t[4], s = prog_get32(t + 5);
        if (!have || prog_newer(s, prog_seq)) prog_seq = s;
        have = 1;
        if (prog_map[l] < 0 || (ok && !prog_good[l]) || (ok == prog_good[l] && prog_newer(s, seq[l])))
            prog_map[l] = (int8_t)p, prog_good[l] = (uint8_t)ok, seq[l] = s;
    }
    prog_scanned = 1, prog_buf_slot = -1;
}

static void prog_fetch(uint32_t l)
{
    if (!prog_scanned) prog_scan();
    if (prog_buf_slot == (int32_t)l) return;
    memset(prog_buf, 0, sizeof prog_buf);              /* (never saved: all zeros, every record empty) */
    if (prog_map[l] >= 0) (void)slot_load((uint32_t)prog_map[l], prog_buf, sizeof prog_buf);   /* -1: the records'
                                                        * own CRC16 decide */
    prog_buf_slot = (int32_t)l;
}

uint32_t prog_factory_count(void) { return FACTORY_COUNT; }

int prog_factory(uint32_t n, program_t *p)
{
#if FACTORY_COUNT
    if (n < FACTORY_COUNT && program_packed_size(factory_bank[n][0]) == FACTORY_SIZE &&
        program_unpack(p, factory_bank[n], FACTORY_SIZE) == 0) {
        program_set_seq_gen(p, 0);                     /* (a factory program names no stored sequence: its own is the
                                                        * bank's, prog_seq.c factory_seq) */
        return 0;
    }
#else
    (void)n;
#endif
    program_init(p);
    return -1;
}

int prog_load(uint32_t n, program_t *p)
{
    const uint8_t *r;
    uint32_t size;
    if (n < PROG_COUNT) {
        prog_fetch(n / PROG_PER_SLOT);
        r = prog_buf + n % PROG_PER_SLOT * PROG_STRIDE;
        size = program_packed_size(r[0]);
        if (size && size <= PROG_STRIDE && program_unpack(p, r, size) == 0) return 0;
        for (size = 0; size < PROG_STRIDE && !r[size]; size++) {}
        if (size < PROG_STRIDE) { program_init(p); return -2; }   /* something there but broken (or of an unknown
                                                        * version): the init program, never silently the factory one */
    }
    return prog_factory(n, p) == 0 ? 1 : -1;           /* (never written: the factory program, else init) */
}

int prog_save(uint32_t n, const program_t *p)
{
    uint8_t *r;
    uint32_t l = n / PROG_PER_SLOT, k, ph;
    if (n >= PROG_COUNT) return -1;
    prog_fetch(l);
    r = prog_buf + n % PROG_PER_SLOT * PROG_STRIDE;
    memset(r, 0, PROG_STRIDE);
    if (program_pack(p, r, PROG_STRIDE) < 0) { prog_buf_slot = -1; return -1; }
    for (k = 0; k < NSLOT; k++) {                      /* a free physical slot, round robin from the last used */
        uint32_t m;
        ph = (prog_next + k) % NSLOT;
        for (m = 0; m < PROG_LSLOTS && prog_map[m] != (int8_t)ph; m++) {}
        if (m == PROG_LSLOTS) break;
    }
    memset(prog_buf + PROG_DATA_BYTES, 0, PROG_SLOT_BYTES - PROG_DATA_BYTES);
    prog_put32(prog_buf + PROG_TAG_AT, PROG_TAG_MAGIC);
    prog_buf[PROG_TAG_AT + 4u] = (uint8_t)l;
    prog_put32(prog_buf + PROG_TAG_AT + 5u, prog_seq + 1u);
    if (slot_save(ph, prog_buf, sizeof prog_buf) != 0) {   /* cut or refused: the old copy is still the slot's */
        prog_buf_slot = -1;
        return -1;
    }
    prog_seq++;
    prog_map[l] = (int8_t)ph, prog_good[l] = 1, prog_next = ph + 1u;
    return 0;
}
