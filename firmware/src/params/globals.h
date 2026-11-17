/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX globals: the OB-6's global settings that apply here (docs/ob6-reference.md section 7; the spec's list),
 * the keys' octave and the last program, kept in the base's settings sector (settings_load / settings_save) and
 * restored at power-on. Saved only while silent (an erase stalls the sound). Integers only. */
#ifndef KNOBI_GLOBALS_H
#define KNOBI_GLOBALS_H
#include <stdint.h>

/* the globals the panel edits (Globals screen; ui/controls.c ctl_glob[]), each GLOB_MIN..GLOB_MAX:
 *   MIDI CH     0 = ALL, 1..16                    POT MODE   RELATIVE / PASSTHRU / JUMP (the faders)
 *   PARAM TX    0 OFF, 1 CC (NRPN later)          PARAM RX   0 OFF, 1 CC
 *   TRANSPOSE   -12..+12 semitones (master coarse) TUNE      -50..+50 cents (master tune)
 *   VEL CURVE   0..7 (0 linear, 1..3 softer, 4..6 harder, 7 fixed 127) the keybed's velocity
 *   LOCAL       0 OFF (the keybed and pads go to MIDI only), 1 ON
 *   STEREO      0 STEREO, 1 MONO (both outputs the sum)
 *   SUSTAIN     0 N.O., 1 N.C. (the polarity of the sustain message, MIDI CC 64: N.C. reads it upside down)
 *   AT CURVE    0..3 (0 linear, 1 soft, 2 softer, 3 hard) the bottom pads' pressure */
enum { G_MIDI_CH, G_POT_MODE, G_PARAM_TX, G_PARAM_RX, G_TRANSPOSE, G_TUNE, G_VEL_CURVE, G_LOCAL, G_STEREO, G_SUSTAIN,
       G_AT_CURVE, G_COUNT };
enum { POT_RELATIVE, POT_PASSTHRU, POT_JUMP };
extern const int8_t GLOB_MIN[G_COUNT], GLOB_MAX[G_COUNT], GLOB_DEF[G_COUNT];

#define GLOBALS_VERSION 1
#define GLOBALS_BYTES (1 + G_COUNT + 1 + 2 + 2)    /* version, the values, octave, last program (LE), CRC16 (LE) */
typedef struct {
    int8_t v[G_COUNT];
    int8_t oct;                                   /* the keys' octave, -2..+2 (OCTAVE - / +) */
    uint16_t last_prog;                           /* the program loaded at power-on, 0..PROG_COUNT-1 */
} globals_t;

void globals_default(globals_t *g);
void globals_load(globals_t *g);                  /* nothing saved, a bad CRC, another version, a value out of range:
                                                   * the defaults */
void globals_save(const globals_t *g);            /* main loop, while silent */
void globals_pack(const globals_t *g, uint8_t *b);   /* GLOBALS_BYTES, the CRC16 last */
int glob_vel_curve(int curve, int v);             /* 0..127 -> 0..127; 0 stays 0, 1..127 never 0 */
int glob_at_curve(int curve, int v);              /* 0..127 -> 0..127; 0 -> 0, 127 -> 127 */

#endif
