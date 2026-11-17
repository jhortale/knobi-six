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
/* field by field: a struct assignment is a call to libc.c's byte-loop memcpy */
#define VOICE_COEF_COPY(d, s) ((d).g = (s).g, (d).a1 = (s).a1, (d).a2 = (s).a2, (d).k = (s).k, (d).lp = (s).lp, \
                               (d).hp = (s).hp, (d).bp = (s).bp)
/* C4, the key-track pivot, Q16, as a variable the compiler cannot fold: key - 60 x 65536 would carry 0xFFC40000,
 * which lies in the chip's ROM window, and tools/build.py fails such a build (osc.c osc_note_lo_neg). Never written. */
int32_t voice_c4_q16 = 60 * 65536;

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
    v->le0 = v->led = v->voff = 0;
    v->pitch = v->target = VOICE_NO_PITCH, v->glide = 0;   /* nothing to glide from: the manager's last_pitch decides */
    v->busy = v->key = v->latched = v->pend_off = v->min_gate = v->shed = 0;
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
    int32_t kt = prog[P_FLT_TRACK] == 2 ? key - voice_c4_q16 : prog[P_FLT_TRACK] == 1 ? (key - voice_c4_q16) >> 1 : 0;
    v->cut = c->cut + kt + fenv_mod(fenv, prog[P_FLT_ENV_AMT], prog[P_FLT_VEL], v->vel) + v->ctl.flt_freq;
    v->mode = c->mode + v->ctl.flt_mode;
    v->bp = c->bp + v->ctl.bp_mix;
    v->g0[0] = v->coef.lp, v->g0[1] = v->coef.hp, v->g0[2] = v->coef.bp;   /* ramp from these */
    if (c->xm_flt) svf_coef_fast(&v->coef, v->cut, c->kres, voice_r16(v->mode), v->bp);
    else svf_coef(&v->coef, v->cut, voice_r16(c->reso), voice_r16(v->mode), v->bp), VOICE_COEF_COPY(v->xc, v->coef);
    if (!c->xm_pitch) v->xi = v->osc.inc1, v->xv = v->osc.inv1;   /* X-Mod's held targets follow while a path is off */
    if (!c->xm_shape) v->xa1 = v->osc.a1, v->xw1 = v->osc.w1;

    /* VCA: AMP modulation only while the voice is active (faded by act), output gains */
    v->amp = (int32_t)(((int64_t)v->ctl.amp * v->act) >> 15);
    int32_t p = 32768 + (int32_t)(((int64_t)v->pan * c->spread) >> 15);   /* 0..65536 */
    v->gl = (c->gain * voice_pan_sin(65536 - p)) >> 15;
    v->gr = (c->gain * voice_pan_sin(p)) >> 15;
}

void voice_env(voice_t *v, const voice_ctx_t *c, const int16_t *prog)
{
    adsr_tick16(&v->fenv, &c->fr);
    int32_t l0 = v->lenv.level, l1 = adsr_tick16(&v->lenv, v->shed ? &c->shed : &c->lr);
    v->le0 = l0;
    v->led = (int32_t)(((int64_t)l1 - l0) >> 4);   /* per frame of the 16 */
    v->vk = vca_env_k(prog[P_LENV_AMT], prog[P_LENV_VEL], v->vel);
    v->voff = v->amp < -2 * VCA_CEIL ? -2 * VCA_CEIL : v->amp > 2 * VCA_CEIL ? 2 * VCA_CEIL : v->amp;
}

void voice_xm_sync(voice_t *v)
{
    v->xi = v->osc.inc1, v->xv = v->osc.inv1, v->xa1 = v->osc.a1, v->xw1 = v->osc.w1;
    VOICE_COEF_COPY(v->xc, v->coef);
}

