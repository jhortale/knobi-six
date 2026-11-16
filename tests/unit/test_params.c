/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include <stdlib.h>
/* pretend there are two record versions: v1 stored only the first 61 ids, v2 (current) stores all */
#define PROGRAM_VERSION 2
#define PROGRAM_VERSION_TABLE { P_COUNT_PHASE1 - 5, P_COUNT_PHASE1 }
#include "params/params.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void ids_dense(void)
{
    for (unsigned i = 0; i < P_COUNT_PHASE1; i++) { CHECK(PARAMS[i].id == i); CHECK(PARAMS[i].name != 0); }
}

static void defaults_in_range(void)
{
    for (unsigned i = 0; i < P_COUNT_PHASE1; i++) {
        CHECK(PARAMS[i].min <= PARAMS[i].def && PARAMS[i].def <= PARAMS[i].max);
        CHECK(PARAMS[i].section < PS_COUNT);
        CHECK(PARAMS[i].kind != PK_SELECT || PARAMS[i].labels != 0);
        CHECK(PARAMS[i].kind != PK_SWITCH || (PARAMS[i].min == 0 && PARAMS[i].max == 1));
    }
    program_t p; program_init(&p);
    for (unsigned i = 0; i < P_COUNT_PHASE1; i++) CHECK(p.v[i] == PARAMS[i].def);
    CHECK(strcmp(p.name, "INIT") == 0);
}

static void bipolar_set(void)
{
    for (unsigned i = 0; i < P_COUNT_PHASE1; i++) {
        int want = i == P_VCO2_FINE || i == P_FLT_ENV_AMT || i == P_AT_AMT || i == P_XM_FENV || i == P_XM_VCO2;
        CHECK(PARAMS[i].bipolar == want);
        if (want) CHECK(PARAMS[i].min < 0 && PARAMS[i].max > 0 && PARAMS[i].def == 0);
    }
}

static void lfo_dests_are_switches(void)
{
    unsigned s[] = { P_LFO_D_VCO1, P_LFO_D_VCO2, P_LFO_D_AMP, P_LFO_D_FFREQ, P_LFO_D_FMODE,
                     P_XM_D_VCO1, P_XM_D_SHAPE1, P_XM_D_PW1, P_XM_D_FFREQ, P_XM_D_FMODE, P_XM_D_BP,
                     P_AT_D_VCO1, P_AT_D_VCO2, P_AT_D_LFOAMT, P_AT_D_AMP, P_AT_D_FFREQ, P_AT_D_FMODE };
    for (unsigned i = 0; i < sizeof s / sizeof *s; i++) CHECK(PARAMS[s[i]].kind == PK_SWITCH);
    /* the 4-state pulse-width destination (none/PW1/PW2/both) is a select 0..3 */
    CHECK(PARAMS[P_LFO_D_PW].kind == PK_SELECT && PARAMS[P_LFO_D_PW].max == 3);
}

static void ranges_from_reference(void)
{
    CHECK(PARAMS[P_VCO1_FREQ].max == 60 && PARAMS[P_VCO2_FREQ].max == 60);
    CHECK(PARAMS[P_VCO2_FINE].min == -50 && PARAMS[P_VCO2_FINE].max == 50);
    CHECK(PARAMS[P_LFO_SHAPE].max == 4 && PARAMS[P_FLT_TRACK].max == 2 && PARAMS[P_BEND_RANGE].max == 12);
    CHECK(PARAMS[P_PORTA_MODE].max == 3 && PARAMS[P_KEY_MODE].max == 5 && PARAMS[P_UNISON_VOICES].max == 6);
    CHECK(param_clamp(P_VCO1_FREQ, 99) == 60 && param_clamp(P_VCO2_FINE, -99) == -50 && param_clamp(P_VCO1_FREQ, -1) == 0);
    CHECK(param_clamp(P_COUNT_PHASE1, 5) == 0);
}

static void rndfill(program_t *p, unsigned seed)
{
    srand(seed);
    program_init(p);
    for (unsigned i = 0; i < P_COUNT_PHASE1; i++) p->v[i] = param_clamp((uint16_t)i, (rand() % 600) - 300);
    for (unsigned i = P_COUNT_PHASE1; i < PROGRAM_VALUES; i++) p->v[i] = (int16_t)(rand() % 2000 - 1000);
    for (int i = 0; i < 6; i++) p->chord[i] = (uint8_t)(rand() & 0x7F);
    memcpy(p->name, "ABCDEFGHIJKLMNOPQRST", 20);   /* all 20 characters */
}

