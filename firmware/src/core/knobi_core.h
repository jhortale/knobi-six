/* SPDX-License-Identifier: GPL-3.0-only */
/* knobi_core: KNOBI-SIX's behaviour code (synth/, engine.c, params/, the arp, clock, sequencer, ui/midi_map.c,
 * ui/seq_rec.c) as a host library, for the AU / VST3 plugin (docs/superpowers/specs/2026-11-19-plugin-design.md). The
 * firmware's own files, compiled a second time by knobi_core.c (a unity file of its own, outside platform.c), with no
 * hardware: no flash, no screen, no USB. The firmware build is not affected.
 *
 * The sound: 44.1 kHz, interleaved stereo Q15 (int16), rendered internally in the firmware's blocks (KC_BLOCK frames,
 * 16-frame control periods) exactly as the firmware's host build (tests/host.c) renders them: the same MIDI before the
 * same block gives the same samples (tests/unit/test_core.c). kc_render hands out any number of frames from a FIFO of
 * one block, so the output does not depend on how the caller cuts it.
 * Timing: kc_midi, kc_param_set, kc_program_* and kc_global_set act at the start of the next internal block (the one
 * after the frames already rendered into the FIFO): a caller that renders in multiples of KC_BLOCK frames gets them at
 * the frame it called them before. Sample-accurate (the plugin): kc_render_exact renders exactly the frames asked for,
 * no FIFO ahead, so a call made between two kc_render_exact calls acts at that frame: the firmware's block is split
 * there, as the firmware's host build does it with "split" (tests/scripts/core_split.txt, test_core
 * split_matches_host).
 *
 * Exports: built as a shared object with -fvisibility=hidden (the plugin, tests/Makefile), the kc_* calls (KC_API) are
 * the only symbols exported, none of the firmware's own. Build it with -ffp-contract=off (bit-exactness).
 *
 * Instances and threads: every instance owns all of its state (the firmware modules' file-scope state included:
 * knobi_core.c, tools/core_state.py), so any number of instances may exist, be created and destroyed in any order, and
 * run at the same time on different threads. One instance is not thread-safe: the calls on one instance must not
 * overlap (one thread at a time drives it, e.g. the plugin's audio thread; another thread may take over between calls).
 * The one exception is kc_bank_set: any thread, at any time (see there).
 *
 * Storage: the core reads and writes no file and no flash. The program bank's user layer comes in through kc_bank_set
 * (a snapshot the caller owns and keeps unchanged while set); programs it does not hold are the factory programs of
 * this build (none unless built with the local factory bank: KNOBI_FACTORY_LOCAL), else the init program. WRITE,
 * RESTORE FACTORY and the globals' persistence are the caller's (the plugin's library). MIDI out (the arp's and the
 * sequencer's notes, CC out, MIDI clock out) is dropped. */
#ifndef KNOBI_CORE_H
#define KNOBI_CORE_H
#include <stdint.h>
#include "../params/params.h"
#include "../synth/seq.h"
#include "../params/globals.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef KC_API                            /* (the plugin links the core in: -DKC_API= , nothing exported) */
#if defined __GNUC__
#define KC_API __attribute__((visibility("default")))   /* (the only exports under -fvisibility=hidden) */
#else
#define KC_API
#endif
#endif

#define KC_RATE 44100u                    /* the sample rate */
#define KC_BLOCK 256u                     /* frames an internal block */
#define KC_PROGRAMS 500u                  /* the bank: programs 0..499 (bank / tens / ones; store_prog.h PROG_COUNT) */
#define KC_PARAM_ID_LIMIT 96u             /* parameter ids are below this (kc_param_info says which are exposed) */

typedef struct kc kc_t;

KC_API kc_t *kc_create(void);                    /* the init program (program 0 of the bank), the globals at their defaults
                                           * (MIDI CH = ALL), silent; NULL: no memory */
KC_API void kc_destroy(kc_t *k);
KC_API void kc_render(kc_t *k, int16_t *lr, uint32_t frames);   /* frames x (L, R), any count */
KC_API void kc_render_exact(kc_t *k, int16_t *lr, uint32_t frames);   /* the same, rendering no frame ahead: first what
                                           * kc_render left in the FIFO, then internal blocks of exactly the frames
                                           * asked for (at most KC_BLOCK each: the firmware's block, cut short). With
                                           * calls that end on the multiples of KC_BLOCK (and anywhere a call to act
                                           * there is made), the same samples as kc_render for the same calls made
                                           * before the same blocks; the frames of a cut block add up (the control
                                           * period, the clock's steps and the notes' gates run on across the cut) */
