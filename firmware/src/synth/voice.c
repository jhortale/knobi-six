/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX voice (see voice.h for what runs where and the levels). Unity-build safe: every file-scope name is
 * voice_ / VOICE_ prefixed. Pitch choices (the reference gives none):
 *   - VCO FREQUENCY 0..60 steps in semitones around 24 = the played key (so -2 .. +3 octaves).
 *   - VCO 2 KEYBOARD off: VCO 2 sits at key 60 (C4) + its FREQUENCY offset, ignoring the key and the glide; bend,
 *     detune and modulation still move it. LOW FREQ: VCO 2 drops VOICE_LOWFREQ = 7 octaves (FREQUENCY 24 at C4:
 *     2.04 Hz; the oscillator floor is 0.255 Hz).
 *   - Pitch bend moves both VCOs. Key track follows the gliding key pitch (no bend, no detune). */
#include "voice.h"
#include "../params/params.h"

#define VOICE_LOWFREQ (84 * 65536)

/* sin(i pi / 64) x 32767, i = 0..32: the constant-power pan law, R = sin, L = cos */
static const int16_t voice_pan_tab[33] = {
    0, 1608, 3212, 4808, 6393, 7962, 9512, 11039, 12539, 14010, 15446, 16846, 18204, 19519, 20787, 22005, 23170,
    24279, 25329, 26319, 27245, 28105, 28898, 29621, 30273, 30852, 31356, 31785, 32137, 32412, 32609, 32728, 32767,
};

/* sin(pi / 4 x j / 32768), j = 0..65536 (Q15 position across the half circle), linear between table points */
static int32_t voice_pan_sin(int32_t j)
{
    int32_t i = j >> 11, f = j & 2047;
    if (i >= 32) return 32767;
    return voice_pan_tab[i] + (((voice_pan_tab[i + 1] - voice_pan_tab[i]) * f) >> 11);
}

static int32_t voice_r16(int32_t q16) { return (q16 + 32768) >> 16; }

void voice_reset(voice_t *v, uint32_t rnd)
{
    v->svf.s1 = v->svf.s2 = 0;
    v->osc.sub = 0, v->osc.synced = 0, v->osc.sync_tb = 0;
    v->osc.ph1 = rnd, v->osc.ph2 = rnd * 2654435761u;
    v->fenv.level = v->lenv.level = 0;
    v->fenv.stage = v->lenv.stage = ADSR_IDLE;
    v->act = 0, v->amp = 0;
    v->pitch = v->target = VOICE_NO_PITCH, v->glide = 0;   /* nothing to glide from: the manager's last_pitch decides */
    v->busy = v->key = v->latched = v->pend_off = v->min_gate = 0;
}

void voice_init(voice_t *v, uint32_t seed)
{
    uint8_t *b = (uint8_t *)v;
    for (unsigned i = 0; i < sizeof *v; i++) b[i] = 0;
    osc_init(&v->osc, seed);
    voice_reset(v, seed * 747796405u + 2891336453u);
    v->pitch = v->target = VOICE_NO_PITCH;
    v->vel = 127;
    svf_coef(&v->coef, SVF_CUT_MAX, 0, 0, 0);
}

void voice_glide(voice_t *v)
{
    if (v->pitch == VOICE_NO_PITCH || !v->glide) v->pitch = v->target;
    else if (v->pitch < v->target) v->pitch = v->target - v->pitch <= v->glide ? v->target : v->pitch + v->glide;
    else if (v->pitch > v->target) v->pitch = v->pitch - v->target <= v->glide ? v->target : v->pitch - v->glide;
}

void voice_control(voice_t *v, const voice_ctx_t *c, const int16_t *prog)
{
    if (v->pitch == VOICE_NO_PITCH || !v->glide) v->pitch = v->target;   /* the glide itself steps in voice_glide */
    int32_t key = v->pitch;
    int32_t fenv = v->fenv.level;
    mods_sum(&v->ctl, prog, c->lfo, c->depth, fenv, 0, c->at);

    /* pitch */
    int32_t d1 = (int32_t)(((int64_t)v->dr1 * c->detune) >> 15), d2 = (int32_t)(((int64_t)v->dr2 * c->detune) >> 15);
    int32_t n1 = key + c->bend + d1 + (prog[P_VCO1_FREQ] - 24) * 65536 + v->ctl.vco1_pitch;
    int32_t n2 = (prog[P_VCO2_KEYBOARD] ? key : 60 * 65536) + c->bend + d2 + (prog[P_VCO2_FREQ] - 24) * 65536 +
                 prog[P_VCO2_FINE] * 65536 / 100 - (prog[P_VCO2_LOWFREQ] ? VOICE_LOWFREQ : 0) + v->ctl.vco2_pitch;
    osc_set_pitch(&v->osc, n1, n2);
    v->inc1 = v->osc.inc1, v->inv1 = v->osc.inv1, v->note1 = n1;
    v->osc.sync = (uint8_t)(prog[P_VCO1_SYNC] != 0);

    /* shapes and widths (X-Mod's VCO 2 term is added per sample on top of shape1 / pw1) */
    v->shape1 = c->shape1 + v->ctl.vco1_shape;
    v->pw1 = c->pw1 + v->ctl.vco1_pw;
    osc_set_shape(&v->osc, voice_r16(v->shape1), voice_r16(v->pw1), voice_r16(c->shape2),
                  voice_r16(c->pw2 + v->ctl.vco2_pw));

    /* filter: FREQ + key track (FULL: one step per semitone from C4, HALF: half) + envelope + modulation */
    int32_t kt = prog[P_FLT_TRACK] == 2 ? key - 60 * 65536 : prog[P_FLT_TRACK] == 1 ? (key - 60 * 65536) >> 1 : 0;
    v->cut = c->cut + kt + fenv_mod(fenv, prog[P_FLT_ENV_AMT], prog[P_FLT_VEL], v->vel) + v->ctl.flt_freq;
    v->mode = c->mode + v->ctl.flt_mode;
    v->bp = c->bp + v->ctl.bp_mix;
    v->g0[0] = v->coef.lp, v->g0[1] = v->coef.hp, v->g0[2] = v->coef.bp;   /* ramp from these */
    if (c->xm_flt) svf_coef_fast(&v->coef, v->cut, c->kres, voice_r16(v->mode), v->bp);
    else svf_coef(&v->coef, v->cut, voice_r16(c->reso), voice_r16(v->mode), v->bp);

    /* VCA: AMP modulation only while the voice is active (faded by act), output gains */
    v->amp = (int32_t)(((int64_t)v->ctl.amp * v->act) >> 15);
    int32_t p = 32768 + (int32_t)(((int64_t)v->pan * c->spread) >> 15);   /* 0..65536 */
    v->gl = (c->gain * voice_pan_sin(65536 - p)) >> 15;
    v->gr = (c->gain * voice_pan_sin(p)) >> 15;
}

