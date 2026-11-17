/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX modulation (OB-6 schema rules 4, 6, 7): the one shared LFO, its depth (INITIAL AMOUNT + mod wheel +
 * aftertouch), X-Mod (filter envelope and VCO 2 into VCO1 freq / shape / PW, filter freq / mode, Norm<->BP) and
 * aftertouch (VCO1, VCO2, LFO AMT, AMP, F FREQ, F MODE). Integers only. Reference: docs/ob6-reference.md 2.6-2.8, 3.
 *
 * LFO (rule 4). FREQUENCY 0..254 is exponential, 0.022 Hz * (500 / 0.022)^(p / 254): 0 = 0.022 Hz, 254 = 500 Hz
 * (one step = 1.0397x, 0.068 octave). Output Q15 (LFO_ONE = 32767 = +1): SINE and RANDOM are bipolar (+-32767),
 * SAW (rising 0 -> +1), REV SAW (+1 -> 0) and SQUARE (+1 for the first half cycle, then 0) are >= 0. RANDOM is a
 * sample and hold, one new uniform value per cycle; at FREQUENCY 254 (and only there: the increment equals LFO_INC_TOP)
 * it takes a new value on every lfo_tick, i.e. white noise at the tick rate (the hidden 6th shape). LFO SYNC: when
 * synced and a clock increment is given (phase 2's clock: Q32 cycles per sample), the LFO runs at that increment
 * (capped just below LFO_INC_TOP, so a synced LFO never turns into noise); clock_inc 0 or SYNC off = free running.
 * Key reset: lfo_key(first_key_down = 1) restarts the phase at 0 (and RANDOM draws a new value at the next tick);
 * lfo_key(0), a key pressed while others are held, does nothing. The reset applies with SYNC on and off (ruling: the
 * reference is ambiguous, rule 4 says "key reset on first key").
 * lfo_tick(frames) advances the phase by frames samples and returns the value at the new phase. It is cheap (one
 * 32 x 32 -> 64 product, a short polynomial for SINE) and shared by all voices: the caller may tick it per sample (a
 * 500 Hz LFO is audio rate) or per control block.
 *
 * Aftertouch. at_q15 in lfo_depth and mods_sum is the SIGNED aftertouch: pressure (0..1) x AT AMOUNT (-127..127) /
 * 127, Q15 (+-32768 = full pressure at +-127). mods_at() makes it. Channel pressure is one value for every voice.
 *
 * Depth (rule 4): lfo_depth = INITIAL AMOUNT / 255 + MOD WHEEL / 127 + (AT -> LFO AMT on ? at_q15 : 0), Q15
 * (32768 = full depth), the sum clamped to +-32768. Linear: a negative aftertouch adds an inverted LFO (with INITIAL
 * and WHEEL at 0 the depth is negative and every destination sees the LFO upside down). The wheel has no other route.
 *
 * mods_t: the offsets one voice adds to its parameters, every destination in its consumer's units:
 *   vco1_pitch, vco2_pitch  Q16 MIDI notes (osc_set_pitch; 65536 = one semitone)
 *   vco1_shape              Q16 of VCO1 SHAPE units (osc_set_shape, 0..255)
 *   vco1_pw, vco2_pw        Q16 of PW units (osc_set_shape, 0..255; 127 = square)
 *   flt_freq                Q16 cutoff semitones (svf_coef; 65536 = one semitone, not one FILTER FREQ step)
 *   flt_mode                Q16 of filter MODE units 0..255 (svf_coef's mode; the MODE param maps m * 255 / 127)
 *   bp_mix                  svf_coef's bp_mix units (0..32768 = Norm -> BP), an offset from the BP switch's 0 / 32768
 *   amp                     Q15 VCA control (vca_control's lfo_q15 argument; 32768 = a full VCA)
 * The consumer adds base << 16 (or the base in its own units), clamps and rounds ((x + 32768) >> 16).
 *
 * Full-scale depths: the offset for source 1.0 at full amount (LFO: |lfo x depth| = 1; X-Mod filter env: env 1 at
 * amount +-127; X-Mod VCO 2: VCO 2 at +-OSC_ONE (its nominal +-1) at amount +-127; aftertouch: at_q15 = +-32768). The
 * reference gives no depths; these are our calls, linear in the amount, to be checked by ear:
 *              VCO pitch   VCO1 shape   PW         filter freq        filter mode   Norm<->BP   AMP
 *   LFO        +-12 st     -            +-128      +-82 st            +-255         -           +-32768 (full VCA)
 *   X-Mod      +-48 st     +-255        +-128      +-164 st           +-255         +-32768     -
 *   AT         +-12 st     -            -          +-164 st           +-255         -           +-32768
 * (Filter freq in semitones, the cutoff's own unit, kept when the FILTER FREQ knob was re-anchored to 16 Hz .. 18.8 kHz
 * in 0.746-semitone steps: 164 st is the filter's whole 1.44 Hz .. 18.8 kHz range and more than the knob's 122.4 st,
 * so a full X-Mod / AT amount still sweeps from any FREQ to either end, a little further than FILTER ENV AMOUNT 127
 * (FENV_RANGE_Q16 = the knob's span); the LFO's 82 st makes a full bipolar sine span the knob's range and more, peak to
 * peak. PW 128 and mode / shape 255 reach the ends of their ranges from the centre / either end.) The LFO PW
 * destination is the 4-state select: 0 none, 1 VCO1 PW, 2 VCO2 PW, 3 both.
 *
 * Where each part runs:
 *   control rate (every 16 frames in app_audio): lfo_set (on edit), lfo_tick (shared), lfo_depth (shared), mods_at
 *     (on pressure), mods_sum per voice (LFO, aftertouch and X-Mod filter env terms; pass vco2_q15 = 0), mods_xm_gains
 *     per voice (or per program edit: it only reads v[]).
 *   per sample, per voice: mods_xm_tick (the VCO 2 X-Mod term, one 32 x 32 -> 64 product per enabled destination)
 *     and, when X-Mod -> VCO1 is on, mods_fm1 (VCO 1's increment and its inverse at the modulated pitch: two cubic
 *     polynomials, no exp2 call, no division). mods_sum(..., vco2_q15) == mods_sum(..., 0) + the mods_xm_tick term
 *     exactly, so a caller may also run mods_sum per sample. */
#ifndef KNOBI_MOD_H
#define KNOBI_MOD_H
#include <stdint.h>
#include "osc.h"

#define LFO_ONE 32767                     /* LFO output +1, Q15 */
#define LFO_INC_TOP 48695774u             /* 500 Hz in Q32 cycles per sample at 44100 Hz: FREQUENCY 254 */
#define MODS_DEPTH_ONE 32768              /* full LFO depth, Q15 */

/* full-scale offsets (see the table above) */
#define MODS_LFO_PITCH (12 * 65536)
#define MODS_LFO_PW (128 * 65536)
#define MODS_LFO_FREQ (82 * 65536)
#define MODS_LFO_MODE (255 * 65536)
#define MODS_LFO_AMP 32768
#define MODS_XM_PITCH (48 * 65536)
#define MODS_XM_SHAPE (255 * 65536)
#define MODS_XM_PW (128 * 65536)
#define MODS_XM_FREQ (164 * 65536)
#define MODS_XM_MODE (255 * 65536)
#define MODS_XM_BP 32768
#define MODS_AT_PITCH (12 * 65536)
#define MODS_AT_FREQ (164 * 65536)
#define MODS_AT_MODE (255 * 65536)
#define MODS_AT_AMP 32768

enum { LFO_SINE, LFO_SAW, LFO_RSAW, LFO_SQUARE, LFO_RANDOM };   /* P_LFO_SHAPE values */

typedef struct { uint32_t ph, inc, rnd; int32_t held; uint8_t keyed; } lfo_t;    /* one shared LFO */
void lfo_init(lfo_t *l, uint32_t seed);                                       /* phase 0, FREQUENCY 100, free */
void lfo_set(lfo_t *l, int freq_param, int synced, uint32_t clock_inc);      /* 0.022..500 Hz */
void lfo_key(lfo_t *l, int first_key_down);                                   /* reset on first key only */
int32_t lfo_tick(lfo_t *l, int shape, uint32_t frames);                       /* Q15; sine/random bipolar, saw/rev saw/square >= 0; random at max rate = noise */
/* total depth: initial amount + mod wheel + (aftertouch if AT->LFO AMT), Q15; negative AT amount inverts */
int32_t lfo_depth(int initial, int wheel_0_127, int32_t at_q15, int at_to_lfoamt);
/* signed aftertouch: pressure 0..127 x AT AMOUNT -127..127 / 127, Q15 (+-32768 at 127 x +-127) */
int32_t mods_at(int pressure_0_127, int at_amt);

typedef struct { int32_t vco1_pitch, vco1_shape, vco1_pw, flt_freq, flt_mode, bp_mix, vco2_pitch, vco2_pw, amp; } mods_t;
/* sum every enabled destination for one voice from LFO, X-Mod (fenv, vco2 sample) and aftertouch; rules 4, 6, 7.
 * lfo_q15: lfo_tick's value; depth_q15: lfo_depth's; fenv_q30: the voice's filter envelope (adsr_tick, 0..2^30);
 * vco2_q15: osc_tick's out[OSC_VCO2] (OSC_ONE = 1; pass 0 at control rate, see mods_xm_tick); at_q15: mods_at's. */
void mods_sum(mods_t *m, const int16_t *v /* program values */, int32_t lfo_q15, int32_t depth_q15,
              int32_t fenv_q30, int32_t vco2_q15, int32_t at_q15);

/* per-sample X-Mod VCO 2 term: gains per VCO 2 sample (0 where the destination is off), Q14 x output unit, so a
 * term is (vco2 x gain) >> 14 rounded; X-Mod's VCO 2 amount is in them */
typedef struct { int32_t pitch, shape, pw, freq, mode, bp; } mods_xm_t;
void mods_xm_gains(mods_xm_t *g, const int16_t *v);                   /* control rate (reads v[] only) */

static inline __attribute__((always_inline)) int32_t mods_xm_term(int32_t vco2_q15, int32_t gain)
{
    return (int32_t)(((int64_t)vco2_q15 * gain + 8192) >> 14);
}
/* out = ctl + the VCO 2 X-Mod term of every enabled destination (ctl from mods_sum with vco2_q15 = 0) */
static inline __attribute__((always_inline)) void mods_xm_tick(mods_t *out, const mods_t *ctl, const mods_xm_t *g,
                                                               int32_t vco2_q15)
{
    *out = *ctl;
    if (g->pitch) out->vco1_pitch += mods_xm_term(vco2_q15, g->pitch);
    if (g->shape) out->vco1_shape += mods_xm_term(vco2_q15, g->shape);
    if (g->pw) out->vco1_pw += mods_xm_term(vco2_q15, g->pw);
    if (g->freq) out->flt_freq += mods_xm_term(vco2_q15, g->freq);
    if (g->mode) out->flt_mode += mods_xm_term(vco2_q15, g->mode);
    if (g->bp) out->bp_mix += mods_xm_term(vco2_q15, g->bp);
}

/* Per-sample VCO 1 pitch for audio-rate X-Mod, without osc_set_pitch's exp2 calls: inc1 / inv1 are the increment and
 * its inverse that osc_set_pitch computed for note1_q16 (saved by the voice at control rate); sets o->inc1 and o->inv1
 * for note1_q16 + semis_q16 (the sum clamped to the oscillator's -60..135 range). Accuracy: 0.31 cent (a cubic 2^f;
 * measured over +-160 st); semis_q16 = 0 gives inc1 / inv1 back exactly. */
void mods_fm1(osc_t *o, uint32_t inc1, uint32_t inv1, int32_t note1_q16, int32_t semis_q16);

#endif