KC_API void kc_midi(kc_t *k, const uint8_t *msg, uint32_t len);   /* one MIDI message (1..3 bytes: a channel message or a
                                           * realtime byte; SysEx and others ignored), the firmware's MIDI in
                                           * (ui/midi_map.c controls_midi: notes, pressure, bend, CC map, sustain, bank
                                           * select + program change, MIDI clock per the MIDI CLOCK global); queued
                                           * for the next block (up to KC_MIDI_QUEUE; beyond: dropped, counted) */
#define KC_MIDI_QUEUE 1024u
KC_API uint32_t kc_midi_dropped(const kc_t *k);  /* messages dropped so far (the queue full) */

/* parameters: the firmware's live ids (params.h P_IS_LIVE: Phase 1's, P_DISTORTION, P_BPM, P_CLK_DIV, P_ARP_*), the
 * firmware's own values (a knob min..max, a switch 0 / 1, a select min..max) */
enum { KC_KIND_KNOB, KC_KIND_SWITCH, KC_KIND_SELECT };
typedef struct {
    const char *name;                     /* the firmware's (params.c), e.g. "FREQUENCY" */
    int kind;                             /* KC_KIND_* */
    int32_t min, max, def;
    int bipolar;                          /* a knob centred on 0 (min = -max) */
    int section;                          /* params.h enum param_section (PS_*) */
    const char *const *labels;            /* a select: labels[v - min], max - min + 1 of them; else NULL */
} kc_param_info_t;
KC_API int kc_param_info(uint16_t id, kc_param_info_t *out);   /* 0: id is exposed (out filled unless NULL); -1 not */
KC_API void kc_param_set(kc_t *k, uint16_t id, int32_t v);   /* as a panel edit (clamped; not sent as CC); not exposed: none */
KC_API int32_t kc_param_get(const kc_t *k, uint16_t id);     /* the value in the edit buffer; not exposed: 0 */

/* programs: the edit buffer (the program in play + its sequence) and the number it came from. edited: changed since
 * it was loaded (a parameter, CC or the sequence), or set as edited */
KC_API int kc_program_set(kc_t *k, int32_t number, const program_t *p, const seq_t *s, int edited);   /* into the edit buffer
                                           * as a load (the player stops, recording ends); 0 ok, -1 refused: number
                                           * not 0..499, or p / s not records the firmware would store */
KC_API void kc_program_get(const kc_t *k, int32_t *number, program_t *p, seq_t *s, int *edited);   /* any pointer may be NULL */
KC_API int kc_program_select(kc_t *k, int32_t number);   /* load program number from the bank, as a program change: 0 ok */
KC_API int kc_program_pending(const kc_t *k);   /* 1: a load chosen by a MIDI program change or kc_program_select
                                           * waits for the engine (busy with an earlier load: a later block takes it);
                                           * meanwhile kc_program_get reports its number with the previous edit buffer,
                                           * so a caller storing number + program (the plugin's WRITE, its set state)
                                           * takes program number's content from the bank instead. 0: number and edit
                                           * buffer agree (kc_program_set is never pending: reported as set) */
KC_API uint32_t kc_program_loads(const kc_t *k);   /* program loads asked for so far (kc_program_set,
                                           * kc_program_select, a MIDI program change queued or deferred): read with
                                           * kc_program_get, it tells kc_program_mark_stored whether a load came since */
KC_API int kc_program_mark_stored(kc_t *k, int32_t number, const program_t *p, const seq_t *s, uint32_t loads);
                                          /* the caller stored p + s (read from kc_program_get, with kc_program_loads
                                           * = loads, in the same call through the seam) as program number (WRITE):
                                           * the edit buffer is now program number, with no load (the sound, the
                                           * sequencer and the arp play on). "edited" clears only if the edit buffer
                                           * still is p + s (the program's values and chord, the sequence); an edit made
                                           * since stays edited (a kc_program_set the engine has not taken yet: that
                                           * load is marked the same way). 0 marked unedited, 1 marked still edited, -1
                                           * refused (number not 0..499, NULL, or a program change still waiting:
                                           * kc_program_pending -- its content is not known yet); -2 a load came
                                           * since loads was read (it is the program in play: nothing renamed) */
