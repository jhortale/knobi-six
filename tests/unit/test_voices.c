/* SPDX-License-Identifier: GPL-3.0-only */
/* Voice manager (firmware/src/synth/voice.c, voices.c): poly allocation, unison stack / chord memory, key modes,
 * portamento, bend range, pan spread, HOLD, panic (docs/ob6-reference.md 2.14, 6), and the voice layer's duties from
 * the earlier tasks: 2 ms minimum gate, freed voices reset, parameter smoothing, audio-rate X-Mod into the filter, the
 * output level. All the synth modules in one translation unit, as app.c's unity build will (a name clash fails here).
 * Measurements use doubles (host only). Every test prints what it measured. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846   /* (not in strict C11 on glibc) */
#endif
#include "params/params.c"
#include "synth/osc.c"
#include "synth/filter.c"
#include "synth/env.c"
#include "synth/mod.c"
#include "synth/voice.c"
#include "synth/voices.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define FS 44100
static synth_t S;
static int32_t BUF[2 * FS * 2];

static void fresh(void) { synth_init(&S); }
static void set(int id, int v) { synth_param(&S, (uint16_t)id, (int16_t)v); }
/* render n frames into BUF (cleared first) */
static void run(uint32_t n)
{
    memset(BUF, 0, 2 * n * sizeof BUF[0]);
    synth_render(&S, BUF, n);
}
static int voice_of(int note)
{
    for (int i = 0; i < NVOICE; i++)
        if (S.voice[i].busy && S.voice[i].note == note && (S.voice[i].key || S.voice[i].latched)) return i;
    return -1;
}
/* a voice's VCO 1 pitch in semitones (from its increment; LFO etc. off) */
static double semis(const voice_t *v) { return 69 + 12 * log2(v->osc.inc1 / 4294967296.0 * FS / 440); }

static void poly_six(void)
{
    fresh();
    set(P_LENV_R, 127);                       /* long release: released voices keep sounding */
    for (int k = 0; k < 6; k++) { synth_note(&S, 60 + k, 100); run(64); }
    int v61 = voice_of(61), v63 = voice_of(63), v60 = voice_of(60);
    synth_note(&S, 63, 0); run(64);
    synth_note(&S, 61, 0); run(64);           /* released after 63: 63 is the oldest release */
    synth_note(&S, 70, 100); run(64);
    int got1 = voice_of(70);
    synth_note(&S, 71, 100); run(64);
    int got2 = voice_of(71);
    synth_note(&S, 72, 100); run(64);         /* nothing released: the oldest held note (60) */
    int got3 = voice_of(72);
    printf("poly_six: 6 notes busy %d; 7th takes the oldest release (voice %d, want %d), 8th the next (%d, want %d), "
           "9th the oldest held (%d, want %d)\n", 6, got1, v63, got2, v61, got3, v60);
    CHECK(got1 == v63 && got2 == v61 && got3 == v60 && synth_active(&S) == 6);
    /* free voices: the one released longest ago (never-used voices count as released at time 0) */
    fresh();
    set(P_LENV_R, 0);
    static const int order[6] = { 63, 61, 60, 65, 62, 64 };
    for (int k = 0; k < 6; k++) { synth_note(&S, 60 + k, 100); run(64); }
    int first = voice_of(63);
    for (int k = 0; k < 6; k++) { synth_note(&S, order[k], 0); run(64); }
    run(FS / 2);
    int idle = synth_active(&S);
    synth_note(&S, 70, 100); run(64);
    int c = voice_of(70);
    printf("poly_six: all released and free (%d busy): the next note takes voice %d (63's, released first: %d)\n", idle,
           c, first);
    CHECK(idle == 0 && c == first);
    /* a same-key retrigger of a releasing voice keeps its voice and its detune slop (no pitch jump) */
    fresh();
    set(P_LENV_R, 127), set(P_DETUNE, 127);
    synth_note(&S, 60, 100); run(256);
    int rv = voice_of(60);
    int32_t d1 = S.voice[rv].dr1, d2 = S.voice[rv].dr2;
    synth_note(&S, 60, 0); run(256); synth_note(&S, 60, 100); run(64);
    printf("poly_six: same-key retrigger: voice %d -> %d, slop %d / %d -> %d / %d\n", rv, voice_of(60), d1, d2,
           S.voice[voice_of(60)].dr1, S.voice[voice_of(60)].dr2);
    CHECK(voice_of(60) == rv && S.voice[rv].dr1 == d1 && S.voice[rv].dr2 == d2);
}

static void unison_stack(void)
{
    fresh();
    set(P_UNISON_ON, 1), set(P_UNISON_VOICES, 5), set(P_DETUNE, 127);
    synth_note(&S, 57, 100);
    run(256);
    double p[NVOICE], lo = 1e9, hi = -1e9, sum = 0;
    int on = 0;
    for (int i = 0; i < NVOICE; i++) {
        if (!S.voice[i].busy || S.voice[i].note != 57) continue;
        on++, p[i] = semis(&S.voice[i]) - 57, sum += p[i];
        if (p[i] < lo) lo = p[i];
        if (p[i] > hi) hi = p[i];
    }
    printf("unison_stack: 6 voices on one note: %d; detune 127 spread %.3f .. %.3f st (mean %.4f)\n", on, lo, hi,
           sum / on);
    CHECK(on == 6 && fabs(lo + 0.5) < 0.01 && fabs(hi - 0.5) < 0.01 && fabs(sum / on) < 0.01);
    /* 3 voices: the centre one in tune and centred */
    fresh();
    set(P_UNISON_ON, 1), set(P_UNISON_VOICES, 2), set(P_DETUNE, 64);
    synth_note(&S, 60, 100);
    run(256);
    printf("unison_stack: 3 voices: %d busy, slot 0 %.4f st pan %d, slots 1 / 2 %.3f / %.3f st\n", synth_active(&S),
           semis(&S.voice[0]) - 60, S.voice[0].pan, semis(&S.voice[1]) - 60, semis(&S.voice[2]) - 60);
    CHECK(synth_active(&S) == 3 && fabs(semis(&S.voice[0]) - 60) < 0.001 && S.voice[0].pan == 0 &&
          semis(&S.voice[1]) < 60 && semis(&S.voice[2]) > 60);
    /* chord memory: C E G stored, key D with LOW -> D F# A */
    fresh();
    S.prog.chord[0] = 67, S.prog.chord[1] = 60, S.prog.chord[2] = 64;
    set(P_UNISON_ON, 1), set(P_UNISON_VOICES, 6), set(P_KEY_MODE, 0);
    synth_note(&S, 62, 100);
    run(256);
    printf("unison_stack: chord memory C E G, key D -> %d %d %d (%d voices)\n", S.voice[0].note, S.voice[1].note,
           S.voice[2].note, synth_active(&S));
    CHECK(synth_active(&S) == 3 && S.voice[0].note == 62 && S.voice[1].note == 66 && S.voice[2].note == 69);
    set(P_KEY_MODE, 2);   /* HIGH: the key is the chord's top */
    synth_note(&S, 62, 0), run(64), synth_note(&S, 74, 100), run(64);
    CHECK(S.voice[0].note == 67 && S.voice[1].note == 71 && S.voice[2].note == 74);
}

