/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX filter (OB-6 schema rule 1): a 2-pole, 12 dB/oct state-variable filter per voice. MODE morphs
 * continuously LP -> notch -> HP; the BP switch and the X-Mod Norm<->BP destination crossfade that output with the
 * band-pass; RESONANCE never self-oscillates; the input and the band-pass node saturate softly. The audio path is
 * single-precision float on the FPU (filter.c); the interface stays integer (Q15 in and out, Q16 cutoff).
 *
 * Levels: input and output are Q15 like the oscillators (osc.h: OSC_ONE = 16384 is a waveform's +-1). Unity gain in
 * the pass band. The input drive is an odd polynomial that saturates gradually (no linear zone, no knee): THD -64 dB at
 * +-16384 (one oscillator at full level), -49 dB at +-32768, rising <= 12 dB per 0.25 x full scale to -12 dB at 4 x;
 * it flattens at +-62226 from +-79594 up: high mixer levels overdrive softly instead of wrapping. The output is
 * bounded by +-65536 whatever the input, RESONANCE or state (so a voice's VCA input has 1 bit over Q15 full scale).
 *
 * Cutoff: cutoff_q16 is in semitones, Q16 (65536 = one semitone), clamped to 0..164: fc = 440 Hz * 2^((cutoff - 99)
 * / 12), so 0 = 1.44 Hz, 99 = 440 Hz, 164 = 18.8 kHz (below Nyquist). The FILTER FREQ knob 0..164 spans 16 Hz ..
 * 18.8 kHz of it exponentially (svf_knob_q16: 0.7462 semitone a step, 122.4 semitones in all; the OB-6's 165 steps
 * over the audible range): the old one-semitone-a-step map put FREQ 0 at 1.44 Hz, a closed filter for the many
 * imported programs that sit there. Modulation may go below the knob's 16 Hz, down to the 1.44 Hz floor (a negative
 * envelope from FREQ 0 closes the filter further). Key track FULL is + (note - 60) semitones (one octave an octave)
 * and HALF + (note - 60) / 2: the caller adds them (Q16) with the envelope and the other modulations.
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
#define SVF_CUT_MAX (164 * 65536)         /* top of cutoff_q16: 18.8 kHz */
/* the FILTER FREQ knob: 0 = 16 Hz (41.624 semitones, Q16), 164 = SVF_CUT_MAX; SVF_KNOB_SPAN = 122.376 semitones, Q16 */
#define SVF_KNOB_LO 2727850
#define SVF_KNOB_SPAN (SVF_CUT_MAX - SVF_KNOB_LO)
static inline int32_t svf_knob_q16(int freq)   /* FILTER FREQ 0..164 (clamped) -> cutoff_q16 */
{
    freq = freq < 0 ? 0 : freq > 164 ? 164 : freq;
    return SVF_KNOB_LO + freq * SVF_KNOB_SPAN / 164;
}

typedef struct { float s1, s2; } svf_t;   /* integrator states, float (1.0 = Q15 32768; filter.c: FPU) */
/* g: tan(pi fc / fs); a1, a2: 1 / (1 + g (g + k)), g a1; k: 1 / Q; lp, hp, bp: output gains (1.0 = unity) */
typedef struct { float g, a1, a2, k, lp, hp, bp; } svf_coef_t;

/* cutoff_q16: svf_knob_q16(FILTER FREQ) + modulations, semitones Q16; mode 0..255 LP->notch->HP; bp 0..32768 Norm->BP */
void svf_coef(svf_coef_t *c, int32_t cutoff_q16, int reso, int mode, int32_t bp_mix);
int32_t svf_tick(svf_t *f, int32_t in_q15, const svf_coef_t *c);
void svf_flush(svf_t *f);                 /* |state| < 1e-20 -> 0: no denormals (control rate) */
/* The per-sample coefficient path, for audio-rate X-Mod into F FREQ / F MODE / Norm<->BP only (the voice uses
 * svf_coef at control rate otherwise): kres = svf_reso_k(RESONANCE), computed at control rate; mode 0..255 and bp_mix
 * as svf_coef's. No exp2: a table g (as svf_coef), the damping floor from a table, one FPU division. Against svf_coef:
 * g exact, k within 0.05 % (2 % at the floor's cap, below 2.4 Hz), a1 / a2 within 1e-3 (test_filter
 * coef_fast_matches). */
int32_t svf_reso_k(int reso);             /* k = 1 / Q of RESONANCE 0..255 before the damping floor, Q28 */
void svf_coef_fast(svf_coef_t *c, int32_t cutoff_q16, int32_t kres, int mode, int32_t bp_mix);
#ifdef DINGHY_HOST
uint32_t svf_cutoff_hz(int32_t cutoff_q16);   /* the cutoff the coefficients realise, rounded to whole Hz */
#endif

#endif
