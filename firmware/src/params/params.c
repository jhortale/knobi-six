/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX program model (see params.h). Included by app.c (unity build); also built alone by the host unit tests. */
#include "params.h"
#include <string.h>

static const char *const param_l_track[] = { "OFF", "HALF", "FULL" };
static const char *const param_l_shape[] = { "SINE", "SAW", "RSAW", "SQR", "RND" };
static const char *const param_l_pw[]    = { "OFF", "PW1", "PW2", "BOTH" };
static const char *const param_l_keymode[] = { "LOW", "LOW-R", "HIGH", "HIGH-R", "LAST", "LAST-R" };
static const char *const param_l_porta[] = { "RATE", "RATE-A", "TIME", "TIME-A" };
static const char *const param_l_uni[]   = { "1", "2", "3", "4", "5", "6", "CHD" };

/* the designated initialisers fix each entry's position to its enum value; the id is written too (unit test ids_dense). */
#define P_ID(i) .id = (i)
#define K(i, sec, nm, mn, mx, df)  [i] = { P_ID(i), sec, PK_KNOB,   1, 0, mn, mx, df, nm, 0 }
#define B(i, sec, nm, mn, mx, df)  [i] = { P_ID(i), sec, PK_KNOB,   1, 1, mn, mx, df, nm, 0 }
#define S(i, sec, nm, df)          [i] = { P_ID(i), sec, PK_SWITCH, 1, 0, 0, 1, df, nm, 0 }
#define L(i, sec, nm, mn, mx, df, lb) [i] = { P_ID(i), sec, PK_SELECT, 1, 0, mn, mx, df, nm, lb }

/* Choices where docs/ob6-reference.md is silent or contradictory:
 *  - bipolar knobs are stored signed -127..+127, 0 = centre (the reference gives 0..254 with 127 as centre; same
 *    resolution, and 0 is the natural default); VCO2 FINE is -50..+50 cents as the Task brief says.
 *  - VCO2 SHAPE 0..254 as the reference states (VCO1 SHAPE is 0..255).
 *  - UNISON VOICES 0..6: 0..5 = 1..6 voices, 6 = chord memory (the NRPN range; the manual text says 2-6, ambiguous).
 *  - LFO D_PW is a 4-state switch (none/PW1/PW2/both), so PK_SELECT 0..3; the other LFO destinations are PK_SWITCH.
 *  - defaults are a plain init patch: VCO1 saw at full level, filter open, loudness env sustained and open. */