/* KEY MODE LAST (4) / LAST-R (5) */
static void key_mode_last(int mode, int *retrig_press, int *retrig_return, int *n_after_press, int *n_after_return)
{
    fresh();
    set(P_UNISON_ON, 1), set(P_UNISON_VOICES, 1), set(P_KEY_MODE, mode);
    synth_note(&S, 60, 100); run(2000);
    int st0 = S.voice[0].lenv.stage;
    synth_note(&S, 64, 100);
    *retrig_press = st0 == ADSR_DECAY && S.voice[0].lenv.stage == ADSR_ATTACK;
    run(2000);
    *n_after_press = S.voice[0].note;
    synth_note(&S, 64, 0);
    *retrig_return = S.voice[0].lenv.stage == ADSR_ATTACK;
    run(64);
    *n_after_return = S.voice[0].note;
}
static void key_mode_last_retrig(void)
{
    int rp, rr, np, nr;
    key_mode_last(5, &rp, &rr, &np, &nr);
    printf("key_mode_last_retrig: press -> note %d retrig %d; release newest -> note %d retrig %d\n", np, rp, nr, rr);
    CHECK(rp && np == 64 && nr == 60 && !rr && S.voice[0].busy && S.voice[1].note == nr);
}
static void key_mode_last_legato(void)
{
    int rp, rr, np, nr;
    key_mode_last(4, &rp, &rr, &np, &nr);
    printf("key_mode_last_legato: press -> note %d retrig %d; release newest -> note %d retrig %d\n", np, rp, nr, rr);
    CHECK(!rp && np == 64 && nr == 60 && !rr && S.voice[0].lenv.stage == ADSR_DECAY);
    /* all keys up: release */
    synth_note(&S, 60, 0);
    CHECK(S.voice[0].lenv.stage == ADSR_RELEASE);
}

static void key_mode_low_high(void)
{
    int got[2][4];
    for (int h = 0; h < 2; h++) {
        fresh();
        set(P_UNISON_ON, 1), set(P_UNISON_VOICES, 0), set(P_KEY_MODE, h ? 3 : 1);   /* LOW-R / HIGH-R */
        synth_note(&S, 60, 100); run(500);
        synth_note(&S, 55, 100); run(500); got[h][0] = S.voice[0].note;
        synth_note(&S, 65, 100); run(500); got[h][1] = S.voice[0].note;
        int stage_before = S.voice[0].lenv.stage;
        synth_note(&S, 67, 100); got[h][2] = S.voice[0].lenv.stage != stage_before;   /* changed note: retrig? */
        run(500);
        synth_note(&S, h ? 67 : 55, 0); run(64); got[h][3] = S.voice[0].note;
    }
    printf("key_mode_low_high: LOW: 60+55 -> %d, +65 -> %d, +67 retrig %d, release lowest -> %d; HIGH: %d, %d, retrig "
           "%d, release highest -> %d\n", got[0][0], got[0][1], got[0][2], got[0][3], got[1][0], got[1][1], got[1][2],
           got[1][3]);
    CHECK(got[0][0] == 55 && got[0][1] == 55 && got[0][2] && got[0][3] == 60);   /* -R: every keystroke retriggers */
    CHECK(got[1][0] == 60 && got[1][1] == 65 && got[1][2] && got[1][3] == 65);
}

/* ticks for a glide from note a to b (unison, 1 voice), legato (b pressed while a held) or not */
static int glide_ticks(int mode, int rate, int a, int b, int legato)
{
    fresh();
    set(P_UNISON_ON, 1), set(P_UNISON_VOICES, 0), set(P_PORTA_ON, 1), set(P_PORTA_MODE, mode), set(P_PORTA_RATE, rate);
    synth_note(&S, a, 100); run(4096);
    if (!legato) { synth_note(&S, a, 0); run(64); }
    synth_note(&S, b, 100);
    int t = 0;
    while (S.voice[0].pitch != b * 65536 && t < 100000) run(VOICE_CTL), t++;
    return t;
}
static void porta_modes(void)
{
    /* RATE 64: T = 2^6 ms = 64 ms = 176.4 ticks */
    int r12 = glide_ticks(0, 64, 48, 60, 1), r24 = glide_ticks(0, 64, 48, 72, 1);
    int t12 = glide_ticks(2, 64, 48, 60, 1), t24 = glide_ticks(2, 64, 48, 72, 1);
    int a_leg = glide_ticks(1, 64, 48, 60, 1), a_sep = glide_ticks(1, 64, 48, 60, 0);
    int ta_leg = glide_ticks(3, 64, 48, 60, 1), ta_sep = glide_ticks(3, 64, 48, 60, 0);
    int r1 = glide_ticks(2, 1, 48, 60, 1), r127 = glide_ticks(2, 127, 48, 60, 1), off = glide_ticks(2, 0, 48, 60, 1);
    printf("porta_modes: FIXED RATE 64: 12 st %.1f ms, 24 st %.1f ms; FIXED TIME: %.1f / %.1f ms; RATE-A legato %d "
           "ticks, detached %d; TIME-A %d / %d; TIME at rate 1 %.2f ms, 127 %.0f ms, 0 -> %d ticks\n",
           r12 * 16000.0 / FS, r24 * 16000.0 / FS, t12 * 16000.0 / FS, t24 * 16000.0 / FS, a_leg, a_sep, ta_leg, ta_sep,
           r1 * 16000.0 / FS, r127 * 16000.0 / FS, off);
    CHECK(abs(r12 - 176) <= 3 && abs(r24 - 2 * r12) <= 2 && abs(t12 - 176) <= 3 && abs(t24 - t12) <= 1);
    CHECK(a_leg == r12 && a_sep == 0 && ta_leg == t12 && ta_sep == 0);
    CHECK(r1 <= 4 && abs(r127 - 11290) < 120 && off == 0);
}

