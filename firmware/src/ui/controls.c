/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX panel mapping (controls.h; the map in words: docs/CONTROLS.md). Included by app.c after engine.c (unity
 * build); tests/unit/test_controls.c includes it with CONTROLS_PURE (the tables and the pure functions only).
 *
 * Encoders K2..K8 (hardware 1..7; K1 stays the master volume) are the screen's knobs in reading order: a detent steps a
 * knob by (range + 127) / 128 (1 for ranges up to 128 values, 2 up to 256), four times that when the same encoder
 * turned less than CTL_FAST_MS ago (acceleration); a switch goes on with a clockwise detent and off with an
 * anticlockwise one; a select steps one choice a detent. Nothing wraps. Faders are absolute: their move goes through
 * the global pot mode (ctl_fader_move). Top pads (0..7) act once per press (a switch toggles, a select cycles, an
 * action runs; a release, a held pad's pressure, do nothing); bottom pads (8..15) never switch: their highest pressure
 * is the aftertouch (with MIDI channel pressure: the higher of the two). */
#include "controls.h"
#include "../params/store_prog.h"

#define N B_NONE
#define A(x) B_ACT(ACT_##x)
#define G(x) B_GLOBAL(G_##x)
#if KNOBI_CPU1
#define CTL_PAD_DUAL G(DUAL)                    /* a KNOBI_CPU1 build: Globals pad 7 = DUAL CORE (engine.c) */
#else
#define CTL_PAD_DUAL N
#endif
const binding_t BINDINGS[SCR_COUNT] = {
    [SCR_BOOT] = {{N, N, N, N, N, N, N}, {N, N, N, N}, {N, N, N, N, N, N, N, N}},
    [SCR_PLAY] = {{P_FLT_FREQ, P_FLT_RESO, P_FLT_MODE, P_FLT_ENV_AMT, N, N, N},
                  {P_FLT_FREQ, P_FLT_RESO, P_FLT_MODE, P_FLT_ENV_AMT},
                  {P_VCO1_SYNC, P_UNISON_ON, P_ARP_ON, A(HOLD), N, N, N, N}},   /* pad 0: the mockup's X-MOD slot is SYNC */
    [SCR_VCO] = {{P_VCO1_FREQ, P_VCO1_SHAPE, P_VCO1_PW, P_VCO2_FREQ, P_VCO2_FINE, P_VCO2_SHAPE, P_VCO2_PW},
                 {P_VCO1_SHAPE, P_VCO1_PW, P_VCO2_SHAPE, P_VCO2_PW},
                 {A(PREV), A(NEXT), P_VCO1_SYNC, P_VCO2_LOWFREQ, P_VCO2_KEYBOARD, N, N, N}},
    [SCR_MIX_FILTER] = {{P_MIX_VCO1, P_MIX_VCO2, P_MIX_SUB, P_MIX_NOISE, P_FLT_FREQ, P_FLT_RESO, P_FLT_MODE},
                        {P_FLT_FREQ, P_FLT_RESO, P_FLT_MODE, P_FLT_ENV_AMT},
                        {A(PREV), A(NEXT), P_FLT_BP, P_FLT_TRACK, N, N, N, N}},
    [SCR_ENV] = {{P_FENV_A, P_FENV_D, P_FENV_S, P_FENV_R, P_FLT_ENV_AMT, N, N},        /* row 1: FILTER */
                 {P_FENV_A, P_FENV_D, P_FENV_S, P_FENV_R},
                 {A(PREV), A(NEXT), P_FLT_VEL, P_LENV_VEL, A(ROW), N, N, N}},
    [SCR_LFO_XMOD] = {{P_LFO_FREQ, P_LFO_SYNC, P_LFO_SHAPE, P_LFO_AMT, P_XM_FENV, P_XM_VCO2, N},   /* row 1: LFO pads */
                      {P_LFO_FREQ, P_LFO_AMT, P_XM_FENV, P_XM_VCO2},
                      {A(PREV), A(NEXT), P_LFO_D_VCO1, P_LFO_D_VCO2, P_LFO_D_PW, P_LFO_D_AMP, P_LFO_D_FFREQ, P_LFO_D_FMODE}},
    [SCR_AT_MISC] = {{P_AT_AMT, P_DETUNE, P_PAN_SPREAD, P_BEND_RANGE, P_KEY_MODE, P_PRG_VOL, P_PORTA_RATE},  /* row 1: AT pads */
                     {P_AT_AMT, P_DETUNE, P_PORTA_RATE, P_PRG_VOL},
                     {A(PREV), A(NEXT), P_AT_D_VCO1, P_AT_D_VCO2, P_AT_D_LFOAMT, P_AT_D_AMP, P_AT_D_FFREQ, P_AT_D_FMODE}},
    [SCR_CLOCK] = {{P_BPM, P_CLK_DIV, P_ARP_MODE, P_ARP_RANGE, N, N, N}, {N, N, N, N},   /* ARP + CLOCK */
                   {A(PREV), A(NEXT), A(TAP), G(MIDI_CLOCK), G(ARP_SYNC), P_ARP_ON, P_ARP_HOLD, N}},
    [SCR_PROGRAM] = {{B_PROG(PRG_BANK), B_PROG(PRG_TENS), B_PROG(PRG_ONES), N, N, N, N}, {N, N, N, N},
                     {A(PREV), A(NEXT), A(WRITE), A(ENTER), A(MANUAL), N, N, N}},
    [SCR_GLOBALS] = {{G(MIDI_CH), G(POT_MODE), G(PARAM_TX), G(PARAM_RX), G(TRANSPOSE), G(TUNE), G(VEL_CURVE)}, {N, N, N, N},
                     {A(PREV), A(NEXT), G(LOCAL), G(STEREO), G(SUSTAIN), G(AT_CURVE), CTL_PAD_DUAL, N}},
};
const binding_t BINDINGS_ROW2[SCR_COUNT] = {
    [SCR_ENV] = {{P_LENV_A, P_LENV_D, P_LENV_S, P_LENV_R, P_LENV_AMT, N, N},           /* row 2: LOUDNESS */
                 {P_LENV_A, P_LENV_D, P_LENV_S, P_LENV_R},
                 {A(PREV), A(NEXT), P_FLT_VEL, P_LENV_VEL, A(ROW), N, N, N}},
    [SCR_LFO_XMOD] = {{P_LFO_FREQ, P_LFO_SYNC, P_LFO_SHAPE, P_LFO_AMT, P_XM_FENV, P_XM_VCO2, N},   /* row 2: X-MOD pads */
                      {P_LFO_FREQ, P_LFO_AMT, P_XM_FENV, P_XM_VCO2},
                      {A(PREV), A(NEXT), P_XM_D_VCO1, P_XM_D_SHAPE1, P_XM_D_PW1, P_XM_D_FFREQ, P_XM_D_FMODE, P_XM_D_BP}},
    [SCR_AT_MISC] = {{P_AT_AMT, P_DETUNE, P_PAN_SPREAD, P_BEND_RANGE, P_KEY_MODE, P_PRG_VOL, P_PORTA_RATE},  /* row 2: MISC pads */
                     {P_AT_AMT, P_DETUNE, P_PORTA_RATE, P_PRG_VOL},
                     {A(PREV), A(NEXT), P_UNISON_ON, P_UNISON_VOICES, P_PORTA_ON, P_PORTA_MODE, N, N}},
};
#undef N
#undef A
#undef G
const uint8_t SCR_ROWS[SCR_COUNT] = {[SCR_BOOT] = 1, [SCR_PLAY] = 1, [SCR_VCO] = 1, [SCR_MIX_FILTER] = 1, [SCR_ENV] = 2,
                                     [SCR_LFO_XMOD] = 2, [SCR_AT_MISC] = 2, [SCR_CLOCK] = 1, [SCR_PROGRAM] = 1,
                                     [SCR_GLOBALS] = 1};
const char *const SCR_NAME[SCR_COUNT] = {"KNOBI-SIX", "PLAY", "VCO 1 + 2", "MIX + FILTER", "ENVELOPES", "LFO + X-MOD",
                                         "AT + MISC", "ARP + CLOCK", "PROGRAM", "GLOBALS"};
static const char *const CTL_ROW_NAME[3][2] = {{"FILTER ENV", "LOUDNESS ENV"}, {"LFO", "X-MOD"}, {"AFTERTOUCH", "MISC"}};

const binding_t *ctl_binding(int screen, int row)
{
    if (screen < 0 || screen >= SCR_COUNT) screen = SCR_BOOT;
    return row && SCR_ROWS[screen] > 1 ? &BINDINGS_ROW2[screen] : &BINDINGS[screen];
}
const char *ctl_row_name(int screen, int row)
{
    if (screen < 0 || screen >= SCR_COUNT) return "";
    if (SCR_ROWS[screen] < 2) return SCR_NAME[screen];
    return CTL_ROW_NAME[screen == SCR_ENV ? 0 : screen == SCR_LFO_XMOD ? 1 : 2][row != 0];
}

/* ---- the pure parts */
int32_t ctl_from7(uint16_t id, uint32_t x)
{
    const param_t *p = param_get(id);
    if (!p) return 0;
    if (x > 127u) x = 127u;
    if (p->bipolar) {                                  /* 0 -> min, 64 -> 0, 127 -> max, rounded */
        if (x >= 64u) return (int32_t)(((x - 64u) * (uint32_t)p->max + 31u) / 63u);
        return -(int32_t)(((64u - x) * (uint32_t)(-p->min) + 32u) / 64u);
    }
    return p->min + (int32_t)((x * (uint32_t)(p->max - p->min) + 63u) / 127u);
}
uint32_t ctl_to7(uint16_t id, int32_t v)
{
    const param_t *p = param_get(id);
    uint32_t r;
    if (!p) return 0;
    if (v < p->min) v = p->min;
    if (v > p->max) v = p->max;
    if (p->bipolar) {
        if (v >= 0) return 64u + ((uint32_t)v * 63u + (uint32_t)p->max / 2u) / (uint32_t)p->max;
        return 64u - ((uint32_t)(-v) * 64u + (uint32_t)(-p->min) / 2u) / (uint32_t)(-p->min);
    }
    r = (uint32_t)(p->max - p->min);
    return r ? ((uint32_t)(v - p->min) * 127u + r / 2u) / r : 0u;
}

#define CTL_FAST_MS 40u                                /* encoder acceleration: detents closer than this x4 */
int32_t ctl_enc_value(uint16_t id, int32_t cur, int32_t det, int fast)
{
    int32_t lo = 0, hi, v;
    if (B_IS_GLOBAL(id)) {
        if ((id & 0xFFu) >= G_COUNT) return cur;
        lo = GLOB_MIN[id & 0xFFu], hi = GLOB_MAX[id & 0xFFu];
        v = cur + det;
    } else {
        const param_t *p;
        if (!P_IS_LIVE(id)) return cur;
        p = param_get(id);
        lo = p->min, hi = p->max;
        if (p->kind == PK_SWITCH) v = det > 0 ? 1 : det < 0 ? 0 : cur;
        else if (p->kind == PK_SELECT) v = cur + det;
        else v = cur + det * (id == P_BPM ? 1 : (hi - lo + 127) / 128) * (fast ? 4 : 1);   /* (BPM: 1 a detent) */
    }
    return v < lo ? lo : v > hi ? hi : v;
}
int32_t ctl_pad_value(uint16_t id, int32_t cur)
{
    const param_t *p;
    if (B_IS_GLOBAL(id) && (id & 0xFFu) < G_COUNT)
        return cur >= GLOB_MAX[id & 0xFFu] ? GLOB_MIN[id & 0xFFu] : cur + 1;
    if (!P_IS_LIVE(id)) return cur;
    p = param_get(id);
    if (p->kind == PK_SWITCH) return cur ? 0 : 1;
    if (p->kind == PK_SELECT) return cur >= p->max ? p->min : cur + 1;
    return cur;
}
/* the program number's digits: bank 0..PROG_BANKS-1, tens and ones 0..9; a digit clamps (no carry, no wrap) */
int32_t ctl_prog_digit(int32_t n, uint32_t d, int32_t det)
{
    int32_t dig[PRG_COUNT], i;
    if (n < 0) n = 0;
    if (n >= (int32_t)PROG_COUNT) n = PROG_COUNT - 1;
    if (d >= PRG_COUNT) return n;
    dig[PRG_BANK] = n / 100, dig[PRG_TENS] = n / 10 % 10, dig[PRG_ONES] = n % 10;
    i = dig[d] + det;
    dig[d] = i < 0 ? 0 : i > (d == PRG_BANK ? (int32_t)PROG_BANKS - 1 : 9) ? (d == PRG_BANK ? (int32_t)PROG_BANKS - 1 : 9) : i;
    return dig[PRG_BANK] * 100 + dig[PRG_TENS] * 10 + dig[PRG_ONES];
}

int ctl_pad_at(const uint8_t *press)
{
    int m = 0;
    for (unsigned i = 8; i < 16u; i++)
        if (press[i] > m) m = press[i];
    return m > 127 ? 127 : m;
}

/* a fader moved from..to (0..127) over param id whose value is cur. The state re-anchors (anchor = where the fader
 * was, the value it had; not caught) when the fader's param or the pot mode changed, or the value changed elsewhere
 * (an encoder, MIDI, a program) since this fader last set it.
 *   JUMP: the fader's value.  PASSTHRU: nothing until a move crosses (or lands on) the value, then the fader's value.
 *   RELATIVE: up from the anchor scales toward the range's top (the fader's top = max), down toward its bottom
 *   (the fader's bottom = min); computed from the anchor each time, so no small move is lost to rounding. */
void ctl_fader_reset(ctl_fader_t *f) { f->id = B_NONE; f->caught = 0; }
int32_t ctl_fader_move(ctl_fader_t *f, int mode, uint16_t id, int32_t cur, uint32_t from, uint32_t to)
{
    int32_t v, lo, hi;
    const param_t *pp = param_get(id);
    if (!pp) return cur;
    if (from > 127u) from = 127u;
    if (to > 127u) to = 127u;
    if (f->id != id || f->mode != (uint8_t)mode || f->last_v != cur) {
        f->id = id, f->mode = (uint8_t)mode, f->anchor_p = (uint8_t)from, f->anchor_v = (int16_t)cur, f->caught = 0;
    }
    if (mode == POT_JUMP) v = ctl_from7(id, to);
    else if (mode == POT_PASSTHRU) {
        if (!f->caught) {
            lo = ctl_from7(id, from), hi = ctl_from7(id, to);
            if (lo > hi) v = lo, lo = hi, hi = v;
            if (cur >= lo && cur <= hi) f->caught = 1;
        }
        v = f->caught ? ctl_from7(id, to) : cur;
    } else {
        int32_t p0 = f->anchor_p, v0 = f->anchor_v, p = (int32_t)to;
        if (p > p0) v = v0 + (pp->max - v0) * (p - p0) / (127 - p0);
        else if (p < p0) v = v0 - (v0 - pp->min) * (p0 - p) / p0;
        else v = v0;
    }
    v = param_clamp(id, v);
    f->last_v = (int16_t)v;
    return v;
}

#ifndef CONTROLS_PURE
/* ---- the main loop's: state */
#define CTL_BOOT_MS 1000u                              /* the boot screen, then Play (or at the first button) */
static int8_t ctl_glob[G_COUNT] = {0, POT_RELATIVE, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0};   /* (GLOB_DEF; app.c loads them) */
static clk_t ctl_tapclk;                               /* TAP's state (clock_tap; only its tap count is used here) */
static uint32_t ctl_beat_seen, ctl_beat_ms;            /* TAP's LED: the clock's last beat, when it came */
#define CTL_BEAT_MS 90u                                /* TAP lit this long on each beat */
static uint8_t ctl_row[SCR_COUNT];
static int ctl_last_edit = SCR_VCO, ctl_before_glob = SCR_PLAY;
static uint8_t ctl_hold, ctl_sustain, ctl_midi_at, ctl_midi_wheel, ctl_midi_vol = 127;
static int32_t ctl_midi_bend;                          /* MIDI pitch bend - 8192 (added to the wheel's) */
static uint32_t ctl_pressed, ctl_released;             /* this pass's button edges (the app reads them after) */
static int32_t ctl_enc_pos[8];                         /* detents turned, per encoder (screen, probes) */
static uint32_t ctl_enc_ms[8];
static uint8_t ctl_fpos[4];
static ctl_fader_t ctl_fad[4];
static uint8_t ctl_chord_on, ctl_chord_n, ctl_chord_cap[6];
static int32_t ctl_prg_bank = -1, ctl_prg_req = -1, ctl_bank_sel = -1;   /* the last program change received, its bank;
                                                                            CC 32's bank (-1: the current one) */
/* programs: the one in play, MANUAL, a load to retry (the engine's buffer busy), WRITE (0 idle, 1 armed: the digits
 * pick the destination, 2 committed: fading out, then the save, then the fade back in) */
static int32_t ctl_prog, ctl_prog_want = -1, ctl_wr_dest;
static uint8_t ctl_manual, ctl_wr, ctl_want_factory;    /* the load to retry is "restore factory" */
static uint32_t ctl_restores;                          /* restore-factory loads done (probe) */
/* a notice for the player (the header's display on every screen, words on Program; controls.h NT_*): a broken stored
 * program, restore factory asked (GLOBE + BT once: again within CTL_RESTORE_MS to confirm), restored */
static uint8_t ctl_notice;
static int32_t ctl_restore_ask = -1;                   /* the program a first GLOBE + BT asked to restore, -1 none */
static uint32_t ctl_restore_t;
#define CTL_RESTORE_MS 2000u
static int8_t ctl_wr_rc;                               /* the last WRITE: 1 stored, -1 failed, 0 none yet */
static uint32_t ctl_wr_t, ctl_writes;
#define CTL_FADE_WAIT_MS 100u                          /* the fade-out not confirmed (no audio running): save anyway */
static uint16_t ctl_last_id = B_NONE;                  /* the last edit (screen) */
static int32_t ctl_last_v;
static uint32_t ctl_edits, ctl_scr_n;                  /* counters: an edit, a screen / row change */
static uint32_t ctl_led[16];
static void midi_cc_send(uint16_t id, int32_t v);     /* midi_map.c */
static void midi_map_reset(void);

static void ctl_set(program_t *edit, uint16_t id, int32_t v, int panel)   /* panel: sent as CC when PARAM TX is on */
{
    int16_t c;
    if (B_IS_GLOBAL(id)) {
        uint32_t g = id & 0xFFu;
        if (g >= G_COUNT) return;
        v = v < GLOB_MIN[g] ? GLOB_MIN[g] : v > GLOB_MAX[g] ? GLOB_MAX[g] : v;
        if (ctl_glob[g] == v) return;
        ctl_glob[g] = (int8_t)v;
    } else {
        if (!P_IS_LIVE(id)) return;
        c = param_clamp(id, v);
        if (c == edit->v[id] && c == eng_edit.v[id]) return;
        eng_param(id, c);                              /* the queue (kept and retried if it is full) */
        edit->v[id] = c;
        v = c;
        if (panel && ctl_glob[G_PARAM_TX]) midi_cc_send(id, c);
    }
    ctl_last_id = id, ctl_last_v = v;
    ctl_edits++;
}

static void ctl_goto(int *screen, int s)
{
    if (s < SCR_PLAY) s = SCR_PLAY;
    if (s > SCR_GLOBALS) s = SCR_GLOBALS;
    if (s == *screen) return;
    if (s >= SCR_VCO && s <= SCR_CLOCK) ctl_last_edit = s;
    if (s == SCR_GLOBALS) ctl_before_glob = *screen;
    *screen = s;
    for (unsigned k = 0; k < 4u; k++) ctl_fader_reset(&ctl_fad[k]);   /* the faders pick up afresh */
    ctl_scr_n++;
}
static void ctl_other_row(int screen)
{
    if (SCR_ROWS[screen] < 2) return;
    ctl_row[screen] ^= 1u;
    for (unsigned k = 0; k < 4u; k++) ctl_fader_reset(&ctl_fad[k]);
    ctl_scr_n++;
}

/* ---- programs (main loop): load, MANUAL, WRITE */
static void ctl_prog_select(int32_t n)                 /* load program n (MANUAL off); retried while the engine is busy */
{
    static program_t p;
    if (n < 0 || n >= (int32_t)PROG_COUNT) return;
    if (ctl_wr != 0) { ctl_prog_want = n; return; }   /* (not under a WRITE, armed or committing: it stores the edit as
                                                       * it is; MIDI program change is ignored then, midi_map.c) */
    ctl_prog = n, ctl_manual = 0, ctl_want_factory = 0, ctl_restore_ask = -1;
    ctl_notice = prog_load((uint32_t)n, &p) == -2 ? NT_BROKEN : NT_NONE;   /* (never written: the factory program,
                                                        * else init; broken: init, and the notice says so) */
    ctl_prog_want = eng_program(&p) ? n : -1;
    if (ctl_notice) ctl_last_id = B_NOTICE, ctl_last_v = ctl_notice, ctl_edits++;
}
/* restore factory (hold GLOBE, press WRITE / BT): factory program n into the edit buffer (no factory bank: the init
 * program); nothing is saved until WRITE. The stored program n stays as it is. */
static void ctl_restore_factory(int32_t n)
{
    static program_t p;
    if (n < 0 || n >= (int32_t)PROG_COUNT) return;
    ctl_prog = n, ctl_manual = 0;
    prog_factory((uint32_t)n, &p);
    if (eng_program(&p)) { ctl_prog_want = n, ctl_want_factory = 1; return; }   /* busy: again next pass */
    ctl_prog_want = -1, ctl_want_factory = 0;
    ctl_restores++;
    ctl_notice = NT_RESTORED, ctl_last_id = B_NOTICE, ctl_last_v = NT_RESTORED, ctl_edits++;
}
/* GLOBE held + BT: the first asks (the edits would be lost: "SurE" in the header, words on Program), the second within
 * CTL_RESTORE_MS on the same program restores; a WRITE armed: nothing */
static void ctl_restore_press(void)
{
    if (ctl_wr != 0) return;
    if (ctl_restore_ask == ctl_prog && fm1_ms - ctl_restore_t < CTL_RESTORE_MS) {
        ctl_restore_ask = -1;
        ctl_restore_factory(ctl_prog);
        return;
    }
    ctl_restore_ask = ctl_prog, ctl_restore_t = fm1_ms;
    ctl_notice = NT_RESTORE_ASK, ctl_last_id = B_NOTICE, ctl_last_v = NT_RESTORE_ASK, ctl_edits++;
}
static void ctl_manual_press(program_t *edit)          /* MANUAL: the sound is the panel; the faders (Play's: FILTER FREQ,
                                                        * RESO, MODE, ENV AMT) set where they stand. MIDI program change
                                                        * is ignored meanwhile; turning a program digit loads it */
{
    const binding_t *b = ctl_binding(SCR_PLAY, 0);
    ctl_manual ^= 1u;
    if (!ctl_manual) return;
    for (unsigned k = 0; k < 4u; k++)
        if (P_IS_LIVE(b->fader[k])) ctl_set(edit, b->fader[k], ctl_from7(b->fader[k], ctl_fpos[k]), 1);
}
static void ctl_write_press(int *screen);
/* WRITE saves at once (spec, Our calls): the output fades out (10 ms, engine.c), the slot is saved as the base saves
 * (its erases hold the interrupts off: the sound stops, on silence), it fades back in (10 ms). The base's sliced save
 * (store_sliced) might keep the sound playing instead, but only the chip can show whether it is click-free (the host
 * does not model the flash part's suspends): build with -DKNOBI_WRITE_SLICED to try it there (no fade, a sliced
 * save); -DKNOBI_WRITE_NOFADE (host evidence) saves without the fade */
static void ctl_enter(void)                            /* commit an armed WRITE: the fade out starts */
{
    if (ctl_wr != 1) return;
    ctl_wr = 2, ctl_wr_t = fm1_ms;
#if !defined KNOBI_WRITE_SLICED && !defined KNOBI_WRITE_NOFADE
    eng_fade(1);
#endif
}
static void ctl_write_poll(const program_t *edit)      /* faded out (or waited long enough): save, fade back in */
{
#ifdef KNOBI_WRITE_SLICED
    if (ctl_wr != 2) return;
    store_sliced = 1;
#else
    if (ctl_wr != 2 || (!eng_faded() && fm1_ms - ctl_wr_t < CTL_FADE_WAIT_MS)) return;
#endif
    ctl_wr_rc = prog_save((uint32_t)ctl_wr_dest, edit) ? -1 : 1;   /* (blocks: the slot's erase; the sound is silent) */
    store_sliced = 0;
    eng_fade(0);
    if (ctl_wr_rc > 0) ctl_prog = ctl_wr_dest, ctl_manual = 0, ctl_writes++;
    ctl_wr = 0;
}
static void ctl_prog_turn(uint32_t d, int32_t det)     /* a program digit's encoder (Program screen) */
{
    int32_t n;
    if (ctl_wr == 2) return;
    n = ctl_prog_digit(ctl_wr == 1 ? ctl_wr_dest : ctl_prog, d, det);
    if (ctl_wr == 1) ctl_wr_dest = n;
    else if (n != ctl_prog || ctl_manual) ctl_prog_select(n);
}

static void ctl_action(int *screen, uint32_t a)
{
    if (a == ACT_PREV) ctl_goto(screen, *screen - 1);
    else if (a == ACT_NEXT) ctl_goto(screen, *screen + 1);
    else if (a == ACT_ROW) ctl_other_row(*screen);
    else if (a == ACT_HOLD) { ctl_hold ^= 1u; ctl_last_id = B_ACT(ACT_HOLD), ctl_last_v = ctl_hold; ctl_edits++; }
    else if (a == ACT_WRITE) ctl_write_press(screen);
    else if (a == ACT_ENTER) ctl_enter();
    else if (a == ACT_MANUAL) ctl_manual_press(&eng_edit);
    else if (a == ACT_TAP) {                           /* tap tempo: from the 4th tap the program's BPM (synth/clock.h) */
        int bpm = clock_tap(&ctl_tapclk, fm1_ms);
        if (bpm) ctl_set(&eng_edit, P_BPM, bpm, 1);
        else ctl_last_id = B_ACT(ACT_TAP), ctl_last_v = ctl_tapclk.tap_n, ctl_edits++;   /* (the taps so far) */
    }
}
static void ctl_write_press(int *screen)               /* WRITE (the pad, BT): arm (Program screen, the destination the
                                                        * current program) or cancel */
{
    if (ctl_wr == 0) { ctl_wr = 1, ctl_wr_dest = ctl_prog; ctl_goto(screen, SCR_PROGRAM); }
    else if (ctl_wr == 1) ctl_wr = 0;
}

/* chord memory: hold CHORD (BTN_ENV) and play up to 6 notes; on release they are the program's chord (absolute MIDI
 * notes, ascending order not needed, 0 = empty) and UNISON goes on with UNISON VOICES = CHD. CHORD tapped with no
 * note leaves the chord as it was. */
void controls_key(int note)
{
    unsigned i;
    if (!ctl_chord_on || note <= 0 || note > 127) return;
    for (i = 0; i < ctl_chord_n; i++)
        if (ctl_chord_cap[i] == note) return;
    if (ctl_chord_n < 6u) ctl_chord_cap[ctl_chord_n++] = (uint8_t)note;
}
static void ctl_chord_commit(program_t *edit)
{
    uint8_t c[6] = {0};
    unsigned i;
    ctl_chord_on = 0;
    if (!ctl_chord_n) return;
    for (i = 0; i < ctl_chord_n; i++) c[i] = ctl_chord_cap[i];
    eng_chord(c);                                      /* (queued by eng_flush) */
    for (i = 0; i < 6u; i++) edit->chord[i] = c[i];
    ctl_set(edit, P_UNISON_VOICES, 6, 1);
    ctl_set(edit, P_UNISON_ON, 1, 1);
}

int controls_aftertouch(void) { return glob_at_curve(ctl_glob[G_AT_CURVE], ctl_pad_at(fm1_in.pad_press)); }

void controls_init(void)
{
    smk_pad_events = 1;                                /* the pads are ours: events, no notes */
    midi_map_reset();
    for (unsigned k = 0; k < 4u; k++) { ctl_fpos[k] = fm1_in.fader[k]; ctl_fader_reset(&ctl_fad[k]); }
    for (unsigned k = 0; k < 16u; k++) ctl_led[k] = ~0u;
}

/* TAP's light: on for CTL_BEAT_MS from each quarter note of the clock (engine.c eng_clk_beats), off while it does not
 * run (an external clock absent or stopped) */
static int ctl_tap_lit(void)
{
    uint32_t b = eng_clk_beats;
    if (b != ctl_beat_seen) ctl_beat_seen = b, ctl_beat_ms = fm1_ms;
    return eng_clk_run && fm1_ms - ctl_beat_ms < CTL_BEAT_MS;
}

static void ctl_leds(int screen, const program_t *edit)   /* top pads lit when on, bottom pads glow with pressure */
{
    const binding_t *b = ctl_binding(screen, ctl_row[screen]);
    uint32_t i, col, changed = 0;
    for (i = 0; i < 16u; i++) {
        if (i < 8u) {
            uint16_t x = b->pad_top[i];
            if (P_IS_LIVE(x)) col = edit->v[x] != param_get(x)->min ? 0xFF0000u : 0x180000u;   /* red / dim red */
            else if (B_IS_GLOBAL(x)) col = ctl_glob[x & 0xFFu] != GLOB_MIN[x & 0xFFu] ? 0xFF0000u : 0x180000u;
            else if (x == B_ACT(ACT_HOLD)) col = ctl_hold ? 0xFF0000u : 0x180000u;
            else if (x == B_ACT(ACT_WRITE)) col = ctl_wr == 2 || (ctl_wr == 1 && fm1_ms >> 8 & 1u) ? 0xFF0000u : 0x180000u;   /* armed: blinks */
            else if (x == B_ACT(ACT_ENTER)) col = ctl_wr == 1 ? 0xFF0000u : 0x180000u;
            else if (x == B_ACT(ACT_MANUAL)) col = ctl_manual ? 0xFF0000u : 0x180000u;
            else if (x == B_ACT(ACT_TAP)) col = ctl_tap_lit() ? 0xFF0000u : 0x180000u;          /* flashes on the beat */
            else if (B_IS_ACT(x)) col = 0x282828u;                                            /* dim white */
            else col = 0;
        } else {
            col = (uint32_t)fm1_in.pad_press[i] * 2u << 16;                                   /* red, 0..254 */
        }
        if (col != ctl_led[i]) {
            ctl_led[i] = col;
            padled_set(i, (uint8_t)(col >> 16), (uint8_t)(col >> 8), (uint8_t)col);
            changed = 1;
        }
    }
    if (changed) padled_flush();
}

/* s is not used: the synth belongs to the audio interrupt, every change goes through the queue (eng_*); edit is the
 * program as edited (app.c passes &eng_edit) */
void controls_poll(synth_t *s, program_t *edit, int *screen)
{
    const binding_t *b;
    uint32_t rel, k;
    int32_t e;
    (void)s;
    eng_flush();                                       /* what the queue could not take last time, first */

    ctl_pressed = fm1_input_edges(&rel);
    ctl_released = rel;
    if (*screen == SCR_BOOT && (ctl_pressed || fm1_ms >= CTL_BOOT_MS)) { *screen = SCR_PLAY; ctl_scr_n++; }
    if (*screen != SCR_BOOT) {
        uint32_t p = ctl_pressed;
        if (p >> BTN_LEFT & 1u) ctl_goto(screen, *screen - 1);
        if (p >> BTN_RIGHT & 1u) ctl_goto(screen, *screen + 1);
        if (p >> BTN_HOME & 1u) ctl_goto(screen, SCR_PLAY);
        if (p >> BTN_EDIT & 1u) ctl_goto(screen, ctl_last_edit);
        if (p >> BTN_GLO & 1u) ctl_goto(screen, *screen == SCR_GLOBALS ? ctl_before_glob : SCR_GLOBALS);
        if (p >> BTN_SEL & 1u) ctl_other_row(*screen);
    }
    if (*screen != SCR_BOOT && ctl_pressed >> BTN_SAVE & 1u) {
        if (fm1_in.buttons >> BTN_GLO & 1u) {          /* GLOBE held + WRITE: restore factory, asked then confirmed */
            if (ctl_wr == 0) {
                ctl_restore_press();
                if (*screen == SCR_GLOBALS) ctl_goto(screen, ctl_before_glob);   /* (GLOBE's press opened Globals) */
            }
        } else { ctl_restore_ask = -1; ctl_write_press(screen); }   /* BT = WRITE */
    }
    if (ctl_restore_ask >= 0 && fm1_ms - ctl_restore_t >= CTL_RESTORE_MS) {   /* not confirmed in time: forgotten */
        ctl_restore_ask = -1;
        if (ctl_notice == NT_RESTORE_ASK) ctl_notice = NT_NONE;
        if (ctl_last_id == B_NOTICE && ctl_last_v == NT_RESTORE_ASK) ctl_last_v = NT_NONE;
    }
    if (ctl_pressed >> BTN_ENV & 1u) { ctl_chord_on = 1; ctl_chord_n = 0; }
    if (ctl_released >> BTN_ENV & 1u && ctl_chord_on) ctl_chord_commit(edit);

    b = ctl_binding(*screen, ctl_row[*screen]);
    for (k = 1; k < 8u; k++)                           /* K2..K8 */
        if ((e = fm1_enc_take(k)) != 0) {
            uint16_t id = b->enc[k - 1u];
            int fast = fm1_ms - ctl_enc_ms[k] < CTL_FAST_MS;
            ctl_enc_pos[k] += e;
            ctl_enc_ms[k] = fm1_ms;
            if (P_IS_LIVE(id)) ctl_set(edit, id, ctl_enc_value(id, edit->v[id], e, fast), 1);
            else if (B_IS_GLOBAL(id) && (id & 0xFFu) < G_COUNT) ctl_set(edit, id, ctl_enc_value(id, ctl_glob[id & 0xFFu], e, 0), 1);
            else if (B_IS_PROG(id)) ctl_prog_turn(id & 0xFFu, e);
        }
    for (k = 0; k < 4u; k++) {
        uint32_t pos = fm1_in.fader[k];
        if (pos != ctl_fpos[k]) {
            uint16_t id = b->fader[k];
            if (P_IS_LIVE(id))
                ctl_set(edit, id, ctl_fader_move(&ctl_fad[k], ctl_glob[G_POT_MODE], id, edit->v[id], ctl_fpos[k], pos), 1);
            ctl_fpos[k] = (uint8_t)pos;
        }
    }
    while ((e = smk_pad_take()) >= 0) {                /* top pads: one action per press; bottom pads: nothing */
        uint32_t pad = (uint32_t)e >> 8 & 15u, vel = (uint32_t)e & 0x7Fu;
        uint16_t x;
        if (pad >= 8u || !vel) continue;
        b = ctl_binding(*screen, ctl_row[*screen]);    /* (a pad may have changed the screen) */
        x = b->pad_top[pad];
        if (P_IS_LIVE(x)) ctl_set(edit, x, ctl_pad_value(x, edit->v[x]), 1);
        else if (B_IS_GLOBAL(x) && (x & 0xFFu) < G_COUNT) ctl_set(edit, x, ctl_pad_value(x, ctl_glob[x & 0xFFu]), 1);
        else if (B_IS_ACT(x)) ctl_action(screen, x & 0xFFu);
    }
    if (ctl_prog_want >= 0 && ctl_wr == 0) {          /* a load the engine could not take yet */
        if (ctl_want_factory) ctl_restore_factory(ctl_prog_want);
        else ctl_prog_select(ctl_prog_want);
    }
    ctl_write_poll(edit);

    {
        int at = ctl_glob[G_LOCAL] ? controls_aftertouch() : 0;   /* LOCAL OFF: the pads' pressure goes nowhere */
        eng_pressure(at > ctl_midi_at ? at : ctl_midi_at);
    }
    eng_tune(ctl_glob[G_TRANSPOSE] * 100 + ctl_glob[G_TUNE]);   /* the master transpose + tune (sent when it changes) */
    eng_hold(ctl_hold || ctl_sustain);
    eng_volume(ctl_midi_vol);                          /* MIDI CC 7 (sent every pass: a refused one is retried) */
    eng_clock_mode(ctl_glob[G_MIDI_CLOCK]);            /* the clock's mode (sent when it changes) */
    eng_arp_sync(ctl_glob[G_ARP_SYNC]);                /* ARP BEAT SYNC (sent when it changes) */
    ctl_leds(*screen, edit);
}
#endif
