/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio): from Melodee (github.com/keremimo/melodee), fm1_core1_stop.h,
 *   under MPL-2.0 by agreement (https://github.com/hugelton/felucca-bsp/issues/5)
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments: Felucca 1.6.1's fm1_cpu1_halt
 *   (a53ecf4; https://github.com/hugelton/Felucca/pull/247)
 * KNOBI-SIX: hugelton/felucca-bsp fbc254d chip/fm1_cpu1_stop.h's hold without the flash gate's idle word, so a build
 * without the second core (the update loader) can include it. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

/* The second core held, registers only: the update loader's first line (ldr_cstart), in every build, behind the app's
 * own holds (fm1_enter_update, fm1_core_reset with KNOBI_CPU1), for a CPU1 a crashed app may have left running. The
 * hold sequence of felucca-bsp's fm1_cpu1_stop.h (Melodee's): the bus traps masked, C1_CON (0x1EEE004) bit 1 set
 * then bit 3 cleared, the delayed bus flag given time and cleared, the debug enables and the write lock put back.
 * On a CPU1 that never ran (the SPL starts none) it writes the held state it already has. */
FM1_INLINE void fm1_cpu1_hold_regs(void)
{
    uint32_t locked = !(*(volatile uint32_t *)0x1EEE240u & 1u), en, bus = 0x003F0030u;
    volatile uint32_t n;
    __asm__("" : "+r"(bus));                    /* (not folded into an address that looks like the chip ROM's) */
    if (locked)
        *(volatile uint32_t *)0x1EEE240u = 0xE7u;          /* DBG_WR_EN: writing 0xE7 toggles the unlock */
    en = *(volatile uint32_t *)0x1EEE340u;                 /* DBG_EN */
    *(volatile uint32_t *)0x1EEE340u = en & ~bus;
    __asm__ volatile("csync" ::: "memory");
    *(volatile uint32_t *)0x1EEE004u |= 2u;
    *(volatile uint32_t *)0x1EEE004u &= ~8u;
    __asm__ volatile("csync" ::: "memory");
    for (n = 0; n < 256u; n++)
        ;
    *(volatile uint32_t *)0x1EEE248u = bus;                /* DBG_MSG_CLR */
    __asm__ volatile("csync" ::: "memory");
    *(volatile uint32_t *)0x1EEE340u = en;
    if (locked && (*(volatile uint32_t *)0x1EEE240u & 1u))
        *(volatile uint32_t *)0x1EEE240u = 0xE7u;
}
