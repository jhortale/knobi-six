/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX voice: one OB-6 voice (osc.h -> mixer -> filter.h -> VCA, two env.h ADSRs, the mod.h offsets), run in
 * two parts. The voice manager (voices.h) owns the voices, the shared state and the note logic. Integers only.
 *
 * Where each part runs (the control period is VOICE_CTL = 16 frames, 0.36 ms):
 *   control rate, voice_control: glide, pitch (osc_set_pitch, saving inc1 / inv1 / note1 for mods_fm1), shapes and PW
 *     (osc_set_shape), mods_sum (LFO, aftertouch and X-Mod filter-env terms; VCO 2 term 0), the cutoff (smoothed FREQ +
 *     key track + fenv_mod + mods) and svf_coef, the VCA's AMP modulation, the output gains.
 *   per sample, voice_render: osc_tick; the X-Mod VCO 2 terms (mods_xm_term) into mods_fm1 (VCO 1 pitch), osc_set_shape
 *     (VCO 1 shape / PW) and svf_coef_fast (F FREQ, F MODE, Norm<->BP: the audio-rate filter path, used only when one
 *     of those destinations is on with a non-zero VCO 2 amount; svf_coef at control rate otherwise); the mixer;
 *     adsr_tick x 2; svf_tick; vca_control + vca_apply; the pan gains into the stereo bus. The mixer levels and the
 *     filter's output gains (MODE, BP: svf_coef's lp / hp / bp) ramp linearly per sample from the previous tick's
 *     values to this tick's (no 16-frame steps when they move; skipped when they do not, and the gains on the
 *     audio-rate filter path, which recomputes them every sample).
 *
 * Levels: the mixer gains are Q14 (16384 = level 127 = unity: one oscillator at full level is OSC_ONE = -6 dBFS into
 * the filter, the four at full overdrive its input drive, as rule 1 wants). The filter and the VCA keep 1 bit over Q15
 * (|y| <= 65536); the output gains gl / gr are Q15 <= 32767, so y x g fits int32 and (y x g) >> 15 is <= 65535 per
 * voice; six voices sum to < 2^19 in the int32 bus (voices.h soft-limits it to Q15).
 *
 * Detune: dr1 / dr2 are fractions in -1..1 (Q15) set at note-on (random per oscillator in poly, the unison stack's
 * fixed spread in unison); the offset is the fraction x the DETUNE depth (VOICE_DETUNE_MAX x (DETUNE / 127)^2). */
#ifndef KNOBI_VOICE_H
#define KNOBI_VOICE_H
#include <stdint.h>
#include "osc.h"
#include "filter.h"
#include "env.h"
#include "mod.h"

#define VOICE_CTL 16                      /* control period, frames */
#define VOICE_DETUNE_MAX 32768            /* DETUNE 127: +-0.5 semitone, Q16 */
#define VOICE_NO_PITCH INT32_MIN          /* a voice that has never sounded: nothing to glide from */

/* what every voice reads at a control tick: the smoothed program values, the shared modulation (voices.c fills it) */
typedef struct {
    int32_t cut;                          /* FILTER FREQ as a cutoff, Q16 semitones (svf_knob_q16, smoothed) */
    int32_t reso;                         /* RESONANCE, Q16 (smoothed); kres = svf_reso_k of it */
    int32_t mode;                         /* filter mode 0..255, Q16 (MODE param m -> m * 255 / 127, smoothed) */
    int32_t bp;                           /* BP switch 0..32768 (smoothed: the switch crossfades) */
    int32_t shape1, pw1, shape2, pw2;     /* Q16 param units (smoothed) */
    int32_t lvl[4];                       /* mixer gains Q14, OSC_VCO1 .. OSC_NOISE (smoothed) */
    int32_t lvl0[4];                      /* the previous tick's: voice_render ramps from them to lvl per sample */
    int32_t kres;                         /* svf_reso_k(reso) */
    int32_t gain;                         /* output gain Q15 (voice gain x program volume, smoothed) */
    int32_t spread;                       /* PAN SPREAD 0..32768 (smoothed) */
    int32_t detune;                       /* DETUNE depth, Q16 semitones */
    int32_t bend;                         /* pitch bend, Q16 semitones */
    int32_t lfo, depth, at;               /* lfo_tick, lfo_depth, mods_at of this tick */
    adsr_rates_t fr, lr;                  /* filter / loudness envelope rates */
    mods_xm_t xm;                         /* X-Mod VCO 2 gains (mods_xm_gains) */
    uint8_t xm_pitch, xm_shape, xm_flt;   /* per-sample paths on: VCO1 pitch, VCO1 shape / PW, filter */
    uint8_t pos;                          /* frames of the current control period already rendered (0..15) */
} voice_ctx_t;

