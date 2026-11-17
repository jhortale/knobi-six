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
    s->tgt[SM_CUT] = v[P_FLT_FREQ] * 65536;
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
    s->tgt[SM_VOL] = v[P_PRG_VOL] * 65536;
    s->tgt[SM_SPREAD] = v[P_PAN_SPREAD] * 65536;
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
    lfo_set(&s->lfo, s->prog.v[P_LFO_FREQ], s->prog.v[P_LFO_SYNC], 0);
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
    /* bend: +-range at the ends (8191 up, 8192 down), Q16 semitones */
    int32_t d = s->bend14 - 8192, r = v[P_BEND_RANGE];
    c->bend = d >= 0 ? d * r * 8 + d * r * 8 / 8191 : d * r * 8;
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
    v->key = 1, v->latched = 0, v->busy = 1;
    synth_glide(s, v, note * 65536, legato);
    if (trigger || fresh) {
        v->pend_off = 0;
        v->min_gate = VOICE_MIN_GATE;
        v->t_on = ++s->clock;
        synth_gate(v, 1);
    }
    voice_control(v, &s->ctx, s->prog.v);   /* the right pitch and filter from the first sample */
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

static int synth_alloc(const synth_t *s, int note)
{
    int best = -1;
    for (int i = 0; i < NVOICE; i++)
        if (s->voice[i].busy && s->voice[i].note == note) return i;   /* the same key: retrigger its voice */
    for (int i = 0; i < NVOICE; i++)   /* free: released longest ago */
        if (!s->voice[i].busy && (best < 0 || s->voice[i].t_off < s->voice[best].t_off)) best = i;
    if (best >= 0) return best;
    for (int i = 0; i < NVOICE; i++) {   /* released, still sounding: the oldest release */
        const voice_t *v = &s->voice[i];
        if (!v->key && !v->latched && (best < 0 || v->t_off < s->voice[best].t_off)) best = i;
    }
    if (best >= 0) return best;
    for (int i = 0; i < NVOICE; i++)     /* all held: the oldest note */
        if (best < 0 || s->voice[i].t_on < s->voice[best].t_on) best = i;
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
    if (s->last_pitch != VOICE_NO_PITCH) v->pitch = s->last_pitch;   /* glide from the latest note */
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
        *nchord = n < NVOICE ? n : NVOICE;
        return n ? *nchord : NVOICE;
    }
    return u + 1 < NVOICE ? u + 1 : NVOICE;
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
        if (!(v->busy && v->key) && s->last_pitch != VOICE_NO_PITCH)
            v->pitch = s->last_pitch + (nchord ? (chord[k] - ref) * 65536 : 0);
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
    synth_all_off(s);
    s->last_pitch = VOICE_NO_PITCH;   /* a new program does not glide from the old one's notes */
    s->prog = *p;
    for (unsigned i = 0; i < P_COUNT_PHASE1; i++) s->prog.v[i] = param_clamp((uint16_t)i, p->v[i]);
    synth_targets(s);
    for (int i = 0; i < SM_COUNT; i++) s->sm[i] = s->tgt[i];
    synth_rates(s);
    synth_xmod(s);
    synth_lfo_set(s);
    s->ctx.kres = 0;
    synth_derive(s);
}

void synth_init(synth_t *s)
{
    uint8_t *b = (uint8_t *)s;
    for (unsigned i = 0; i < sizeof *s; i++) b[i] = 0;
    s->rnd = 0x2545f491u;
    s->bend14 = 8192;
    s->mono = -1;
    s->last_pitch = VOICE_NO_PITCH;
    for (int i = 0; i < NVOICE; i++) voice_init(&s->voice[i], synth_rand(s));
    lfo_init(&s->lfo, synth_rand(s));
    program_t p;
    program_init(&p);
    synth_program(s, &p);
}

void synth_param(synth_t *s, uint16_t id, int16_t value)
{
    if (id >= P_COUNT_PHASE1) return;
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

void synth_panic(synth_t *s)
{
    for (int i = 0; i < NVOICE; i++) voice_reset(&s->voice[i], synth_rand(s));
    s->nstack = 0, s->mono = -1, s->hold = 0;
    s->last_pitch = VOICE_NO_PITCH;
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
        if (!live && !vo->act) { voice_reset(vo, synth_rand(s)); continue; }   /* silent: free, states cleared */
        voice_glide(vo);
        voice_control(vo, c, v);
    }
}

/* the bus soft limiter: linear to the knee, then y = K + u - u^2 / 2^15 (u = |x| - K) up to 32767 at K + 16384 */
static int32_t synth_limit(int32_t x)
{
    int32_t a = x < 0 ? -x : x;
    if (a <= SYNTH_LIMIT_KNEE) return x;
    int32_t u = a - SYNTH_LIMIT_KNEE, y = 32767;
    if (u < 16384) y = SYNTH_LIMIT_KNEE + u - ((u * u) >> 15);
    if (y > 32767) y = 32767;
    return x < 0 ? -y : y;
}

void synth_render(synth_t *s, int32_t *lr, uint32_t frames)
{
    while (frames) {
        if (!s->ctl_left) synth_tick(s), s->ctl_left = VOICE_CTL;
        int n = (int)(frames < s->ctl_left ? frames : s->ctl_left);
        int32_t bus[2 * VOICE_CTL];
        for (int i = 0; i < 2 * n; i++) bus[i] = 0;
        for (int i = 0; i < NVOICE; i++)
            if (s->voice[i].busy) voice_render(&s->voice[i], &s->ctx, s->prog.v, bus, n);
        for (int i = 0; i < 2 * n; i++) lr[i] += synth_limit(bus[i]);
        lr += 2 * n;
        frames -= (uint32_t)n;
        s->ctl_left = (uint8_t)(s->ctl_left - n);
    }
}
