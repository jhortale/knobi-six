/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX filter (OB-6 schema rule 1): a 2-pole, 12 dB/oct state-variable filter per voice. MODE morphs
 * continuously LP -> notch -> HP; the BP switch and the X-Mod Norm<->BP destination crossfade that output with the
 * band-pass; RESONANCE never self-oscillates; the input and the band-pass node saturate softly. Integers only.
 *
 * Levels: input and output are Q15 like the oscillators (osc.h: OSC_ONE = 16384 is a waveform's +-1). Unity gain in
 * the pass band. The input is linear up to +-32768 (two oscillators at full level), then a soft shoulder rises to
 * +-65536 at 3 x 32768 and stays there: high mixer levels overdrive softly instead of wrapping. The output is
 * bounded by +-65536 whatever the input, RESONANCE or state (so a voice's VCA input has 1 bit over Q15 full scale).
 *
 * Cutoff: cutoff_q16 is the FILTER FREQ parameter (0..164) plus modulation, Q16 (65536 = one FILTER FREQ step),
 * clamped to 0..164. One step is one semitone: fc = 440 Hz * 2^((cutoff - 99) / 12), so 0 = 1.44 Hz, 99 = 440 Hz,
 * 164 = 18.8 kHz (below Nyquist; 13.67 octaves, the reference's guess). Key track FULL is therefore + (note - 60)
 * steps and HALF + (note - 60) / 2: the caller adds them (Q16) with the envelope and the other modulations.
 *
 * reso: RESONANCE 0..255, Q 0.5 .. 25 exponential; below ~120 Hz the Q is capped further (a damping floor) so the
 * ring of an impulse always dies within a second. mode: 0 = LP, 128 = notch (LP + HP), 255 = HP, linear crossfades
 * either side (the MODE parameter 0..127 maps as m * 255 / 127). bp_mix: 0 = the MODE output, 32768 = band-pass
 * (BP switch on), linear between (X-Mod Norm<->BP). The band-pass peaks at 0 dB for Q <= 1 and at Q (like the LP's
 * resonant peak) above. */
#ifndef KNOBI_FILTER_H
#define KNOBI_FILTER_H
#include <stdint.h>

#define SVF_RATE 44100                    /* sample rate the cutoff table is computed for */
#define SVF_CUT_MAX (164 * 65536)         /* top of cutoff_q16 */

typedef struct { int32_t s1, s2; } svf_t;   /* integrator states, Q28 (2^28 = Q15 32768) */
/* g: Q28 tan(pi fc / fs); a1, a2: Q30 1 / (1 + g (g + k)), g a1; k: Q28 1 / Q; lp, hp, bp: Q15 output gains */
typedef struct { int32_t g, a1, a2, k, lp, hp, bp; } svf_coef_t;

/* cutoff_q16: filter FREQ param + modulations, in param units Q16; mode 0..255 LP->notch->HP; bp 0..32768 Norm->BP */
void svf_coef(svf_coef_t *c, int32_t cutoff_q16, int reso, int mode, int32_t bp_mix);
int32_t svf_tick(svf_t *f, int32_t in_q15, const svf_coef_t *c);
#ifdef DINGHY_HOST
uint32_t svf_cutoff_hz(int32_t cutoff_q16);   /* the cutoff the coefficients realise, rounded to whole Hz */
#endif

#endif
