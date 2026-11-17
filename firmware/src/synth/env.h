/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX envelopes and VCA (OB-6 schema rules 2 and 3): two ADSRs per voice (filter, loudness), the filter
 * envelope's bipolar cutoff offset and the additive, clamped VCA. Integers only.
 *
 * ADSR: level Q30 (ADSR_ONE = 2^30 = full). Every stage is exponential (RC-like, as the reference's ADSR figures
 * suggest): attack charges towards 4/3 and stops at full, decay falls towards SUSTAIN, release falls towards -1/1024
 * and stops at zero. Times (the reference does not give any; ANALOG's ranges, exponential over the knob):
 *   ATTACK  0..127: 1 ms * 5000^(i / 127), 1 ms .. 5 s   (0 -> full)
 *   DECAY   0..127: 5 ms * 2000^(i / 127), 5 ms .. 10 s  (full -> within 1/1024 (-60 dB) of SUSTAIN; with SUSTAIN 0)
 *   RELEASE 0..127: 5 ms * 2000^(i / 127), 5 ms .. 10 s  (full -> zero; from a lower level it is shorter)
 *   SUSTAIN 0..127: level i / 127.
 * adsr_gate(on) starts the attack from the current level (a retrigger does not jump to zero); adsr_gate(off)
 * releases from wherever the envelope is. Velocity never touches the envelope (rule 2): it scales amounts only. No
 * minimum gate here: a zero-length note is the voice layer's to stretch (ANALOG's 2 ms MIN_GATE).
 *
 * fenv_mod: the filter envelope's offset in the filter's cutoff units (filter.h: Q16, 65536 = one FILTER FREQ step =
 * one semitone). FILTER ENV AMOUNT -127..127 is bipolar and linear: +-127 at full envelope is +-164 steps, the whole
 * cutoff range (FENV_RANGE), so from FREQ 0 a full envelope opens the filter completely and from FREQ 164 a full
 * negative one closes it. With the VELOCITY switch on, the amount is scaled by velocity / 127 (sign kept).
 *
 * vca_control: Q15 gain, 0 .. VCA_CEIL (32768 = unity). The loudness envelope times LOUDNESS ENV AMOUNT 0..127
 * (unipolar: negative amounts count as 0; velocity / 127 scales the amount when the switch is on), PLUS the LFO's
 * and aftertouch's AMP contributions (signed Q15, 32768 = a full VCA, already scaled by their amounts by the
 * caller), the sum clamped to 0 .. VCA_CEIL. A sum, not a product: LFO square with ENV AMOUNT 0 still gates sound,
 * and aftertouch adds nothing once the envelope is at full amount (rule 3).
 *
 * vca_apply: a voice's output = filter output (|x| <= 65536, filter.h) x control >> 15, a 32 x 32 -> 64 product, so
 * the result keeps the filter's 1 bit of headroom over Q15 full scale (|y| <= 65536). */
#ifndef KNOBI_ENV_H
#define KNOBI_ENV_H
#include <stdint.h>

#define ADSR_RATE 44100                   /* sample rate the rates are computed for */
#define ADSR_ONE (INT32_C(1) << 30)       /* full level, Q30 */
#define FENV_RANGE 164                    /* cutoff steps at full envelope and |FILTER ENV AMOUNT| 127 */
#define VCA_CEIL 32768                    /* VCA control ceiling, Q15 (unity gain) */

enum { ADSR_IDLE, ADSR_ATTACK, ADSR_DECAY, ADSR_RELEASE };   /* adsr_t.stage; DECAY is also the sustain stage */

typedef struct { int32_t level; uint8_t stage; } adsr_t;           /* level Q30, 0..2^30 */
/* a_inc, d_coef, r_coef: per-sample stage rates m = 1 - c (c = the RC step's decay factor), Q32;
 * s_level: SUSTAIN level, Q30 */
typedef struct { int32_t a_inc, d_coef, s_level, r_coef; } adsr_rates_t;
void adsr_rates(adsr_rates_t *r, int a, int d, int s, int rel);   /* param units -> per-sample rates (fm_exp2_fixed style, no tables) */
void adsr_gate(adsr_t *e, int on);
int32_t adsr_tick(adsr_t *e, const adsr_rates_t *r);              /* level Q30 */
/* VCA control = env*amt + lfo_amp + at_amp, clamped 0..ceiling (Q15); rule 3 */
int32_t vca_control(int32_t env_q30, int lenv_amt, int vel_on, int vel, int32_t lfo_q15, int32_t at_q15);
/* filter env contribution in cutoff units Q16; amount bipolar; velocity scales it when vel_on (rule 2) */
int32_t fenv_mod(int32_t env_q30, int fenv_amt, int vel_on, int vel);
/* filter output (|in| <= 65536) x VCA control (Q15, 0..VCA_CEIL); |result| <= 65536 */
int32_t vca_apply(int32_t in, int32_t ctl_q15);

#endif