typedef struct {
    uint8_t used[KC_PROGRAMS];            /* 0: the factory program (or init) */
    program_t program[KC_PROGRAMS];
    seq_t seq[KC_PROGRAMS];
} kc_bank_t;
KC_API int kc_bank_set(kc_t *k, const kc_bank_t *bank);   /* the user layer (NULL: none), read on loads only. 0 taken; -1
                                           * refused (the previous one stays): a used entry is not a program + sequence
                                           * the firmware would store (kc_program_valid). Any thread, at any time, also
                                           * while another thread renders: the instance takes the newest bank set at the
                                           * start of each of its calls that run the shared code (kc_create, kc_render,
                                           * kc_param_*, kc_program_*, kc_global_*; not kc_midi, which only queues).
                                           * The caller keeps a bank unchanged and alive while it may be in use: a bank
                                           * B replaced by a later kc_bank_set may be freed once kc_bank_in_use, called
                                           * after that kc_bank_set, returns a bank other than B (the instance has moved
                                           * on and never reads B again), or once the instance is destroyed. An instance
                                           * that makes no call (idle, not rendering) keeps its bank in use as long */
KC_API const kc_bank_t *kc_bank_in_use(const kc_t *k);   /* the bank the instance's calls read now (any thread) */
KC_API int kc_program_valid(const program_t *p, const seq_t *s);   /* 0: a program + sequence the firmware would store (its
                                           * record formats: program_pack / unpack, seq_pack / unpack); -1 not */

/* globals (params/globals.h G_*: MIDI CH, TRANSPOSE, TUNE, VEL CURVE, LOCAL, STEREO, SUSTAIN, AT CURVE, MIDI CLOCK, ARP
 * BEAT SYNC, ...), GLOB_MIN..GLOB_MAX, clamped */
KC_API void kc_global_set(kc_t *k, int g, int32_t v);
KC_API int32_t kc_global_get(const kc_t *k, int g);

/* The host's transport (plan Task 5; synth/clock.h "The host's transport", spec "Clock from Live"): playing, the tempo
 * (BPM) and the position (quarter notes from the song's start, ppq; negative in a count-in) as of the frame
 * frame_offset frames after the next frame the core hands out (kc_render / kc_render_exact; a frame already rendered
 * into kc_render's FIFO: at the next block's first frame, the position moved on to it at that tempo). Given once a host block for its first frame, and once more
 * for a loop's wrap inside the block (at the wrap's frame, the loop's start), in frame order. While the host plays the
 * clock follows it: the steps are the host's grid from position 0 (a start mid-bar, a loop's wrap, a locate land on the
 * matching step: the sequence's step = the step's number modulo its length), the tempo the host's (automation
 * followed; P_BPM kept, not used: kc_clock_following), MIDI clock in and the arp's restart do nothing. A jump or a stop
 * releases the arp's and the sequencer's notes at its frame. Stopped: the firmware's clock (MIDI CLOCK OFF: internal at
 * P_BPM, from where it was: the arp plays with keys held, the firmware's rules) -- but not the sequencer: once a host
 * transport has been given (playing or not), the sequencer plays only while the host plays. SEQ PLAY on with the host
 * stopped (or not played yet) arms it (kc_seq_state 2, no note, no clock restart); the host's play starts it on the
 * song position's step; the host's stop silences it and leaves it armed. The
 * output frame: an event at internal frame f acts at f, as a MIDI message given for f does (kc_midi before rendering
 * f): a step at the host's position p acts where a MIDI note the host places at p would. Not a number (bpm, ppq):
 * 120 BPM, position 0 */
KC_API void kc_clock_host(kc_t *k, int playing, double bpm, double ppq_pos, uint32_t frame_offset);
KC_API int kc_clock_following(const kc_t *k);   /* the clock follows the host now (playing): P_BPM is not used */
/* SEQ PLAY (the panel's button: on plays, off stops; recording ends first, as SEQ PLAY on the keyboard); the state as
 * asked */
KC_API void kc_seq_play(kc_t *k, int on);
KC_API int kc_seq_playing(const kc_t *k);
KC_API int kc_seq_state(const kc_t *k);   /* 0 stopped, 1 playing, 2 armed (SEQ PLAY on, waiting: the host stopped or
                                           * the clock not running), as of the last block */

/* The sequence on the panel (plugin plan Task 8: the 64-step grid's mouse editing and the step entry). Host only: these
 * calls exist in the core alone, the firmware's files are unchanged.
 * kc_seq_get: the edit buffer's sequence (a kc_program_set the engine has not taken yet: its sequence), steps past
 * the length zero. kc_seq_set: the sequence as an edit of the edit buffer (params/prog_seq.h edit_seq_mut: the program
 * is then edited, WRITE stores it), checked through the firmware's record format first (seq_pack / seq_unpack: 64
 * steps, 6 notes, notes 12..108, velocities 0..127, the tie rules of synth/seq.h). The player takes it at the next
 * block as it takes any change (engine.c eng_seq_load; seq.h "The player": the notes sounding are its own list, every
 * note-on gets its note-off, a shorter sequence is followed). 0 set; 1 the same sequence: nothing done (not edited);
 * -1 refused (not a sequence the firmware would store); -2 refused: recording (or armed) -- the step entry owns it.
 * kc_seq_version: changes whenever the sequence may have (an edit, a recorded step, a load, a WRITE): cheap, for the
 * audio thread to publish. kc_seq_step: the step playing (0-based, the one played last), -1 when not playing.
 * kc_seq_xp: the transpose playing (semitones; 0 none) */
