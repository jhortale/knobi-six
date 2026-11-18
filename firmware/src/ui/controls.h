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
#include "../params/globals.h"

enum screen_id { SCR_BOOT, SCR_PLAY, SCR_VCO, SCR_MIX_FILTER, SCR_ENV, SCR_LFO_XMOD, SCR_AT_MISC, SCR_CLOCK, SCR_SEQ,
                 SCR_PROGRAM, SCR_GLOBALS, SCR_COUNT };   /* SCR_SEQ: the Sequencer (Phase 3 Task 5; ids from 8 on moved
                                                 * up by one then) */

/* a binding: a live param id (params.h P_IS_LIVE), a global (B_GLOBAL, params/globals.h G_*), a program digit (B_PROG,
 * encoders only), an action (B_ACT, top pads only) or B_NONE */
#define B_NONE 0xFFFFu
#define B_PROG(d) (0x2000u | (d))
#define B_GLOBAL(g) (0x4000u | (g))
#define B_ACT(a) (0x8000u | (a))
#define B_IS_PROG(x) (((x) & 0xFF00u) == 0x2000u)
#define B_IS_GLOBAL(x) (((x) & 0xFF00u) == 0x4000u)
#define B_IS_ACT(x) (((x) & 0xFF00u) == 0x8000u)
enum { ACT_PREV, ACT_NEXT, ACT_ROW, ACT_HOLD, ACT_WRITE, ACT_ENTER, ACT_MANUAL, ACT_TAP, ACT_SEQPLAY, ACT_SEQREC,
       ACT_COUNT };
/* section back / on, the other row, HOLD, WRITE (arm / cancel), ENTER (commit the armed WRITE), MANUAL (the live
 * panel), TAP (tap tempo: the program's BPM from the 4th tap; synth/clock.h), SEQ PLAY and SEQ REC (the Sequencer
 * screen's pads: as the buttons, SEQ REC acting at once). ARP ON is a param pad (P_ARP_ON) */
enum { PRG_BANK, PRG_TENS, PRG_ONES, PRG_COUNT };
/* a notice in the header (ctl_last_id = B_NOTICE, the value one of these): none, a broken stored program loaded as
 * init ("Err"), restore factory asked ("SurE": GLOBE + BT again within 2 s), factory restored ("FAC"), the stored
 * sequence missing ("Err") */
enum { NT_NONE, NT_BROKEN, NT_RESTORE_ASK, NT_RESTORED, NT_SEQ_MISSING };   /* NT_SEQ_MISSING: the record names a
                                                 * sequence the store does not give (params/prog_seq.h): empty loaded */
#define B_NOTICE B_ACT(0x7Fu)   /* the program number's digits (OB-6: bank / tens / ones) */
#define B_SEQXP B_ACT(0x7Eu)    /* the header's value: the sequence's transpose (ctl_last_v, semitones) */

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
int32_t ctl_pad_value(uint16_t id, int32_t cur);    /* a top pad pressed: a switch toggles, a select (or global) cycles */
uint16_t ctl_enc_target(uint16_t bound, int fx_held);   /* what an encoder edits: FX held, DISTORTION; else bound */
int32_t ctl_prog_digit(int32_t n, uint32_t d, int32_t det);   /* program n with digit d (PRG_*) turned det, clamped */
int ctl_pad_at(const uint8_t *press);               /* the highest pressure of pads 8..15 */
typedef struct { uint16_t id; int16_t last_v, anchor_v; uint8_t anchor_p, caught, mode; } ctl_fader_t;
void ctl_fader_reset(ctl_fader_t *f);
int32_t ctl_fader_move(ctl_fader_t *f, int mode, uint16_t id, int32_t cur, uint32_t from, uint32_t to);

/* the main loop's (not with CONTROLS_PURE) */
void controls_init(void);                            /* after eng_init: pad events on, the faders' positions */
void controls_poll(synth_t *s, program_t *edit, int *screen);   /* main loop: encoders K2..K8, faders, pads, buttons */
int  controls_aftertouch(void);                                /* highest bottom-row pressure 0..127 */
void controls_key(int note);                         /* a note played (keys, MIDI): chord memory capture */
int  controls_seq_key(int note);                     /* a key pressed (its note after the octave shift): 1 = it transposes
                                                      * the sequence playing (SEQ REC held, or UNISON on): the caller
                                                      * swallows it (no sound, no MIDI, no record; its release neither) */
void controls_rec_note(int note, int vel);           /* a note to the sequencer's recorder (keys under LOCAL ON, MIDI in):
                                                      * vel 1..127 on, 0 off; nothing unless recording */
int  controls_btn_lit(uint32_t btn);                 /* a button's LED beside its press: SEQ PLAY playing, SEQ REC recording */
void controls_midi(uint32_t st, uint32_t d1, uint32_t d2);   /* one MIDI message in (midi_map.c) */

#endif
