/* SPDX-License-Identifier: GPL-3.0-only */
/* The panel and MIDI mapping's pure parts (firmware/src/ui/controls.c, midi_map.c with CONTROLS_PURE: no hardware, no
 * engine): the bindings, encoder steps, pad switches, the three pot modes, the pad aftertouch split and the CC map.
 * The whole path (pads, faders, MIDI through the main loop and the queue) is in tests/scripts/pads_split.txt,
 * faders_potmode.txt and midi_cc.txt. */
#include <stdio.h>
#include <stdlib.h>
#include "params/params.c"
#define CONTROLS_PURE 1
#include "ui/controls.c"
#include "ui/midi_map.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int is_param(uint16_t b) { return b < P_COUNT_PHASE1; }

/* every binding is a param, a global, an action or nothing; faders only take knobs; top pads only switches, selects
 * and actions; at most 7 encoders (the struct) and every phase-1 param reachable from some screen */
static void bindings_valid(void)
{
    uint8_t seen[P_COUNT_PHASE1] = {0};
    int bad = 0;
    for (int s = 0; s < SCR_COUNT; s++)
        for (int r = 0; r < SCR_ROWS[s]; r++) {
            const binding_t *b = ctl_binding(s, r);
            for (int i = 0; i < 7; i++) {
                uint16_t x = b->enc[i];
                if (is_param(x)) seen[x] = 1;
                else if (x != B_NONE && !(B_IS_GLOBAL(x) && (x & 0xFFu) < G_COUNT)) bad++, printf("  enc %d.%d.%d %x\n", s, r, i, x);
            }
            for (int i = 0; i < 4; i++) {
                uint16_t x = b->fader[i];
                if (is_param(x) && PARAMS[x].kind == PK_KNOB) seen[x] = 1;
                else if (x != B_NONE) bad++, printf("  fader %d.%d.%d %x\n", s, r, i, x);
            }
            for (int i = 0; i < 8; i++) {
                uint16_t x = b->pad_top[i];
                if (is_param(x) && PARAMS[x].kind != PK_KNOB) seen[x] = 1;
                else if (x != B_NONE && !(B_IS_ACT(x) && (x & 0xFFu) < ACT_COUNT)) bad++, printf("  pad %d.%d.%d %x\n", s, r, i, x);
            }
        }
    int missing = 0;
    for (int i = 0; i < P_COUNT_PHASE1; i++) if (!seen[i]) missing++, printf("  unreachable: %s\n", PARAMS[i].name);
    /* the brief's pads_split: on LFO + X-MOD (first row) top pad 2 is LFO > VCO1 */
    int lfo_pad2 = ctl_binding(SCR_LFO_XMOD, 0)->pad_top[2] == P_LFO_D_VCO1;
    /* Play's faders: FREQUENCY, RESONANCE, MODE, ENV AMOUNT (spec) */
    const binding_t *pl = ctl_binding(SCR_PLAY, 0);
    int play_faders = pl->fader[0] == P_FLT_FREQ && pl->fader[1] == P_FLT_RESO && pl->fader[2] == P_FLT_MODE && pl->fader[3] == P_FLT_ENV_AMT;
    printf("bindings_valid: %d bad bindings, %d params unreachable, LFO pad 2 = LFO>VCO1 %d, Play faders %d\n", bad, missing,
           lfo_pad2, play_faders);
    CHECK(bad == 0 && missing == 0 && lfo_pad2 && play_faders);
}

/* encoders: a knob steps (range + 127) / 128 a detent, x4 when turned fast; switches go on (+) / off (-); selects step
 * by one; everything clamps (no wrap) */
static void encoder_steps(void)
{
    int k255 = ctl_enc_value(P_FLT_RESO, 100, 1, 0), k255f = ctl_enc_value(P_FLT_RESO, 100, 1, 1);
    int k60 = ctl_enc_value(P_VCO1_FREQ, 24, -3, 0), top = ctl_enc_value(P_FLT_RESO, 254, 5, 1);
    int bot = ctl_enc_value(P_FLT_ENV_AMT, -126, -4, 0);
    int sw_on = ctl_enc_value(P_VCO1_SYNC, 0, 1, 0), sw_off = ctl_enc_value(P_VCO1_SYNC, 1, -2, 0), sw_stay = ctl_enc_value(P_VCO1_SYNC, 1, 3, 1);
    int sel = ctl_enc_value(P_LFO_SHAPE, 3, 4, 1), sel0 = ctl_enc_value(P_LFO_SHAPE, 1, -1, 0);
    int gpot = ctl_enc_value(B_GLOBAL(G_POT_MODE), 2, 1, 0), gch = ctl_enc_value(B_GLOBAL(G_MIDI_CH), 0, 20, 1);
    printf("encoder_steps: RESO 100 +1 -> %d (fast %d), VCO1 FREQ 24 -3 -> %d, RESO 254 +5 -> %d, ENV AMT -126 -4 -> %d; "
           "SYNC %d %d %d; SHAPE %d %d; POT MODE 2 +1 -> %d, MIDI CH -> %d\n", k255, k255f, k60, top, bot, sw_on, sw_off,
           sw_stay, sel, sel0, gpot, gch);
    CHECK(k255 == 102 && k255f == 108 && k60 == 21 && top == 255 && bot == -127);
    CHECK(sw_on == 1 && sw_off == 0 && sw_stay == 1 && sel == 4 && sel0 == 0 && gpot == 2 && gch == 16);
}