/* Two passes over the period's frames, each voice's state in locals (the CPU gate, docs/MEASUREMENTS.md):
 *   1. oscillators, X-Mod into VCO 1 (pitch, shape / PW), the mixer -> mix[], and VCO 2 -> x2[] for the filter path;
 *   2. the filter (X-Mod coefficients or the gain ramp), the VCA ramp, the pan gains into the bus.
 * X-Mod every 2 samples: on even frames of the period (pos + i) the targets are computed from this frame's VCO 2 and
 * this frame gets the mean of them and the previous targets; the odd frame between gets the targets themselves. A
 * linear interpolation of a modulation sampled at 22 kHz, one frame late (23 us).
 * Each pass is one always-inline frame body specialised by constant arguments (which X-Mod paths, even or odd frame),
 * so the loops carry no per-frame tests of the patch's switches (a taken branch costs ~7.5 cycles on this core); the
 * level and gain ramps run unconditionally (a zero step when nothing moves). */
#define VOICE_HOT static inline __attribute__((always_inline))
enum { VOICE_PLAIN, VOICE_EVEN, VOICE_ODD, VOICE_EVEN_GK, VOICE_ODD_GK };   /* GK: X-Mod into F FREQ only */

typedef struct {
    osc_t *o;
    uint32_t pi, pv, pw;                  /* X-Mod's held VCO 1 targets */
    int32_t pa;
    int32_t la[4], ld[4];                 /* the mixer levels' ramp, Q4 */
} voice_p1_t;

VOICE_HOT void voice_p1(voice_p1_t *p, const voice_t *v, const voice_ctx_t *c, int32_t *mixb, int32_t *x2b, int i,
                        const int frame, const int xp, const int xs)
{
    int32_t s[4];
    osc_t *o = p->o;
    osc_tick_hot(o, s);
    if (frame == VOICE_EVEN) {
        osc_t tmp;
        int32_t x = s[OSC_VCO2];
        if (xp) {
            mods_fm1_hot(&tmp, v->inc1, v->inv1, v->note1, mods_xm_term(x, c->xm.pitch));
            o->inc1 = (p->pi >> 1) + (tmp.inc1 >> 1), o->inv1 = (p->pv >> 1) + (tmp.inv1 >> 1);
            p->pi = tmp.inc1, p->pv = tmp.inv1;
        }
        if (xs) {
            osc_set_shape1_hot(&tmp, voice_r16(v->shape1 + mods_xm_term(x, c->xm.shape)),
                               voice_r16(v->pw1 + mods_xm_term(x, c->xm.pw)));
            o->a1 = (p->pa >> 1) + (tmp.a1 >> 1), o->w1 = (p->pw >> 1) + (tmp.w1 >> 1);
            p->pa = tmp.a1, p->pw = tmp.w1;
        }
    } else if (frame == VOICE_ODD) {
        if (xp) o->inc1 = p->pi, o->inv1 = p->pv;
        if (xs) o->a1 = p->pa, o->w1 = p->pw;
    }
    p->la[0] += p->ld[0], p->la[1] += p->ld[1], p->la[2] += p->ld[2], p->la[3] += p->ld[3];
    mixb[i] = (s[OSC_VCO1] * (p->la[0] >> 4) + s[OSC_VCO2] * (p->la[1] >> 4) + s[OSC_SUB] * (p->la[2] >> 4) +
               s[OSC_NOISE] * (p->la[3] >> 4)) >> 14;
    x2b[i] = s[OSC_VCO2];
}

/* frames 0..n-1 starting at period frame t0: X-Mod pairs (even, odd), an odd first frame and an even last one alone */
VOICE_HOT void voice_pass1(voice_p1_t *p, const voice_t *v, const voice_ctx_t *c, int32_t *mixb, int32_t *x2b, int n,
                           int t0, const int xp, const int xs)
{
    int i = 0;
    if (!(xp | xs)) {
        for (; i < n; i++) voice_p1(p, v, c, mixb, x2b, i, VOICE_PLAIN, 0, 0);
        return;
    }
    if (t0 & 1 && n) voice_p1(p, v, c, mixb, x2b, i++, VOICE_ODD, xp, xs);
    for (; i + 1 < n; i += 2) {
        voice_p1(p, v, c, mixb, x2b, i, VOICE_EVEN, xp, xs);
        voice_p1(p, v, c, mixb, x2b, i + 1, VOICE_ODD, xp, xs);
    }
    if (i < n) voice_p1(p, v, c, mixb, x2b, i, VOICE_EVEN, xp, xs);
}

