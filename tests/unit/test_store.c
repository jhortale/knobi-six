/* SPDX-License-Identifier: GPL-3.0-only */
/* Programs and globals in flash (firmware/src/params/store_prog.c, globals.c) against a fake of platform.c's store:
 * slots and blobs kept as the device keeps them (a header of magic, length and CRC32, then the bytes; a load whose
 * length or magic is wrong reads nothing, one whose CRC32 is wrong reads the bytes and returns -1, as area_load). A
 * save erases, writes the bytes, then the header (area_save); f_cut >= 0 cuts the next save after that many bytes, the
 * header never written: a power loss. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BOARD_SMK37 1
#include "store.h"
#include "params/params.c"

/* ---- the fake store */
static uint8_t f_slot[NSLOT][SLOT_SECT * 0x1000u], f_blob[NBLOB][0x1000u];
static int f_slot_saves[NSLOT];
static int32_t f_cut = -1;
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
    if (f_cut >= 0) {                           /* the power goes: part of the bytes, no header */
        memcpy(a + 12, p, (uint32_t)f_cut < n ? (uint32_t)f_cut : n);
        f_cut = -1;
        return -1;
    }
    memcpy(a + 12, p, n);
    f_put32(a, 0x4B4E4236u), f_put32(a + 4, n), f_put32(a + 8, f_crc32(p, n));   /* the header last */
    return 0;
}
static int slot_load(uint32_t i, void *p, uint32_t n) { return i < NSLOT ? f_load(f_slot[i], SLOT_MAX, p, n) : -1; }
static int slot_save(uint32_t i, const void *p, uint32_t n) { if (i >= NSLOT) return -1; f_slot_saves[i]++; return f_save(f_slot[i], SLOT_MAX, p, n); }
static uint8_t f_blob_copy[SECTOR_MAX];
static int blob_load(uint32_t i, void *p, uint32_t n)   /* (a failed load leaves p alone, as sector_load) */
{
    if (i >= NBLOB || f_load(f_blob[i], SECTOR_MAX, f_blob_copy, n)) return -1;
    memcpy(p, f_blob_copy, n);
    return 0;
}
static int blob_save(uint32_t i, const void *p, uint32_t n) { return i < NBLOB ? f_save(f_blob[i], SECTOR_MAX, p, n) : -1; }

#include "params/store_prog.c"
#include "params/globals.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void forget_cache(void) { prog_buf_slot = -1, prog_scanned = 0, prog_seq = 0, prog_next = 0; }   /* a power
                                                                       cycle: the next load reads the flash again */
static void keep_only_current(uint32_t l)      /* erase every stale copy of logical slot l (one copy left) */
{
    for (uint32_t p = 0; p < NSLOT; p++) {
        uint32_t m;
        for (m = 0; m < PROG_LSLOTS && prog_map[m] != (int8_t)p; m++) {}
        if (m == PROG_LSLOTS) memset(f_slot[p], 0xFF, sizeof f_slot[p]);
    }
    (void)l;
}

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
    printf("count_at_least_100: %u programs (%u banks), %u a slot of %u B at %u B a record, %u logical slots + 1 spare, raw %u\n",
           prog_count(), (unsigned)PROG_BANKS, (unsigned)PROG_PER_SLOT, (unsigned)SLOT_MAX, (unsigned)PROG_STRIDE,
           (unsigned)PROG_LSLOTS, (unsigned)PROG_RAW);
    CHECK(prog_count() >= 100u);
    CHECK(prog_count() % 100u == 0u && prog_count() == PROG_BANKS * 100u && prog_count() <= PROG_RAW);
    CHECK(PROG_PER_SLOT * PROG_STRIDE + PROG_TAG_BYTES <= SLOT_MAX && PROG_STRIDE >= program_packed_size(PROGRAM_VERSION));
    CHECK(PROG_LSLOTS < NSLOT && PROG_COUNT <= PROG_LSLOTS * PROG_PER_SLOT);
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
    for (n = 0; n < PROG_LSLOTS; n++) used += prog_map[n] >= 0;
    /* a save leaves its neighbours as they were: write 123 again, the rest still read back */
    prog_pattern(&p, 999u);
    CHECK(prog_save(123u, &p) == 0);
    forget_cache();
    CHECK(prog_load(123u, &q) == 0 && prog_same(&p, &q));
    prog_pattern(&p, 122u);
    CHECK(prog_load(122u, &q) == 0 && prog_same(&p, &q));
    prog_pattern(&p, 124u);
    CHECK(prog_load(124u, &q) == 0 && prog_same(&p, &q));
    printf("store_roundtrip_all_slots: %u programs, %u differ, %u logical slots used\n", prog_count(), bad, used);
    CHECK(bad == 0 && used == (prog_count() + PROG_PER_SLOT - 1u) / PROG_PER_SLOT);
    for (n = 0, used = 0; n < NSLOT; n++) used += f_slot_saves[n] > 0;
    printf("store_roundtrip_all_slots: every physical slot written in turn: %u of %u\n", used, (unsigned)NSLOT);
    CHECK(used == NSLOT);
    CHECK(prog_save(prog_count(), &p) != 0);    /* past the end: refused */
}

