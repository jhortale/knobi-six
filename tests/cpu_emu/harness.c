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
 * sizeof (synth_t), a checksum of the output (bit-identity across optimisations). */
#include <stdint.h>
#include "libc.c"
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
