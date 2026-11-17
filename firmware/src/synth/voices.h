/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX voice manager: the whole synth (NVOICE voice.h voices, the shared LFO, the smoothed program values) and the
 * note logic: poly allocation, unison (stack or chord memory) with key modes, portamento, HOLD, bend, detune slop,
 * pan spread, program volume. docs/ob6-reference.md 2.9, 2.14, 6. Integers only; synth_t holds no pointers. Every
 * synth_* call must come from the context that calls synth_render (or be serialised with it).
 *
 * Poly: a new note takes a free voice (the one released longest ago), else the oldest released voice still sounding,
 * else the oldest held note (latched notes count as held). A note already sounding but released (or latched) on the
 * same key is retriggered on its own voice. Key modes do nothing in poly. Pan: voices alternate L / R by index.
 * Detune: each oscillator of each note gets a random offset, uniform in +-depth, drawn at note-on (vintage slop);
 * depth = 0.5 semitone x (DETUNE / 127)^2.
 *
 * Unison (UNISON on): one note at a time on a stack of UNISON VOICES + 1 voices (UNISON VOICES 0..5 = 1..6), or with
 * UNISON VOICES 6 (CHD) the program's chord memory: one voice per chord note, the played key mapped to the chord's
 * lowest note (key modes LOW / LAST) or highest (HIGH). program_t.chord[] holds absolute MIDI notes, 0 = an empty
 * slot (our call: the record leaves it open); no notes stored = a 6-voice stack. The stack is spread, not random:
 * slot k's detune is a fixed fraction of the depth (+-depth at the outermost pair, the centre voice of an odd stack in
 * tune), and its pan alternates L / R with an odd stack's first voice centred (reference 6, 9.12).
 * Key modes (unison only): LOW / HIGH / LAST note priority over the keys held; the -R variants retrigger the
 * envelopes when a key press changes the sounding note, the plain ones play legato (pitch only). A key release that
 * returns to a still-held note is legato in every mode (the reference retriggers "on each keystroke"; a release is
 * not one). The first note after silence always triggers.
 *
 * Portamento (on, RATE > 0): RATE r sets T = 1 ms x 2^((r - 1) x 12 / 126), 1 ms .. 4.1 s. FIXED RATE: T per octave
 * (a glide of n semitones takes n / 12 x T); FIXED TIME: T whatever the interval; the -A (auto) variants glide only
 * when the new note starts while another key is held (legato). Each voice glides from the pitch it last played
 * (per-voice slew, as an analog poly; in unison the stack glides from the previous note).
 * Bend: +-BEND RANGE semitones at the wheel's ends (both VCOs). HOLD: key releases are latched until HOLD goes off.
 * Minimum gate: a note released within 2 ms (VOICE_MIN_GATE control ticks, >= 2.2 ms) is released when they elapse.
 *
 * Smoothing (no zipper): FILTER FREQ, RESONANCE, MODE, the BP switch, the shapes and widths, the four mixer levels,
 * PROGRAM VOLUME and PAN SPREAD glide to a new value with a one-pole at control rate, time constant 16 ticks = 5.8 ms
 * (BP and MODE jumps become crossfades). synth_program snaps them.
 *
 * Output: synth_render ADDS its stereo frames (L R int32 pairs, Q15 full scale 32767) into lr. Per-voice gain
 * SYNTH_VOICE_GAIN x PROGRAM VOLUME / 127, constant-power pan (centre: -3 dB each side): a 6-note chord of the init
 * patch (saw, filter open) at the default volume 100 peaks around -6 dBFS per channel (test_voices output_level). The
 * synth's bus then passes a soft limiter (linear to 24576 = -2.5 dBFS, a quadratic shoulder to 32767 at 40960, flat
 * after): the worst case (six overdriven voices, +-65536 each) cannot exceed +-32767. */
#ifndef KNOBI_VOICES_H
#define KNOBI_VOICES_H
#include <stdint.h>
#include "voice.h"
#include "../params/params.h"

#ifndef NVOICE
#define NVOICE 6
#endif
#define SYNTH_STACK 32                    /* keys remembered for note priority / legato */
#define VOICE_MIN_GATE 7                  /* control ticks: 97..112 frames = 2.2..2.5 ms (a tick may fall at once) */
#define SYNTH_LIMIT_KNEE 24576            /* the bus soft limiter's knee */

enum { SM_CUT, SM_RESO, SM_MODE, SM_BP, SM_SHAPE1, SM_PW1, SM_SHAPE2, SM_PW2, SM_L1, SM_L2, SM_LSUB, SM_LNOISE,
       SM_VOL, SM_SPREAD, SM_COUNT };

typedef struct synth synth_t;
struct synth {
    program_t prog;                       /* the values in play (prog.v[]), chord memory */
    voice_t voice[NVOICE];
    voice_ctx_t ctx;
    lfo_t lfo;
    int32_t sm[SM_COUNT], tgt[SM_COUNT];  /* smoothed values and their targets */
    uint32_t clock, rnd;                  /* event clock (allocation order), xorshift32 */
    int32_t bend14, wheel, pressure;
    uint8_t stack[SYNTH_STACK], svel[SYNTH_STACK], sdown[SYNTH_STACK];   /* keys in press order */
    uint8_t nstack;
    int16_t mono;                         /* unison: the sounding key, -1 none */
    uint8_t hold, ctl_left, rates_dirty;
};

void synth_init(synth_t *s);
void synth_program(synth_t *s, const program_t *p); /* load all values, snap smoothing */
void synth_param(synth_t *s, uint16_t id, int16_t v); /* live edit, smoothed */
void synth_note(synth_t *s, int note, int vel);     /* vel 0 = off */
void synth_hold(synth_t *s, int on);
void synth_bend(synth_t *s, int bend14);            /* 0..16383, 8192 = centre */
void synth_wheel(synth_t *s, int v);                /* 0..127 */
void synth_pressure(synth_t *s, int v);             /* 0..127 */
void synth_panic(synth_t *s);                       /* every voice silent now, keys and HOLD cleared */
void synth_render(synth_t *s, int32_t *lr, uint32_t frames);   /* adds into L R pairs, Q15 */
int  synth_active(const synth_t *s);                /* voices sounding */

#endif
