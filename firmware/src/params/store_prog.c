/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX programs in flash (store_prog.h). Included by app.c after platform.c (slot_load / slot_save) and
 * params.c; tests/unit/test_store.c includes it after its own fake of those two.
 *
 * Program n lives in slot n / PROG_PER_SLOT, record n % PROG_PER_SLOT: the record (program_pack: version, name, values,
 * chord, CRC16) at offset record x PROG_STRIDE, zero-padded to the stride. The slot is one record of the base's store
 * (magic, length, CRC32): a save rewrites the whole slot, so the slot is read into prog_buf (16 KB, in the pool), the
 * record replaced, the slot saved. prog_buf caches the last slot read; a failed save forgets it.
 * A load trusts each record's own CRC16, not the slot's CRC32: a slot whose CRC32 fails (a bit gone bad in one record)
 * still gives its other programs; a record that is empty (never saved: zeros or 0xFF), of a version this build does
 * not know, or broken loads as the init program. A slot never saved (or a save cut before its header) reads as
 * nothing: every program of it is the init program. */
#include "store_prog.h"

#define PROG_SLOT_BYTES (PROG_PER_SLOT * PROG_STRIDE)  /* 16354 B a slot */
#ifdef DINGHY_HOST
static uint8_t prog_buf[PROG_SLOT_BYTES];
#else
static uint8_t prog_buf[PROG_SLOT_BYTES] __attribute__((section(".pool")));   /* (the pool: not in .bss) */
#endif
static int32_t prog_buf_slot = -1;                     /* the slot prog_buf holds, -1 none */

uint32_t prog_count(void) { return PROG_COUNT; }

static void prog_fetch(uint32_t slot)
{
    if (prog_buf_slot == (int32_t)slot) return;
    memset(prog_buf, 0, sizeof prog_buf);              /* (nothing saved: all zeros, every record empty) */
    (void)slot_load(slot, prog_buf, sizeof prog_buf);  /* -1: nothing, or the bytes with a bad CRC32: the records'
                                                        * own CRC16 decide */
    prog_buf_slot = (int32_t)slot;
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
    }
    program_init(p);
    return -1;
}

int prog_save(uint32_t n, const program_t *p)
{
    uint8_t *r;
    uint32_t slot = n / PROG_PER_SLOT;
    if (n >= PROG_COUNT) return -1;
    prog_fetch(slot);
    r = prog_buf + n % PROG_PER_SLOT * PROG_STRIDE;
    memset(r, 0, PROG_STRIDE);
    if (program_pack(p, r, PROG_STRIDE) < 0 || slot_save(slot, prog_buf, sizeof prog_buf) != 0) {
        prog_buf_slot = -1;                            /* the buffer is not what the flash holds: read it again */
        return -1;
    }
    return 0;
}
