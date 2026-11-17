/* SPDX-License-Identifier: GPL-3.0-only */
/* The factory bank (firmware/src/params/store_prog.c, docs/FACTORY_BANK.md) with tests/unit/factory_test_bank.h: three
 * made-up programs written by tools/ob6_import.py (tests/ob6_import_test.py --write; no OB-6 data). A program never
 * written loads its factory program, past the bank the init program; a written one overrides it; WRITE never touches
 * the bank; prog_factory (restore factory) reads it whatever is stored. The flash is a plain fake here (test_store.c
 * tests the store itself). */
#include <stdio.h>
#include <string.h>
#define BOARD_SMK37 1
#include "store.h"
#include "params/params.c"

static uint8_t f_slot[NSLOT][SLOT_MAX];
static uint8_t f_saved[NSLOT];
static int slot_load(uint32_t i, void *p, uint32_t n)
{
    if (i >= NSLOT || !f_saved[i] || n > SLOT_MAX) return -1;
    memcpy(p, f_slot[i], n);
    return 0;
}
static int slot_save(uint32_t i, const void *p, uint32_t n)
{
    if (i >= NSLOT || n > SLOT_MAX) return -1;
    memcpy(f_slot[i], p, n), f_saved[i] = 1;
    return 0;
}

#undef KNOBI_NO_FACTORY
#define KNOBI_FACTORY_H "../../../tests/unit/factory_test_bank.h"   /* (from firmware/src/params/) */
#include "params/store_prog.c"
static void (*const keep_owner_tag)(uint8_t *) __attribute__((unused)) = smk_owner_tag;   /* (store.h's, unused here) */

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    static program_t p, q, init;
    program_init(&init);
    CHECK(prog_factory_count() == 3u);

    /* an empty store (the first boot): every program never written is its factory program */
    CHECK(prog_load(0u, &p) == 1);
    CHECK(memcmp(p.name, "FACTORY ZERO", 12) == 0 && p.name[12] == 0);
    CHECK(p.v[P_VCO2_FINE] == -50 && p.v[P_VCO1_SHAPE] == 200 && p.v[P_FLT_FREQ] == 82 && p.v[P_FLT_MODE] == 127);
    CHECK(p.v[P_FLT_ENV_AMT] == 127 && p.v[P_AT_AMT] == -127 && p.v[P_XM_FENV] == 73 && p.v[P_XM_VCO2] == -100);
    CHECK(p.v[P_UNISON_ON] == 1 && p.v[P_UNISON_VOICES] == 6 && p.v[P_FLT_BP] == 1 && p.v[P_XM_D_BP] == 1);
    CHECK(p.chord[0] == 60 && p.chord[1] == 63 && p.chord[2] == 67 && p.chord[3] == 70 && p.chord[4] == 0);
    CHECK(p.v[P_DISTORTION] == 99 && p.v[P_BPM] == 250 && p.v[P_ARP_ON] == 1 && p.v[P_FXB_TYPE] == 13);
    CHECK(prog_load(1u, &p) == 1 && memcmp(p.name, "Factory One", 11) == 0 && p.v[P_VCO2_FINE] == 50);
    CHECK(p.v[P_FENV_A] == 11 && p.v[P_LENV_A] == 22 && p.v[P_LENV_R] == 88 && p.v[P_KEY_MODE] == 5);
    CHECK(prog_load(2u, &p) == 1 && p.name[0] == 'X' && p.v[P_VCO1_FREQ] == 60 && p.v[P_MIX_NOISE] == 127);
    CHECK(prog_load(3u, &p) == -1 && memcmp(&p, &init, sizeof p) == 0);   /* past the bank: init */
    CHECK(prog_load(PROG_COUNT, &p) == -1);                                 /* past the store */

    /* WRITE: the written program overrides its factory one; the bank is untouched; the neighbours stay factory */
    program_init(&q);
    memcpy(q.name, "MINE", 5);
    q.v[P_FLT_FREQ] = 7;
    CHECK(prog_save(1u, &q) == 0);
    CHECK(prog_load(1u, &p) == 0 && memcmp(p.name, "MINE", 5) == 0 && p.v[P_FLT_FREQ] == 7);
    CHECK(prog_load(0u, &p) == 1 && p.v[P_FLT_FREQ] == 82);
    CHECK(prog_load(2u, &p) == 1 && p.name[0] == 'X');

    /* restore factory: the factory program whatever is stored (into the edit buffer; the store stays as it is) */
    CHECK(prog_factory(1u, &p) == 0 && memcmp(p.name, "Factory One", 11) == 0 && p.v[P_FLT_FREQ] == 0);
    CHECK(prog_load(1u, &p) == 0 && p.v[P_FLT_FREQ] == 7);
    CHECK(prog_factory(3u, &p) == -1 && memcmp(&p, &init, sizeof p) == 0);   /* no factory program 3: init */

    /* a broken stored record: the init program and -2 (the panel's notice), never silently the factory program */
    f_slot[prog_map[0]][1u * PROG_STRIDE + 5u] ^= 0x55u;
    prog_buf_slot = -1;
    CHECK(prog_load(1u, &p) == -2 && memcmp(&p, &init, sizeof p) == 0);
    CHECK(prog_load(0u, &p) == 1 && prog_load(2u, &p) == 1);                /* its neighbours: still factory */
    f_slot[prog_map[0]][1u * PROG_STRIDE] = PROGRAM_VERSION + 1u;           /* a version this build does not know */
    prog_buf_slot = -1;
    CHECK(prog_load(1u, &p) == -2 && memcmp(&p, &init, sizeof p) == 0);

    printf("test_factory: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
