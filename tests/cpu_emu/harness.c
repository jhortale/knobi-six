/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX CPU gate (Task 11): a bare-metal harness for the pi32v2 emulator joakimeriksson/fm1-emu (MIT). It builds
 * the synth exactly as app.c includes it (params + synth/, unity build, the app's compiler flags), loads a patch,
 * plays six keys and renders BLOCKS blocks of 256 frames (one audio interrupt's worth) through synth_render. run.sh
 * builds it for two BLOCKS values and analyze.py takes the difference, so start-up and the note-ons cancel out.
 * After the spike harness in jhortale/smk37-analog (branch spike/device-link, spike/cpu/, by João Hortale).
 *
 *   PATCH 0: the init program, C4 E4 G4 C5 E5 G5 at velocity 100 (tests/scripts/chord.txt)
 *   PATCH 1: the worst case (tests/scripts/worst.txt, test_render WORST): C3 G3 C4 E4 G4 C5 at velocity 127
 *   PATCH 2: the same patch on a high chord, notes 120..127 (VCO 1 pulse + VCO 2 pulse + sync: the oscillators'
 *            out-of-line PolyBLEP / sync path runs on most samples there). Patches 1 and 2 are the "extreme" cases:
 *            measured, guarded (voices.c's CPU guard), not the gate.
 *   PATCH 3: the musical stress patch, the gate (tests/scripts/musical.txt; docs/MEASUREMENTS.md): C3 G3 C4 E4 G4 C5
 *   PATCH 4: the same on C5 G5 C6 E6 G6 C7 (tests/scripts/musical_high.txt)
 * NVOICE (voices.h) comes from the command line: 6 or 4. Results at 0x01C70000: magic, voices sounding,
 * sizeof (synth_t), a checksum of the output (bit-identity across optimisations).
 *
 * DUAL=1 (Phase 2 Task 3, the dual-core split as engine.c does it in a KNOBI_CPU1 build with DUAL CORE on): the
 * harness starts the emulator's second core (the entry word at 0x01C7FFF8, 0x10008 bit 3: _c1_entry below) and each
 * control period hands set B (synth_balance) to it through a two-line mailbox, renders set A, waits (c0_join), and
 * adds CPU1's bus. A minimal mailbox, not the HAL's (hal/fm1_cpu1.h: its check word and deadline are a few dozen
 * cycles a period more); the voices, the balance, the bus and the 32-byte alignment are the firmware's. The
 * checksum must equal the one-core run's. RES[4]: the jobs CPU1 ran. patch_emu.py records core 1's pcs with bit 31
 * set; analyze.py splits the cycles by core. */
#include <stdint.h>
#include "libc.c"
#ifndef DUAL
#define DUAL 0
#endif
#if DUAL
struct synth;
static void dual_split(struct synth *s, int32_t *bus, uint32_t n);
#define SYNTH_SPLIT(s, bus, n) dual_split((s), (bus), (n))
#define SYNTH_VOICE_ALIGN __attribute__((aligned(32)))   /* (engine.c: each voice on lines of its own) */
#endif
#include "params/params.c"
#include "synth/osc.c"
#include "synth/filter.c"
#include "synth/env.c"
#include "synth/mod.c"
#include "synth/voice.c"
#include "synth/voices.c"

#define RES ((volatile uint32_t *)0x01C70000u)
#define FRAMES 256
#ifndef PATCH
#define PATCH 1
#endif
#ifndef BLOCKS
#define BLOCKS 40
#endif

static synth_t S;
static program_t P;
static int32_t OUT[2 * FRAMES];

#if DUAL
#define SYNC() __asm__ volatile("csync" ::: "memory")
/* CPU0's words, then CPU1's, each on 32-byte lines of their own (the AC791N's line: SDK asm/cache.h) */
static struct { volatile uint32_t req, mask, n; } mb0 __attribute__((aligned(32)));
static struct { volatile uint32_t ready, done, jobs; int32_t bus[2 * VOICE_CTL] __attribute__((aligned(32))); }
    mb1 __attribute__((aligned(32)));
_Static_assert(sizeof(voice_t) % 32u == 0, "voice_t: whole cache lines");
static void __attribute__((noinline)) cpu1_job_voices(void)   /* on CPU1 */
{
    uint32_t n = mb0.n;
    for (uint32_t i = 0; i < 2u * n; i++) mb1.bus[i] = 0;
    synth_render_set(&S, mb0.mask, mb1.bus, n);
}
void __attribute__((noreturn, used)) c1_main(void)            /* _c1_entry below */
{
    uint32_t seen = 0;
    mb1.ready = 1;
    SYNC();
    for (;;) {
        uint32_t r = mb0.req;
        SYNC();
        if (r == seen) continue;
        cpu1_job_voices();
        seen = r;
        mb1.jobs++;
        SYNC();
        mb1.done = r;
        SYNC();
    }
}
static void __attribute__((noinline)) c0_join(uint32_t r)    /* CPU0 waits (analyze.py leaves it out of CPU0's work) */
{
    while (mb1.done != r) SYNC();
    SYNC();
}
static void dual_split(struct synth *s, int32_t *bus, uint32_t n)
{
    static uint32_t req;
    uint32_t a, b;
    if (synth_balance(s, &a, &b) < 2u) { synth_render_set(s, SYNTH_ALL_VOICES, bus, n); return; }
    mb0.mask = b, mb0.n = n;
    SYNC();
    mb0.req = ++req;
    SYNC();
    synth_render_set(s, a, bus, n);
    c0_join(req);
    for (uint32_t i = 0; i < 2u * n; i++) bus[i] += mb1.bus[i];
}
/* the second core's entry (the word at 0x01C7FFF8): interrupts off, its own stacks below RES, then c1_main */
__asm__(".section .text.c1_entry,\"ax\",@progbits\n.globl _c1_entry\n_c1_entry:\n\tcli\n"
        "\tusp = 0x01C6E000\n\tsp = 0x01C6F000\n\tssp = 0x01C6F000\n\tcall c1_main\n\tgoto .\n");
extern void _c1_entry(void);
static void c1_start(void)
{
    *(volatile uint32_t *)0x01C7FFF8u = (uint32_t)(uintptr_t)_c1_entry;
    *(volatile uint32_t *)0x10008u |= 8u;
    while (!mb1.ready) SYNC();
}
#endif

#if PATCH >= 3
static const int16_t WORST[][2] = {   /* = tests/scripts/musical.txt */
    {P_MIX_VCO1, 95}, {P_MIX_VCO2, 95}, {P_MIX_SUB, 95}, {P_MIX_NOISE, 95},
    {P_VCO1_SHAPE, 255}, {P_VCO1_PW, 90}, {P_VCO2_SHAPE, 254}, {P_VCO2_PW, 160}, {P_VCO1_SYNC, 1},
    {P_FLT_FREQ, 90}, {P_FLT_RESO, 201}, {P_FLT_ENV_AMT, 76}, {P_FENV_S, 40}, {P_PRG_VOL, 100},
    {P_XM_VCO2, 64}, {P_XM_D_VCO1, 1}, {P_XM_D_FFREQ, 1},
    {P_LFO_FREQ, 173}, {P_LFO_AMT, 128}, {P_LFO_D_VCO1, 1}, {P_LFO_D_FFREQ, 1},
    {P_AT_AMT, 64}, {P_AT_D_FFREQ, 1},
};
#elif PATCH != 0
static const int16_t WORST[][2] = {   /* = tests/unit/test_render.c WORST = tests/scripts/worst.txt */
    {P_MIX_VCO1, 127}, {P_MIX_VCO2, 127}, {P_MIX_SUB, 127}, {P_MIX_NOISE, 127},
    {P_VCO1_SHAPE, 255}, {P_VCO1_PW, 0}, {P_VCO2_SHAPE, 254}, {P_VCO2_PW, 255}, {P_VCO1_SYNC, 1},
    {P_FLT_FREQ, 100}, {P_FLT_RESO, 255}, {P_FLT_ENV_AMT, 127}, {P_PRG_VOL, 127},
    {P_XM_VCO2, 127}, {P_XM_FENV, 127}, {P_XM_D_VCO1, 1}, {P_XM_D_SHAPE1, 1}, {P_XM_D_PW1, 1}, {P_XM_D_FFREQ, 1},
    {P_XM_D_FMODE, 1}, {P_XM_D_BP, 1},
    {P_LFO_FREQ, 254}, {P_LFO_SHAPE, 3}, {P_LFO_AMT, 255}, {P_LFO_D_VCO1, 1}, {P_LFO_D_VCO2, 1}, {P_LFO_D_PW, 3},
    {P_LFO_D_AMP, 1}, {P_LFO_D_FFREQ, 1}, {P_LFO_D_FMODE, 1},
    {P_AT_AMT, 127}, {P_AT_D_VCO1, 1}, {P_AT_D_VCO2, 1}, {P_AT_D_LFOAMT, 1}, {P_AT_D_AMP, 1}, {P_AT_D_FFREQ, 1},
    {P_AT_D_FMODE, 1},
};
#endif
#if PATCH == 0
static const uint8_t NOTES[6] = {60, 64, 67, 72, 76, 79};
#elif PATCH == 1
static const uint8_t NOTES[6] = {48, 55, 60, 64, 67, 72};
#elif PATCH == 2
static const uint8_t NOTES[6] = {120, 121, 123, 124, 126, 127};
#elif PATCH == 3
static const uint8_t NOTES[6] = {48, 55, 60, 64, 67, 72};
#else
static const uint8_t NOTES[6] = {72, 79, 84, 88, 91, 96};
#endif

int main(void)
{
    uint32_t sum = 0;
#if DUAL
    c1_start();
#endif
    synth_init(&S);
    program_init(&P);
#if PATCH != 0
    for (unsigned i = 0; i < sizeof WORST / sizeof WORST[0]; i++) P.v[WORST[i][0]] = WORST[i][1];
#endif
    synth_program(&S, &P);
#if PATCH == 1 || PATCH == 2
    synth_wheel(&S, 127);
    synth_pressure(&S, 127);
#elif PATCH >= 3
    synth_pressure(&S, 100);
#endif
    for (unsigned i = 0; i < 6; i++) synth_note(&S, NOTES[i], PATCH == 1 || PATCH == 2 ? 127 : 100);
    for (unsigned b = 0; b < BLOCKS; ++b) {
        for (unsigned i = 0; i < 2 * FRAMES; i++) OUT[i] = 0;
        synth_render(&S, OUT, FRAMES);
        for (unsigned i = 0; i < 2 * FRAMES; i++) sum = (sum << 1 | sum >> 31) ^ (uint32_t)OUT[i];
    }
    RES[1] = (uint32_t)synth_active(&S);
    RES[2] = (uint32_t)sizeof S;
    RES[3] = sum;
#if DUAL
    RES[4] = mb1.jobs;
#endif
    RES[0] = 0x50524F42u;
    ((void (*)(void))0x00000100u)();   /* leave every code region: the emulator halts */
    return 0;
}

extern uint32_t __data_lma[], __data_start[], __data_end[], __bss_start[], __bss_end[];
/* Runs from XIP: copy code + data to RAM, clear bss. fm1-emu models no cache, so this is the all-hits case (on the
 * device the synth runs from the cached XIP flash). */
__attribute__((section(".text.boot"), noinline)) void boot(void)
{
    volatile uint32_t *s = __data_lma, *d = __data_start, *z = __bss_start;
    while (d < __data_end) *d++ = *s++;
    while (z < __bss_end) *z++ = 0;
    main();
}
