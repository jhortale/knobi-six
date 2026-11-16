/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX oscillator section (OB-6 schema rule 5): VCO 1 (saw -> pulse, hard-sync slave of VCO 2), VCO 2
 * (triangle -> saw -> pulse), sub (square one octave below VCO 1), noise (white, per voice). Integers only.
 *
 * Levels: every output is Q15 with the waveform's nominal +-1 at OSC_ONE = 16384 (-6 dBFS). Saw, triangle, square
 * and noise peak at +-16384; a thin pulse (DC-free, width 5.35 %) reaches 1.893 x 16384 = 31015 < 32767. The four
 * outputs of one voice therefore sum to < 2^17 and six voices to < 2^20: the mixer has 11 bits of int32 headroom for
 * its levels and overdrive, and scales the voice to the bus itself. */
#ifndef KNOBI_OSC_H
#define KNOBI_OSC_H
#include <stdint.h>

#define OSC_ONE 16384                     /* nominal +-1 of every output, Q15 */
#define OSC_RATE 44100                    /* sample rate the increments are computed for */

enum { OSC_VCO1, OSC_VCO2, OSC_SUB, OSC_NOISE };   /* out[] of osc_tick */

typedef struct {
    uint32_t ph1, ph2, inc1, inc2;        /* Q32 phases and increments (cycles per sample) */
    int16_t shape1, pw1, shape2, pw2;     /* param units: VCO1 SHAPE 0..255, PW 0..255, VCO2 SHAPE 0..254, PW 0..255 */
    uint8_t sync;                         /* 1: VCO 1 restarts on every VCO 2 cycle */
    uint32_t noise;                       /* xorshift32 state, never 0 */
    /* derived by osc_set_pitch / osc_set_shape, and the sub / sync state (internal) */
    uint32_t inv1, inv2;                  /* Q16 of 2^32 / inc, for the PolyBLEP distance t / dt without a divide */
    uint32_t w1, w2;                      /* pulse width as a Q32 phase offset (2^31 = square) */
    int32_t a1, a2, b2;                   /* Q30 amounts: VCO1 pulse, VCO2 pulse, VCO2 saw (1 - triangle) */
    uint32_t sync_tb;                     /* VCO 1 phase just before its last sync restart */
    uint8_t sub, synced;                  /* sub flip-flop; 1 = VCO 1 was restarted at VCO 2's last wrap */
} osc_t;

void osc_init(osc_t *o, uint32_t seed);   /* phases 0, A4 on both, saw / tri shapes, square PW, sync off */
void osc_set_pitch(osc_t *o, int32_t note1_q16, int32_t note2_q16);   /* fractional MIDI notes, Q16 */
void osc_set_shape(osc_t *o, int shape1, int pw1, int shape2, int pw2); /* param units from PARAMS */
/* one frame: VCO1, VCO2, SUB, NOISE in Q15 (OSC_ONE = 1) via out[4]; out[OSC_VCO2] is also X-Mod's VCO 2 source */
void osc_tick(osc_t *o, int32_t out[4]);
int32_t note_to_inc(int32_t note_q16);    /* Q32 phase increment for a Q16 MIDI note; no division */

#endif
