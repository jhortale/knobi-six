/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX clock (docs/ob6-reference.md 2.11 and 5): the tempo the arpeggiator, the synced LFO (and later the synced
 * delays and the sequencer) follow. Internal BPM 30..250 (per program, P_BPM), tap tempo, a division of the beat per
 * step (P_CLK_DIV, the OB-6's 10 values) and MIDI clock in (USB, 24 a quarter note, start / continue / stop) under the
 * global MIDI CLOCK. Integers only; no 64-bit division; no libgcc. Pure (no platform call): unit-tested on the host
 * (tests/unit/test_clock.c).
 *
 * The type is clk_t, not the brief's clock_t: <time.h> (and <sys/types.h> on some hosts) already define clock_t.
 *
 * Where it runs: engine.c owns one clk_t (eng_clk) on CPU0, in the audio interrupt (the consumer of the event queue):
 * clock_set_bpm / clock_set_div / clock_set_mode / clock_midi as their events drain, then clock_tick once per block,
 * before the synth renders. Never on CPU1. The main loop sees what engine.c publishes (eng_clk_* words) and uses a clk_t
 * of its own for tap tempo only (clock_tap's state; ui/controls.c).
 *
 * Time. The position is counted in MIDI clock ticks (CLOCK_PPQ = 24 a quarter note) plus a fraction of the next tick
 * in CLOCK_U counts. The rate is counts per frame: internal BPM x CLOCK_RATE_Q (64). A tick is 110250 / BPM frames at
 * 44.1 kHz, so CLOCK_U = 110250 x 64 counts. Everything is exact integer arithmetic: a step boundary at tick T fires at
 * the first frame whose position is >= T ticks, so event k of a constant tempo lands on ceil(k x step x U / rate)
 * frames from the start: never more than one frame off, and no drift however long it runs (tests: 30 / 120 / 250 BPM,
 * and 97 BPM where a step is not a whole number of frames). A BPM change keeps the position (the rate changes, no jump).
 *
 * Divisions (P_CLK_DIV 0..9; CLOCK_DIV_TICKS: the even and the odd step of a pair, in ticks). The reference's rates
 * per BPM: Half = /2, Qtr = x1, 8th / 8th D / 8th S = x2, 8th T = x3, 16th / 16th S = x4, 16th T = x6, 32nd = x8; so
 * 8th D is a dotted pair (a dotted 8th then a 16th: 18 + 6 ticks, average x2, as the reference's table has it), not
 * a steady dotted 8th (that would be x1.33). Swing amounts are not in the reference (builder's call): 8th S = 16 + 8
 * ticks (the triplet shuffle, 66.7 %), 16th S ("full swing") = 8 + 4 ticks (66.7 %). Every boundary falls on a whole
 * tick, so a followed MIDI clock places every division exactly. Step 0 (the first after a start) is a long one.
 *
 *   value   0     1    2    3       4       5     6     7        8       9
 *   name    HALF  QTR  8TH  8TH D   8TH S   8TH T 16TH  16TH S   16TH T  32ND
 *   ticks   48    24   12   18 + 6  16 + 8  8     6     8 + 4    4       3
 *
 * MIDI clock in (global MIDI CLOCK = IN, SLAVE THRU or IN-no-start-stop; the clock's mode):
 *   - 0xF8 at time t (frames on this clock's own count, clk_t.now; engine.c passes the block's start, when the event
 *     drains): the position may not pass the tick received (it waits just short of the next one), and a tick that
 *     arrives before the position got there pulls it forward at once (the steps crossed fire at frame 0 of the next
 *     block). Between ticks the position runs at the received tempo: the frames between the newest and the oldest of
 *     the last CLOCK_EXT_N ticks (one beat), so it settles within a beat; arriving at block starts (256 frames), the
 *     estimate is within ~1 % at 120 BPM. With fewer than 2 ticks no tempo: the steps fire at the ticks.
 *   - 0xFA start: the next 0xF8 is the downbeat (position 0, step 0 fires there). 0xFB continue: on from where it
 *     stopped. 0xFC stop: stopped. IN-no-start-stop ignores all three: ticks alone run it (from position 0 at the first
 *     tick after a silence).
 *   - No tick for CLOCK_EXT_TIMEOUT frames (0.25 s: more than 3 tick intervals at 30 BPM, the slowest tempo) and the
 *     clock is not running (the tempo estimate restarts). Ticks again: it runs on (unless a 0xFC stopped it).
 *   - clock_running(): internal modes (OFF, OUT) always; external ones only with ticks arriving and not stopped. Per
 *     the reference, with the clock external and absent the arpeggiator does not run: Task 6 asks clock_running().
 * MIDI clock out (MIDI CLOCK = OUT; clock_out_poll, from the main loop with what engine.c publishes): clk_t.ticks
 * counts the ticks the clock has ENTERED (never going back), a start (clk_t.starts, start_ticks) included: switching to
 * OUT starts the clock at step 0, which enters tick 0. The poll then sends 0xFA and at once tick 0's 0xF8 (a slave
 * takes the first 0xF8 after 0xFA as its downbeat: our step 0), then one 0xF8 for each tick entered, so a slave's
 * steps fall with ours, not a tick (1/24 beat) behind; 0xFC when OUT is left. clock_restart under OUT would be passed
 * on (a start like any other: the poll re-sends 0xFA with the new tick 0's 0xF8), but nothing calls it there: engine.c's
 * arp start never restarts a clock that slaves follow (final review 1: a DAW would jump to its bar start on every
 * chord). Owed ticks beyond CLOCK_OUT_OWED (a main-loop stall) are dropped, not
 * sent in a burst. SLAVE THRU: the main loop echoes what comes in. tests/unit/test_clock.c midi_clock_out runs a
 * sender into this same clock as a receiver: every step of the receiver within one block after the sender's.
 *
 * Tap tempo (clock_tap, ms = the main loop's millisecond count): the reference says tap at least 4 times. From the
 * 4th tap on the tempo is the average of the last intervals, at most 4 (60000 x intervals / their span, rounded),
 * clamped 30..250; a gap of more than CLOCK_TAP_GAP_MS (2 s) starts a new count (that tap is the first).
 *
 * Events (for the arpeggiator, Task 6). clock_tick(c, frames) advances the clock by one block and returns the steps
 * that start in it, c->n (<= CLOCK_EV_MAX; more in one block, after a long catch-up, are counted in c->steps but not
 * listed): step i starts at frame c->at[i] of the block (0..frames-1, ascending: sample resolution) and is step number
 * c->step[i] since the start (bit 0: the short step of a swing pair). The renderer may split its block at those frames
 * (synth_render takes any frame count). clock_restart(c) puts the internal clock back to step 0 at once (the next
 * clock_tick fires a step at frame 0): an arp that starts free (ARP BEAT SYNC off) restarts the clock (not under OUT:
 * engine.c); with BEAT SYNC on it waits for the next step event instead. It does nothing on an external clock (the master owns the phase).
 *
 * LFO sync: clock_quarter_inc(c) is the tempo as Q32 quarter notes per frame (0: none known), for mod.c
 * lfo_sync_inc.
 *
 * The host's transport (CLOCK_HOST: the plugin's core only, firmware/src/core/knobi_core.c defines it; the device build
 * never does, so none of this is in the firmware: plugin plan Task 5, spec "Clock from Live"). clock_host(c, at, playing,
 * rate, pos, frac) gives the host's transport as of frame `at` of the next clock_tick (at >= that tick's frames: a later
 * one; anchors in frame order: one at the last one's frame (or before it) replaces it, at most CLOCK_HOST_Q waiting:
 * more and the newest replaces the last): playing, the tempo
 * (rate: counts a frame, BPM x CLOCK_RATE_Q) and the position there (pos ticks, frac counts into the next; pos as a
 * signed 32-bit tick count: before the song's start, a count-in, is negative). clock_tick applies each anchor at its
 * frame (the block is cut there; the steps' frames stay the block's):
 *   - playing (the clock follows the host: clock_following): the position is the host's and the tempo its tempo; the
 *     steps are the host's grid: step g of the division starts at tick g / 2 x (even + odd) (+ even when g is odd)
 *     counted from position 0, its step number is g (signed 32-bit), so a step's number says where in the song it is.
 *     P_BPM (clock_set_bpm) is kept for later and not used, clock_restart and MIDI clock in (clock_midi) do nothing, the
 *     mode (clock_set_mode) is kept for later; a division change (clock_set_div) puts the step under way on the grid
 *     at once (no step fires: the next boundary of the new division comes on time).
 *   - an anchor where the position is within CLOCK_HOST_TOL of the clock's own (it ran on at the tempo since the last
 *     one): the position is the host's from there (a tempo ramp, rounding); the steps go on: one already fired never
 *     fires again (a host position a little behind it), one passed fires there.
 *   - any other (a start, a loop's wrap, a locate): a jump. The step under way at the new position is computed from
 *     the grid; an event at the anchor's frame says so (kind, below): on its boundary (CLK_EV_JUMP: a step, the
 *     notes sounding released first), inside its first half (CLK_EV_RESUME: the step plays at once, its gate ending
 *     where it would on the grid: gate[] frames), after it (CLK_EV_RELEASE: the notes sounding released, no step; the
 *     next step on the grid). No step doubled or skipped across a wrap: the old position's step at the wrap frame is not
 *     fired, the new position's is.
 *   - stopped (from playing): CLK_EV_RELEASE at its frame, and the clock is the firmware's again from where it is
 *     (its mode, at P_BPM if internal: the next step where the grid has it, at that tempo; no restart). Stopped
 *     anchors while not following change nothing: with the host stopped the clock is exactly the firmware's.
 * Every event has a kind (kind[]: CLK_EV_*, | CLK_EV_HOSTED for a step on the host's grid) and gate[] (RESUME: the
 * gate's frames left; else 0). Not integers-only (the core is host code): 64-bit arithmetic. */
#ifndef KNOBI_CLOCK_H
#define KNOBI_CLOCK_H
#include <stdint.h>

#define CLOCK_PPQ 24u                    /* ticks a quarter note (MIDI clock) */
#define CLOCK_RATE_Q 64u                 /* counts per frame per BPM */
#define CLOCK_U (110250u * CLOCK_RATE_Q) /* counts a tick: 44100 x 60 / 24 frames per tick at 1 BPM, x CLOCK_RATE_Q */
#define CLOCK_BPM_MIN 30
#define CLOCK_BPM_MAX 250
#define CLOCK_EV_MAX 8u                  /* step events listed per clock_tick */
#define CLOCK_EXT_N 25u                  /* tick times kept for the tempo of a MIDI clock: 24 intervals, a beat */
#define CLOCK_EXT_TIMEOUT 11025u         /* frames without a tick: not running (0.25 s; 3 ticks at 30 BPM = 11025) */
#define CLOCK_TAP_GAP_MS 2000u           /* a longer gap between taps starts a new tap count */
#define CLOCK_TAP_MIN 4u                 /* taps before the tempo changes */
#define CLOCK_OUT_OWED 8u                /* 0xF8 sent at most at once after a stall (clock_out_poll) */
#ifdef CLOCK_HOST
#define CLOCK_HOST_Q 16u                 /* host anchors waiting (clock_host) */
#define CLOCK_HOST_TOL (CLOCK_U / 4u)    /* a host position this close to the clock's: no jump (a quarter tick) */
#define CLOCK_HOST_RATE_MAX (999u * CLOCK_RATE_Q)   /* a host tempo is taken up to 999 BPM */
enum { CLK_EV_STEP, CLK_EV_JUMP, CLK_EV_RESUME, CLK_EV_RELEASE };
#define CLK_EV_KIND 0x0Fu
#define CLK_EV_HOSTED 0x80u              /* kind[]: a step on the host's grid (its step number: g) */
#endif

enum { CLK_DIV_HALF, CLK_DIV_QTR, CLK_DIV_8TH, CLK_DIV_8TH_D, CLK_DIV_8TH_S, CLK_DIV_8TH_T, CLK_DIV_16TH, CLK_DIV_16TH_S,
       CLK_DIV_16TH_T, CLK_DIV_32ND, CLK_DIV_COUNT };
/* the global MIDI CLOCK (params/globals.h G_MIDI_CLOCK), the reference's order */
enum { CLK_MIDI_OFF, CLK_MIDI_OUT, CLK_MIDI_IN, CLK_MIDI_THRU, CLK_MIDI_IN_NSS, CLK_MIDI_COUNT };
#define CLOCK_EXTERNAL(mode) ((mode) >= CLK_MIDI_IN)

extern const uint8_t CLOCK_DIV_TICKS[CLK_DIV_COUNT][2];   /* the even and odd step of a pair, ticks */

typedef struct clk {
    uint32_t rate;                       /* counts a frame now (0: an external clock with no tempo yet) */
    uint32_t pos, frac;                  /* the position: ticks, and counts into the next tick (< CLOCK_U) */
    uint32_t last, next;                 /* the tick the current step started at, the tick the next one starts */
    uint32_t steps;                      /* steps started since the start (clock_restart, 0xFA) */
    uint32_t ticks, beats;               /* ticks and quarter notes passed, never going back (MIDI out, the LED) */
    uint32_t now;                        /* frames since clock_init */
    uint32_t n;                          /* this block's step events */
    uint16_t at[CLOCK_EV_MAX];           /* their frames in the block */
    uint32_t step[CLOCK_EV_MAX];         /* their step numbers */
    int16_t bpm;                         /* the internal tempo */
    uint8_t div, mode, sub;              /* sub: pos % 24 */
    uint8_t go, alive, armed;            /* external: started (not stopped), ticks arriving, 0xFA waiting a tick */
    uint32_t ext_pos;                    /* external: the newest tick received, as a position */
    uint32_t ext_last;                   /* external: when it came (clk_t.now) */
    uint32_t ext_t[CLOCK_EXT_N];         /* external: the last ticks' times, a ring */
    uint8_t ext_n, ext_w;
    uint8_t tap_n;                       /* tap tempo (the main loop's clk_t) */
    uint32_t tap_t[5];
    uint32_t starts, start_ticks;        /* restarts to step 0 so far; ticks before the latest one's tick 0 */
#ifdef CLOCK_HOST
    uint8_t host;                        /* following the host (playing) */
    uint8_t hpart;                       /* clock_tick: a part of a block cut at an anchor (events kept, frames + hoff) */
    uint8_t hq_n;                        /* anchors waiting */
    uint32_t hoff;                       /* the part's first frame in the block */
    struct clk_host { uint32_t at, rate, pos, frac; uint8_t playing; } hq[CLOCK_HOST_Q];
    uint8_t kind[CLOCK_EV_MAX];          /* the events' kinds (CLK_EV_*) */
    uint32_t gate[CLOCK_EV_MAX];         /* CLK_EV_RESUME: the gate's frames left */
#endif
} clk_t;
typedef struct { int8_t mode; uint8_t started; uint32_t starts, sent; } clk_out_t;   /* MIDI clock out (main loop) */

void clock_init(clk_t *c);                               /* 120 BPM, QTR, OFF (internal), step 0 at the first frame */
void clock_set_bpm(clk_t *c, int bpm);                   /* 30..250 (clamped); the position is kept */
void clock_set_div(clk_t *c, int div);                   /* 0..9; the current step's next boundary re-derived */
void clock_set_mode(clk_t *c, int mode);                 /* CLK_MIDI_*: internal (OFF, OUT) or external */
int clock_tap(clk_t *c, uint32_t ms);                    /* the new BPM (and set), 0 while fewer than 4 taps */
void clock_midi(clk_t *c, uint8_t status, uint32_t ticks);   /* 0xF8 / FA / FB / FC at time ticks (frames, as
                                                           * clk_t.now); ignored on an internal clock */
uint32_t clock_tick(clk_t *c, uint32_t frames);          /* one block: the steps starting in it (c->n, c->at[], ...) */
void clock_restart(clk_t *c);                            /* internal: step 0 at the next clock_tick's frame 0 */
int clock_running(const clk_t *c);                       /* steps will come: internal, or an external clock arriving */
int clock_bpm_x10(const clk_t *c);                       /* the tempo followed, BPM x 10 (0: none known) */
uint32_t clock_quarter_inc(const clk_t *c);              /* Q32 quarter notes per frame (0: none known) */
void clock_out_init(clk_out_t *o);
/* the MIDI clock bytes to send now (into bytes, max >= 3 + CLOCK_OUT_OWED; returns how many): mode = the global MIDI
 * CLOCK, applied = the clock's mode (clk_t.mode: OUT applied means its start happened), starts / start_ticks / ticks as
 * the clock has them (a consistent snapshot) */
uint32_t clock_out_poll(clk_out_t *o, int mode, int applied, uint32_t starts, uint32_t start_ticks, uint32_t ticks,
                        uint8_t *bytes, uint32_t max);
#ifdef CLOCK_HOST
void clock_host(clk_t *c, uint32_t at, int playing, uint32_t rate, uint32_t pos, uint32_t frac);   /* see above */
int clock_following(const clk_t *c);     /* following the host's transport now (P_BPM not used) */
#endif

#endif
