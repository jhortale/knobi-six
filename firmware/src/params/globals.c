/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX globals (globals.h). Included by app.c after platform.c (blob_load / blob_save), params.c and
 * store_prog.c; tests/unit/test_store.c includes it after its fakes; tests/unit/test_controls.c with GLOBALS_PURE
 * (the tables and the curves only). */
#include "globals.h"

#if KNOBI_CPU1
#define GLOB_DUAL_MIN , 0                                 /* DUAL CORE: OFF / ON, OFF by default */
#define GLOB_DUAL_MAX , 1
#else
#define GLOB_DUAL_MIN
#define GLOB_DUAL_MAX
#endif
/*                                   CH  POT TX RX  TRANSP TUNE VEL LOCAL STEREO SUS AT (DUAL) */
const int8_t GLOB_MIN[G_COUNT] = {   0,  0,  0, 0,   -12,  -50,  0,  0,    0,     0,  0 GLOB_DUAL_MIN};
const int8_t GLOB_MAX[G_COUNT] = {  16,  2,  1, 1,    12,   50,  7,  1,    1,     1,  3 GLOB_DUAL_MAX};
const int8_t GLOB_DEF[G_COUNT] = {   0,  POT_RELATIVE, 1, 1, 0, 0, 0, 1,  0,     0,  0 GLOB_DUAL_MIN};

#ifndef GLOBALS_PURE
#include "store_prog.h"
void globals_default(globals_t *g)
{
    memset(g, 0, sizeof *g);
    for (unsigned i = 0; i < G_COUNT; i++) g->v[i] = GLOB_DEF[i];
}

void globals_pack(const globals_t *g, uint8_t *b)
{
    unsigned o = 0, i;
    uint16_t c;
    b[o++] = GLOBALS_VERSION;
    for (i = 0; i < G_COUNT_V1; i++) b[o++] = (uint8_t)g->v[i];
    b[o++] = (uint8_t)g->oct;
    b[o++] = (uint8_t)(g->last_prog & 0xFFu);
    b[o++] = (uint8_t)(g->last_prog >> 8);
    for (i = 0; i < 4u; i++) b[o++] = (uint8_t)(g->seq >> 8 * i);
    c = program_crc16(b, o);
    b[o++] = (uint8_t)(c & 0xFFu);
    b[o++] = (uint8_t)(c >> 8);
}

static int globals_read(uint32_t blob, globals_t *t)   /* 0: a good record */
{
    uint8_t b[GLOBALS_BYTES];
    unsigned o = 1, i;
    if (blob_load(blob, b, sizeof b) || b[0] != GLOBALS_VERSION ||
        program_crc16(b, GLOBALS_BYTES - 2u) != (uint16_t)(b[GLOBALS_BYTES - 2u] | b[GLOBALS_BYTES - 1u] << 8))
        return -1;
    for (i = 0; i < G_COUNT_V1; i++) {
        t->v[i] = (int8_t)b[o++];
        if (t->v[i] < GLOB_MIN[i] || t->v[i] > GLOB_MAX[i]) return -1;
    }
    t->oct = (int8_t)b[o++];
    t->last_prog = (uint16_t)(b[o] | b[o + 1u] << 8);
    o += 2u;
    t->seq = (uint32_t)b[o] | (uint32_t)b[o + 1u] << 8 | (uint32_t)b[o + 2u] << 16 | (uint32_t)b[o + 3u] << 24;
    return t->oct < -2 || t->oct > 2 || t->last_prog >= PROG_COUNT ? -1 : 0;
}
static uint8_t globals_at;                            /* the blob (0 / 1 after GLOBALS_BLOB) the newest record is in */
#if KNOBI_CPU1
/* DUAL CORE (a KNOBI_CPU1 build only): its own blob, GLOBALS_DUAL_BLOB, never touched by a default build, so the
 * version-1 record in blobs 0 / 1 stays byte for byte a default build's and the builds can be swapped both ways.
 * Record: "DUAL" (LE), the value, its CRC16. Missing, broken or out of range: OFF */