void voice_render(voice_t *v, const voice_ctx_t *c, const int16_t *prog, int32_t *bus, int n)
{
    int32_t l0 = c->lvl[OSC_VCO1], l1 = c->lvl[OSC_VCO2], l2 = c->lvl[OSC_SUB], l3 = c->lvl[OSC_NOISE];
    const int32_t gl = v->gl, gr = v->gr, amp = v->amp;
    const int lamt = prog[P_LENV_AMT], lvel = prog[P_LENV_VEL], vel = v->vel;
    const int xm_pitch = c->xm_pitch, xm_shape = c->xm_shape, xm_flt = c->xm_flt;
    /* ramps across the control period: value at its frame t (1..16) = old + (new - old) x t / 16, in Q4 accumulators */
    const int32_t t0 = c->pos;
    int32_t ld[4], la[4], gd[3], ga[3];
    const int rl = c->lvl0[0] != l0 || c->lvl0[1] != l1 || c->lvl0[2] != l2 || c->lvl0[3] != l3;
    const int rg = !xm_flt && (v->g0[0] != v->coef.lp || v->g0[1] != v->coef.hp || v->g0[2] != v->coef.bp);
    svf_coef_t cf;
    const svf_coef_t *cp = &v->coef;
    if (rl)
        for (int k = 0; k < 4; k++) ld[k] = c->lvl[k] - c->lvl0[k], la[k] = c->lvl0[k] * 16 + ld[k] * t0;
    if (rg) {
        cf = v->coef, cp = &cf;
        gd[0] = cf.lp - v->g0[0], gd[1] = cf.hp - v->g0[1], gd[2] = cf.bp - v->g0[2];
        for (int k = 0; k < 3; k++) ga[k] = v->g0[k] * 16 + gd[k] * t0;
    }
    for (int i = 0; i < n; i++) {
        int32_t o[4];
        osc_tick(&v->osc, o);
        if (xm_pitch | xm_shape | xm_flt) {
            int32_t x = o[OSC_VCO2];
            if (xm_pitch) mods_fm1(&v->osc, v->inc1, v->inv1, v->note1, mods_xm_term(x, c->xm.pitch));
            if (xm_shape)
                osc_set_shape(&v->osc, voice_r16(v->shape1 + mods_xm_term(x, c->xm.shape)),
                              voice_r16(v->pw1 + mods_xm_term(x, c->xm.pw)), v->osc.shape2, v->osc.pw2);
            if (xm_flt)
                svf_coef_fast(&v->coef, v->cut + mods_xm_term(x, c->xm.freq), c->kres,
                              voice_r16(v->mode + mods_xm_term(x, c->xm.mode)), v->bp + mods_xm_term(x, c->xm.bp));
        }
        if (rl) {
            la[0] += ld[0], la[1] += ld[1], la[2] += ld[2], la[3] += ld[3];
            l0 = la[0] >> 4, l1 = la[1] >> 4, l2 = la[2] >> 4, l3 = la[3] >> 4;
        }
        if (rg) {
            ga[0] += gd[0], ga[1] += gd[1], ga[2] += gd[2];
            cf.lp = ga[0] >> 4, cf.hp = ga[1] >> 4, cf.bp = ga[2] >> 4;
        }
        int32_t mix = (o[OSC_VCO1] * l0 + o[OSC_VCO2] * l1 + o[OSC_SUB] * l2 + o[OSC_NOISE] * l3) >> 14;
        adsr_tick(&v->fenv, &c->fr);
        int32_t le = adsr_tick(&v->lenv, &c->lr);
        int32_t y = vca_apply(svf_tick(&v->svf, mix, cp), vca_control(le, lamt, lvel, vel, amp, 0));
        bus[2 * i] += (y * gl) >> 15;
        bus[2 * i + 1] += (y * gr) >> 15;
    }
}