/* poly portamento: a new note glides from the latest note-on, whichever voice it gets */
static int poly_glide(int mode, int legato, int32_t *start)
{
    fresh();
    set(P_PORTA_ON, 1), set(P_PORTA_MODE, mode), set(P_PORTA_RATE, 64);
    synth_note(&S, 48, 100); run(4096);
    if (!legato) { synth_note(&S, 48, 0); run(64); }
    synth_note(&S, 55, 100);
    voice_t *v = &S.voice[voice_of(55)];
    *start = v->pitch;
    int t = 0;
    while (v->pitch != 55 * 65536 && t < 100000) run(VOICE_CTL), t++;
    return t;
}
static void porta_poly(void)
{
    int32_t s0, s1, s2, s3;
    fresh();
    set(P_PORTA_ON, 1), set(P_PORTA_RATE, 64);
    synth_note(&S, 60, 100);
    int first = S.voice[voice_of(60)].glide;
    int leg = poly_glide(1, 1, &s0), sep = poly_glide(1, 0, &s1), rate_sep = poly_glide(0, 0, &s2);
    int time_leg = poly_glide(3, 1, &s3);
    printf("porta_poly: first note glide step %d; RATE-A legato C3 -> G3 from %.2f in %d ticks (want 7/12 x 176 = "
           "103), detached from %.2f in %d; RATE detached from %.2f in %d; TIME-A legato from %.2f in %d\n", first,
           s0 / 65536.0, leg, s1 / 65536.0, sep, s2 / 65536.0, rate_sep, s3 / 65536.0, time_leg);
    CHECK(first == 0 && s0 == 48 * 65536 && abs(leg - 103) <= 2 && sep == 0 && s2 == 48 * 65536 &&
          abs(rate_sep - 103) <= 2 && s3 == 48 * 65536 && abs(time_leg - 176) <= 2);
}

/* no glide from before a panic or a program change; unison slots all glide from the latest note */
static void porta_reset_and_unison(void)
{
    program_t p;
    fresh();
    set(P_PORTA_ON, 1), set(P_PORTA_RATE, 64);
    synth_note(&S, 48, 100); run(4096);
    synth_panic(&S);
    synth_note(&S, 55, 100);
    int g_panic = S.voice[voice_of(55)].glide;
    p = S.prog;
    synth_note(&S, 55, 0); run(256);
    synth_program(&S, &p);
    synth_note(&S, 60, 100);
    int g_prog = S.voice[voice_of(60)].glide;
    /* poly C held, switch to unison 6, play G: every slot glides from C */
    fresh();
    set(P_PORTA_ON, 1), set(P_PORTA_RATE, 64), set(P_LENV_R, 127);
    for (int k = 0; k < 4; k++) { synth_note(&S, 40 + k, 100); run(256); }   /* stale pitches on voices 0..3 */
    synth_note(&S, 48, 100); run(4096);
    set(P_UNISON_ON, 1), set(P_UNISON_VOICES, 5);
    synth_note(&S, 55, 100);
    int same = 1;
    for (int k = 0; k < 6; k++) if (S.voice[k].pitch != 48 * 65536 || !S.voice[k].glide) same = 0;
    /* grow the stack mid-note (2 -> 6 voices): the next note's new slots glide from the latest note too */
    run(8192);
    set(P_UNISON_VOICES, 1);
    synth_note(&S, 55, 0), synth_note(&S, 57, 100); run(8192);
    set(P_UNISON_VOICES, 5);
    synth_note(&S, 57, 0), synth_note(&S, 52, 100);
    int grown = 1;
    for (int k = 0; k < 6; k++) if (S.voice[k].pitch != 57 * 65536) grown = 0;
    /* RATE-A, detached: no slot glides */
    set(P_PORTA_MODE, 1);
    synth_note(&S, 52, 0); run(64); synth_note(&S, 50, 100);
    int none = 1;
    for (int k = 0; k < 6; k++) if (S.voice[k].glide || S.voice[k].pitch != 50 * 65536) none = 0;
    printf("porta_reset_and_unison: glide step after panic %d, after a program load %d; poly -> unison: all 6 slots "
           "from C3 %d; grown stack all from the latest note %d; RATE-A detached: none glide %d\n", g_panic, g_prog,
           same, grown, none);
    CHECK(g_panic == 0 && g_prog == 0 && same && grown && none);
}

/* after a program load, a note landing on a voice still in release (stale pitch) does not glide; the other release
 * tails go on untouched */
static void porta_program_on_releasing(void)
{
    program_t p;
    /* poly: 6 voices all releasing (long release), so the new note must take a releasing voice */
    fresh();
    set(P_PORTA_ON, 1), set(P_PORTA_RATE, 64), set(P_LENV_R, 127);
    for (int k = 0; k < 6; k++) synth_note(&S, 40 + k, 100);
    run(1024);
    p = S.prog;
    synth_program(&S, &p);   /* releases all six */
    run(256);
    int busy = synth_active(&S);
    synth_note(&S, 60, 100);
    voice_t *v = &S.voice[voice_of(60)];
    int poly_ok = v->glide == 0 && v->pitch == 60 * 65536, tails = 1;
    for (int k = 0; k < NVOICE; k++)
        if (&S.voice[k] != v && (!S.voice[k].busy || S.voice[k].lenv.stage != ADSR_RELEASE)) tails = 0;
    /* unison 6: every slot releasing with the old program's pitch */
    fresh();
    set(P_PORTA_ON, 1), set(P_PORTA_RATE, 64), set(P_LENV_R, 127), set(P_UNISON_ON, 1), set(P_UNISON_VOICES, 5);
    synth_note(&S, 40, 100);
    run(1024);
    p = S.prog;
    synth_program(&S, &p);
    run(256);
    int ubusy = synth_active(&S);
    synth_note(&S, 60, 100);
    int uni_ok = 1;
    for (int k = 0; k < 6; k++) if (S.voice[k].glide || S.voice[k].pitch != 60 * 65536) uni_ok = 0;
    printf("porta_program_on_releasing: poly: %d releasing voices, new note no glide %d, other tails releasing %d; "
           "unison 6: %d releasing, all slots start at the note %d\n", busy, poly_ok, tails, ubusy, uni_ok);
    CHECK(busy == 6 && poly_ok && tails && ubusy == 6 && uni_ok);
}