/* a top pad: a switch toggles, a select cycles (wrapping) */
static void pad_switches(void)
{
    int t1 = ctl_pad_value(P_LFO_D_VCO1, 0), t0 = ctl_pad_value(P_LFO_D_VCO1, 1);
    int c = ctl_pad_value(P_LFO_D_PW, 2), cw = ctl_pad_value(P_LFO_D_PW, 3), tr = ctl_pad_value(P_FLT_TRACK, 2);
    printf("pad_switches: LFO>VCO1 0 -> %d, 1 -> %d; LFO>PW 2 -> %d, 3 -> %d; KEY TRACK 2 -> %d\n", t1, t0, c, cw, tr);
    CHECK(t1 == 1 && t0 == 0 && c == 3 && cw == 0 && tr == 0);
}

/* the fader 0..127 <-> a param's range: the ends are the range's ends, 64 is a bipolar param's 0; the 7-bit value
 * survives a round trip on ranges of 128 values or more, the param value on smaller ones */
static void seven_bit_map(void)
{
    int bad = 0;
    for (uint16_t id = 0; id < P_COUNT_PHASE1; id++) {
        int range = PARAMS[id].max - PARAMS[id].min;
        if (ctl_from7(id, 0) != PARAMS[id].min || ctl_from7(id, 127) != PARAMS[id].max) bad++, printf("  ends %s\n", PARAMS[id].name);
        if (PARAMS[id].bipolar && ctl_from7(id, 64) != 0) bad++, printf("  centre %s\n", PARAMS[id].name);
        if (range >= 127) {
            for (uint32_t x = 0; x < 128; x++) if (ctl_to7(id, ctl_from7(id, x)) != x) { bad++; printf("  7-bit %s %u\n", PARAMS[id].name, x); break; }
        } else {
            for (int v = PARAMS[id].min; v <= PARAMS[id].max; v++) if (ctl_from7(id, ctl_to7(id, v)) != v) { bad++; printf("  value %s %d\n", PARAMS[id].name, v); break; }
        }
        for (int x = 1; x < 128; x++) if (ctl_from7(id, (uint32_t)x) < ctl_from7(id, (uint32_t)x - 1u)) { bad++; printf("  monotonic %s\n", PARAMS[id].name); break; }
    }
    printf("seven_bit_map: %d failures; FILTER FREQ 64 -> %d, ENV AMT 0/64/127 -> %d/%d/%d\n", bad, ctl_from7(P_FLT_FREQ, 64),
           ctl_from7(P_FLT_ENV_AMT, 0), ctl_from7(P_FLT_ENV_AMT, 64), ctl_from7(P_FLT_ENV_AMT, 127));
    CHECK(bad == 0);
}

/* pot modes (OB-6 globals): JUMP takes the fader's value at once; PASSTHRU changes nothing until the fader crosses the
 * stored value, then follows; RELATIVE scales the move between the stored value and the travel's end (the fader's
 * top = the range's top), no step lost however small the moves. A value changed elsewhere re-anchors. */