/* nothing saved, a broken record, a broken slot: the init program (and only where it is broken) */
static void bad_crc_loads_init(void)
{
    program_t p, q, init;
    uint32_t n, ph;
    int rc_empty, rc_bad, rc_next, rc_out, rc_ver, rc_old;
    program_init(&init);
    memset(f_slot, 0xFF, sizeof f_slot);        /* erased flash: nothing saved anywhere */
    forget_cache();
    rc_empty = prog_load(5u, &q);
    CHECK(rc_empty != 0 && prog_same(&q, &init));
    for (n = 0; n < 3u; n++) { prog_pattern(&p, n); prog_save(n, &p); }
    keep_only_current(0);                       /* one copy of logical slot 0: no older one to fall back on */
    ph = (uint32_t)prog_map[0];
    /* one byte of program 1's record flipped in flash: the slot's CRC32 fails, program 1's CRC16 too */
    f_slot[ph][12u + 1u * PROG_STRIDE + 30u] ^= 0x5Au;
    forget_cache();
    rc_bad = prog_load(1u, &q);
    CHECK(rc_bad != 0 && prog_same(&q, &init));
    prog_pattern(&p, 2u);
    rc_next = prog_load(2u, &q);                 /* its neighbours: their own CRC16 holds, they load */
    CHECK(rc_next == 0 && prog_same(&p, &q));
    rc_out = prog_load(prog_count(), &q);        /* past the end */
    CHECK(rc_out != 0 && prog_same(&q, &init));
    /* a record of an unknown (future) version: init */
    f_slot[ph][12u + 0u * PROG_STRIDE] = PROGRAM_VERSION + 1u;
    forget_cache();
    rc_ver = prog_load(0u, &q);
    CHECK(rc_ver != 0 && prog_same(&q, &init));
    /* the only copy's header gone: every program of the slot is init */
    memset(f_slot[ph], 0xFF, 12u);
    forget_cache();
    CHECK(prog_load(2u, &q) != 0 && prog_same(&q, &init));
    /* two copies, the newer one broken: the older good copy is the slot */
    memset(f_slot, 0xFF, sizeof f_slot);
    forget_cache();
    prog_pattern(&p, 7u);
    prog_save(7u, &p);
    prog_pattern(&q, 8u);
    prog_save(7u, &q);
    f_slot[prog_map[0]][12u + 100u] ^= 1u;
    forget_cache();
    rc_old = prog_load(7u, &q);
    CHECK(rc_old == 0 && prog_same(&p, &q));
    printf("bad_crc_loads_init: empty %d, broken record %d, its neighbour %d, past the end %d, future version %d; newest "
           "copy broken: the older copy %d\n", rc_empty, rc_bad, rc_next, rc_out, rc_ver, rc_old);
}