static void bend_range_program(void)
{
    program_t p;
    program_init(&p);
    p.v[P_BEND_RANGE] = 12;
    fresh();
    synth_program(&S, &p);
    synth_note(&S, 60, 100);
    run(64);
    double c = semis(&S.voice[voice_of(60)]);
    synth_bend(&S, 16383); run(64);
    double up = semis(&S.voice[voice_of(60)]);
    synth_bend(&S, 0); run(64);
    double dn = semis(&S.voice[voice_of(60)]);
    p.v[P_BEND_RANGE] = 2;
    synth_program(&S, &p);
    synth_note(&S, 62, 100); run(64);
    double up2 = semis(&S.voice[voice_of(62)]);
    printf("bend_range_program: range 12: centre %.4f, up %+.5f, down %+.5f st; range 2 at bend 0: %+.5f st\n", c,
           up - c, dn - c, up2 - 62);
    CHECK(fabs(up - c - 12) < 0.01 && fabs(dn - c + 12) < 0.01 && fabs(up2 - 62 + 2) < 0.01);
}

static void pan_spread_alternates(void)
{
    fresh();
    set(P_PAN_SPREAD, 127);
    double l[NVOICE] = {0}, r[NVOICE] = {0};
    for (int k = 0; k < NVOICE; k++) {   /* one voice at a time: its own L / R energy */
        synth_panic(&S);
        S.voice[k].t_off = 0;
        for (int j = 0; j < NVOICE; j++) S.voice[j].t_off = (uint32_t)(j == k ? 0 : 1000);
        synth_note(&S, 60, 100);
        run(FS / 10);
        for (int i = FS / 20; i < FS / 10; i++)
            l[k] += (double)BUF[2 * i] * BUF[2 * i], r[k] += (double)BUF[2 * i + 1] * BUF[2 * i + 1];
    }
    /* voice k at x = -+(k / 2 + 1) / 3: constant power, L/R = cot((x + 1) pi / 4); hard-panned pair: > 60 dB */
    printf("pan_spread_alternates: spread 127, L/R dB per voice (want):");
    int alt = 1;
    for (int k = 0; k < NVOICE; k++) {
        double d = 10 * log10((l[k] + 1) / (r[k] + 1)), x = ((k & 1) ? 1 : -1) * (k / 2 + 1) / 3.0;
        double want = k / 2 == 2 ? ((k & 1) ? -HUGE_VAL : HUGE_VAL)
                                 : 20 * log10(cos((x + 1) * M_PI / 4) / sin((x + 1) * M_PI / 4));
        printf(" %+.1f (%+.1f)", d, want);
        if (k / 2 == 2 ? ((k & 1) ? d > -60 : d < 60) : fabs(d - want) > 0.3) alt = 0;
    }
    printf("\n");
    CHECK(alt);
    /* unison 5: slot 0 centred, then L R L R */
    fresh();
    set(P_PAN_SPREAD, 64), set(P_UNISON_ON, 1), set(P_UNISON_VOICES, 4);
    synth_note(&S, 60, 100);
    run(64);
    printf("pan_spread_alternates: unison 5, spread 64: gains (L, R)");
    for (int k = 0; k < 5; k++) printf(" (%d, %d)", S.voice[k].gl, S.voice[k].gr);
    printf("\n");
    CHECK(S.voice[0].gl == S.voice[0].gr && S.voice[1].gl > S.voice[1].gr && S.voice[2].gl < S.voice[2].gr &&
          S.voice[3].gl > S.voice[3].gr && S.voice[4].gl < S.voice[4].gr && S.voice[1].gl == S.voice[2].gr &&
          S.voice[3].gl == S.voice[4].gr && S.voice[3].gl - S.voice[3].gr > S.voice[1].gl - S.voice[1].gr);
    /* spread 0: all centred */
    set(P_PAN_SPREAD, 0);
    run(FS / 10);
    CHECK(S.voice[1].gl == S.voice[1].gr);
}

/* synth_note_release (the sequencer's note-offs): never latched by HOLD; a live key under HOLD still latches; in
 * unison the line follows the keys left and stops when none is */
static void release_ignores_hold(void)
{
    int poly, uni;
    fresh();
    synth_hold(&S, 1);
    synth_note(&S, 60, 100);                    /* (a sequence note) */
    synth_note(&S, 64, 100), synth_note(&S, 64, 0);   /* a live key, let go: latched */
    synth_note_release(&S, 60);
    poly = voice_of(60) < 0 || (!S.voice[voice_of(60)].key && !S.voice[voice_of(60)].latched);
    poly &= voice_of(64) >= 0 && S.voice[voice_of(64)].latched;
    for (unsigned i = 0; i < S.nstack; i++) poly &= S.stack[i] != 60;
    fresh();
    set(P_UNISON_ON, 1);
    synth_hold(&S, 1);
    synth_note(&S, 60, 100), synth_note(&S, 62, 100);
    synth_note_release(&S, 62);
    uni = S.mono == 60;                         /* (follows back to the key left) */
    synth_note_release(&S, 60);
    uni &= S.mono == -1 && S.nstack == 0;
    synth_hold(&S, 0);
    printf("release_ignores_hold: poly: released under HOLD, a live key still latched %d; unison: follows, then off %d\n",
           poly, uni);
    CHECK(poly && uni);
}