static void pot_modes(void)
{
    ctl_fader_t f = {0};
    ctl_fader_reset(&f);
    int16_t cur = 164;                                   /* FILTER FREQ at its init 164; the fader at 10 */
    int j = ctl_fader_move(&f, POT_JUMP, P_FLT_FREQ, cur, 0, 10);
    ctl_fader_reset(&f);
    int p1 = ctl_fader_move(&f, POT_PASSTHRU, P_FLT_FREQ, cur, 0, 10);
    int p2 = ctl_fader_move(&f, POT_PASSTHRU, P_FLT_FREQ, p1, 10, 100);
    int p3 = ctl_fader_move(&f, POT_PASSTHRU, P_FLT_FREQ, p2, 100, 127);   /* reaches 164: caught */
    int p4 = ctl_fader_move(&f, POT_PASSTHRU, P_FLT_FREQ, p3, 127, 64);    /* follows */
    /* passthru from below: RESO stored 128, fader from 20 up */
    ctl_fader_reset(&f);
    int q1 = ctl_fader_move(&f, POT_PASSTHRU, P_FLT_RESO, 128, 20, 60);
    int q2 = ctl_fader_move(&f, POT_PASSTHRU, P_FLT_RESO, q1, 60, 70);      /* 60 -> 70 maps 120 -> 141: crosses 128 */
    /* relative: RESO 100, fader at 50: up by single steps to 127 ends at 255; then down to 0 ends at 0 */
    ctl_fader_reset(&f);
    int r = 100, steps_moved = 0;
    for (uint32_t x = 50; x < 127; x++) { int n = ctl_fader_move(&f, POT_RELATIVE, P_FLT_RESO, r, x, x + 1u); steps_moved += n != r; r = n; }
    int r_top = r;
    int r_mid = ctl_fader_move(&f, POT_RELATIVE, P_FLT_RESO, r, 127, 50);   /* back to the anchor: back to 100 */
    for (uint32_t x = 50; x > 0; x--) r_mid = ctl_fader_move(&f, POT_RELATIVE, P_FLT_RESO, r_mid, x, x - 1u);
    int r_bot = r_mid;
    /* a value changed elsewhere (an encoder) re-anchors: relative goes on from the new value */
    ctl_fader_reset(&f);
    int a1 = ctl_fader_move(&f, POT_RELATIVE, P_FLT_RESO, 0, 0, 64);
    int a2 = ctl_fader_move(&f, POT_RELATIVE, P_FLT_RESO, 200, 64, 127);   /* someone set 200 meanwhile */
    printf("pot_modes: jump 10 -> %d; passthru 164: %d %d %d then follows %d; from below 128: %d then %d; relative 100 "
           "up to %d (%d of 77 steps moved), back to the anchor and down to %d; re-anchored %d -> %d\n", j, p1, p2, p3, p4,
           q1, q2, r_top, steps_moved, r_bot, a1, a2);
    CHECK(j == ctl_from7(P_FLT_FREQ, 10) && p1 == 164 && p2 == 164 && p3 == 164 && p4 == ctl_from7(P_FLT_FREQ, 64));
    CHECK(q1 == 128 && q2 == ctl_from7(P_FLT_RESO, 70));
    CHECK(r_top == 255 && steps_moved >= 70 && r_bot == 0);
    CHECK(a1 == 128 && a2 == 255);
}

/* aftertouch from the pads: the highest pressure on the bottom row (8..15) only; the top row never counts */
static void pad_aftertouch(void)
{
    uint8_t p[16] = {0};
    p[2] = 127; p[5] = 90;                               /* top pads pressed hard */
    int top_only = ctl_pad_at(p);
    p[10] = 100; p[13] = 40;
    int both = ctl_pad_at(p);
    p[15] = 101;
    int hi = ctl_pad_at(p);
    printf("pad_aftertouch: top row only %d, bottom 100 + 40 (top 127) %d, + 101 -> %d\n", top_only, both, hi);
    CHECK(top_only == 0 && both == 100 && hi == 101);
}

/* the CC map: one CC per continuous (knob) param, none for switches and selects, every CC used once, none of the
 * CCs MIDI gives a meaning (bank 0/32, mod 1, foot 4, data 6/38, volume 7, pan 10, expression 11, sustain 64, 96..101,
 * channel mode 120..127); midi_cc_param is the inverse */
static void cc_map(void)
{
    static const uint8_t RESERVED[] = {0, 1, 4, 6, 7, 10, 11, 32, 38, 64, 96, 97, 98, 99, 100, 101, 120, 121, 122, 123, 124, 125, 126, 127};
    uint8_t used[128] = {0};
    int bad = 0, knobs = 0;
    midi_map_init();
    for (uint16_t id = 0; id < P_COUNT_PHASE1; id++) {
        uint8_t cc = MIDI_CC[id];
        if (PARAMS[id].kind == PK_KNOB) {
            knobs++;
            if (cc > 127) { bad++; printf("  no CC: %s\n", PARAMS[id].name); continue; }
            if (used[cc]++) bad++, printf("  CC %u twice\n", cc);
            for (unsigned r = 0; r < sizeof RESERVED; r++) if (cc == RESERVED[r]) bad++, printf("  reserved CC %u\n", cc);
            if (midi_cc_param(cc) != id) bad++, printf("  inverse %u\n", cc);
        } else if (cc != MIDI_CC_NONE) bad++, printf("  CC on a switch: %s\n", PARAMS[id].name);
    }
    int unmapped = midi_cc_param(3) == -1 && midi_cc_param(64) == -1;
    printf("cc_map: %d knobs mapped, %d failures; FILTER FREQ = CC %u, RESONANCE = CC %u\n", knobs, bad, MIDI_CC[P_FLT_FREQ],
           MIDI_CC[P_FLT_RESO]);
    CHECK(bad == 0 && unmapped && MIDI_CC[P_FLT_FREQ] == 74 && MIDI_CC[P_FLT_RESO] == 71);
}

int main(void)
{
    bindings_valid();
    encoder_steps();
    pad_switches();
    seven_bit_map();
    pot_modes();
    pad_aftertouch();
    cc_map();
    if (fails) { printf("test_controls: %d FAILED\n", fails); return 1; }
    printf("test_controls: all passed\n");
    return 0;
}
