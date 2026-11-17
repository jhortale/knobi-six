/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX voice manager: the whole synth (NVOICE voice.h voices, the shared LFO, the smoothed program values) and the
 * note logic: poly allocation, unison (stack or chord memory) with key modes, portamento, HOLD, bend, detune slop,
 * pan spread, program volume. docs/ob6-reference.md 2.9, 2.14, 6. Integers only; synth_t holds no pointers. Every
 * synth_* call must come from the context that calls synth_render (or be serialised with it).
 *
 * Poly: a new note takes a free voice (the one released longest ago), else the oldest released voice still sounding,
 * else the oldest held note (latched notes count as held). A note already sounding but released (or latched) on the
 * same key is retriggered on its own voice. Key modes do nothing in poly. Pan: voices alternate L / R with the offset
 * growing by pair: voice i at (i / 2 + 1) / (NVOICE / 2) of PAN SPREAD (6 voices: 1/3, 2/3, 1). Detune: each
 * oscillator gets a random offset, uniform in +-depth, drawn when a voice is allocated (a same-key retrigger keeps
 * it); depth = 0.5 semitone x (DETUNE / 127)^2.
 *
 * Unison (UNISON on): one note at a time on a stack of UNISON VOICES + 1 voices (UNISON VOICES 0..5 = 1..6), or with
 * UNISON VOICES 6 (CHD) the program's chord memory: one voice per chord note, the played key mapped to the chord's
 * lowest note (key modes LOW / LAST) or highest (HIGH). program_t.chord[] holds absolute MIDI notes, 0 = an empty
 * slot (our call: the record leaves it open); no notes stored = a 6-voice stack. The stack is spread, not random:
 * slot k's detune is a fixed fraction of the depth (+-depth at the outermost pair, the centre voice of an odd stack in
 * tune), and its pan alternates L / R with the offset growing by pair, an odd stack's first voice centred
 * (reference 6, 9.12).
 * Key modes (unison only): LOW / HIGH / LAST note priority over the keys held; the -R variants retrigger the
 * envelopes on each keystroke (controller's ruling, reference 6), even one that leaves the sounding note as it is;
 * the plain ones play legato (pitch only). A key release that returns to a still-held note is legato in every mode (a
 * release is not a keystroke). The first note after silence always triggers.
 *
 * Portamento (on, RATE > 0): RATE r sets T = 1 ms x 2^((r - 1) x 12 / 126), 1 ms .. 4.1 s. FIXED RATE: T per octave
 * (a glide of n semitones takes n / 12 x T); FIXED TIME: T whatever the interval; the -A (auto) variants glide only
 * when the new note starts while another key is held (legato). Every new note glides from the latest note-on's pitch
 * (poly: whichever voice it gets; unison: every slot of the stack); the first note after init, synth_program or
 * synth_panic does not glide (a freed voice forgets its pitch).
 * Bend: +-BEND RANGE semitones at the wheel's ends (both VCOs). HOLD: key releases are latched until HOLD goes off.
 * Minimum gate: a note released within 2 ms (VOICE_MIN_GATE control ticks, >= 2.2 ms) is released when they elapse.
 *
 * Smoothing (no zipper): FILTER FREQ, RESONANCE, MODE, the BP switch, the shapes and widths, the four mixer levels,
 * PROGRAM VOLUME and PAN SPREAD glide to a new value with a one-pole at control rate, time constant 16 ticks = 5.8 ms
 * (BP and MODE jumps become crossfades). synth_program snaps them when nothing sounds; with voices still sounding
 * (a program change under held notes: they release) they glide like an edit, so the change does not click. Those
 * release tails then sound with the new program's values (timbre, filter, levels: gliding; envelope rates at once).
 *
 * Output: synth_render ADDS its stereo frames (L R int32 pairs, Q15 full scale 32767) into lr. Per-voice gain
 * SYNTH_VOICE_GAIN x PROGRAM VOLUME / 127, constant-power pan (centre: -3 dB each side): a 6-note chord of the init
 * patch (saw, filter open) at the default volume 100 peaks around -6 dBFS per channel (test_voices output_level). The
 * synth's bus then passes a DC blocker (a one-pole high-pass at 5 Hz: a sync lead's or a narrow pulse's offset goes,
 * 30 Hz loses 0.12 dB; its tail runs on after the voices stop, until under half an LSB) and a soft limiter: linear to
 * SYNTH_LIMIT_KNEE (-4.4 dBFS), then a smooth knee (slope 1 there, falling) that approaches SYNTH_LIMIT_CEIL (-0.45
 * dBFS) and never reaches it: six overdriven voices at every level at full (+-65536 each, the bus up to ~13.6 dB over
 * full scale) come out below the ceiling, rounded, never flat at a clip. */
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
#define SYNTH_LIMIT_KNEE 19661            /* the bus soft limiter's knee: 0.6 x full scale, -4.4 dBFS */
#define SYNTH_LIMIT_CEIL 31129            /* its ceiling, approached, never reached: 0.95 x full scale, -0.45 dBFS */

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
    int32_t bend14, wheel, pressure, midi_vol;   /* midi_vol: MIDI CC 7, 0..127 (127 at init) */
    uint8_t stack[SYNTH_STACK], svel[SYNTH_STACK], sdown[SYNTH_STACK];   /* keys in press order */
    uint8_t nstack;
    int16_t mono;                         /* unison: the sounding key, -1 none */
    int32_t last_pitch;                   /* the latest note-on's key, Q16: where a new poly voice glides from */
    int32_t tune;                         /* the globals TRANSPOSE + TUNE, Q16 semitones (added like the bend) */
    uint8_t hold, ctl_left, rates_dirty;
    uint8_t vlimit;                       /* the CPU guard's voice limit, 1..NVOICE (synth_guard) */
    uint16_t calm;                        /* blocks in a row where one more voice fits (synth_guard) */
    uint32_t sheds;                       /* the CPU guard's sheds since init (each lowered the limit) */
    float dcx[2], dcy[2];                 /* the bus DC blocker: last input, last output, L R */
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
void synth_all_notes_off(synth_t *s);               /* every note released (envelopes release), HOLD unchanged */
void synth_volume(synth_t *s, int v);               /* MIDI volume 0..127, times PROGRAM VOLUME, smoothed */
void synth_tune(synth_t *s, int cents);             /* master transpose + tune, -1250..+1250 cents, both VCOs */
void synth_render(synth_t *s, int32_t *lr, uint32_t frames);   /* adds into L R pairs, Q15 */
int  synth_active(const synth_t *s);                /* voices sounding */
/* The render seam (Phase 2: part of the voices can render on the second core). Each control period synth_render runs
 * the tick (the smoothers, the LFO, the shared voice_ctx_t, every voice's control and envelopes, freeing silent
 * voices), sets ctx.pos, renders the busy voices into a zeroed int32 bus through SYNTH_SPLIT, then the DC blocker and
 * the limiter. The bus sums int32 (exact in any order), so any split of the voices into sets is bit-exact with the
 * one loop (tests/unit/test_split.c: every partition, and set B on a second thread under ThreadSanitizer).
 *
 * synth_render_set: the busy voices in voice_mask (bit i = voice i) for frames_in_period (<= the frames left in the
 * period) ADDED into bus (L R int32 pairs; the caller zeroes it). It writes only those voices' voice_t and bus; it
 * reads s->ctx (incl. pos), s->prog.v and those voices; nothing else of synth_t, no global. So two calls with
 * disjoint masks may run at once (on two cores, each with its own bus, summed after), provided nothing writes the
 * synth meanwhile: no tick, no synth_note / param / ... (those are serialised with synth_render already).
 * synth_balance: the busy voices split by voice_t.busy (not by index): set A (mask_a: CPU0's, the core that also takes
 * the other interrupts) the first n / 2 busy voices by index, set B (mask_b) the rest; returns n, the voices busy.
 *
 * SYNTH_SPLIT(s, bus, n): one period's voices into bus. A dual-core build defines it before voices.c: synth_balance,
 * set B handed to the other core (its own bus), set A here, join, sum into bus. One core (the default): every voice
 * as one set (set A all, set B empty: the Phase 1 loop, at its cost). */