static void hold_latches_and_releases(void)
{
    fresh();
    set(P_LENV_R, 10);
    synth_hold(&S, 1);
    synth_note(&S, 60, 100), synth_note(&S, 64, 100);
    run(256);
    synth_note(&S, 60, 0), synth_note(&S, 64, 0);
    run(FS);
    int held = synth_active(&S), st = S.voice[voice_of(60)].lenv.stage;
    synth_note(&S, 67, 100); run(256); synth_note(&S, 67, 0);   /* a third, played and released while held */
    synth_hold(&S, 0);
    int rel = S.voice[0].lenv.stage == ADSR_RELEASE && S.voice[1].lenv.stage == ADSR_RELEASE;
    run(FS);
    printf("hold_latches_and_releases: 1 s after key up with HOLD: %d voices, stage %d; HOLD off -> release %d, 1 s "
           "later %d voices\n", held, st, rel, synth_active(&S));
    CHECK(held == 2 && st == ADSR_DECAY && rel && synth_active(&S) == 0);
    /* unison: the latched key keeps sounding, HOLD off releases */
    fresh();
    set(P_UNISON_ON, 1);
    synth_hold(&S, 1);
    synth_note(&S, 60, 100); run(256); synth_note(&S, 60, 0); run(FS / 2);
    int u = S.voice[0].lenv.stage;
    synth_hold(&S, 0);
    CHECK(u == ADSR_DECAY && S.voice[0].lenv.stage == ADSR_RELEASE);
}

/* rule 3's AMP modulation only on active voices, faded out (not cut) when the voice ends: LFO square at 0.022 Hz
 * (held at +1) -> AMP at full, LOUDNESS ENV AMOUNT 0, so only the LFO opens the VCA */
static void amp_mod_fades(void)
{
    fresh();
    set(P_LENV_AMT, 0), set(P_LENV_R, 0), set(P_LFO_D_AMP, 1), set(P_LFO_AMT, 255), set(P_LFO_SHAPE, LFO_SQUARE);
    set(P_LFO_FREQ, 0);
    run(4096);
    int32_t mx0 = 0;
    for (int i = 0; i < 2 * 4096; i++) if (abs(BUF[i]) > mx0) mx0 = abs(BUF[i]);
    synth_note(&S, 48, 100);
    run(FS / 10);
    double on = 0;
    for (int i = 0; i < FS / 10; i++) if (fabs((double)BUF[2 * i]) > on) on = fabs((double)BUF[2 * i]);
    synth_note(&S, 48, 0);
    run(FS / 10);
    int last = -1;
    /* the voice's end: the last frame that is not the bus DC blocker's free decay (once its input is 0 it goes on as
     * y[n] = R y[n-1], a few LSB at 5 Hz: within 1.5 LSB of that after truncation) */
    for (int i = 1; i < FS / 10; i++) if (fabs(BUF[2 * i] - SYNTH_DC_R * BUF[2 * i - 2]) > 1.5) last = i;
    double tail = 0;
    for (int i = last - 340; i <= last && i >= 0; i++)
        if (fabs((double)BUF[2 * i]) > tail) tail = fabs((double)BUF[2 * i]);
    printf("amp_mod_fades: no note: max %d; note held: peak %.0f (the LFO alone opens the VCA); after key up only the "
           "DC tail from frame %d (%.2f ms), last 340 frames (a period of C3) peak %.0f (%.1f dB under the note)\n", mx0,
           on, last + 1, (last + 1) * 1000.0 / FS, tail, 20 * log10(tail / on + 1e-9));
    CHECK(mx0 == 0 && on > 3000 && last > 340 && last < FS / 50 && tail < on / 4 && synth_active(&S) == 0);
}

static void panic_silences(void)
{
    fresh();
    set(P_LENV_R, 127), set(P_LFO_D_AMP, 1), set(P_LFO_AMT, 255);
    for (int k = 0; k < 6; k++) synth_note(&S, 50 + 3 * k, 127);
    run(FS / 4);
    int before = synth_active(&S);
    synth_panic(&S);
    run(FS / 4);
    int32_t mx = 0;
    for (int i = 0; i < FS / 2; i++) if (abs(BUF[i]) > mx) mx = abs(BUF[i]);
    printf("panic_silences: %d voices -> %d, max output after %d\n", before, synth_active(&S), mx);
    CHECK(before == 6 && synth_active(&S) == 0 && mx == 0);
}

/* a note-off within 2 ms keeps the gate until the 2 ms elapse */
static void min_gate(void)
{
    fresh();
    synth_note(&S, 60, 100);
    synth_note(&S, 60, 0);
    int v = voice_of(60) < 0 ? 0 : voice_of(60), first = -1;
    for (int i = 0; i < 400 && first < 0; i++) { run(1); if (S.voice[v].lenv.stage == ADSR_RELEASE) first = i + 1; }
    printf("min_gate: zero-length note released after %d frames (%.2f ms)\n", first, first * 1000.0 / FS);
    CHECK(first >= 88 && first <= 112);
}

/* freed voices: filter states exactly 0; idle voices with LFO -> AMP stay silent */
static void free_resets(void)
{
    fresh();
    set(P_FLT_RESO, 255), set(P_FLT_FREQ, 60), set(P_LENV_R, 0), set(P_LFO_D_AMP, 1), set(P_LFO_AMT, 255);
    set(P_LFO_SHAPE, LFO_SQUARE);
    synth_note(&S, 40, 127); run(FS / 4); synth_note(&S, 40, 0);
    int v = 0;
    run(FS / 2);
    int32_t mx = 0;
    for (int i = FS / 4; i < FS / 2; i++) if (abs(BUF[2 * i]) > mx) mx = abs(BUF[2 * i]);
    printf("free_resets: after release: %d voices, svf state %g / %g, output max %d (LFO square -> AMP at full)\n",
           synth_active(&S), (double)S.voice[v].svf.s1, (double)S.voice[v].svf.s2, mx);
    CHECK(synth_active(&S) == 0 && S.voice[v].svf.s1 == 0 && S.voice[v].svf.s2 == 0 && mx == 0);
}

