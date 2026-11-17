/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX globals (globals.h). Included by app.c after platform.c (settings_load / settings_save), params.c and
 * store_prog.c; tests/unit/test_store.c includes it after its fakes; tests/unit/test_controls.c with GLOBALS_PURE
 * (the tables and the curves only). */
#include "globals.h"

/*                                   CH  POT TX RX  TRANSP TUNE VEL LOCAL STEREO SUS AT */
const int8_t GLOB_MIN[G_COUNT] = {   0,  0,  0, 0,   -12,  -50,  0,  0,    0,     0,  0};
const int8_t GLOB_MAX[G_COUNT] = {  16,  2,  1, 1,    12,   50,  7,  1,    1,     1,  3};
const int8_t GLOB_DEF[G_COUNT] = {   0,  POT_RELATIVE, 1, 1, 0, 0, 0, 1,  0,     0,  0};

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
    for (i = 0; i < G_COUNT; i++) b[o++] = (uint8_t)g->v[i];
    b[o++] = (uint8_t)g->oct;
    b[o++] = (uint8_t)(g->last_prog & 0xFFu);
    b[o++] = (uint8_t)(g->last_prog >> 8);
    c = program_crc16(b, o);
    b[o++] = (uint8_t)(c & 0xFFu);
    b[o++] = (uint8_t)(c >> 8);
}

void globals_load(globals_t *g)
{
    uint8_t b[GLOBALS_BYTES];
    globals_t t;
    unsigned o = 1, i;
    globals_default(g);
    if (settings_load(b, sizeof b) || b[0] != GLOBALS_VERSION ||
        program_crc16(b, GLOBALS_BYTES - 2u) != (uint16_t)(b[GLOBALS_BYTES - 2u] | b[GLOBALS_BYTES - 1u] << 8))
        return;
    for (i = 0; i < G_COUNT; i++) {
        t.v[i] = (int8_t)b[o++];
        if (t.v[i] < GLOB_MIN[i] || t.v[i] > GLOB_MAX[i]) return;
    }
    t.oct = (int8_t)b[o++];
    t.last_prog = (uint16_t)(b[o] | b[o + 1u] << 8);
    if (t.oct < -2 || t.oct > 2 || t.last_prog >= PROG_COUNT) return;
    *g = t;
}

void globals_save(const globals_t *g)
{
    uint8_t b[GLOBALS_BYTES];
    globals_pack(g, b);
    (void)settings_save(b, sizeof b);
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