KC_API void kc_seq_get(const kc_t *k, seq_t *out);
KC_API int kc_seq_set(kc_t *k, const seq_t *s);
KC_API uint32_t kc_seq_version(const kc_t *k);
KC_API int kc_seq_step(const kc_t *k);
KC_API int kc_seq_xp(const kc_t *k);
/* Step entry: the firmware's recorder (ui/seq_rec.h) as the keyboard drives it (ui/controls.c), so the same inputs make
 * the same sequence. MIDI notes in (kc_midi, the channel heard) are recorded while recording, as on the keyboard.
 * kc_seq_rec_button: the SEQ REC button pressed (down 1) / let go (0), the keyboard's rules: not playing, a press arms
 * (playing: stopped first; the arp off), a press while recording ends it (a chord still held recorded); while the
 * sequence plays it acts at its release (stop, then record) unless a MIDI note came while it was held: then that note
 * transposes (the note - SEQ_XP_CENTER, from the next step; the note and its release are swallowed: neither played
 * nor recorded) and the release does nothing; down -1: the hold dropped with no action (the panel closed with it
 * held). kc_seq_rec: on / off directly (on: as a tap; off: ends it). The
 * recording replaces the sequence at its first entry (lazy clear: ending with none leaves it as it was, unedited);
 * it ends by itself at 64 steps; SEQ PLAY, a load, ARP ON end it too.
 * kc_seq_rec_inc / dec: the keyboard's INC (> : a rest, or with notes held: they are recorded and tied) and DEC (< :
 * the last step erased). The panel's buttons: kc_seq_rec_rest (a rest; -1 refused while notes are held: that is a tie),
 * kc_seq_rec_tie (notes held: INC; none: the last step tied, seq_rec_tie's rules -- refused at step 1, after a rest, at
 * 64 steps), kc_seq_rec_back (DEC). Each: 1 the sequence changed, 0 not recording (nothing), -1 refused.
 * kc_seq_rec_state: 0 not recording; else 1 (recording or armed) | 2 (nothing recorded yet: shown empty) | the notes
 * held << 8 */
KC_API void kc_seq_rec_button(kc_t *k, int down);
KC_API void kc_seq_rec(kc_t *k, int on);
KC_API int kc_seq_rec_inc(kc_t *k);
KC_API int kc_seq_rec_dec(kc_t *k);
KC_API int kc_seq_rec_rest(kc_t *k);
KC_API int kc_seq_rec_tie(kc_t *k);
KC_API int kc_seq_rec_back(kc_t *k);
KC_API int kc_seq_rec_state(const kc_t *k);
#define KC_REC_ON 1
#define KC_REC_FRESH 2
#define KC_REC_HELD(st) ((st) >> 8)
/* what the clock and the arp / sequencer did (the panel's playing step, the tests): logs of KC_LOG entries since the
 * last call (older ones dropped), read and emptied here. Steps: every clock step event, its internal frame (frames
 * rendered since kc_create), number (the host's grid: the song position's step), kind (synth/clock.h CLK_EV_*, |
 * CLK_EV_HOSTED) and gate (a resumed step's frames). Notes: the arp's and the sequencer's note-ons (vel > 0) and
 * note-offs (vel 0) in order, with the first frame of the internal block they fell in */
#define KC_LOG 256u
enum { KC_EV_STEP, KC_EV_JUMP, KC_EV_RESUME, KC_EV_RELEASE };   /* kc_step_t.kind & KC_EV_KIND (= clock.h's CLK_EV_*) */
#define KC_EV_KIND 0x0Fu
#define KC_EV_HOSTED 0x80u                /* a step on the host's grid (its number: the song position's 16th, ...) */
typedef struct { uint64_t frame; int32_t step; uint8_t kind; uint32_t gate; } kc_step_t;
typedef struct { uint64_t frame; uint8_t note, vel; } kc_note_t;
KC_API uint32_t kc_clock_steps(kc_t *k, kc_step_t *out, uint32_t max);
KC_API uint32_t kc_notes(kc_t *k, kc_note_t *out, uint32_t max);

#ifdef __cplusplus
}
#endif
#endif