/* parameter smoothing: FREQ, MODE, BP jumps glide (time constant ~5.8 ms); synth_program snaps when nothing sounds,
 * and glides like an edit while voices still sound (a program change under held notes: they release, no click) */
static void smoothing(void)
{
    fresh();
    synth_note(&S, 48, 100);
    run(1024);
    set(P_FLT_FREQ, 0), set(P_FLT_MODE, 127), set(P_FLT_BP, 1);
    run(VOICE_CTL);
    double c1 = S.ctx.cut / 65536.0, m1 = S.ctx.mode / 65536.0, b1 = S.ctx.bp;
    int t63 = 1;
    while (S.ctx.cut > SVF_KNOB_LO + (int32_t)(SVF_KNOB_SPAN * 0.368) && t63 < 1000) run(VOICE_CTL), t63++;
    run(FS / 10);
    printf("smoothing: one tick after FREQ 164 -> 0, MODE 0 -> 127, BP on: cut %.1f, mode %.1f, bp %.0f; 63 %% after "
           "%.2f ms; settled cut %d, mode %d, bp %d\n", c1, m1, b1, t63 * 16000.0 / FS, S.ctx.cut, S.ctx.mode,
           S.ctx.bp);
    CHECK(c1 > 150 && c1 < 160 && m1 > 10 && m1 < 25 && b1 > 1000 && b1 < 3000);
    CHECK(t63 * 16000.0 / FS > 4.5 && t63 * 16000.0 / FS < 7);
    CHECK(S.ctx.cut == SVF_KNOB_LO && S.ctx.mode == 255 * 65536 && S.ctx.bp == 32768);
    /* a program load while the note sounds glides; with nothing sounding it snaps */
    program_t p;
    program_init(&p);
    synth_program(&S, &p);
    int32_t g0 = S.ctx.cut;
    run(VOICE_CTL);
    int32_t g1 = S.ctx.cut;
    synth_panic(&S);
    set(P_FLT_FREQ, 0);
    synth_program(&S, &p);
    printf("smoothing: program load under a sounding note: cut %d -> %d after a tick (glides); silent: snapped to %d\n",
           g0 >> 16, g1 >> 16, S.ctx.cut >> 16);
    CHECK(g0 == SVF_KNOB_LO && g1 > SVF_KNOB_LO && g1 < 164 * 65536);
    CHECK(S.ctx.cut == 164 * 65536 && S.ctx.mode == 0 && S.ctx.bp == 0);
}

/* X-Mod VCO 2 -> F FREQ at audio rate: the coefficients follow svf_coef at VCO 2's value every 2 samples (voice.c:
 * an even frame of the period gets the mean of the previous and the new target, the odd frame the new target), and the
 * control rate path is used when the amount is 0 */
static void xmod_filter_audio_rate(void)
{
    fresh();
    set(P_FLT_FREQ, 90), set(P_FLT_RESO, 150), set(P_XM_VCO2, 60), set(P_XM_D_FFREQ, 1), set(P_XM_D_FMODE, 1);
    set(P_VCO2_FREQ, 31);
    CHECK(S.ctx.xm_flt == 1);
    synth_note(&S, 48, 100);
    run(1024);
    voice_t *v = &S.voice[voice_of(48)];
    /* one period by hand, frame by frame (the context's pos says which frame of the period it is) */
    voice_t w = *v;
    voice_ctx_t cx = S.ctx;
    int changes = 0;
    double ea1 = 0;
    float prev = w.coef.g, tgt = w.xc.a1;
    for (int i = 0; i < VOICE_CTL; i++) {
        int32_t bus[2 * VOICE_CTL] = {0};
        voice_t pre = w;
        cx.pos = (uint8_t)i;
        voice_render(&w, &cx, S.prog.v, bus, 1);
        int32_t o[4];
        osc_tick(&pre.osc, o);
        svf_coef_t ref;
        svf_coef(&ref, pre.cut + mods_xm_term(o[OSC_VCO2], S.ctx.xm.freq), (S.ctx.reso + 32768) >> 16,
                 (pre.mode + mods_xm_term(o[OSC_VCO2], S.ctx.xm.mode) + 32768) >> 16, pre.bp);
        double want = i & 1 ? tgt : 0.5 * (tgt + ref.a1);
        if (!(i & 1)) tgt = ref.a1;
        double e = fabs((double)w.coef.a1 / want - 1);
        if (e > ea1) ea1 = e;
        changes += w.coef.g != prev;
        prev = w.coef.g;
    }
    set(P_XM_VCO2, 0);
    printf("xmod_filter_audio_rate: g changed on %d of %d samples; a1 vs the interpolated svf_coef targets %.1e; "
           "amount 0 -> fast path %d\n", changes, VOICE_CTL, ea1, S.ctx.xm_flt);
    CHECK(changes >= VOICE_CTL - 1 && ea1 < 1e-3 && S.ctx.xm_flt == 0);
}

/* no denormals: a voice held with every mixer level at 0 (and RESONANCE up) decays its filter states to exactly 0
 * (svf_flush at control rate, |state| < 1e-20 -> 0), not into the FPU's denormal range */
static void filter_states_flush(void)
{
    fresh();
    set(P_FLT_RESO, 200), set(P_FLT_FREQ, 40);
    synth_note(&S, 48, 100);
    run(FS / 10);
    int v = voice_of(48);
    set(P_MIX_VCO1, 0);
    int denorm = 0;
    for (int i = 0; i < 40; i++) {
        run(FS / 10);
        float a = fabsf(S.voice[v].svf.s1), b = fabsf(S.voice[v].svf.s2);
        if ((a > 0 && a < 1.2e-38f) || (b > 0 && b < 1.2e-38f)) denorm++;
    }
    printf("filter_states_flush: held, mixer at 0, 4 s: states %g / %g, denormal seen %d times\n",
           (double)S.voice[v].svf.s1, (double)S.voice[v].svf.s2, denorm);
    CHECK(S.voice[v].busy && S.voice[v].svf.s1 == 0 && S.voice[v].svf.s2 == 0 && denorm == 0);
}