typedef struct {
    float s1, s2;
    svf_coef_t cf, pc;                    /* the coefficients in use; X-Mod's held target */
    float gd[3];                          /* the output gains' ramp step (0 when they do not move) */
    int32_t lv, led, voff, gl, gr;
    uint32_t vk;
    float bpf;                            /* the BP mix as a fraction (GK frames) */
} voice_p2_t;

VOICE_HOT void voice_p2(voice_p2_t *p, const voice_t *v, const voice_ctx_t *c, const int32_t *mixb, const int32_t *x2b,
                        int32_t *bus, int i, const int frame)
{
    if (frame == VOICE_EVEN) {
        svf_coef_t nc;
        int32_t x = x2b[i];
        svf_coef_fast_hot(&nc, v->cut + mods_xm_term(x, c->xm.freq), c->kres,
                          voice_r16(v->mode + mods_xm_term(x, c->xm.mode)), v->bp + mods_xm_term(x, c->xm.bp));
        p->cf.g = 0.5f * (p->pc.g + nc.g), p->cf.a1 = 0.5f * (p->pc.a1 + nc.a1), p->cf.a2 = 0.5f * (p->pc.a2 + nc.a2);
        p->cf.k = 0.5f * (p->pc.k + nc.k), p->cf.lp = 0.5f * (p->pc.lp + nc.lp), p->cf.hp = 0.5f * (p->pc.hp + nc.hp);
        p->cf.bp = 0.5f * (p->pc.bp + nc.bp);
        VOICE_COEF_COPY(p->pc, nc);
    } else if (frame == VOICE_ODD) {
        VOICE_COEF_COPY(p->cf, p->pc);
    } else if (frame == VOICE_EVEN_GK) {
        svf_coef_t nc;
        svf_coef_gk_hot(&nc, v->cut + mods_xm_term(x2b[i], c->xm.freq), c->kres, p->bpf);
        p->cf.g = 0.5f * (p->pc.g + nc.g), p->cf.a1 = 0.5f * (p->pc.a1 + nc.a1), p->cf.a2 = 0.5f * (p->pc.a2 + nc.a2);
        p->cf.k = 0.5f * (p->pc.k + nc.k), p->cf.bp = 0.5f * (p->pc.bp + nc.bp);
        p->pc.g = nc.g, p->pc.a1 = nc.a1, p->pc.a2 = nc.a2, p->pc.k = nc.k, p->pc.bp = nc.bp;
    } else if (frame == VOICE_ODD_GK) {
        p->cf.g = p->pc.g, p->cf.a1 = p->pc.a1, p->cf.a2 = p->pc.a2, p->cf.k = p->pc.k, p->cf.bp = p->pc.bp;
    } else {
        p->cf.lp += p->gd[0], p->cf.hp += p->gd[1], p->cf.bp += p->gd[2];
    }
    svf_t f = {p->s1, p->s2};
    int32_t y = svf_tick_hot(&f, mixb[i], &p->cf);
    p->s1 = f.s1, p->s2 = f.s2;
    /* the VCA: |y| <= 65528 (the filter's soft cap) times g <= 32768 fits int32 */
    p->lv += p->led;
    int32_t le = svf_imax(svf_imin(p->lv, ADSR_ONE), 0);       /* (filter.c: branch-free min / max) */
    int32_t g = svf_imax(svf_imin((int32_t)(((uint32_t)le >> 15) * p->vk >> 16) + p->voff, VCA_CEIL), 0);
    y = (y * g) >> 15;
    bus[2 * i] += (y * p->gl) >> 15;
    bus[2 * i + 1] += (y * p->gr) >> 15;
}