const param_t PARAMS[P_COUNT_PHASE1] = {
    K(P_VCO1_FREQ, PS_VCO1, "VCO1 FREQ", 0, 60, 24),              /* semitone steps, 5 octaves */
    K(P_VCO1_SHAPE, PS_VCO1, "VCO1 SHAPE", 0, 255, 0),            /* saw .. pulse */
    K(P_VCO1_PW, PS_VCO1, "VCO1 PW", 0, 255, 127),                /* centre = square */
    S(P_VCO1_SYNC, PS_VCO1, "VCO1 SYNC", 0),
    K(P_VCO2_FREQ, PS_VCO2, "VCO2 FREQ", 0, 60, 24),
    B(P_VCO2_FINE, PS_VCO2, "VCO2 FINE", -50, 50, 0),             /* cents */
    K(P_VCO2_SHAPE, PS_VCO2, "VCO2 SHAPE", 0, 254, 0),            /* tri .. saw .. pulse */
    K(P_VCO2_PW, PS_VCO2, "VCO2 PW", 0, 255, 127),
    S(P_VCO2_LOWFREQ, PS_VCO2, "VCO2 LOW FREQ", 0),
    S(P_VCO2_KEYBOARD, PS_VCO2, "VCO2 KEYBOARD", 1),
    K(P_MIX_VCO1, PS_MIXER, "VCO1 LEVEL", 0, 127, 127),
    K(P_MIX_VCO2, PS_MIXER, "VCO2 LEVEL", 0, 127, 0),
    K(P_MIX_SUB, PS_MIXER, "SUB OCTAVE", 0, 127, 0),
    K(P_MIX_NOISE, PS_MIXER, "NOISE", 0, 127, 0),
    K(P_FLT_FREQ, PS_FILTER, "FILTER FREQ", 0, 164, 164),         /* 165 steps */
    K(P_FLT_RESO, PS_FILTER, "RESONANCE", 0, 255, 0),
    K(P_FLT_MODE, PS_FILTER, "FILTER MODE", 0, 127, 0),           /* LP .. notch .. HP */
    S(P_FLT_BP, PS_FILTER, "BP", 0),
    L(P_FLT_TRACK, PS_FILTER, "KEY TRACK", 0, 2, 0, param_l_track),
    B(P_FLT_ENV_AMT, PS_FILTER, "FENV AMOUNT", -127, 127, 0),
    S(P_FLT_VEL, PS_FILTER, "FENV VELOCITY", 0),
    K(P_FENV_A, PS_FENV, "FENV ATTACK", 0, 127, 0),
    K(P_FENV_D, PS_FENV, "FENV DECAY", 0, 127, 64),
    K(P_FENV_S, PS_FENV, "FENV SUSTAIN", 0, 127, 127),
    K(P_FENV_R, PS_FENV, "FENV RELEASE", 0, 127, 20),
    K(P_LENV_A, PS_LENV, "LENV ATTACK", 0, 127, 0),
    K(P_LENV_D, PS_LENV, "LENV DECAY", 0, 127, 64),
    K(P_LENV_S, PS_LENV, "LENV SUSTAIN", 0, 127, 127),
    K(P_LENV_R, PS_LENV, "LENV RELEASE", 0, 127, 20),
    K(P_LENV_AMT, PS_LENV, "LENV AMOUNT", 0, 127, 127),           /* unipolar */
    S(P_LENV_VEL, PS_LENV, "LENV VELOCITY", 0),
    K(P_LFO_FREQ, PS_LFO, "LFO FREQ", 0, 254, 100),
    S(P_LFO_SYNC, PS_LFO, "LFO SYNC", 0),
    L(P_LFO_SHAPE, PS_LFO, "LFO SHAPE", 0, 4, 0, param_l_shape),
    K(P_LFO_AMT, PS_LFO, "LFO INITIAL AMT", 0, 255, 0),
    S(P_LFO_D_VCO1, PS_LFO, "LFO>VCO1", 0),
    S(P_LFO_D_VCO2, PS_LFO, "LFO>VCO2", 0),
    L(P_LFO_D_PW, PS_LFO, "LFO>PW", 0, 3, 0, param_l_pw),
    S(P_LFO_D_AMP, PS_LFO, "LFO>AMP", 0),
    S(P_LFO_D_FFREQ, PS_LFO, "LFO>FILTER FREQ", 0),
    S(P_LFO_D_FMODE, PS_LFO, "LFO>FILTER MODE", 0),
    B(P_XM_FENV, PS_XMOD, "XMOD FILTER ENV", -127, 127, 0),
    B(P_XM_VCO2, PS_XMOD, "XMOD VCO2", -127, 127, 0),
    S(P_XM_D_VCO1, PS_XMOD, "XMOD>VCO1", 0),
    S(P_XM_D_SHAPE1, PS_XMOD, "XMOD>SHAPE1", 0),
    S(P_XM_D_PW1, PS_XMOD, "XMOD>PW1", 0),
    S(P_XM_D_FFREQ, PS_XMOD, "XMOD>FILTER FREQ", 0),
    S(P_XM_D_FMODE, PS_XMOD, "XMOD>FILTER MODE", 0),
    S(P_XM_D_BP, PS_XMOD, "XMOD>NORM-BP", 0),
    B(P_AT_AMT, PS_AFTERTOUCH, "AT AMOUNT", -127, 127, 0),
    S(P_AT_D_VCO1, PS_AFTERTOUCH, "AT>VCO1", 0),
    S(P_AT_D_VCO2, PS_AFTERTOUCH, "AT>VCO2", 0),
    S(P_AT_D_LFOAMT, PS_AFTERTOUCH, "AT>LFO AMT", 0),
    S(P_AT_D_AMP, PS_AFTERTOUCH, "AT>LOUDNESS", 0),
    S(P_AT_D_FFREQ, PS_AFTERTOUCH, "AT>FILTER FREQ", 0),
    S(P_AT_D_FMODE, PS_AFTERTOUCH, "AT>FILTER MODE", 0),
    K(P_DETUNE, PS_MISC, "DETUNE", 0, 127, 0),
    K(P_PAN_SPREAD, PS_MISC, "PAN SPREAD", 0, 127, 0),
    K(P_BEND_RANGE, PS_MISC, "BEND RANGE", 0, 12, 2),
    L(P_KEY_MODE, PS_MISC, "KEY MODE", 0, 5, 4, param_l_keymode),   /* unison only; default LAST */
    K(P_PRG_VOL, PS_MISC, "PROGRAM VOLUME", 0, 127, 100),
    S(P_PORTA_ON, PS_MISC, "PORTAMENTO", 0),
    L(P_PORTA_MODE, PS_MISC, "PORTA MODE", 0, 3, 0, param_l_porta),
    K(P_PORTA_RATE, PS_MISC, "PORTA RATE", 0, 127, 0),
    S(P_UNISON_ON, PS_MISC, "UNISON", 0),
    L(P_UNISON_VOICES, PS_MISC, "UNISON VOICES", 0, 6, 5, param_l_uni),
};