static void peak_rms(uint32_t n, double *peak, double *rms)
{
    double p = 0, r = 0;
    for (uint32_t i = 0; i < 2 * n; i++) { double x = fabs((double)BUF[i]); if (x > p) p = x; r += x * x; }
    *peak = 20 * log10(p / 32767), *rms = 10 * log10(r / (2 * n) / (32767.0 * 32767));
}

/* the bus at its worst level: six voices, every mixer level at 127, saws, the filter open (no resonance), PROGRAM
 * VOLUME and velocity 127, as a C3..C5 chord and as a 6-voice unison stack (the voices in tune). The soft limiter
 * keeps every sample under its ceiling (so under 32767: never a hard clip), and nothing sits flat at the top: the
 * longest run of equal samples within 2 % of the peak is a few frames (a waveform's own crest), not a clipped
 * plateau (the old limiter went flat at 32767 from 40960 up: program 420's 91 clipped samples). */
static void bus_never_clips(void)
{
    static const int chord[6] = { 48, 55, 60, 64, 67, 72 };
    for (int u = 0; u < 2; u++) {
        fresh();
        set(P_MIX_VCO1, 127), set(P_MIX_VCO2, 127), set(P_MIX_SUB, 127), set(P_MIX_NOISE, 127);
        set(P_FLT_FREQ, 164), set(P_FLT_RESO, 0), set(P_FLT_ENV_AMT, 0), set(P_PRG_VOL, 127), set(P_DETUNE, 0);
        if (u) set(P_UNISON_ON, 1), set(P_UNISON_VOICES, 5), synth_note(&S, 48, 127);
        else for (int k = 0; k < 6; k++) synth_note(&S, chord[k], 127);
        run(FS / 10);
        int32_t mx = 0, top = 0;
        int run_n = 0, longest = 0, at_top = 0;
        double busmax = 0;
        for (int b = 0; b < 2; b++) {
            /* the voice bus itself, 16 frames by hand (synth_render's loop without the blocker and limiter) */
            for (int f = 0; f < FS / 2; f += VOICE_CTL) {
                int32_t bus[2 * VOICE_CTL] = {0};
                synth_tick(&S);
                for (int i = 0; i < NVOICE; i++)
                    if (S.voice[i].busy) voice_render(&S.voice[i], &S.ctx, S.prog.v, bus, VOICE_CTL);
                for (int i = 0; i < 2 * VOICE_CTL; i++)
                    if (fabs((double)bus[i]) > busmax) busmax = fabs((double)bus[i]);
            }
            S.ctl_left = 0;
        }
        run(FS);
        for (int i = 0; i < 2 * FS; i++) if (abs(BUF[i]) > mx) mx = abs(BUF[i]);
        top = mx - mx / 50;
        for (int c = 0; c < 2; c++)
            for (int i = 1, n = 0; i < FS; i++) {
                int32_t a = BUF[2 * i + c];
                n = abs(a) >= top && a == BUF[2 * (i - 1) + c] ? n + 1 : 0;
                if (n + 1 > longest) longest = n + 1;
                if (abs(a) >= 32767) at_top++;
            }
        run_n = longest;
        printf("bus_never_clips: %s: bus before the limiter up to %.0f (%+.1f dBFS); out max |x| %d (%.2f dBFS, "
               "ceiling %d), %d samples at 32767, longest run of equal samples within 2 %% of the peak %d\n",
               u ? "6-voice unison C3" : "6-note chord C3..C5", busmax, 20 * log10(busmax / 32767), mx,
               20 * log10(mx / 32767.0), SYNTH_LIMIT_CEIL, at_top, run_n);
        CHECK(busmax > 40960 && mx < SYNTH_LIMIT_CEIL && mx < 32767 && at_top == 0 && run_n <= 3);
    }
}

/* the bus DC blocker: a DC-heavy patch (a sync lead: VCO 1 a saw synced to VCO 2 at a fifth + an octave below it, so
 * every cycle is a cut ramp, far off zero mean; filter open) keeps a mean under -60 dBFS over a second; and a 30 Hz
 * sine through the blocker alone comes out within 0.5 dB */
static void bus_dc_blocker(void)
{
    fresh();
    set(P_VCO1_SHAPE, 0), set(P_VCO1_SYNC, 1), set(P_VCO1_FREQ, 24 + 7), set(P_FLT_FREQ, 164), set(P_MIX_VCO1, 127);
    for (int k = 0; k < 3; k++) synth_note(&S, 48 + 7 * k, 127);
    run(FS / 2);
    double raw = 0;   /* the voice bus's own mean over 1 s (synth_render's loop by hand), then the output's */
    for (int f = 0; f < FS; f += VOICE_CTL) {
        int32_t bus[2 * VOICE_CTL] = {0};
        synth_tick(&S);
        for (int i = 0; i < NVOICE; i++)
            if (S.voice[i].busy) voice_render(&S.voice[i], &S.ctx, S.prog.v, bus, VOICE_CTL);
        for (int i = 0; i < VOICE_CTL; i++) raw += bus[2 * i];
    }
    S.ctl_left = 0;
    run(FS / 2);
    run(FS);
    double m = 0;
    for (int i = 0; i < FS; i++) m += BUF[2 * i];
    raw /= FS, m /= FS;
    /* 30 Hz, amplitude 10000 (under the limiter's knee), 2 s; the gain from the second second's projection */
    synth_t t;
    memset(&t, 0, sizeof t);
    static int32_t in[2 * 2 * FS], out[2 * 2 * FS];
    for (int i = 0; i < 2 * FS; i++) in[2 * i] = in[2 * i + 1] = (int32_t)lrint(10000 * sin(2 * M_PI * 30 * i / FS));
    memset(out, 0, sizeof out);
    for (int i = 0; i < 2 * FS; i += VOICE_CTL) synth_bus(&t, &in[2 * i], &out[2 * i], VOICE_CTL);
    double ps = 0, pc = 0;
    for (int i = FS; i < 2 * FS; i++) {
        double ph = 2 * M_PI * 30 * i / FS;
        ps += out[2 * i] * sin(ph), pc += out[2 * i] * cos(ph);
    }
    double g = 20 * log10(2 * sqrt(ps * ps + pc * pc) / FS / 10000);
    printf("bus_dc_blocker: sync lead x 3: voice bus mean %.0f (%.1f dBFS), out mean %.2f (%.1f dBFS); 30 Hz sine "
           "%+.2f dB\n", raw, 20 * log10(fabs(raw) / 32767 + 1e-12), m, 20 * log10(fabs(m) / 32767 + 1e-12), g);
    CHECK(fabs(raw) / 32767 > 0.03 && fabs(m) / 32767 < 0.001 && fabs(g) < 0.5);
}