void voice_render(voice_t *v, const voice_ctx_t *c, const int16_t *prog, int32_t *bus, int n)
{
    (void)prog;
    int32_t mixb[VOICE_CTL], x2b[VOICE_CTL];
    const int t0 = c->pos;

    /* 1: oscillators, VCO 1 X-Mod, mixer. The levels ramp across the control period: value at its frame t (1..16) =
     * old + (new - old) x t / 16, in Q4 accumulators */
    voice_p1_t p;
    p.o = &v->osc, p.pi = v->xi, p.pv = v->xv, p.pw = v->xw1, p.pa = v->xa1;
    for (int k = 0; k < 4; k++) p.ld[k] = c->lvl[k] - c->lvl0[k], p.la[k] = c->lvl0[k] * 16 + p.ld[k] * t0;
    if (c->xm_pitch && c->xm_shape) voice_pass1(&p, v, c, mixb, x2b, n, t0, 1, 1);
    else if (c->xm_pitch) voice_pass1(&p, v, c, mixb, x2b, n, t0, 1, 0);
    else if (c->xm_shape) voice_pass1(&p, v, c, mixb, x2b, n, t0, 0, 1);
    else voice_pass1(&p, v, c, mixb, x2b, n, t0, 0, 0);
    v->xi = p.pi, v->xv = p.pv, v->xa1 = p.pa, v->xw1 = p.pw;

    /* 2: filter, VCA, pan */
    voice_p2_t q;
    q.s1 = v->svf.s1, q.s2 = v->svf.s2;
    VOICE_COEF_COPY(q.cf, v->coef);
    VOICE_COEF_COPY(q.pc, v->xc);
    q.lv = v->le0 + v->led * t0, q.led = v->led, q.voff = v->voff, q.gl = v->gl, q.gr = v->gr, q.vk = v->vk;
    int i = 0;
    if (c->xm_flt && !c->xm.mode && !c->xm.bp) {   /* F FREQ only: the output gains stay the tick's */
        q.bpf = (float)(v->bp < 0 ? 0 : v->bp > 32768 ? 32768 : v->bp) * (1.0f / 32768);
        q.pc.lp = q.cf.lp, q.pc.hp = q.cf.hp;
        if (t0 & 1 && n) voice_p2(&q, v, c, mixb, x2b, bus, i++, VOICE_ODD_GK);
        for (; i + 1 < n; i += 2) {
            voice_p2(&q, v, c, mixb, x2b, bus, i, VOICE_EVEN_GK);
            voice_p2(&q, v, c, mixb, x2b, bus, i + 1, VOICE_ODD_GK);
        }
        if (i < n) voice_p2(&q, v, c, mixb, x2b, bus, i, VOICE_EVEN_GK);
        VOICE_COEF_COPY(v->xc, q.pc), VOICE_COEF_COPY(v->coef, q.cf);
    } else if (c->xm_flt) {
        if (t0 & 1 && n) voice_p2(&q, v, c, mixb, x2b, bus, i++, VOICE_ODD);
        for (; i + 1 < n; i += 2) {
            voice_p2(&q, v, c, mixb, x2b, bus, i, VOICE_EVEN);
            voice_p2(&q, v, c, mixb, x2b, bus, i + 1, VOICE_ODD);
        }
        if (i < n) voice_p2(&q, v, c, mixb, x2b, bus, i, VOICE_EVEN);
        VOICE_COEF_COPY(v->xc, q.pc), VOICE_COEF_COPY(v->coef, q.cf);   /* the last applied (tests read it) */
    } else {
        /* the filter's output gains ramp from the previous tick's (MODE, BP moves: no 16-frame steps) */
        q.gd[0] = (q.cf.lp - v->g0[0]) * (1.0f / 16), q.gd[1] = (q.cf.hp - v->g0[1]) * (1.0f / 16);
        q.gd[2] = (q.cf.bp - v->g0[2]) * (1.0f / 16);
        q.cf.lp = v->g0[0] + q.gd[0] * (float)t0, q.cf.hp = v->g0[1] + q.gd[1] * (float)t0;
        q.cf.bp = v->g0[2] + q.gd[2] * (float)t0;
        for (; i < n; i++) voice_p2(&q, v, c, mixb, x2b, bus, i, VOICE_PLAIN);
    }
    v->svf.s1 = q.s1, v->svf.s2 = q.s2;
}