#undef K
#undef B
#undef S
#undef L

int16_t param_clamp(uint16_t id, int32_t value)
{
    if (id >= P_COUNT_PHASE1) return 0;
    if (value < PARAMS[id].min) return PARAMS[id].min;
    if (value > PARAMS[id].max) return PARAMS[id].max;
    return (int16_t)value;
}

void program_init(program_t *p)
{
    memset(p, 0, sizeof *p);
    p->version = PROGRAM_VERSION;
    memcpy(p->name, "INIT", 4);
    for (unsigned i = 0; i < P_COUNT_PHASE1; i++) p->v[i] = PARAMS[i].def;
}

uint16_t program_crc16(const uint8_t *d, uint32_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) {
        c ^= (uint16_t)(*d++) << 8;
        for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

/* Record versions. Entry [v-1] = how many parameter values a version-v record stores (ids 0..n-1). A later phase adds
 * ONE entry here (e.g. { P_COUNT_PHASE2 }) and bumps PROGRAM_VERSION; older records then load with the defaults for the
 * ids they lack. Tests may predefine PROGRAM_VERSION_TABLE. */
#ifndef PROGRAM_VERSION_TABLE
#define PROGRAM_VERSION_TABLE { P_COUNT_PHASE1 }
#endif
static const uint16_t program_nparams[PROGRAM_VERSION] = PROGRAM_VERSION_TABLE;
#define PROGRAM_NCOUNT P_COUNT_PHASE1   /* ids with a PARAMS[] entry in this build: all of them are clamped on load */

uint32_t program_packed_size(unsigned version)
{
    if (version < 1 || version > PROGRAM_VERSION) return 0;
    return 1 + PROGRAM_NAME_LEN + 2u * program_nparams[version - 1] + 6 + 2;
}

/* name: 20 bytes, NUL-padded; a full 20-character name has no terminator, so consumers must bound reads to 20.
 * packed: version (always the current one), name[20], v[nparams of that version] as LE int16, chord[6], crc16 (LE) of
 * everything before it. */
int program_pack(const program_t *p, uint8_t *buf, uint32_t max)
{
    uint32_t o = 0, size = program_packed_size(PROGRAM_VERSION);
    if (max < size) return -1;
    buf[o++] = PROGRAM_VERSION;
    memcpy(buf + o, p->name, PROGRAM_NAME_LEN); o += PROGRAM_NAME_LEN;
    for (unsigned i = 0; i < program_nparams[PROGRAM_VERSION - 1]; i++) {
        buf[o++] = (uint8_t)((uint16_t)p->v[i] & 0xFF);
        buf[o++] = (uint8_t)((uint16_t)p->v[i] >> 8);
    }
    memcpy(buf + o, p->chord, 6); o += 6;
    uint16_t c = program_crc16(buf, o);
    buf[o++] = (uint8_t)(c & 0xFF);
    buf[o++] = (uint8_t)(c >> 8);
    return (int)o;
}

/* Accepts any known version 1..PROGRAM_VERSION whose length and CRC match; unknown/future versions, wrong lengths, bad
 * CRC and a chord note > 127 are rejected (-1, *p untouched). Ids the stored version lacks get their PARAMS default; all
 * loaded values are clamped; the saved crc field is kept in p->crc. */
int program_unpack(program_t *p, const uint8_t *buf, uint32_t n)
{
    uint32_t o = 0;
    unsigned ver;
    program_t t;
    if (n < 1) return -1;
    ver = buf[0];
    if (n != program_packed_size(ver)) return -1;
    uint16_t c = (uint16_t)(buf[n - 2] | (buf[n - 1] << 8));
    if (c != program_crc16(buf, n - 2)) return -1;
    program_init(&t);                              /* defaults for everything the record does not carry */
    o = 1;
    memcpy(t.name, buf + o, PROGRAM_NAME_LEN); o += PROGRAM_NAME_LEN;
    for (unsigned i = 0; i < program_nparams[ver - 1]; i++) {
        t.v[i] = (int16_t)(uint16_t)(buf[o] | (buf[o + 1] << 8)); o += 2;
    }
    for (unsigned i = 0; i < PROGRAM_NCOUNT; i++) t.v[i] = param_clamp((uint16_t)i, t.v[i]);
    for (int i = 0; i < 6; i++) { if (buf[o + i] > 127) return -1; }
    memcpy(t.chord, buf + o, 6);
    t.version = PROGRAM_VERSION;                   /* in memory it is a current-version program */
    t.crc = c;
    *p = t;
    return 0;
}
