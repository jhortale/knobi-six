/* SPDX-License-Identifier: GPL-3.0-only */
/* The default build: no firmware/gen/factory_bank.h (it is never committed; CI never has one), so no factory bank and
 * every program never written is the init program; "restore factory" gives the init program. Built as the device
 * builds (no KNOBI_NO_FACTORY: the __has_include path). With a local factory_bank.h this test says so and skips. */
#include <stdio.h>
#include <string.h>
#define BOARD_SMK37 1
#include "store.h"
#include "params/params.c"

static int slot_load(uint32_t i, void *p, uint32_t n) { (void)i, (void)p, (void)n; return -1; }   /* nothing saved */
static int slot_save(uint32_t i, const void *p, uint32_t n) { (void)i, (void)p, (void)n; return -1; }

#undef KNOBI_NO_FACTORY
#include "params/store_prog.c"
static void (*const keep_owner_tag)(uint8_t *) __attribute__((unused)) = smk_owner_tag;   /* (store.h's, unused here) */

int main(void)
{
    static program_t p, init;
    int fails = 0;
#if __has_include("factory_bank.h")
    printf("test_factory_default: a local firmware/gen/factory_bank.h is there: skipped (the default build has none)\n");
    return 0;
#endif
    program_init(&init);
    if (prog_factory_count() != 0u) fails++, printf("FAIL a factory bank without a header\n");
    for (uint32_t n = 0; n < PROG_COUNT; n += 99u) {
        memset(&p, 0x55, sizeof p);
        if (prog_load(n, &p) != -1 || memcmp(&p, &init, sizeof p)) fails++, printf("FAIL program %u is not init\n", (unsigned)n);
        memset(&p, 0x55, sizeof p);
        if (prog_factory(n, &p) != -1 || memcmp(&p, &init, sizeof p)) fails++, printf("FAIL restore %u is not init\n", (unsigned)n);
    }
    if (memcmp(init.name, "INIT", 5)) fails++;
    printf("test_factory_default: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
