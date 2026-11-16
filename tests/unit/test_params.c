/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include <stdlib.h>
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
        CHECK(n == PROGRAM_PACKED_SIZE);
        CHECK(program_unpack(&b, buf, (uint32_t)n) == 0);
        CHECK(memcmp(a.name, b.name, 20) == 0 && memcmp(a.v, b.v, sizeof a.v) == 0 && memcmp(a.chord, b.chord, 6) == 0);
        CHECK(b.version == a.version);
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
    CHECK(program_unpack(&b, buf, (uint32_t)n - 1) == -1);
}

static void packed_size(void)
{
    printf("sizeof(program_t)=%u packed=%d params=%d\n", (unsigned)sizeof(program_t), PROGRAM_PACKED_SIZE, P_COUNT_PHASE1);
    CHECK(PROGRAM_PACKED_SIZE <= 256);
}

int main(void)
{
    ids_dense(); defaults_in_range(); bipolar_set(); lfo_dests_are_switches(); ranges_from_reference();
    pack_roundtrip(); unpack_rejects_corrupt(); packed_size();
    printf(fails ? "test_params: %d FAILED\n" : "test_params: ok\n", fails);
    return fails != 0;
}