typedef struct {
    osc_t osc;
    svf_t svf;
    svf_coef_t coef;
    float g0[3];                          /* the previous tick's filter output gains lp, hp, bp (ramped per sample) */
    adsr_t fenv, lenv;
    mods_t ctl;                           /* this tick's mods_sum (VCO 2 term 0) */
    uint32_t inc1, inv1;                  /* VCO 1's increment and inverse at note1 (mods_fm1's base) */
    int32_t note1;                        /* VCO 1 note at this tick, Q16 */
    int32_t cut, mode, bp;                /* this tick's filter inputs (Q16 semitones, Q16 mode, 0..32768): fast path's */
    int32_t shape1, pw1;                  /* this tick's VCO 1 shape / PW, Q16 (X-Mod per-sample base) */
    int32_t amp;                          /* AMP modulation (LFO + AT) x activity, Q15 */
    int32_t pitch, target, glide;         /* key pitch (Q16 notes, gliding), its target, step per tick (0: jump) */
    int32_t dr1, dr2;                     /* detune fractions, Q15 */
    int32_t gl, gr;                       /* output gains Q15 */
    int32_t act;                          /* activity 0..32768: gates the AMP modulation, fades it at the end */
    uint32_t t_on, t_off;                 /* event clock at gate on / release (allocation order) */
    /* the loudness envelope and VCA at control rate (voice_env): level at the period's frame t = le0 + led x t (Q30,
     * ramped per sample, clamped to 0..ADSR_ONE); VCA = level x vk (vca_env_k) + voff, clamped to 0..VCA_CEIL */
    int32_t le0, led, voff;
    int32_t shedg;                        /* a shed voice: its VCA gain when shed (Q15), faded to 0 over 6 ticks */
    uint32_t vk;
    /* X-Mod every 2 samples (voice_render): the last computed targets, VCO 1 increment / inverse / pulse amount /
     * width and the filter coefficients; the sample between two computations takes their mean */
    uint32_t xi, xv, xw1;
    int32_t xa1;
    svf_coef_t xc;
    int32_t pan;                          /* position -32768 (left) .. 32768 (right) at PAN SPREAD 127, Q15 */
    uint8_t note, vel;                    /* sounding note (chord offset included), velocity 1..127 */
    uint8_t busy;                         /* 1 from note-on until silent and reset */
    uint8_t key;                          /* the key is down */
    uint8_t latched;                      /* key up while HOLD is on: keeps sounding */
    uint8_t pend_off;                     /* key up before the minimum gate elapsed: release when it does */
    uint8_t min_gate;                     /* control ticks left of the 2 ms minimum gate */
    uint8_t shed;                         /* the CPU guard took it: fading out until free (voice_env) */
} voice_t;

void voice_init(voice_t *v, uint32_t seed);
/* back to silence: filter states 0, sub / sync state cleared, phases from rnd (so a unison stack starts unaligned) */
void voice_reset(voice_t *v, uint32_t rnd);
void voice_glide(voice_t *v);             /* one control tick of portamento (before voice_control) */
void voice_control(voice_t *v, const voice_ctx_t *c, const int16_t *prog);
/* the envelopes' next control period (after voice_control at a tick): 16 samples of both ADSRs, the VCA ramp */
void voice_env(voice_t *v, const voice_ctx_t *c, const int16_t *prog);
/* X-Mod's held targets = the voice's current (unmodulated) values: at note-on, and while a path is off */
void voice_xm_sync(voice_t *v);
/* n <= VOICE_CTL frames, added into bus (L R pairs, int32, Q15 per voice) */
void voice_render(voice_t *v, const voice_ctx_t *c, const int16_t *prog, int32_t *bus, int n);

#endif