/* Review fix: a power loss in the middle of a save (after the erase, half the bytes, all the bytes but no header):
 * the slot's previous 74 programs all survive, the others too, and the next save works */
static void power_cut_keeps_slot(void)
{
    static const int32_t CUT[] = {0, 5000, (int32_t)SLOT_MAX};
    program_t p, q;
    uint32_t n, k, bad = 0;
    memset(f_slot, 0xFF, sizeof f_slot);
    forget_cache();
    for (n = 0; n < PROG_PER_SLOT + 10u; n++) { prog_pattern(&p, n); prog_save(n, &p); }
    for (k = 0; k < sizeof CUT / sizeof CUT[0]; k++) {
        prog_pattern(&p, 1000u + k);
        f_cut = CUT[k];
        CHECK(prog_save(10u, &p) != 0);
        forget_cache();                         /* the power comes back */
        for (n = 0; n < PROG_PER_SLOT + 10u; n++) {
            prog_pattern(&p, n == 10u && k ? 1000u + k - 1u : n);
            if (prog_load(n, &q) || !prog_same(&p, &q)) bad++;
        }
        prog_pattern(&p, 1000u + k);            /* then the save goes through */
        CHECK(prog_save(10u, &p) == 0);
        forget_cache();
        CHECK(prog_load(10u, &q) == 0 && prog_same(&p, &q));
    }
    printf("power_cut_keeps_slot: cuts after %d, %d, %d bytes: %u programs differ afterwards\n", CUT[0], CUT[1], CUT[2], bad);
    CHECK(bad == 0);
}

/* the sequence numbers wrap around (compared as a signed difference): the newest copy still wins */
static void seq_wraps(void)
{
    program_t p, q;
    uint32_t k;
    memset(f_slot, 0xFF, sizeof f_slot);
    forget_cache();
    prog_load(0u, &q);                          /* (the scan) */
    prog_seq = 0xFFFFFFF0u;
    for (k = 0; k < 40u; k++) { prog_pattern(&p, 2000u + k); prog_save(0u, &p); prog_pattern(&p, 3000u + k); prog_save(80u, &p); }
    forget_cache();
    prog_pattern(&p, 2039u);
    CHECK(prog_load(0u, &q) == 0 && prog_same(&p, &q));
    prog_pattern(&p, 3039u);
    CHECK(prog_load(80u, &q) == 0 && prog_same(&p, &q));
    printf("seq_wraps: from 0xFFFFFFF0, 80 saves later the newest copies load; sequence now %u\n", prog_seq);
    CHECK(prog_seq == 0xFFFFFFF0u + 80u);
}

/* globals: saved and loaded; nothing saved, a bad CRC, a wrong version or a value out of range: the defaults */
static void globals_roundtrip(void)
{
    globals_t g, h, d;
    int i, same = 1;
    globals_default(&d);
    memset(f_blob, 0xFF, sizeof f_blob);
    globals_load(&h);
    CHECK(!memcmp(&h, &d, sizeof h));            /* nothing saved */
    g = d;
    for (i = 0; i < G_COUNT; i++) g.v[i] = GLOB_MAX[i];
    g.v[G_TRANSPOSE] = -7, g.v[G_TUNE] = -33, g.oct = -2, g.last_prog = (uint16_t)(prog_count() - 1u);
    globals_save(&g);
    globals_load(&h);
    g.seq = h.seq;
    for (i = 0; i < G_COUNT; i++) same &= h.v[i] == g.v[i];
    same &= h.oct == g.oct && h.last_prog == g.last_prog;
    printf("globals_roundtrip: %d values, oct %d, last program %u: same %d\n", G_COUNT, h.oct, h.last_prog, same);
    CHECK(same);
}
/* Review fix: the globals go to blobs 0 and 1 in turn; a save cut by a power loss leaves the previous record, and
 * the newer good record wins */