static int8_t globals_dual_saved = -1;                /* the value in the blob (-1: not known yet) */
static int globals_dual_read(void)
{
    uint8_t b[GLOBALS_DUAL_BYTES];
    if (blob_load(GLOBALS_DUAL_BLOB, b, sizeof b) || b[0] != 'D' || b[1] != 'U' || b[2] != 'A' || b[3] != 'L' ||
        program_crc16(b, 5u) != (uint16_t)(b[5] | b[6] << 8) || (int8_t)b[4] < GLOB_MIN[G_DUAL] ||
        (int8_t)b[4] > GLOB_MAX[G_DUAL])
        return 0;
    return (int8_t)b[4];
}
static int globals_dual_save(int v)
{
    uint8_t b[GLOBALS_DUAL_BYTES] = {'D', 'U', 'A', 'L', (uint8_t)v, 0, 0};
    uint16_t c = program_crc16(b, 5u);
    b[5] = (uint8_t)(c & 0xFFu), b[6] = (uint8_t)(c >> 8);
    return blob_save(GLOBALS_DUAL_BLOB, b, sizeof b);
}
#endif
void globals_load(globals_t *g)
{
    globals_t a, b;
    int ga = globals_read(GLOBALS_BLOB, &a) == 0, gb = globals_read(GLOBALS_BLOB + 1u, &b) == 0;
    globals_default(g);
    globals_at = 1;                                   /* (nothing good: the first save goes to blob 0) */
    if (ga && (!gb || (int32_t)(a.seq - b.seq) > 0)) *g = a, globals_at = 0;
    else if (gb) *g = b, globals_at = 1;
#if KNOBI_CPU1
    g->v[G_DUAL] = (int8_t)(globals_dual_saved = (int8_t)globals_dual_read());
#endif
}

int globals_save(globals_t *g)
{
    uint8_t b[GLOBALS_BYTES];
    globals_t t = *g;
    t.seq = g->seq + 1u;
#if KNOBI_CPU1
    if (g->v[G_DUAL] != globals_dual_saved) {         /* (first: a failure changes nothing, the caller tries again) */
        if (globals_dual_save(g->v[G_DUAL])) return -1;
        globals_dual_saved = g->v[G_DUAL];
    }
#endif
    globals_pack(&t, b);
    if (blob_save(GLOBALS_BLOB + (globals_at ^ 1u), b, sizeof b) == 0) {   /* the other blob: a cut save leaves this one */
        globals_at ^= 1u;
        g->seq = t.seq;
        return 0;
    }
    return -1;
}

#endif

/* curves: y = v + a x v (127 - v) / 127 bends a line through 0 and 127 (a > 0 softer: more out at a light touch;
 * a < 0 harder), |a| <= 1 keeps it rising */
static int glob_bend(int v, int num, int den)
{
    int y;
    if (v <= 0) return 0;
    if (v >= 127) return 127;
    y = v + num * v * (127 - v) / (127 * den);
    return y < 1 ? 1 : y > 127 ? 127 : y;
}
int glob_vel_curve(int curve, int v)
{
    if (v <= 0) return 0;
    if (v > 127) v = 127;
    if (curve >= 1 && curve <= 3) return glob_bend(v, curve, 3);          /* softer: a 1/3, 2/3, 1 */
    if (curve >= 4 && curve <= 6) return glob_bend(v, -(curve - 3), 3);   /* harder */
    if (curve == 7) return 127;                                           /* fixed */
    return v;
}
int glob_at_curve(int curve, int v)
{
    if (v <= 0) return 0;
    if (v > 127) v = 127;
    if (curve == 1) return glob_bend(v, 1, 2);
    if (curve == 2) return glob_bend(v, 1, 1);
    if (curve == 3) return glob_bend(v, -1, 2);
    return v;
}