static void output_level(void)
{
    static const int chord[6] = { 48, 52, 55, 60, 64, 67 };
    double pk, rm, pk1, rm1;
    fresh();
    for (int k = 0; k < 6; k++) synth_note(&S, chord[k], 100);
    run(FS / 10);
    run(FS);
    peak_rms(FS, &pk, &rm);
    fresh();
    synth_note(&S, 60, 100);
    run(FS / 10), run(FS);
    peak_rms(FS, &pk1, &rm1);
    /* worst case: four sources at full, pulses, sync, resonance, volume 127, six voices at full velocity */
    fresh();
    set(P_MIX_VCO2, 127), set(P_MIX_SUB, 127), set(P_MIX_NOISE, 127), set(P_VCO1_SHAPE, 255), set(P_VCO1_PW, 0);
    set(P_VCO2_SHAPE, 254), set(P_VCO2_PW, 255), set(P_FLT_RESO, 255), set(P_FLT_FREQ, 100), set(P_PRG_VOL, 127);
    set(P_VCO1_SYNC, 1), set(P_LFO_D_AMP, 1), set(P_LFO_AMT, 255);
    for (int k = 0; k < 6; k++) synth_note(&S, 36 + k, 127);
    run(FS / 10);
    double pkw, rmw;
    int32_t mx = 0;
    run(FS);
    for (int i = 0; i < 2 * FS; i++) if (abs(BUF[i]) > mx) mx = abs(BUF[i]);
    peak_rms(FS, &pkw, &rmw);
    printf("output_level: init patch, 6-note chord vel 100 vol 100: peak %.1f dBFS, RMS %.1f dBFS; one note: "
           "peak %.1f, RMS %.1f; worst case: max |x| %d (peak %.2f dBFS, RMS %.1f)\n", pk, rm, pk1, rm1, mx, pkw, rmw);
    CHECK(pk > -8 && pk < -4 && mx <= 32767);
}

/* key track in the re-anchored cutoff (semitones, not knob steps): FULL follows the key an octave an octave, HALF half
 * an octave; the cutoff in Hz from the voice's coefficient position (FREQ 82, envelope amount 0, no modulation) */
static void key_track_octaves(void)
{
    double r[3];
    for (int t = 0; t <= 2; t++) {
        fresh();
        set(P_FLT_FREQ, 82), set(P_FLT_ENV_AMT, 0), set(P_FLT_TRACK, t);
        synth_note(&S, 48, 100), synth_note(&S, 72, 100);
        run(256);
        double lo = svf_cutoff_hz(S.voice[voice_of(48)].cut), hi = svf_cutoff_hz(S.voice[voice_of(72)].cut);
        r[t] = log2(hi / lo) / 2;   /* octaves of cutoff per octave of key */
    }
    printf("key_track_octaves: C3 -> C5, cutoff octaves per key octave: OFF %.3f, HALF %.3f, FULL %.3f\n", r[0], r[1],
           r[2]);
    CHECK(fabs(r[0]) < 0.01 && fabs(r[1] - 0.5) < 0.01 && fabs(r[2] - 1) < 0.01);
}

static void mode_mapping(void)
{
    fresh();
    int ok = 1;
    for (int m = 0; m <= 127; m++) {
        program_t p;
        program_init(&p);
        p.v[P_FLT_MODE] = (int16_t)m;
        synth_program(&S, &p);
        if (S.ctx.mode != m * 255 * 65536 / 127) ok = 0;
    }
    printf("mode_mapping: MODE 0..127 -> filter mode m * 255 / 127 (Q16), 127 -> %d\n", S.ctx.mode >> 16);
    CHECK(ok && S.ctx.mode == 255 * 65536);
}

static double cost_run(int xm)
{
    fresh();
    if (xm) set(P_XM_VCO2, 50), set(P_XM_D_FFREQ, 1), set(P_XM_D_FMODE, 1), set(P_XM_D_BP, 1);
    if (xm == 2)
        set(P_XM_D_VCO1, 1), set(P_XM_D_SHAPE1, 1), set(P_XM_D_PW1, 1), set(P_VCO1_SYNC, 1), set(P_MIX_VCO2, 127);
    for (int k = 0; k < 6; k++) synth_note(&S, 48 + 4 * k, 100);
    run(256);
    const int blocks = 4000;
    clock_t t0 = clock();
    for (int b = 0; b < blocks; b++) run(256);
    return 1e9 * (double)(clock() - t0) / CLOCKS_PER_SEC / (blocks * 256.0 * 6);
}
static void cost(void)
{
    double a = cost_run(0), b = cost_run(1), c = cost_run(2);
    printf("cost: host ns per voice-sample (6 voices, everything included): init patch %.2f; X-Mod VCO2 -> F FREQ + "
           "F MODE + BP %.2f; all six X-Mod dests + sync + VCO2 on %.2f; sizeof(synth_t) %u bytes, voice_t %u\n", a, b,
           c, (unsigned)sizeof(synth_t), (unsigned)sizeof(voice_t));
}

int main(void)
{
    poly_six();
    unison_stack();
    key_mode_last_retrig();
    key_mode_last_legato();
    key_mode_low_high();
    porta_modes();
    porta_poly();
    porta_reset_and_unison();
    porta_program_on_releasing();
    bend_range_program();
    pan_spread_alternates();
    hold_latches_and_releases();
    release_ignores_hold();
    panic_silences();
    amp_mod_fades();
    min_gate();
    free_resets();
    smoothing();
    xmod_filter_audio_rate();
    filter_states_flush();
    output_level();
    bus_never_clips();
    bus_dc_blocker();
    key_track_octaves();
    mode_mapping();
    cost();
    if (fails) { printf("test_voices: %d FAILED\n", fails); return 1; }
    printf("test_voices: all passed\n");
    return 0;
}
