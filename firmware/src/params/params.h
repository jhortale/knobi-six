/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX program model: the OB-6 parameter schema as one table, and the program record.
 * Ranges come from docs/ob6-reference.md section 2 (see the comment next to each entry for any choice made where the
 * reference is silent or contradicts itself). Integers only; no pointers in program_t. */
#ifndef KNOBI_PARAMS_H
#define KNOBI_PARAMS_H
#include <stdint.h>

enum param_kind { PK_KNOB, PK_SWITCH, PK_SELECT };

enum param_section {
    PS_VCO1, PS_VCO2, PS_MIXER, PS_FILTER, PS_FENV, PS_LENV, PS_LFO, PS_XMOD, PS_AFTERTOUCH, PS_MISC, PS_COUNT
};

typedef struct {
    uint16_t id;
    uint8_t section, kind, phase, bipolar;
    int16_t min, max, def;
    const char *name;
    const char *const *labels;   /* PK_SELECT: labels[v - min], else 0 */
} param_t;

enum param_id {
    P_VCO1_FREQ, P_VCO1_SHAPE, P_VCO1_PW, P_VCO1_SYNC, P_VCO2_FREQ, P_VCO2_FINE, P_VCO2_SHAPE, P_VCO2_PW, P_VCO2_LOWFREQ, P_VCO2_KEYBOARD,
    P_MIX_VCO1, P_MIX_VCO2, P_MIX_SUB, P_MIX_NOISE, P_FLT_FREQ, P_FLT_RESO, P_FLT_MODE, P_FLT_BP, P_FLT_TRACK, P_FLT_ENV_AMT, P_FLT_VEL,
    P_FENV_A, P_FENV_D, P_FENV_S, P_FENV_R, P_LENV_A, P_LENV_D, P_LENV_S, P_LENV_R, P_LENV_AMT, P_LENV_VEL,
    P_LFO_FREQ, P_LFO_SYNC, P_LFO_SHAPE, P_LFO_AMT, P_LFO_D_VCO1, P_LFO_D_VCO2, P_LFO_D_PW, P_LFO_D_AMP, P_LFO_D_FFREQ, P_LFO_D_FMODE,
    P_XM_FENV, P_XM_VCO2, P_XM_D_VCO1, P_XM_D_SHAPE1, P_XM_D_PW1, P_XM_D_FFREQ, P_XM_D_FMODE, P_XM_D_BP,
    P_AT_AMT, P_AT_D_VCO1, P_AT_D_VCO2, P_AT_D_LFOAMT, P_AT_D_AMP, P_AT_D_FFREQ, P_AT_D_FMODE,
    P_DETUNE, P_PAN_SPREAD, P_BEND_RANGE, P_KEY_MODE, P_PRG_VOL, P_PORTA_ON, P_PORTA_MODE, P_PORTA_RATE, P_UNISON_ON, P_UNISON_VOICES,
    /* phase 2/3 ids reserved after this line: distortion, FX A/B, clock, arp, sequencer */
    P_COUNT_PHASE1
};

extern const param_t PARAMS[P_COUNT_PHASE1];

#define PROGRAM_VERSION 1
#define PROGRAM_NAME_LEN 20
#define PROGRAM_VALUES 96                 /* sized for phases 2-3 */
#define PROGRAM_PACKED_SIZE (1 + PROGRAM_NAME_LEN + 2 * PROGRAM_VALUES + 6 + 2)

typedef struct {
    uint8_t version;
    char name[PROGRAM_NAME_LEN];
    int16_t v[PROGRAM_VALUES];
    uint8_t chord[6];
    uint16_t crc;
} program_t;

void program_init(program_t *p);                                    /* every value = its default, name "INIT" */
int  program_pack(const program_t *p, uint8_t *buf, uint32_t max);  /* bytes written or -1 */
int  program_unpack(program_t *p, const uint8_t *buf, uint32_t n);  /* 0 ok, -1 bad crc/version/size */
int16_t param_clamp(uint16_t id, int32_t value);
uint16_t program_crc16(const uint8_t *d, uint32_t n);               /* CRC-16/CCITT-FALSE */

#endif