#define SYNTH_ALL_VOICES ((1u << NVOICE) - 1u)
void synth_render_set(synth_t *s, uint32_t voice_mask, int32_t *bus, uint32_t frames_in_period);
uint32_t synth_balance(const synth_t *s, uint32_t *mask_a, uint32_t *mask_b);
#ifndef SYNTH_SPLIT
#define SYNTH_SPLIT(s, bus, n) synth_render_set((s), SYNTH_ALL_VOICES, (bus), (n))
#endif
/* The CPU guard: after each render, the time it took (used) and the block's time (budget), in any one unit (the
 * platform's tick counter). Over SYNTH_GUARD_HI % of the budget the voice limit drops, in one step, to the voices that
 * fit under it at the measured cost per voice (used / voices sounding; at least one fewer, at least one), and the
 * voices over it are shed at once (the oldest released first, then the oldest held), each fading out linearly in
 * ~2.2 ms (6 control ticks) like a steal and freed as soon as it is silent (no further shedding until they are gone:
 * their fade is in the next measurement); each shed counts in sheds. The limit goes back up by one after
 * SYNTH_GUARD_HOLD blocks in a row where one more voice fits: the load projected at limit + 1 voices, at the measured
 * cost per voice sounding (used x (limit + 1) / voices sounding), under SYNTH_GUARD_FIT % (with no voice sounding: the
 * load under SYNTH_GUARD_LO %). Projected, not a fixed low mark: the init chord measures ~65 % on the chip, so after a
 * shed its five voices (~54 %) would sit over a fixed 55 % forever; a patch whose next voice does not fit stays where
 * it is, a patch on the edge does not toggle. New notes respect the limit (they steal instead of taking a free voice). */
#define SYNTH_GUARD_HI 80
#define SYNTH_GUARD_FIT 70
#define SYNTH_GUARD_LO 55
#define SYNTH_GUARD_HOLD 172              /* blocks: ~1 s of 256 frames */
void synth_guard(synth_t *s, uint32_t used, uint32_t budget);

#endif