static void globals_cut_save(void)
{
    globals_t g, h;
    int after_cut, newest, again;
    memset(f_blob, 0xFF, sizeof f_blob);
    globals_load(&g);
    g.v[G_MIDI_CH] = 1; globals_save(&g);
    g.v[G_MIDI_CH] = 2; globals_save(&g);
    g.v[G_MIDI_CH] = 3; f_cut = 7; globals_save(&g);
    globals_load(&h);
    after_cut = h.v[G_MIDI_CH];
    g.v[G_MIDI_CH] = 4; globals_save(&g);
    globals_load(&h);
    newest = h.v[G_MIDI_CH];
    h.v[G_MIDI_CH] = 6; globals_save(&h);
    globals_load(&h);
    again = h.v[G_MIDI_CH];
    printf("globals_cut_save: 1, 2, then 3 cut -> %d; then 4 -> %d; then 6 -> %d\n", after_cut, newest, again);
    CHECK(after_cut == 2 && newest == 4 && again == 6);
}

static void globals_bad_crc_defaults(void)
{
    globals_t g, h, d;
    uint8_t raw[GLOBALS_BYTES];
    int crc_bad, range_bad, ver_bad, prog_bad;
    globals_default(&d);
    memset(f_blob, 0xFF, sizeof f_blob);
    globals_load(&h);
    g = d;
    g.v[G_MIDI_CH] = 5, g.last_prog = 42;
    globals_save(&g);                           /* (into blob 0, blob 1 empty) */
    f_blob[0][12 + 3] ^= 0x01u;                 /* a bit flipped in flash: the sector's CRC32 fails */
    globals_load(&h);
    crc_bad = !memcmp(&h, &d, sizeof h);
    /* a record whose own CRC16 fails (settings_load would pass it: written without the CRC fixed up) */
    globals_pack(&g, raw);
    raw[3] ^= 1u;
    blob_save(0, raw, sizeof raw);
    globals_load(&h);
    crc_bad &= !memcmp(&h, &d, sizeof h);
    g.v[G_POT_MODE] = 9;                         /* out of range, with a good CRC: defaults */
    globals_pack(&g, raw);
    blob_save(0, raw, sizeof raw);
    globals_load(&h);
    range_bad = !memcmp(&h, &d, sizeof h);
    g.v[G_POT_MODE] = POT_JUMP;
    globals_pack(&g, raw);
    raw[0] = GLOBALS_VERSION + 1u;               /* another version */
    blob_save(0, raw, sizeof raw);
    globals_load(&h);
    ver_bad = !memcmp(&h, &d, sizeof h);
    g.last_prog = (uint16_t)prog_count();       /* a program past the end */
    globals_pack(&g, raw);
    blob_save(0, raw, sizeof raw);
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

#if KNOBI_CPU1
/* Phase 2 Task 3 (test_store_cpu1.c: this file with KNOBI_CPU1): the DUAL CORE global, in a blob of its own
 * (GLOBALS_DUAL_BLOB). The version-1 record in blobs 0 / 1 is byte for byte what a default build saves (globals_pack
 * packs the first G_COUNT_V1 in both builds, and blob 2 is the only other blob written): a default build's record
 * reads back whole here (DUAL CORE off, no blob 2), this build's reads back whole there (the same bytes); swapping
 * the builds keeps the globals both ways. DUAL CORE's blob missing, broken or out of range: OFF */
static void globals_dual_blob(void)
{
    globals_t g, h, d;
    uint8_t v1[GLOBALS_BYTES], raw[GLOBALS_DUAL_BYTES];
    int from_default, v1_bytes, others_kept, back, kept, missing, broken, range, only_blob2 = 1;
    globals_default(&d);
    CHECK(d.v[G_DUAL] == 0);                     /* off by default */
    memset(f_blob, 0xFF, sizeof f_blob);         /* a default build saved the globals (version 1, blob 0) */
    g = d, g.v[G_MIDI_CH] = 9, g.v[G_TUNE] = -20, g.oct = 1, g.last_prog = 42, g.seq = 40;
    globals_pack(&g, v1);
    blob_save(0, v1, GLOBALS_BYTES);
    globals_load(&h);                            /* this build after it */
    from_default = h.v[G_MIDI_CH] == 9 && h.v[G_TUNE] == -20 && h.oct == 1 && h.last_prog == 42 && h.seq == 40 &&
                   h.v[G_DUAL] == 0;
    h.v[G_DUAL] = 1, h.v[G_MIDI_CH] = 3;         /* DUAL CORE on and an edit, saved here */
    globals_save(&h);
    for (unsigned b = 3; b < NBLOB; b++)
        for (unsigned k = 0; k < sizeof f_blob[b]; k++) only_blob2 &= f_blob[b][k] == 0xFF;
    globals_pack(&h, v1);                        /* what a default build would have saved for the same globals */
    v1_bytes = f_get32(f_blob[1] + 4) == GLOBALS_BYTES && !memcmp(f_blob[1] + 12, v1, GLOBALS_BYTES);
    {                                            /* a default build reads blobs 0 / 1 only: the version-1 record */
        globals_t t;
        others_kept = globals_read(1, &t) == 0 && t.v[G_MIDI_CH] == 3 && t.v[G_TUNE] == -20 && t.seq == 41;
    }
    globals_load(&g);
    back = g.v[G_DUAL] == 1 && g.v[G_MIDI_CH] == 3 && g.seq == 41;
    g.v[G_MIDI_CH] = 4;                          /* a default build edits and saves (blob 0; blob 2 untouched) */
    g.seq = 41;
    { globals_t t = g; t.seq = 42; globals_pack(&t, v1); blob_save(0, v1, GLOBALS_BYTES); }
    globals_load(&h);
    kept = h.v[G_MIDI_CH] == 4 && h.v[G_DUAL] == 1 && h.seq == 42;
    memset(f_blob[GLOBALS_DUAL_BLOB], 0xFF, sizeof f_blob[0]);   /* no DUAL CORE blob */
    globals_load(&h);
    missing = h.v[G_DUAL] == 0 && h.v[G_MIDI_CH] == 4;
    memcpy(raw, "DUAL\x01\x00\x00", sizeof raw);   /* a wrong CRC */
    blob_save(GLOBALS_DUAL_BLOB, raw, sizeof raw);
    globals_load(&h);
    broken = h.v[G_DUAL] == 0;
    {                                            /* the CRC right, the value 2 */
        uint16_t c;
        raw[4] = 2, c = program_crc16(raw, 5u), raw[5] = (uint8_t)(c & 0xFFu), raw[6] = (uint8_t)(c >> 8);
        blob_save(GLOBALS_DUAL_BLOB, raw, sizeof raw);
        globals_load(&h);
        range = h.v[G_DUAL] == 0 && h.v[G_MIDI_CH] == 4;
    }
    printf("globals_dual_blob: a default build's record read here %d; saved here: the version-1 record byte for byte %d, "
           "read as a default build would %d, nothing past blob 2 %d; read back %d; a default build's later save keeps "
           "DUAL CORE %d; blob 2 missing -> OFF %d, broken -> OFF %d, value 2 -> OFF %d\n", from_default, v1_bytes,
           others_kept, only_blob2, back, kept, missing, broken, range);
    CHECK(from_default && v1_bytes && others_kept && only_blob2 && back && kept && missing && broken && range);
}
#endif

int main(void)
{
    uint8_t tag[16];
    smk_owner_tag(tag);                         /* (store.h's, unused here) */
    count_at_least_100();
    store_roundtrip_all_slots();
    bad_crc_loads_init();
    power_cut_keeps_slot();
    seq_wraps();
    globals_roundtrip();
    globals_cut_save();
    globals_bad_crc_defaults();
#if KNOBI_CPU1
    globals_dual_blob();
#endif
    response_curves();
    if (fails) printf("test_store: %d FAILED\n", fails);
    else printf("test_store: all passed\n");
    return fails != 0;
}
