/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX globals: the OB-6's global settings that apply here (docs/ob6-reference.md section 7; the spec's list),
 * the keys' octave and the last program, kept in two of the base's blobs (0 and 1) taken in turn, each record with a
 * sequence number: a save goes to the other blob, so one cut by a power loss leaves the previous record. Restored at
 * power-on. Saved only once nothing has played for 3 s (app.c; an erase stalls the sound). Integers only. */
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
#define GLOBALS_BYTES (1 + G_COUNT + 1 + 2 + 4 + 2)   /* version, the values, octave, last program (LE), seq (LE),
                                                       * CRC16 (LE) */
#define GLOBALS_BLOB 0u                               /* blobs GLOBALS_BLOB and GLOBALS_BLOB + 1 */
typedef struct {
    int8_t v[G_COUNT];
    int8_t oct;                                   /* the keys' octave, -2..+2 (OCTAVE - / +) */
    uint16_t last_prog;                           /* the program loaded at power-on, 0..PROG_COUNT-1 */
    uint32_t seq;                                 /* (the record's sequence number: the newer blob wins) */
} globals_t;

void globals_default(globals_t *g);
void globals_load(globals_t *g);                  /* the newer good record of the two blobs; none good (nothing saved, a
                                                   * bad CRC, another version, a value out of range): the defaults */
int globals_save(globals_t *g);                   /* main loop, while silent: g->seq + 1 into the other blob (g->seq is
                                                   * then the saved one's); 0 saved, -1 failed (g untouched: try again) */
void globals_pack(const globals_t *g, uint8_t *b);   /* GLOBALS_BYTES, the CRC16 last */
int glob_vel_curve(int curve, int v);             /* 0..127 -> 0..127; 0 stays 0, 1..127 never 0 */
int glob_at_curve(int curve, int v);              /* 0..127 -> 0..127; 0 -> 0, 127 -> 127 */

#endif
