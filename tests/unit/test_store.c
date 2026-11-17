/* SPDX-License-Identifier: GPL-3.0-only */
/* Programs and globals in flash (firmware/src/params/store_prog.c, globals.c) against a fake of platform.c's store:
 * slots and settings kept as the device keeps them (a header of magic, length and CRC32, then the bytes; a load whose
 * length or magic is wrong reads nothing, one whose CRC32 is wrong reads the bytes and returns -1, as area_load). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BOARD_SMK37 1
#include "store.h"
#include "params/params.c"

/* ---- the fake store */
static uint8_t f_slot[NSLOT][SLOT_SECT * 0x1000u], f_set[0x1000u];
static int f_slot_saves[NSLOT], f_set_saves;
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
static int f_load(const uint8_t *a, uint32_t max, void *p, uint32_t n)
{
    if (n > max || f_get32(a) != 0x4B4E4236u || f_get32(a + 4) != n) return -1;
    memcpy(p, a + 12, n);                       /* (straight into p, as area_load) */
    return f_crc32(p, n) == f_get32(a + 8) ? 0 : -1;
}
static int f_save(uint8_t *a, uint32_t max, const void *p, uint32_t n)
{
    if (n > max) return -1;
    memset(a, 0xFF, max + 12u);                 /* (erased) */
    f_put32(a, 0x4B4E4236u), f_put32(a + 4, n), f_put32(a + 8, f_crc32(p, n));
    memcpy(a + 12, p, n);
    return 0;
}
static int slot_load(uint32_t i, void *p, uint32_t n) { return i < NSLOT ? f_load(f_slot[i], SLOT_MAX, p, n) : -1; }
static int slot_save(uint32_t i, const void *p, uint32_t n) { if (i >= NSLOT) return -1; f_slot_saves[i]++; return f_save(f_slot[i], SLOT_MAX, p, n); }
static uint8_t f_set_copy[SECTOR_MAX];
static int settings_load(void *p, uint32_t n)   /* (a failed load leaves p alone, as sector_load) */
{
    if (f_load(f_set, SECTOR_MAX, f_set_copy, n)) return -1;
    memcpy(p, f_set_copy, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n) { f_set_saves++; return f_save(f_set, SECTOR_MAX, p, n); }

#include "params/store_prog.c"
#include "params/globals.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void forget_cache(void) { prog_buf_slot = -1; }   /* as a power cycle: the next load reads the flash */

static void prog_pattern(program_t *p, uint32_t n)
{
    program_init(p);
    for (unsigned i = 0; i < P_COUNT_PHASE1; i++)
        p->v[i] = param_clamp((uint16_t)i, PARAMS[i].min + (int32_t)((n * 7u + i * 13u) % (uint32_t)(PARAMS[i].max - PARAMS[i].min + 1)));
    memset(p->name, 0, PROGRAM_NAME_LEN);
    snprintf(p->name, PROGRAM_NAME_LEN, "PROG %u", n);
    for (unsigned i = 0; i < 6u; i++) p->chord[i] = (uint8_t)((n + i * 5u) % 128u);
}
static int prog_same(const program_t *a, const program_t *b)
{
    return !memcmp(a->name, b->name, PROGRAM_NAME_LEN) && !memcmp(a->v, b->v, sizeof a->v) && !memcmp(a->chord, b->chord, 6);
}

/* the count: whole banks of 100 from what the 8 slots hold at the largest record any version may have */
static void count_at_least_100(void)
{
    printf("count_at_least_100: %u programs (%u banks), %u a slot of %u B at %u B a record, raw %u\n", prog_count(),
           (unsigned)PROG_BANKS, (unsigned)PROG_PER_SLOT, (unsigned)SLOT_MAX, (unsigned)PROG_STRIDE, (unsigned)PROG_RAW);
    CHECK(prog_count() >= 100u);
    CHECK(prog_count() % 100u == 0u && prog_count() == PROG_BANKS * 100u && prog_count() <= PROG_RAW);
    CHECK(PROG_PER_SLOT * PROG_STRIDE <= SLOT_MAX && PROG_STRIDE >= program_packed_size(PROGRAM_VERSION));
}

/* every program of every slot saved, then everything read back after a "power cycle" */
static void store_roundtrip_all_slots(void)
{
    program_t p, q;
    uint32_t n, bad = 0, used = 0;
    memset(f_slot, 0xFF, sizeof f_slot);
    forget_cache();
    for (n = 0; n < prog_count(); n++) {
        prog_pattern(&p, n);
        if (prog_save(n, &p)) bad++;
    }
    forget_cache();
    for (n = 0; n < prog_count(); n++) {
        prog_pattern(&p, n);
        if (prog_load(n, &q) || !prog_same(&p, &q)) { if (bad < 5) printf("  program %u differs\n", n); bad++; }
    }
    for (n = 0; n < NSLOT; n++) used += f_slot_saves[n] > 0;
    /* a save leaves its neighbours as they were: write 123 again, the rest still read back */
    prog_pattern(&p, 999u);
    CHECK(prog_save(123u, &p) == 0);
    forget_cache();
    CHECK(prog_load(123u, &q) == 0 && prog_same(&p, &q));
    prog_pattern(&p, 122u);
    CHECK(prog_load(122u, &q) == 0 && prog_same(&p, &q));
    prog_pattern(&p, 124u);
    CHECK(prog_load(124u, &q) == 0 && prog_same(&p, &q));
    printf("store_roundtrip_all_slots: %u programs, %u differ, %u slots used\n", prog_count(), bad, used);
    CHECK(bad == 0 && used == (prog_count() + PROG_PER_SLOT - 1u) / PROG_PER_SLOT);
    CHECK(prog_save(prog_count(), &p) != 0);    /* past the end: refused */
}

/* nothing saved, a broken record, a broken slot: the init program (and only where it is broken) */
static void bad_crc_loads_init(void)
{
    program_t p, q, init;
    uint32_t n;
    int rc_empty, rc_bad, rc_next, rc_out, rc_ver;
    program_init(&init);
    memset(f_slot, 0xFF, sizeof f_slot);        /* erased flash: nothing saved anywhere */
    forget_cache();
    rc_empty = prog_load(5u, &q);
    CHECK(rc_empty != 0 && prog_same(&q, &init));
    for (n = 0; n < 3u; n++) { prog_pattern(&p, n); prog_save(n, &p); }
    /* one byte of program 1's record flipped in flash: the slot's CRC32 fails, program 1's CRC16 too */
    f_slot[0][12u + 1u * PROG_STRIDE + 30u] ^= 0x5Au;
    forget_cache();
    rc_bad = prog_load(1u, &q);
    CHECK(rc_bad != 0 && prog_same(&q, &init));
    prog_pattern(&p, 2u);
    rc_next = prog_load(2u, &q);                 /* its neighbours: their own CRC16 holds, they load */
    CHECK(rc_next == 0 && prog_same(&p, &q));
    rc_out = prog_load(prog_count(), &q);        /* past the end */
    CHECK(rc_out != 0 && prog_same(&q, &init));
    /* a record of an unknown (future) version: init */
    f_slot[0][12u + 0u * PROG_STRIDE] = PROGRAM_VERSION + 1u;
    forget_cache();
    rc_ver = prog_load(0u, &q);
    CHECK(rc_ver != 0 && prog_same(&q, &init));
    /* the slot's header gone (a save cut before its header was written): every program of it is init */
    memset(f_slot[0], 0xFF, 12u);
    forget_cache();
    CHECK(prog_load(2u, &q) != 0 && prog_same(&q, &init));
    printf("bad_crc_loads_init: empty %d, broken record %d, its neighbour %d, past the end %d, future version %d\n",
           rc_empty, rc_bad, rc_next, rc_out, rc_ver);
}

/* globals: saved and loaded; nothing saved, a bad CRC, a wrong version or a value out of range: the defaults */
static void globals_roundtrip(void)
{
    globals_t g, h, d;
    int i, same = 1;
    globals_default(&d);
    memset(f_set, 0xFF, sizeof f_set);
    globals_load(&h);
    CHECK(!memcmp(&h, &d, sizeof h));            /* nothing saved */
    g = d;
    for (i = 0; i < G_COUNT; i++) g.v[i] = GLOB_MAX[i];
    g.v[G_TRANSPOSE] = -7, g.v[G_TUNE] = -33, g.oct = -2, g.last_prog = (uint16_t)(prog_count() - 1u);
    globals_save(&g);
    globals_load(&h);
    for (i = 0; i < G_COUNT; i++) same &= h.v[i] == g.v[i];
    same &= h.oct == g.oct && h.last_prog == g.last_prog;
    printf("globals_roundtrip: %d values, oct %d, last program %u: same %d\n", G_COUNT, h.oct, h.last_prog, same);
    CHECK(same);
}
static void globals_bad_crc_defaults(void)
{
    globals_t g, h, d;
    uint8_t raw[GLOBALS_BYTES];
    int crc_bad, range_bad, ver_bad, prog_bad;
    globals_default(&d);
    g = d;
    g.v[G_MIDI_CH] = 5, g.last_prog = 42;
    globals_save(&g);
    f_set[12 + 3] ^= 0x01u;                     /* a bit flipped in flash: the sector's CRC32 fails */
    globals_load(&h);
    crc_bad = !memcmp(&h, &d, sizeof h);
    /* a record whose own CRC16 fails (settings_load would pass it: written without the CRC fixed up) */
    globals_pack(&g, raw);
    raw[3] ^= 1u;
    settings_save(raw, sizeof raw);
    globals_load(&h);
    crc_bad &= !memcmp(&h, &d, sizeof h);
    g.v[G_POT_MODE] = 9;                         /* out of range, with a good CRC: defaults */
    globals_pack(&g, raw);
    settings_save(raw, sizeof raw);
    globals_load(&h);
    range_bad = !memcmp(&h, &d, sizeof h);
    g.v[G_POT_MODE] = POT_JUMP;
    globals_pack(&g, raw);
    raw[0] = GLOBALS_VERSION + 1u;               /* another version */
    settings_save(raw, sizeof raw);
    globals_load(&h);
    ver_bad = !memcmp(&h, &d, sizeof h);
    g.last_prog = (uint16_t)prog_count();       /* a program past the end */
    globals_pack(&g, raw);
    settings_save(raw, sizeof raw);
    globals_load(&h);
    prog_bad = !memcmp(&h, &d, sizeof h);
    printf("globals_bad_crc_defaults: bad CRC %d, out of range %d, other version %d, program past the end %d\n", crc_bad,
           range_bad, ver_bad, prog_bad);
    CHECK(crc_bad && range_bad && ver_bad && prog_bad);
}

/* the velocity and aftertouch curves: 0 is linear; every curve rises with its input, keeps 0 at 0 and never turns a
 * played note (1..127) into 0 (a note-off) */
static void response_curves(void)
{
    int c, v, bad = 0, lin = 1, differ = 0;
    for (c = 0; c <= GLOB_MAX[G_VEL_CURVE]; c++) {
        for (v = 1; v <= 127; v++) {
            int y = glob_vel_curve(c, v);
            if (y < 1 || y > 127 || y < glob_vel_curve(c, v - 1)) bad++;
            if (c == 0) lin &= y == v;
            else differ += y != v;
        }
        if (glob_vel_curve(c, 0) != 0) bad++;
    }
    for (c = 0; c <= GLOB_MAX[G_AT_CURVE]; c++)
        for (v = 0; v <= 127; v++) {
            int y = glob_at_curve(c, v);
            if (y < 0 || y > 127 || (v && y < glob_at_curve(c, v - 1)) || (v == 0 && y) || (v == 127 && y != 127)) bad++;
            if (c == 0) lin &= y == v;
        }
    printf("response_curves: %d faults, curve 0 linear %d; velocity 32 by curve:", bad, lin);
    for (c = 0; c <= GLOB_MAX[G_VEL_CURVE]; c++) printf(" %d", glob_vel_curve(c, 32));
    printf("\n");
    CHECK(bad == 0 && lin && differ > 0);
}

int main(void)
{
    uint8_t tag[16];
    smk_owner_tag(tag);                         /* (store.h's, unused here) */
    count_at_least_100();
    store_roundtrip_all_slots();
    bad_crc_loads_init();
    globals_roundtrip();
    globals_bad_crc_defaults();
    response_curves();
    if (fails) printf("test_store: %d FAILED\n", fails);
    else printf("test_store: all passed\n");
    return fails != 0;
}
