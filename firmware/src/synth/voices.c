/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX voice manager (see voices.h for the behaviour, the choices and the output level). Unity-build safe: every
 * file-scope name is synth_ / SYNTH_ prefixed. Divisions: 32-bit only, at events and control ticks. */
#include "voices.h"

#ifndef SYNTH_VOICE_GAIN
#define SYNTH_VOICE_GAIN 13000            /* Q15 per voice at PROGRAM VOLUME 127 (test_voices output_level) */
#endif
#define SYNTH_SM_SHIFT 4                  /* one-pole per tick: time constant 16 ticks = 5.8 ms */
#define SYNTH_ACT_SHIFT 2                 /* the activity follower: 4 ticks = 1.5 ms */

static uint32_t synth_rand(synth_t *s)
{
    uint32_t x = s->rnd;
    x ^= x << 13, x ^= x >> 17, x ^= x << 5;
    return s->rnd = x;
}

static int32_t synth_clampi(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* ---- program values -> smoothing targets, derived state ---- */

static void synth_targets(synth_t *s)
{
    const int16_t *v = s->prog.v;
    s->tgt[SM_CUT] = svf_knob_q16(v[P_FLT_FREQ]);              /* FREQ 0..164 -> 16 Hz .. 18.8 kHz, Q16 st */
    s->tgt[SM_RESO] = v[P_FLT_RESO] * 65536;
    s->tgt[SM_MODE] = v[P_FLT_MODE] * 255 * 65536 / 127;     /* MODE 0..127 -> filter mode 0..255 */
    s->tgt[SM_BP] = v[P_FLT_BP] ? 32768 : 0;
    s->tgt[SM_SHAPE1] = v[P_VCO1_SHAPE] * 65536;
    s->tgt[SM_PW1] = v[P_VCO1_PW] * 65536;
    s->tgt[SM_SHAPE2] = v[P_VCO2_SHAPE] * 65536;
    s->tgt[SM_PW2] = v[P_VCO2_PW] * 65536;
    s->tgt[SM_L1] = v[P_MIX_VCO1] * 65536;
    s->tgt[SM_L2] = v[P_MIX_VCO2] * 65536;
    s->tgt[SM_LSUB] = v[P_MIX_SUB] * 65536;
    s->tgt[SM_LNOISE] = v[P_MIX_NOISE] * 65536;
    s->tgt[SM_VOL] = v[P_PRG_VOL] * s->midi_vol * 65536 / 127;   /* PROGRAM VOLUME x MIDI volume (CC 7) */
    s->tgt[SM_SPREAD] = v[P_PAN_SPREAD] * 65536;
    s->tgt[SM_DIST] = v[P_DISTORTION] * 65536;
}

static void synth_xmod(synth_t *s)
{
    voice_ctx_t *c = &s->ctx;
    mods_xm_gains(&c->xm, s->prog.v);
    c->xm_pitch = (uint8_t)(c->xm.pitch != 0);
    c->xm_shape = (uint8_t)((c->xm.shape | c->xm.pw) != 0);
    c->xm_flt = (uint8_t)((c->xm.freq | c->xm.mode | c->xm.bp) != 0);
}

static void synth_lfo_set(synth_t *s)
{
    lfo_set(&s->lfo, s->prog.v[P_LFO_FREQ], s->prog.v[P_LFO_SYNC],
            s->prog.v[P_LFO_SYNC] ? lfo_sync_inc(s->prog.v[P_LFO_FREQ], s->clk_qinc) : 0);
}
void synth_clock(synth_t *s, uint32_t quarter_inc)
{
    if (quarter_inc == s->clk_qinc) return;
    s->clk_qinc = quarter_inc;
    if (s->prog.v[P_LFO_SYNC]) synth_lfo_set(s);
}

/* the distortion (voices.h): G = 16 d^2, d = q / (127 << 16); KZ = G / 32768, KY = 32768 sqrt(1 + G) / G. The square
 * root by Newton's method (no libm): 1 + G is 1..17, six steps from 1 + G / 4 reach float precision */
void synth_dist_coef(int32_t q, float *kz, float *ky)
{
    if (q <= 0) { *kz = *ky = 0.0f; return; }
    if (q > 127 * 65536) q = 127 * 65536;
    float d = (float)q * (1.0f / (127.0f * 65536.0f)), g = 16.0f * d * d, x = 1.0f + g, r = 1.0f + 0.25f * g;
    for (int i = 0; i < 6; i++) r = 0.5f * (r + x / r);
    *kz = g * (1.0f / 32768.0f);
    *ky = 32768.0f * r / g;
}
/* the summed bus (n frames, L R) through the distortion, in place */
static void synth_dist(synth_t *s, int32_t *bus, int n)
{
    float kz = s->dist_kz, ky = s->dist_ky;
    for (int i = 0; i < 2 * n; i++) bus[i] = synth_dist_sample(bus[i], kz, ky);
}

/* the context every voice reads at this tick, from the smoothed values */
static void synth_derive(synth_t *s)
{
    voice_ctx_t *c = &s->ctx;
    const int16_t *v = s->prog.v;
    c->cut = s->sm[SM_CUT];
    if (c->reso != s->sm[SM_RESO] || !c->kres) c->reso = s->sm[SM_RESO], c->kres = svf_reso_k((c->reso + 32768) >> 16);
    c->mode = s->sm[SM_MODE];
    c->bp = s->sm[SM_BP];
    c->shape1 = s->sm[SM_SHAPE1], c->pw1 = s->sm[SM_PW1], c->shape2 = s->sm[SM_SHAPE2], c->pw2 = s->sm[SM_PW2];
    for (int i = 0; i < 4; i++)   /* level 0..127 (Q16) -> Q14: x 16384 / 127 / 65536 = x 8454661 / 2^32 */
        c->lvl[i] = (int32_t)(((int64_t)s->sm[SM_L1 + i] * 8454661) >> 32);
    c->gain = SYNTH_VOICE_GAIN * (s->sm[SM_VOL] >> 8) / (127 * 256);
    c->spread = (s->sm[SM_SPREAD] >> 8) * 32768 / (127 * 256);
    c->detune = v[P_DETUNE] * v[P_DETUNE] * VOICE_DETUNE_MAX / (127 * 127);
    if (s->sm[SM_DIST] != s->dist_q) s->dist_q = s->sm[SM_DIST], synth_dist_coef(s->dist_q, &s->dist_kz, &s->dist_ky);
    /* bend: +-range at the ends (8191 up, 8192 down), Q16 semitones */
    int32_t d = s->bend14 - 8192, r = v[P_BEND_RANGE];
    c->bend = (d >= 0 ? d * r * 8 + d * r * 8 / 8191 : d * r * 8) + s->tune;   /* (+ the master tune: the globals) */
}

static void synth_rates(synth_t *s)
{
    const int16_t *v = s->prog.v;
    adsr_rates(&s->ctx.fr, v[P_FENV_A], v[P_FENV_D], v[P_FENV_S], v[P_FENV_R]);
    adsr_rates(&s->ctx.lr, v[P_LENV_A], v[P_LENV_D], v[P_LENV_S], v[P_LENV_R]);
    s->rates_dirty = 0;
}

/* ---- the key stack (keys down, and keys up latched by HOLD), in press order ---- */

static void synth_stack_remove(synth_t *s, int note)
{
    unsigned j = 0;
    for (unsigned i = 0; i < s->nstack; i++) {
        if (s->stack[i] == note) continue;
        s->stack[j] = s->stack[i], s->svel[j] = s->svel[i], s->sdown[j] = s->sdown[i], j++;
    }
    s->nstack = (uint8_t)j;
}

static void synth_stack_push(synth_t *s, int note, int vel)
{
    synth_stack_remove(s, note);
    if (s->nstack == SYNTH_STACK) {   /* full: forget the oldest */
        for (unsigned i = 1; i < SYNTH_STACK; i++)
            s->stack[i - 1] = s->stack[i], s->svel[i - 1] = s->svel[i], s->sdown[i - 1] = s->sdown[i];
        s->nstack--;
    }
    s->stack[s->nstack] = (uint8_t)note, s->svel[s->nstack] = (uint8_t)vel, s->sdown[s->nstack] = 1;
    s->nstack++;
}

static int synth_keys_down(const synth_t *s)
{
    int n = 0;
    for (unsigned i = 0; i < s->nstack; i++) n += s->sdown[i];
    return n;
}

/* the stack index of the key with priority (KEY MODE LOW / HIGH / LAST), -1 if none */
static int synth_priority(const synth_t *s)
{
    if (!s->nstack) return -1;
    int pm = s->prog.v[P_KEY_MODE] >> 1, best = (int)s->nstack - 1;
    if (pm == 2) return best;
    for (int i = 0; i < (int)s->nstack; i++)
        if (pm == 0 ? s->stack[i] < s->stack[best] : s->stack[i] > s->stack[best]) best = i;
    return best;
}

/* ---- voices ---- */

/* portamento time T(RATE) in control ticks, Q16: 1 ms x 2^((r - 1) x 12 / 126) x 2.75625 ticks per ms */
static uint32_t synth_porta_ticks(int r)
{
    int32_t e = (r - 1) * 12 * 65536 / 126;
    int32_t f = e & 0xffff;
    uint32_t m = 65536u + (((uint32_t)f * (uint32_t)(43025 + ((22511 * f) >> 16))) >> 16);   /* 2^f, quadratic, 0.3 % */
    return ((180634u * (m >> 4)) >> 12) << (e >> 16);
}

static void synth_glide(synth_t *s, voice_t *v, int32_t target, int legato)
{
    const int16_t *p = s->prog.v;
    int r = p[P_PORTA_RATE], mode = p[P_PORTA_MODE];
    v->target = target;
    v->glide = 0;
    if (!p[P_PORTA_ON] || r <= 0 || v->pitch == VOICE_NO_PITCH || ((mode & 1) && !legato)) {
        v->pitch = target;
        return;
    }
    uint32_t t = synth_porta_ticks(r);
    if (mode < 2) {
        v->glide = (int32_t)((786432u << 8) / (t >> 8));   /* FIXED RATE: 12 semitones per T */
    } else {
        int32_t dist = target > v->pitch ? target - v->pitch : v->pitch - target;
        v->glide = (int32_t)(((uint32_t)synth_clampi(dist, 0, 255 * 65536) << 4) / (t >> 12));   /* FIXED TIME */
    }
    if (v->glide < 1) v->glide = 1;
}

static void synth_gate(voice_t *v, int on)
{
    adsr_gate(&v->fenv, on);
    adsr_gate(&v->lenv, on);
}

/* start (trigger) or move (legato) a voice to a note */
static void synth_voice_on(synth_t *s, voice_t *v, int note, int vel, int legato, int trigger)
{
    int fresh = !v->busy;
    v->note = (uint8_t)note, v->vel = (uint8_t)synth_clampi(vel, 1, 127);
    v->key = 1, v->latched = 0, v->busy = 1, v->shed = 0;
    synth_glide(s, v, note * 65536, legato);
    if (trigger || fresh) {
        v->pend_off = 0;
        v->min_gate = VOICE_MIN_GATE;
        v->t_on = ++s->clock;
        synth_gate(v, 1);
    }
    voice_control(v, &s->ctx, s->prog.v);   /* the right pitch and filter from the first sample */
    v->g0[0] = v->coef.lp, v->g0[1] = v->coef.hp, v->g0[2] = v->coef.bp;   /* its filter gains too: no ramp from the
                                                                              voice's previous note */
    voice_xm_sync(v);                       /* nor X-Mod's interpolation */
}

static void synth_voice_off(synth_t *s, voice_t *v)
{
    if (!v->busy || (!v->key && !v->latched)) return;
    v->key = 0, v->latched = 0;
    v->t_off = ++s->clock;
    if (v->min_gate) v->pend_off = 1;
    else synth_gate(v, 0);
}

static void synth_all_off(synth_t *s)
{
    for (int i = 0; i < NVOICE; i++) synth_voice_off(s, &s->voice[i]);
    s->nstack = 0;
    s->mono = -1;
}

/* ---- poly ---- */

/* voices sounding that the CPU guard has not shed */
static int synth_live(const synth_t *s)
{
    int n = 0;
    for (int i = 0; i < NVOICE; i++) n += s->voice[i].busy && !s->voice[i].shed;
    return n;
}

static int synth_alloc(const synth_t *s, int note)
{
    int best = -1;
    for (int i = 0; i < NVOICE; i++)
        if (s->voice[i].busy && s->voice[i].note == note) return i;   /* the same key: retrigger its voice */
    if (synth_live(s) < s->vlimit) {   /* free: released longest ago (unless the CPU guard's limit is reached) */
        for (int i = 0; i < NVOICE; i++)
            if (!s->voice[i].busy && (best < 0 || s->voice[i].t_off < s->voice[best].t_off)) best = i;
        if (best >= 0) return best;
    }
    for (int i = 0; i < NVOICE; i++) {   /* released, still sounding: the oldest release */
        const voice_t *v = &s->voice[i];
        if (v->busy && !v->key && !v->latched && (best < 0 || v->t_off < s->voice[best].t_off)) best = i;
    }
    if (best >= 0) return best;
    for (int i = 0; i < NVOICE; i++)     /* all held: the oldest note */
        if (s->voice[i].busy && (best < 0 || s->voice[i].t_on < s->voice[best].t_on)) best = i;
    return best;
}

static void synth_poly_on(synth_t *s, int note, int vel, int legato)
{
    voice_t *v = &s->voice[synth_alloc(s, note)];
    int i = (int)(v - s->voice);
    if (!(v->busy && v->note == note)) {   /* a new allocation: new slop (a same-key retrigger keeps its own) */
        v->dr1 = (int32_t)synth_rand(s) >> 16;
        v->dr2 = (int32_t)synth_rand(s) >> 16;
    }
    /* L / R alternating, the offset growing by pair: (i / 2 + 1) / (NVOICE / 2), rounded up for an odd count */
    v->pan = ((i & 1) ? 1 : -1) * (i / 2 + 1) * 32768 / ((NVOICE + 1) / 2);
    v->pitch = s->last_pitch;   /* glide from the latest note; none (NO_PITCH) after init / program / panic */
    synth_voice_on(s, v, note, vel, legato, 1);
    s->last_pitch = note * 65536;
}

static void synth_poly_off(synth_t *s, int note)
{
    for (int i = 0; i < NVOICE; i++) {
        voice_t *v = &s->voice[i];
        if (!v->busy || v->note != note || !v->key) continue;
        if (s->hold) v->key = 0, v->latched = 1;
        else synth_voice_off(s, v);
    }
}

/* ---- unison ---- */

/* the stack size and, for chord memory, the chord notes ascending (returns the count of voices) */
static int synth_uni_voices(const synth_t *s, int *chord, int *nchord)
{
    int u = s->prog.v[P_UNISON_VOICES], n = 0;
    *nchord = 0;
    if (u >= 6) {
        for (int i = 0; i < 6; i++)
            if (s->prog.chord[i]) {
                int c = s->prog.chord[i], j = n++;
                while (j > 0 && chord[j - 1] > c) chord[j] = chord[j - 1], j--;
                chord[j] = c;
            }
        *nchord = n < s->vlimit ? n : s->vlimit;
        return n ? *nchord : s->vlimit;
    }
    return u + 1 < s->vlimit ? u + 1 : s->vlimit;   /* (the CPU guard's limit, NVOICE normally) */
}

static void synth_mono_go(synth_t *s, int key, int vel, int trigger, int legato)
{
    int chord[6], nchord, n = synth_uni_voices(s, chord, &nchord);
    int ref = nchord ? ((s->prog.v[P_KEY_MODE] >> 1) == 1 ? chord[nchord - 1] : chord[0]) : 0;
    for (int k = 0; k < n; k++) {
        voice_t *v = &s->voice[k];
        int odd = n & 1;
        /* stack slot k: pan alternates L / R (odd stack: slot 0 centred); detune a fixed spread, outer pair +-1 */
        int sign = odd ? ((k & 1) ? -1 : 1) : ((k & 1) ? 1 : -1);
        int m = odd ? (k + 1) / 2 : 2 * (k / 2) + 1, mm = odd ? (n - 1) / 2 : n - 1;
        /* pan: the offset grows by pair (odd: pairs 1.., even: pairs 1.. from slot 0); left flat, right sharp */
        int pair = odd ? (k + 1) / 2 : k / 2 + 1, npair = odd ? (n - 1) / 2 : n / 2;
        v->pan = odd && k == 0 ? 0 : sign * pair * 32768 / npair;
        v->dr1 = v->dr2 = n > 1 ? sign * m * 32768 / mm : 0;
        int note = synth_clampi(key + (nchord ? chord[k] - ref : 0), 0, 127);
        /* a slot not already in the sounding stack starts from the latest note, as every other slot did */
        if (!(v->busy && v->key))   /* none (NO_PITCH) after init / program / panic, even on a releasing voice */
            v->pitch = s->last_pitch == VOICE_NO_PITCH ? VOICE_NO_PITCH
                                                       : s->last_pitch + (nchord ? (chord[k] - ref) * 65536 : 0);
        synth_voice_on(s, v, note, vel, legato, trigger);
    }
    s->mono = (int16_t)key;
    s->last_pitch = key * 65536;
}

static void synth_mono_off(synth_t *s)
{
    for (int k = 0; k < NVOICE; k++) synth_voice_off(s, &s->voice[k]);
    s->mono = -1;
}

/* after a release (or HOLD off): go legato to the key with priority, or release */
static void synth_mono_follow(synth_t *s)
{
    int p = synth_priority(s);
    if (p < 0) { if (s->mono >= 0) synth_mono_off(s); }
    else if (s->stack[p] != s->mono) synth_mono_go(s, s->stack[p], s->svel[p], 0, 1);
}

/* ---- the interface ---- */

void synth_program(synth_t *s, const program_t *p)
{
    int sounding = synth_active(s);   /* voices still sounding (they release now): glide to the new values, no click */
    synth_all_off(s);
    s->last_pitch = VOICE_NO_PITCH;   /* a new program does not glide from the old one's notes */
    s->prog = *p;
    for (unsigned i = 0; i < P_COUNT_PHASE1; i++) s->prog.v[i] = param_clamp((uint16_t)i, p->v[i]);
    s->prog.v[P_DISTORTION] = param_clamp(P_DISTORTION, p->v[P_DISTORTION]);
    synth_targets(s);
    if (!sounding) for (int i = 0; i < SM_COUNT; i++) s->sm[i] = s->tgt[i];
    synth_rates(s);
    synth_xmod(s);
    synth_lfo_set(s);
    s->ctx.kres = 0;
    synth_derive(s);
    if (!sounding) for (int i = 0; i < 4; i++) s->ctx.lvl0[i] = s->ctx.lvl[i];   /* snapped: no ramp */
}

void synth_init(synth_t *s)
{
    uint8_t *b = (uint8_t *)s;
    for (unsigned i = 0; i < sizeof *s; i++) b[i] = 0;
    s->rnd = 0x2545f491u;
    s->bend14 = 8192;
    s->midi_vol = 127;
    s->mono = -1;
    s->last_pitch = VOICE_NO_PITCH;
    s->vlimit = NVOICE;
    for (int i = 0; i < NVOICE; i++) voice_init(&s->voice[i], synth_rand(s));
    lfo_init(&s->lfo, synth_rand(s));
    program_t p;
    program_init(&p);
    synth_program(s, &p);
}

void synth_param(synth_t *s, uint16_t id, int16_t value)
{
    if (id >= P_COUNT_PHASE1 && id != P_DISTORTION) return;
    int16_t old = s->prog.v[id], v = param_clamp(id, value);
    s->prog.v[id] = v;
    synth_targets(s);
    if (id >= P_FENV_A && id <= P_LENV_R) s->rates_dirty = 1;
    else if (id >= P_XM_FENV && id <= P_XM_D_BP) synth_xmod(s);
    else if (id == P_LFO_FREQ || id == P_LFO_SYNC) synth_lfo_set(s);
    else if ((id == P_UNISON_ON || id == P_UNISON_VOICES) && v != old) synth_all_off(s);
}

void synth_note(synth_t *s, int note, int vel)
{
    if (note < 0 || note > 127) return;
    if (vel > 0) {
        int down = synth_keys_down(s), was = s->nstack;
        lfo_key(&s->lfo, down == 0);
        synth_stack_push(s, note, vel);
        if (!s->prog.v[P_UNISON_ON]) {
            synth_poly_on(s, note, vel, down > 0);
            return;
        }
        int p = synth_priority(s), retrig = s->prog.v[P_KEY_MODE] & 1;
        if (!was || s->mono < 0) synth_mono_go(s, s->stack[p], s->svel[p], 1, 0);
        else if (s->stack[p] == note) synth_mono_go(s, note, vel, retrig, 1);   /* it took priority (or re-pressed) */
        else if (retrig) synth_mono_go(s, s->mono, s->voice[0].vel, 1, 1);    /* -R: every keystroke retriggers */
        return;
    }
    if (s->hold) {
        for (unsigned i = 0; i < s->nstack; i++) if (s->stack[i] == note) s->sdown[i] = 0;
        if (!s->prog.v[P_UNISON_ON]) synth_poly_off(s, note);
        return;
    }
    synth_stack_remove(s, note);
    if (!s->prog.v[P_UNISON_ON]) synth_poly_off(s, note);
    else synth_mono_follow(s);
}

/* a note let go that HOLD never latches (the sequencer's notes, seq.h: HOLD latches the live keys only): out of the
 * key stack and released now, whatever HOLD says; in unison the line follows the keys left (a latched live key
 * included) */
void synth_note_release(synth_t *s, int note)
{
    if (note < 0 || note > 127) return;
    synth_stack_remove(s, note);
    if (s->prog.v[P_UNISON_ON]) { synth_mono_follow(s); return; }
    for (int i = 0; i < NVOICE; i++) {
        voice_t *v = &s->voice[i];
        if (v->busy && v->note == note && v->key) synth_voice_off(s, v);
    }
}

void synth_hold(synth_t *s, int on)
{
    if (on) { s->hold = 1; return; }
    s->hold = 0;
    unsigned j = 0;   /* forget the latched keys */
    for (unsigned i = 0; i < s->nstack; i++)
        if (s->sdown[i]) s->stack[j] = s->stack[i], s->svel[j] = s->svel[i], s->sdown[j] = 1, j++;
    s->nstack = (uint8_t)j;
    if (s->prog.v[P_UNISON_ON]) { synth_mono_follow(s); return; }
    for (int i = 0; i < NVOICE; i++) if (s->voice[i].latched) synth_voice_off(s, &s->voice[i]);
}

void synth_bend(synth_t *s, int bend14) { s->bend14 = synth_clampi(bend14, 0, 16383); }
void synth_wheel(synth_t *s, int v) { s->wheel = synth_clampi(v, 0, 127); }
void synth_pressure(synth_t *s, int v) { s->pressure = synth_clampi(v, 0, 127); }

void synth_all_notes_off(synth_t *s) { synth_all_off(s); }   /* every note released (envelopes release; HOLD stays) */
void synth_volume(synth_t *s, int v)
{
    s->midi_vol = synth_clampi(v, 0, 127);
    s->tgt[SM_VOL] = s->prog.v[P_PRG_VOL] * s->midi_vol * 65536 / 127;   /* glides like PROGRAM VOLUME (no zipper) */
}

void synth_tune(synth_t *s, int cents) { s->tune = synth_clampi(cents, -1250, 1250) * 65536 / 100; }

void synth_panic(synth_t *s)
{
    for (int i = 0; i < NVOICE; i++) voice_reset(&s->voice[i], synth_rand(s));
    s->nstack = 0, s->mono = -1, s->hold = 0;
    s->last_pitch = VOICE_NO_PITCH;
    s->dcx[0] = s->dcx[1] = s->dcy[0] = s->dcy[1] = 0.0f;   /* the bus blocker too: silent now, no tail */
}

int synth_active(const synth_t *s)
{
    int n = 0;
    for (int i = 0; i < NVOICE; i++) n += s->voice[i].busy;
    return n;
}

/* one control tick: smoothing, shared modulation, then every sounding voice */
static void synth_tick(synth_t *s)
{
    const int16_t *v = s->prog.v;
    if (s->rates_dirty) synth_rates(s);
    for (int i = 0; i < SM_COUNT; i++) {
        int32_t d = s->tgt[i] - s->sm[i];
        s->sm[i] = d > -16 && d < 16 ? s->tgt[i] : s->sm[i] + (d >> SYNTH_SM_SHIFT);
    }
    for (int i = 0; i < 4; i++) s->ctx.lvl0[i] = s->ctx.lvl[i];   /* the voices ramp from these to the new levels */
    synth_derive(s);
    voice_ctx_t *c = &s->ctx;
    c->at = mods_at(s->pressure, v[P_AT_AMT]);
    c->lfo = lfo_tick(&s->lfo, v[P_LFO_SHAPE], VOICE_CTL);
    c->depth = lfo_depth(v[P_LFO_AMT], s->wheel, c->at, v[P_AT_D_LFOAMT]);
    for (int i = 0; i < NVOICE; i++) {
        voice_t *vo = &s->voice[i];
        if (!vo->busy) continue;
        if (vo->min_gate && !--vo->min_gate && vo->pend_off) vo->pend_off = 0, synth_gate(vo, 0);
        int live = vo->lenv.stage != ADSR_IDLE || vo->key || vo->latched || vo->pend_off;
        int32_t d = (live ? 32768 : 0) - vo->act;
        vo->act = d > -64 && d < 64 ? vo->act + d : vo->act + (d >> SYNTH_ACT_SHIFT);
        if (!live && (!vo->act || vo->shed)) { voice_reset(vo, synth_rand(s)); continue; }   /* silent: free, states
                                                                                               cleared (a shed voice at once) */
        voice_glide(vo);
        voice_control(vo, c, v);
        voice_env(vo, c, v);
    }
}

/* The bus, per channel (voices.h): a DC blocker, y = x - x1 + R y1 (a one-pole high-pass at 5 Hz: R = 1 - 2 pi 5 / fs),
 * then the soft limiter, linear to the knee K, then y = K + W u / (u + W) (u = |x| - K, W = CEIL - K): slope 1 at the
 * knee, falling smoothly, and it never reaches CEIL whatever x. Float on the FPU (one division, only above the knee);
 * the output is truncated toward zero (no DC from a floor). The blocker comes first, so the limiter's ceiling holds for
 * what goes out (a high-pass after it could overshoot). */
#define SYNTH_DC_R 0.99928762f
#define SYNTH_LIM_K ((float)SYNTH_LIMIT_KNEE)
#define SYNTH_LIM_W ((float)(SYNTH_LIMIT_CEIL - SYNTH_LIMIT_KNEE))
static inline __attribute__((always_inline)) int32_t synth_limit(float x)
{
    float a = __builtin_fabsf(x);
    if (a > SYNTH_LIM_K) {
        float u = a - SYNTH_LIM_K;
        a = SYNTH_LIM_K + SYNTH_LIM_W * u / (u + SYNTH_LIM_W);
        x = x < 0.0f ? -a : a;
    }
    return (int32_t)x;
}
/* n frames of the voice bus (NULL: silence) through the DC blocker and the limiter, added into lr */
static void synth_bus(synth_t *s, const int32_t *bus, int32_t *lr, int n)
{
    float xl = s->dcx[0], yl = s->dcy[0], xr = s->dcx[1], yr = s->dcy[1];
    for (int i = 0; i < n; i++) {
        float l = bus ? (float)bus[2 * i] : 0.0f, r = bus ? (float)bus[2 * i + 1] : 0.0f;
        yl = l - xl + SYNTH_DC_R * yl, xl = l;
        yr = r - xr + SYNTH_DC_R * yr, xr = r;
        lr[2 * i] += synth_limit(yl);
        lr[2 * i + 1] += synth_limit(yr);
    }
    s->dcx[0] = xl, s->dcy[0] = yl, s->dcx[1] = xr, s->dcy[1] = yr;
}
/* the blocker's tail once the voices are silent: run on zeros until it is below half an LSB, then exactly 0 (no
 * denormals, nothing to do in silence) */
static int synth_bus_live(const synth_t *s)
{
    return s->dcx[0] != 0.0f || s->dcx[1] != 0.0f || __builtin_fabsf(s->dcy[0]) >= 0.5f ||
           __builtin_fabsf(s->dcy[1]) >= 0.5f;
}

/* the render seam (voices.h) */
void synth_render_set(synth_t *s, uint32_t voice_mask, int32_t *bus, uint32_t frames_in_period)
{
    for (int i = 0; i < NVOICE; i++)
        if ((voice_mask >> i & 1u) && s->voice[i].busy)
            voice_render(&s->voice[i], &s->ctx, s->prog.v, bus, (int)frames_in_period);
}
uint32_t synth_balance(const synth_t *s, uint32_t *mask_a, uint32_t *mask_b)
{
    uint32_t busy = 0, n = 0;
    for (int i = 0; i < NVOICE; i++)
        if (s->voice[i].busy) busy |= 1u << i, n++;
    uint32_t a = 0, k = n >> 1;   /* set A: the first n / 2 busy voices by index (the smaller half), set B the rest */
    for (int i = 0; k; i++)
        if (busy >> i & 1u) a |= 1u << i, k--;
    *mask_a = a, *mask_b = busy & ~a;
    return n;
}

void synth_render(synth_t *s, int32_t *lr, uint32_t frames)
{
    if (!synth_active(s)) {
        /* silence: nothing to add bar the DC blocker's tail. The smoothed values snap to their targets (no voice to
         * click) and the LFO runs on by the whole block; the next block starts a fresh control period */
        for (int i = 0; i < SM_COUNT; i++) s->sm[i] = s->tgt[i];
        lfo_tick(&s->lfo, s->prog.v[P_LFO_SHAPE], frames);
        s->ctl_left = 0;
        if (synth_bus_live(s)) synth_bus(s, 0, lr, (int)frames);
        else s->dcy[0] = s->dcy[1] = 0.0f;
        return;
    }
    while (frames) {
        if (!s->ctl_left) synth_tick(s), s->ctl_left = VOICE_CTL;
        int n = (int)(frames < s->ctl_left ? frames : s->ctl_left);
        int32_t bus[2 * VOICE_CTL];
        for (int i = 0; i < 2 * n; i++) bus[i] = 0;
        s->ctx.pos = (uint8_t)(VOICE_CTL - s->ctl_left);   /* (shared state: written here, before the sets) */
        SYNTH_SPLIT(s, bus, (uint32_t)n);
        if (s->dist_kz != 0.0f) synth_dist(s, bus, n);   /* (amount 0: no stage, the Phase 1 bus bit for bit) */
        synth_bus(s, bus, lr, n);
        lr += 2 * n;
        frames -= (uint32_t)n;
        s->ctl_left = (uint8_t)(s->ctl_left - n);
    }
    for (int c = 0; c < 2; c++)   /* (a held voice silent for seconds: the blocker's state never goes denormal) */
        if (__builtin_fabsf(s->dcy[c]) < 1e-20f) s->dcy[c] = 0.0f;
}

/* the CPU guard (voices.h) */
void synth_guard(synth_t *s, uint32_t used, uint32_t budget)
{
    if ((uint64_t)used * 100u > (uint64_t)budget * SYNTH_GUARD_HI) {
        s->calm = 0;
        int busy = 0;
        for (int i = 0; i < NVOICE; i++) {
            if (s->voice[i].busy && s->voice[i].shed) return;   /* still fading (in this measurement): wait */
            busy += s->voice[i].busy;
        }
        if (!busy) return;   /* the load is not the synth's (other interrupts): nothing to shed, the limit stays */
        /* the cost per voice sounding is used / busy: keep the most voices k with k x used / busy under the threshold
         * (no division: k used 100 < HI budget busy), at least one fewer than now, at least one */
        int k = busy - 1;
        while (k > 1 && (uint64_t)k * used * 100u >= (uint64_t)budget * SYNTH_GUARD_HI * (uint32_t)busy) k--;
        if (k < 1) k = 1;
        if (k < s->vlimit) s->vlimit = (uint8_t)k;
        else if (s->vlimit > 1) s->vlimit--;
        s->sheds++;
        for (int n = synth_live(s); n > s->vlimit; n--) {
            int best = -1;
            for (int i = 0; i < NVOICE; i++) {   /* the oldest released first */
                const voice_t *v = &s->voice[i];
                if (v->busy && !v->shed && !v->key && !v->latched && (best < 0 || v->t_off < s->voice[best].t_off))
                    best = i;
            }
            if (best < 0)
                for (int i = 0; i < NVOICE; i++) {   /* then the oldest held */
                    const voice_t *v = &s->voice[i];
                    if (v->busy && !v->shed && (best < 0 || v->t_on < s->voice[best].t_on)) best = i;
                }
            voice_t *v = &s->voice[best];
            /* the VCA gain at the end of the period just rendered, held and faded by voice_env */
            int32_t lv = v->lenv.level < 0 ? 0 : v->lenv.level, g = (int32_t)(((uint32_t)lv >> 15) * v->vk >> 16) + v->voff;
            v->shedg = g < 0 ? 0 : g > VCA_CEIL ? VCA_CEIL : g;
            v->shed = 1, v->key = 0, v->latched = 0, v->pend_off = 0, v->min_gate = 0;
            v->lenv.level = ADSR_ONE, v->lenv.stage = ADSR_RELEASE;
        }
    } else if (s->vlimit < NVOICE) {
        /* back up by one only if the load projected at one voice over the limit, at the measured cost per voice
         * sounding (used x (vlimit + 1) / busy: with busy = vlimit that is used x (busy + 1) / busy), stays under
         * SYNTH_GUARD_FIT % (no division: used (vlimit + 1) 100 < FIT budget busy); with nothing sounding (no cost to
         * measure) when the load is under SYNTH_GUARD_LO %. FIT is under HI with room for the estimate's error, so the
         * raised limit is not shed again: no oscillation */
        int busy = 0;
        for (int i = 0; i < NVOICE; i++) busy += s->voice[i].busy;
        int fits = busy ? (uint64_t)used * (uint32_t)(s->vlimit + 1) * 100u < (uint64_t)budget * SYNTH_GUARD_FIT * (uint32_t)busy
                        : (uint64_t)used * 100u < (uint64_t)budget * SYNTH_GUARD_LO;
        if (!fits) s->calm = 0;
        else if (++s->calm >= SYNTH_GUARD_HOLD) s->vlimit++, s->calm = 0;
    } else {
        s->calm = 0;
    }
}