static void pack_roundtrip(void)
{
    for (unsigned s = 1; s <= 50; s++) {
        program_t a, b; uint8_t buf[256];
        rndfill(&a, s);
        int n = program_pack(&a, buf, sizeof buf);
        CHECK(n == (int)program_packed_size(PROGRAM_VERSION));
        CHECK(program_unpack(&b, buf, (uint32_t)n) == 0);
        CHECK(memcmp(a.name, b.name, 20) == 0 && memcmp(a.v, b.v, sizeof a.v) == 0 && memcmp(a.chord, b.chord, 6) == 0);
        CHECK(b.version == PROGRAM_VERSION);
    }
    program_t a; uint8_t small[10]; program_init(&a);
    CHECK(program_pack(&a, small, sizeof small) == -1);
}

static void unpack_rejects_corrupt(void)
{
    program_t a, b; uint8_t buf[256];
    rndfill(&a, 7);
    int n = program_pack(&a, buf, sizeof buf);
    for (int i = 0; i < n; i++) {
        buf[i] ^= 0x10;
        CHECK(program_unpack(&b, buf, (uint32_t)n) == -1);
        buf[i] ^= 0x10;
    }
    CHECK(program_unpack(&b, buf, (uint32_t)n) == 0);
}

/* re-seal a hand-edited record with a valid CRC */
static void seal(uint8_t *buf, uint32_t n)
{
    uint16_t c = program_crc16(buf, n - 2);
    buf[n - 2] = (uint8_t)c; buf[n - 1] = (uint8_t)(c >> 8);
}

static void unpack_version_and_length(void)
{
    program_t a, b; uint8_t buf[256], w[256];
    rndfill(&a, 9);
    uint32_t n = (uint32_t)program_pack(&a, buf, sizeof buf);
    CHECK(program_unpack(&b, buf, n - 1) == -1 && program_unpack(&b, buf, n + 1) == -1 && program_unpack(&b, buf, 0) == -1);
    memcpy(w, buf, n); w[0] = 0; seal(w, n);
    CHECK(program_unpack(&b, w, n) == -1);                    /* version 0 */
    memcpy(w, buf, n); w[0] = PROGRAM_VERSION + 1; seal(w, n);
    CHECK(program_unpack(&b, w, n) == -1);                    /* future version */
    memcpy(w, buf, n); w[0] = 1; seal(w, n);
    CHECK(program_unpack(&b, w, n) == -1);                    /* v1 label but v2 length */
    memcpy(w, buf, n); w[n - 2 - 6] = 200; seal(w, n);        /* first chord byte out of range */
    CHECK(program_unpack(&b, w, n) == -1);
    CHECK(program_packed_size(0) == 0 && program_packed_size(PROGRAM_VERSION + 1) == 0);
}

static void older_version_gets_defaults(void)
{
    program_t b, a; uint8_t buf[256];
    unsigned n1 = P_COUNT_PHASE1 - 5, o = 0;
    buf[o++] = 1;
    memset(buf + o, ' ', 20); memcpy(buf + o, "OLDPATCH", 8); o += 20;
    for (unsigned i = 0; i < n1; i++) {
        int16_t v = param_clamp((uint16_t)i, PARAMS[i].max);          /* every stored value at its maximum */
        if (i == P_VCO1_FREQ) v = 999;                               /* out of range: must be clamped */
        buf[o++] = (uint8_t)((uint16_t)v & 0xFF); buf[o++] = (uint8_t)((uint16_t)v >> 8);
    }
    memset(buf + o, 60, 6); o += 6; o += 2;
    CHECK(o == program_packed_size(1));
    seal(buf, o);
    memset(&b, 0x55, sizeof b);
    CHECK(program_unpack(&b, buf, o) == 0);
    CHECK(b.version == PROGRAM_VERSION && memcmp(b.name, "OLDPATCH", 8) == 0 && b.chord[0] == 60);
    CHECK(b.v[P_VCO1_FREQ] == 60);
    for (unsigned i = 0; i < n1; i++) if (i != P_VCO1_FREQ) CHECK(b.v[i] == PARAMS[i].max);
    for (unsigned i = n1; i < P_COUNT_PHASE1; i++) CHECK(b.v[i] == PARAMS[i].def);   /* not 0 */
    for (unsigned i = P_COUNT_PHASE1; i < PROGRAM_VALUES; i++) CHECK(b.v[i] == 0);
    (void)a;
}

static void packed_size(void)
{
    printf("sizeof(program_t)=%u packed(v%d)=%u max=%d params=%d\n", (unsigned)sizeof(program_t), PROGRAM_VERSION,
           (unsigned)program_packed_size(PROGRAM_VERSION), PROGRAM_PACKED_MAX, P_COUNT_PHASE1);
    CHECK(PROGRAM_PACKED_MAX <= 256);
}

int main(void)
{
    ids_dense(); defaults_in_range(); bipolar_set(); lfo_dests_are_switches(); ranges_from_reference();
    pack_roundtrip(); unpack_rejects_corrupt(); unpack_version_and_length(); older_version_gets_defaults(); packed_size();
    printf(fails ? "test_params: %d FAILED\n" : "test_params: ok\n", fails);
    return fails != 0;
}
