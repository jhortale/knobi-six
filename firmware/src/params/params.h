/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX program model: the OB-6 parameter schema as one table, and the program record.
 * Ranges come from docs/ob6-reference.md section 2 (see the comment next to each entry for any choice made where the
 * reference is silent or contradicts itself). Integers only; no pointers in program_t. */
#ifndef KNOBI_PARAMS_H
#define KNOBI_PARAMS_H
#include <stdint.h>

enum param_kind { PK_KNOB, PK_SWITCH, PK_SELECT };

enum param_section {
    PS_VCO1, PS_VCO2, PS_MIXER, PS_FILTER, PS_FENV, PS_LENV, PS_LFO, PS_XMOD, PS_AFTERTOUCH, PS_MISC, PS_CLOCK, PS_ARP, PS_COUNT
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
    P_COUNT_PHASE1,
    /* Phase 2/3 values, reserved: a program stores and carries them (record version 2); nothing reads them (no
     * PARAMS entry, no screen, no sound) until a task makes one live (P_IS_LIVE below: P_BPM and P_CLK_DIV, Task 5;
     * P_ARP_*, Task 6). Units are the OB-6's own (docs/FACTORY_BANK.md, tools/ob6_import.py):
     * FX types as the OB-6 lists them (A 0..9: off bbd ddl cho FL1 FL2 PH1 PH2 PH3 rin; B 0..13: those, then HAL rOO
     * PLA SPr), P1 0..255, P2 / mix 0..127, the delay sync division 0..10 (1 2d 2 4d 4 4t 8d 8 8t 16d 16), the clock
     * division 0..9 (half qtr 8th 8thD 8thS 8thT 16th 16thS 16thT 32nd), arp mode 0..4 (up down up+down random
     * assign), arp range 0..2 (1..3 octaves). The sequencer (64 steps x 6 notes) does not fit a program record. */
    P_DISTORTION = P_COUNT_PHASE1, P_FX_ON,
    P_FXA_TYPE, P_FXA_MIX, P_FXA_P1, P_FXA_P2, P_FXA_SYNC, P_FXA_DIV,
    P_FXB_TYPE, P_FXB_MIX, P_FXB_P1, P_FXB_P2, P_FXB_SYNC, P_FXB_DIV,
    P_BPM, P_CLK_DIV, P_ARP_ON, P_ARP_MODE, P_ARP_RANGE, P_ARP_HOLD,
    P_COUNT_RESERVED
};
#define P_RESERVED_N (P_COUNT_RESERVED - P_COUNT_PHASE1)
/* the reserved ids' ranges and defaults (params.c): { min, max, def } */
extern const int16_t PARAMS_RESERVED[P_RESERVED_N][3];

extern const param_t PARAMS[P_COUNT_PHASE1];

/* The live ids: what the synth, the clock and the panel use. Phase 1's (PARAMS) and the reserved ids a later task made
 * live, each with an entry of its own (PARAMS_LIVE2; its range and default the same as PARAMS_RESERVED's, so a record
 * of version 2 carries them already, no new record version): P_BPM 30..250 (default 120), P_CLK_DIV the 10 divisions
 * (default QTR): the clock (Task 5, synth/clock.h); P_ARP_ON (switch, default off), P_ARP_MODE (UP DOWN UP+DN RANDOM
 * ASSIGN, default UP), P_ARP_RANGE (1..3 octaves, default 1), P_ARP_HOLD (switch, default off): the arpeggiator (Task 6,
 * synth/arp.h; a version-1 record loads them at their defaults: the arp off); P_DISTORTION (knob 0..127, default 0 =
 * bypass, MISC: the reference's 2.9; Task 7, synth/voices.h; a version-1 record loads it at 0) with an entry of its
 * own (PARAM_DISTORTION). Everything that takes "a param" (the queue, the panel, the screens) takes a live id; the other
 * reserved ids (the effects) are only stored. The live reserved ids: P_DISTORTION, and the run P_BPM .. P_ARP_HOLD. */
#define P_IS_LIVE(id) ((id) < P_COUNT_PHASE1 || (id) == P_DISTORTION || ((id) >= P_BPM && (id) <= P_ARP_HOLD))
#define P_LIVE2_N (P_ARP_HOLD - P_BPM + 1)
extern const param_t PARAMS_LIVE2[P_LIVE2_N];
extern const param_t PARAM_DISTORTION;
const param_t *param_get(uint16_t id);                     /* a live id's entry, else 0 */

#ifndef PROGRAM_VERSION
#define PROGRAM_VERSION 2                 /* current record version; bump together with a PROGRAM_VERSION_TABLE entry
                                           * (1: the phase-1 values; 2: + the reserved phase-2/3 values) */
#endif
#define PROGRAM_NAME_LEN 20
#define PROGRAM_VALUES 96                 /* sized for phases 2-3 */
#define PROGRAM_PACKED_MAX (1 + PROGRAM_NAME_LEN + 2 * PROGRAM_VALUES + 6 + 2)   /* upper bound for any version */

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
int16_t param_clamp(uint16_t id, int32_t value);           /* a reserved id clamps to PARAMS_RESERVED */
/* bytes of a packed record of this version (version, name, v[nparams] LE, chord, crc); 0 if the version is unknown */
uint32_t program_packed_size(unsigned version);
uint16_t program_crc16(const uint8_t *d, uint32_t n);               /* CRC-16/CCITT-FALSE */

#endif
