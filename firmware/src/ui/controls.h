/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX panel and MIDI mapping: which control edits what on each screen (docs/CONTROLS.md), the faders' pot modes,
 * the pads (top row switches, bottom row pressure = aftertouch), the buttons, chord memory capture, and the MIDI CC
 * map in and out (midi_map.c, docs/MIDI.md). Every change to the sound goes through engine.c's queue (eng_*); this
 * code runs in the main loop only. Integers only. */
#ifndef KNOBI_CONTROLS_H
#define KNOBI_CONTROLS_H
#include <stdint.h>
#include "../params/params.h"
#include "../synth/voices.h"

enum screen_id { SCR_BOOT, SCR_PLAY, SCR_VCO, SCR_MIX_FILTER, SCR_ENV, SCR_LFO_XMOD, SCR_AT_MISC, SCR_PROGRAM, SCR_GLOBALS,
                 SCR_COUNT };

/* a binding: a param id (< P_COUNT_PHASE1), a global (B_GLOBAL), an action (B_ACT, top pads only) or B_NONE */
#define B_NONE 0xFFFFu
#define B_GLOBAL(g) (0x4000u | (g))
#define B_ACT(a) (0x8000u | (a))
#define B_IS_GLOBAL(x) (((x) & 0xFF00u) == 0x4000u)
#define B_IS_ACT(x) (((x) & 0xFF00u) == 0x8000u)
enum { ACT_PREV, ACT_NEXT, ACT_ROW, ACT_HOLD, ACT_ARP, ACT_COUNT };   /* section back / on, the other row, HOLD, ARP
                                                                        (phase 2: does nothing yet) */
/* globals edited here (Task 10 stores them with the others): MIDI channel 0 = ALL, 1..16; pot mode; PARAM TX / RX
 * 0 OFF, 1 CC (NRPN later) */
enum { G_MIDI_CH, G_POT_MODE, G_PARAM_TX, G_PARAM_RX, G_COUNT };
enum { POT_RELATIVE, POT_PASSTHRU, POT_JUMP };

typedef struct { uint16_t enc[7]; uint16_t fader[4]; uint16_t pad_top[8]; } binding_t;  /* param ids or 0xFFFF; pad_top may be actions */
extern const binding_t BINDINGS[SCR_COUNT];
extern const binding_t BINDINGS_ROW2[SCR_COUNT];   /* the second row of a screen with SCR_ROWS 2 (SCALE switches) */
extern const uint8_t SCR_ROWS[SCR_COUNT];
extern const char *const SCR_NAME[SCR_COUNT];
const binding_t *ctl_binding(int screen, int row);
const char *ctl_row_name(int screen, int row);       /* a screen with two rows: the row's name, else the screen's */

/* the pure parts (unit-tested without hardware) */
int32_t ctl_from7(uint16_t id, uint32_t x);         /* 0..127 -> the param's range (bipolar: 64 = 0) */
uint32_t ctl_to7(uint16_t id, int32_t v);           /* the inverse, 0..127 */
int32_t ctl_enc_value(uint16_t id, int32_t cur, int32_t det, int fast);   /* a param or global after det detents */
int32_t ctl_pad_value(uint16_t id, int32_t cur);    /* a top pad pressed: a switch toggles, a select cycles */
int ctl_pad_at(const uint8_t *press);               /* the highest pressure of pads 8..15 */
typedef struct { uint16_t id; int16_t last_v, anchor_v; uint8_t anchor_p, caught, mode; } ctl_fader_t;
void ctl_fader_reset(ctl_fader_t *f);
int32_t ctl_fader_move(ctl_fader_t *f, int mode, uint16_t id, int32_t cur, uint32_t from, uint32_t to);

/* the main loop's (not with CONTROLS_PURE) */
void controls_init(void);                            /* after eng_init: pad events on, the faders' positions */
void controls_poll(synth_t *s, program_t *edit, int *screen);   /* main loop: encoders K2..K8, faders, pads, buttons */
int  controls_aftertouch(void);                                /* highest bottom-row pressure 0..127 */
void controls_key(int note);                         /* a note played (keys, MIDI): chord memory capture */
void controls_midi(uint32_t st, uint32_t d1, uint32_t d2);   /* one MIDI message in (midi_map.c) */

#endif
