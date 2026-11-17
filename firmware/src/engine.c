/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX engine: the one synth (voices.h) between the main loop and the audio interrupt. synth_* is not
 * thread-safe, so the main loop never calls it while audio may run: it calls eng_* (producer), which queue events;
 * app_audio calls eng_render (consumer), which applies every queued event, in order, then renders the block.
 * Included by app.c after params.c and the synth/ modules (unity build), and by tests/unit/test_render.c, the same way.
 *
 * The queue: a single-producer / single-consumer ring of ENG_QLEN (256, a power of two) 4-byte events, with free
 * running indices eng_qw (written only by the producer) and eng_qr (written only by the consumer); the producer
 * stores the event, then (compiler barrier) advances eng_qw; the consumer reads eng_qw once, applies up to it, then
 * advances eng_qr. One core: an interrupt sees either the old index or the new one, never half an event. Events:
 * note on/off, param edit, bend, mod wheel, pressure, HOLD, panic, program load, chord memory slot, all notes off
 * (released, HOLD unchanged), MIDI volume, master tune (the globals TRANSPOSE + TUNE, cents).
 * Overflow policy: the consumer empties the queue at every block (5.8 ms), so it fills only if more than 256 events
 * arrive within one block (37 keys pressed and released together are 74). Nothing that would leave the synth wrong is
 * ever lost:
 *  - the last ENG_RESERVE (64) slots take only note-offs, HOLD and PANIC: a note-on (or a bend, wheel, pressure, program
 *    or param event) that would eat into them is refused, counted in eng_drops, and eng_note returns -1 (a refused
 *    note-on just does not play);
 *  - a note-off that finds even the reserve full is kept in eng_off_pend (a bit per note) and eng_flush, which the main
 *    loop calls on every pass, queues it as soon as there is room; a note-on of a note whose off is still pending
 *    queues the off first (or is refused), so the order is kept. eng_note returns 1 for such a deferred off;
 *  - a param edit that does not fit is kept: eng_edit has the new value at once (the screens show it), the id is
 *    marked in eng_par_pend, and eng_flush queues the id's latest value when there is room (several edits meanwhile
 *    send one event). eng_param returns 1 then. A program load supersedes pending edits;
 *  - PANIC and ALL NOTES OFF that do not fit are kept (eng_panic_pend, eng_alloff_pend) and flushed first, and a
 *    note-on queues them before itself (or is refused), so a later note is never cut by an earlier PANIC; chord memory (eng_chord) always goes through
 *    eng_flush (six EV_CHORD events, all or none);
 *  - bend, wheel, pressure, MIDI volume and HOLD are levels: refused, they are not marked sent (eng_last_*), so the caller's next
 *    call (the main loop sends them every pass) tries again.
 * Bend, wheel and pressure are queued only when their value changes, param edits only when the value changes
 * (eng_edit holds the values sent), so a fader or wheel cannot flood it.
 * Program loads: a 220-byte program_t does not go through the ring; eng_program copies it into one buffer (eng_pbuf)
 * and queues a PROGRAM event that points at it (in order with the other events). The buffer is the producer's only
 * while no load is pending (eng_pw == eng_pr: eng_pw counts loads queued, written by the producer; eng_pr counts loads
 * applied, written by the consumer after synth_program has copied the buffer). A second load while one is pending
 * returns -1 and the caller tries again on its next pass (the main loop's work is never held up waiting for audio).
 *
 * Main-loop side: eng_edit is the producer's copy of the program in play (what the queue will have applied once it
 * drains): screens and probes read it, never the synth. eng_voices: voices sounding after the last block (consumer).
 * The audio side does nothing until eng_init has set the synth up (eng_ready), so app_audio may start first.
 *
 * The output fade (WRITE's deliberate dropout, ui/controls.c): eng_fade(1) asks the consumer to take the output to
 * silence along a straight line of ENG_FADE_MS (10 ms), eng_fade(0) back to full; the synth plays on underneath.
 * eng_faded() is true once ENG_FADE_QUIET whole blocks have gone out silent (both halves of the audio DMA's double
 * buffer hold zeros): a flash erase that holds the interrupts off then stalls on silence, not mid-waveform. The fade
 * scales what eng_render added into lr (app_audio's buffer starts at zero). */
#ifndef KNOBI_ENGINE_C
#define KNOBI_ENGINE_C
#include <stdint.h>

#define ENG_QLEN 256u                     /* events; a power of two */
#define ENG_RESERVE 64u                   /* the last slots: note-offs, HOLD and PANIC only */
#define ENG_BARRIER() __asm__ volatile("" ::: "memory")   /* a compiler barrier (as platform.c RING_PUBLISH) */

enum { EV_NOTE, EV_PARAM, EV_BEND, EV_WHEEL, EV_PRESSURE, EV_HOLD, EV_PANIC, EV_PROGRAM, EV_CHORD, EV_ALLOFF, EV_VOLUME,
       EV_TUNE };
typedef struct { uint8_t type, a; int16_t b; } eng_ev_t;   /* NOTE a note b vel; PARAM a id b value; BEND b 0..16383;
                                                              CHORD a slot 0..5 b note (0 = empty) */

static synth_t eng_synth;                 /* the consumer's (and eng_init's, before eng_ready) */
static eng_ev_t eng_q[ENG_QLEN];
static volatile uint32_t eng_qw, eng_qr, eng_drops;
static program_t eng_pbuf, eng_edit;
static volatile uint32_t eng_pw, eng_pr;
static volatile uint8_t eng_ready, eng_voices;
static int32_t eng_last_bend = 8192, eng_last_wheel, eng_last_press, eng_last_hold, eng_last_vol = 127, eng_last_tune;
#define ENG_FADE_ONE 32768                /* the fade's gain, Q15 */
#define ENG_FADE_MS 10
#define ENG_FADE_STEP (ENG_FADE_ONE / (ENG_FADE_MS * 441 / 10))   /* a frame: 74 -> 443 frames = 10.0 ms */
#define ENG_FADE_QUIET 2u                 /* whole silent blocks before eng_faded() */
static volatile uint8_t eng_fade_out;     /* producer: 1 = to silence */
static volatile int32_t eng_fade_g = ENG_FADE_ONE;   /* consumer: the gain now */
static volatile uint8_t eng_fade_quiet;   /* consumer: whole blocks gone out at gain 0 (saturates) */

#define ENG_LOAD_MAX 999u
static volatile uint16_t eng_load, eng_peak;   /* the load (eng_cpu) */
static volatile uint8_t eng_vlimit = NVOICE;
static volatile uint32_t eng_sheds;
static uint32_t eng_load_q3, eng_peak_q8;   /* (consumer only) */
static uint32_t eng_off_pend[4];          /* note-offs waiting for room: bit n = note n (producer only) */
static uint32_t eng_par_pend[(P_COUNT_PHASE1 + 31u) / 32u];   /* param ids whose latest eng_edit value is not queued */
static uint8_t eng_panic_pend, eng_alloff_pend, eng_chord_pend;

/* keep: the free slots that must remain after this event (0 for note-offs, HOLD, PANIC; ENG_RESERVE for the rest) */
static int eng_push(uint32_t type, uint32_t a, int32_t b, uint32_t keep)
{
    uint32_t w = eng_qw;
    if (w - eng_qr + keep >= ENG_QLEN) return -1;
    eng_q[w & (ENG_QLEN - 1u)].type = (uint8_t)type;
    eng_q[w & (ENG_QLEN - 1u)].a = (uint8_t)a;
    eng_q[w & (ENG_QLEN - 1u)].b = (int16_t)b;
    ENG_BARRIER();
    eng_qw = w + 1u;
    return 0;
}

/* ---- producer (main loop) */
static void eng_init(void)                /* the init program; call before audio needs it (or again: a full reset) */
{
    eng_ready = 0;
    ENG_BARRIER();
    synth_init(&eng_synth);
    program_init(&eng_edit);
    synth_program(&eng_synth, &eng_edit);
    eng_qw = eng_qr = eng_drops = 0;
    eng_pw = eng_pr = 0;
    for (unsigned i = 0; i < 4u; i++) eng_off_pend[i] = 0;
    for (unsigned i = 0; i < sizeof eng_par_pend / sizeof eng_par_pend[0]; i++) eng_par_pend[i] = 0;
    eng_panic_pend = eng_alloff_pend = eng_chord_pend = 0;
    eng_voices = 0;
    eng_last_bend = 8192, eng_last_wheel = eng_last_press = eng_last_hold = 0, eng_last_vol = 127, eng_last_tune = 0;
    eng_fade_out = 0, eng_fade_g = ENG_FADE_ONE, eng_fade_quiet = 0;
    eng_load = eng_peak = 0, eng_load_q3 = eng_peak_q8 = 0, eng_vlimit = NVOICE, eng_sheds = 0;
    ENG_BARRIER();
    eng_ready = 1;
}
#define ENG_BIT(m, i) ((m)[(i) >> 5] >> ((i) & 31u) & 1u)
#define ENG_SET(m, i) ((m)[(i) >> 5] |= 1u << ((i) & 31u))
#define ENG_CLR(m, i) ((m)[(i) >> 5] &= ~(1u << ((i) & 31u)))
static int eng_note(int note, int vel)    /* vel 0 = off. 0 queued, 1 an off kept for eng_flush, -1 a note-on refused */
{
    uint32_t n = (uint32_t)note;
    if (note < 0 || note > 127) return -1;
    if (vel <= 0) {
        if (eng_push(EV_NOTE, n, 0, 0)) { ENG_SET(eng_off_pend, n); return 1; }
        ENG_CLR(eng_off_pend, n);
        return 0;
    }
    if (eng_panic_pend) {                 /* a kept PANIC / ALL NOTES OFF goes before this note, not after it */
        if (eng_push(EV_PANIC, 0, 0, 0)) { eng_drops = eng_drops + 1u; return -1; }
        eng_panic_pend = 0;
    }
    if (eng_alloff_pend) {
        if (eng_push(EV_ALLOFF, 0, 0, 0)) { eng_drops = eng_drops + 1u; return -1; }
        eng_alloff_pend = 0;
    }
    if (ENG_BIT(eng_off_pend, n)) {       /* its off first */
        if (eng_push(EV_NOTE, n, 0, 0)) { eng_drops = eng_drops + 1u; return -1; }
        ENG_CLR(eng_off_pend, n);
    }
    if (eng_push(EV_NOTE, n, vel > 127 ? 127 : vel, ENG_RESERVE)) { eng_drops = eng_drops + 1u; return -1; }
    return 0;
}
static int eng_param(uint32_t id, int32_t v)   /* 0 queued (or unchanged), 1 kept for eng_flush, -1 not a param */
{
    int16_t c;
    if (id >= P_COUNT_PHASE1) return -1;
    c = param_clamp((uint16_t)id, v);
    if (eng_edit.v[id] == c && !ENG_BIT(eng_par_pend, id)) return 0;
    eng_edit.v[id] = c;
    if (eng_push(EV_PARAM, id, c, ENG_RESERVE)) { ENG_SET(eng_par_pend, id); return 1; }
    ENG_CLR(eng_par_pend, id);
    return 0;
}
static int eng_level(uint32_t type, int32_t *last, int32_t v, uint32_t keep)
{
    if (v == *last) return 0;
    if (eng_push(type, 0, v, keep)) { eng_drops = eng_drops + 1u; return -1; }
    *last = v;
    return 0;
}
static int eng_bend(int32_t bend14) { return eng_level(EV_BEND, &eng_last_bend, bend14 < 0 ? 0 : bend14 > 16383 ? 16383 : bend14, ENG_RESERVE); }
static int eng_wheel(int32_t v) { return eng_level(EV_WHEEL, &eng_last_wheel, v < 0 ? 0 : v > 127 ? 127 : v, ENG_RESERVE); }
static int eng_pressure(int32_t v) { return eng_level(EV_PRESSURE, &eng_last_press, v < 0 ? 0 : v > 127 ? 127 : v, ENG_RESERVE); }
static int eng_volume(int32_t v) { return eng_level(EV_VOLUME, &eng_last_vol, v < 0 ? 0 : v > 127 ? 127 : v, ENG_RESERVE); }
static int eng_hold(int on) { return eng_level(EV_HOLD, &eng_last_hold, on != 0, 0); }
static int eng_tune(int32_t cents) { return eng_level(EV_TUNE, &eng_last_tune, cents < -1250 ? -1250 : cents > 1250 ? 1250 : cents, ENG_RESERVE); }
static void eng_fade(int out) { eng_fade_out = (uint8_t)(out != 0); }
static int eng_faded(void) { return eng_fade_out && eng_fade_quiet >= ENG_FADE_QUIET; }
static int eng_panic(void)                /* 0 queued, 1 kept for eng_flush */
{
    eng_last_hold = 0;                    /* (panic clears HOLD) */
    if (eng_push(EV_PANIC, 0, 0, 0)) { eng_panic_pend = 1; return 1; }
    eng_panic_pend = 0;
    return 0;
}
static int eng_all_notes_off(void)       /* every note released (MIDI CC 123); HOLD stays. 0 queued, 1 kept */
{
    if (eng_push(EV_ALLOFF, 0, 0, 0)) { eng_alloff_pend = 1; return 1; }
    eng_alloff_pend = 0;
    return 0;
}
static void eng_chord(const uint8_t *c)   /* chord memory: 6 absolute MIDI notes, 0 = empty; queued by eng_flush */
{
    for (unsigned i = 0; i < 6u; i++) eng_edit.chord[i] = c[i] > 127u ? 0 : c[i];
    eng_chord_pend = 1;
}
/* the main loop, every pass: what was kept, in order; returns what is left. A kept param edit goes out here, after
 * whatever was queued since, so it may land after a later note-on: harmless (eng_edit has the value already; the
 * note starts on the value before it, and the edit then applies, smoothed, as any edit under a sounding note) */
static int eng_flush(void)
{
    int left = 0;
    unsigned i;
    if (eng_panic_pend && eng_push(EV_PANIC, 0, 0, 0) == 0) eng_panic_pend = 0;
    left += eng_panic_pend;
    if (eng_alloff_pend && eng_push(EV_ALLOFF, 0, 0, 0) == 0) eng_alloff_pend = 0;
    left += eng_alloff_pend;
    for (i = 0; i < 128u; i++)
        if (ENG_BIT(eng_off_pend, i)) {
            if (eng_push(EV_NOTE, i, 0, 0)) left++;
            else ENG_CLR(eng_off_pend, i);
        }
    for (i = 0; i < P_COUNT_PHASE1; i++)
        if (ENG_BIT(eng_par_pend, i)) {
            if (eng_push(EV_PARAM, i, eng_edit.v[i], ENG_RESERVE)) left++;
            else ENG_CLR(eng_par_pend, i);
        }
    if (eng_chord_pend) {
        if (eng_qw - eng_qr + ENG_RESERVE + 6u > ENG_QLEN) left++;
        else {
            for (i = 0; i < 6u; i++) eng_push(EV_CHORD, i, eng_edit.chord[i], 0);
            eng_chord_pend = 0;
        }
    }
    return left;
}
static int eng_program(const program_t *p)   /* -1: a load is still pending (or the queue is full): try again later */
{
    if (eng_pw != eng_pr) return -1;
    eng_pbuf = *p;
    ENG_BARRIER();
    eng_pw = eng_pw + 1u;
    if (eng_push(EV_PROGRAM, 0, 0, ENG_RESERVE)) { eng_pw = eng_pw - 1u; return -1; }
    eng_edit = *p;
    for (unsigned i = 0; i < P_COUNT_PHASE1; i++) eng_edit.v[i] = param_clamp((uint16_t)i, p->v[i]);
    for (unsigned i = 0; i < sizeof eng_par_pend / sizeof eng_par_pend[0]; i++) eng_par_pend[i] = 0;   /* its values win */
    eng_chord_pend = 0;
    return 0;
}
static int eng_active(void) { return eng_voices; }   /* voices sounding after the last block */

/* ---- consumer (audio interrupt) */
static void eng_drain(void)
{
    uint32_t r = eng_qr, w = eng_qw;
    ENG_BARRIER();
    for (; r != w; r++) {
        const eng_ev_t *e = &eng_q[r & (ENG_QLEN - 1u)];
        switch (e->type) {
        case EV_NOTE: synth_note(&eng_synth, e->a, e->b); break;
        case EV_PARAM: synth_param(&eng_synth, e->a, e->b); break;
        case EV_BEND: synth_bend(&eng_synth, e->b); break;
        case EV_WHEEL: synth_wheel(&eng_synth, e->b); break;
        case EV_PRESSURE: synth_pressure(&eng_synth, e->b); break;
        case EV_HOLD: synth_hold(&eng_synth, e->b); break;
        case EV_PANIC: synth_panic(&eng_synth); break;
        case EV_ALLOFF: synth_all_notes_off(&eng_synth); break;
        case EV_VOLUME: synth_volume(&eng_synth, e->b); break;
        case EV_TUNE: synth_tune(&eng_synth, e->b); break;
        case EV_CHORD: if (e->a < 6u) eng_synth.prog.chord[e->a] = (uint8_t)e->b; break;
        case EV_PROGRAM:
            synth_program(&eng_synth, &eng_pbuf);
            ENG_BARRIER();
            eng_pr = eng_pr + 1u;         /* the buffer is the producer's again */
            break;
        }
    }
    ENG_BARRIER();
    eng_qr = r;
}
/* The CPU guard's clock (voices.h synth_guard): the platform's tick counter (TIMER4, FM1_TICKS_PER_US); on the host a
 * fake clock that each render advances by eng_fake_per_voice ticks per voice sounding (the tests set it). */
#define ENG_TICKS_PER_FRAME_X10 5442u      /* 24 ticks per us x 10 / 44.1 kHz */
#ifdef DINGHY_HOST
uint32_t eng_fake_ticks, eng_fake_per_voice;
static uint32_t eng_ticks(void) { return eng_fake_ticks; }
#else
#if FM1_TICKS_PER_US != 24
#error "ENG_TICKS_PER_FRAME_X10 is computed for 24 ticks per microsecond"
#endif
static uint32_t eng_ticks(void) { return fm1_ticks(); }
#endif

#if KNOBI_CPU1
/* ---- the second core (a KNOBI_CPU1 build with the DUAL CORE global on; docs/API.md "Second core"): voices.c's
 * SYNTH_SPLIT is eng_split (app.c defines it before voices.c). Each control period, inside the audio interrupt: the
 * busy voices split (synth_balance: CPU0 the smaller half), set B posted to CPU1 (cpu1_job_voices into its own bus),
 * set A rendered here, the join, CPU1's bus added (int32 sums: bit-exact with one core in any split). Fewer than two
 * voices busy, DUAL CORE off, or CPU1 off: every voice here, as the default build. A join that fails (the job late or
 * lost: the HAL has retired CPU1, held until power-off) renders set B here in the same period, so the block is whole
 * (a late job may have run part of it: one block can glitch), and from then on one core (eng_c1_failed; Globals shows
 * "C1 FAIL"). The job runs from the flash: every job is joined before the interrupt returns, so a flash erase or
 * write (the main loop's, interrupts off) always finds CPU1 idle in its RAM loop (hal/fm1_flash_access.h).
 * Cache lines: 32 bytes on the AC791N (the SDK's include_lib/driver/cpu/wl82/asm/cache.h CACHE_LINE_COUNT, Melodee's
 * and felucca-bsp's mailbox lines). voice_t is a whole number of lines and synth_t.voice[] starts on one
 * (SYNTH_VOICE_ALIGN), so a voice of set A and one of set B never share a line; CPU1's bus and its time counter are
 * on lines of their own, away from CPU0's words. (The cores share the caches, and internal SRAM needs no cache
 * maintenance, only the HAL's csync for the order: the alignment keeps them off each other's lines all the same.)
 * The CPU guard (synth_guard) keeps timing CPU0's whole render, the joins' waits included: the time the block takes.
 * CPU1's own job time (TIMER4 on CPU1) is eng_c1_load, % of the block, for the Globals line ("C1 ON 40%"). */
#define ENG_C1_LINE 32u                   /* bytes: the AC791N's cache line */
#define ENG_C1_JOIN_TICKS (1500u * FM1_TICKS_PER_US)   /* a period's join, from its post: a 16-frame period is 363 us of
                                           * audio; set B's share of a heavy period takes ~100..200 us in the emulator */
_Static_assert(sizeof(voice_t) % ENG_C1_LINE == 0u, "voice_t: a whole number of cache lines (SYNTH_VOICE_ALIGN)");
_Static_assert(__builtin_offsetof(synth_t, voice) % ENG_C1_LINE == 0u, "synth_t.voice on a cache line");
static struct eng_c1 {
    synth_t *s;                           /* CPU0's, written before each post */
    uint32_t mask, n;
    int32_t bus[2u * VOICE_CTL] __attribute__((aligned(32)));   /* CPU1's: set B's period */
    uint32_t ticks __attribute__((aligned(32)));                /* CPU1's: its jobs' time, summed (TIMER4 ticks) */
} eng_c1 __attribute__((aligned(32)));
static volatile uint8_t eng_dual;         /* producer (main loop): the DUAL CORE global */
static volatile uint8_t eng_c1_failed;    /* consumer: a join failed: one core until power-off */
static volatile uint32_t eng_c1_jobs;     /* consumer: periods set B rendered on CPU1 */
static volatile uint16_t eng_c1_load;     /* consumer: CPU1's job time, % of the block, smoothed as eng_load */
static uint32_t eng_c1_load_q3, eng_c1_seen;
#if KNOBI_CPU1_TEST == 7 || KNOBI_CPU1_TEST == 9
#define ENG_C1_HANG_AT 1000u              /* (fm1-emu cases dualfail / dualmid: the 1000th voice job never returns) */
static uint32_t eng_c1_test_jobs;
static volatile uint32_t eng_c1_test_spin;
#endif

static void cpu1_job_voices(void *arg)    /* on CPU1 (from the flash): set B of this period into its own bus */
{
    struct eng_c1 *j = arg;
    uint32_t t0 = eng_ticks(), n = j->n;
#if KNOBI_CPU1_TEST == 7
    if (++eng_c1_test_jobs == ENG_C1_HANG_AT)
        for (;;) eng_c1_test_spin++;      /* (before it touches a voice: the fallback's output stays exact) */
#endif
    for (uint32_t i = 0; i < 2u * n; i++) j->bus[i] = 0;
#if KNOBI_CPU1_TEST == 9
    if (++eng_c1_test_jobs == ENG_C1_HANG_AT) {   /* (dualmid: one voice of set B rendered, then the hang: CPU0 renders
                                                   * set B again from that voice's advanced state) */
        synth_render_set(j->s, j->mask & (0u - j->mask), j->bus, n);
        for (;;) eng_c1_test_spin++;
    }
#endif
    synth_render_set(j->s, j->mask, j->bus, n);
    j->ticks += eng_ticks() - t0;
}
#if KNOBI_CPU1_PROBE
/* the CPU1 probe build (cpu1_probe.c): its own test renders force one core (0) or two (1) whatever DUAL CORE says;
 * -1 (outside them): DUAL CORE decides, as in any KNOBI_CPU1 build */
static volatile int8_t eng_split_force = -1;
static uint32_t eng_probe_ticks;          /* the probe's own time in the interrupt last block: the guard counts it too */
#define ENG_DUAL_NOW() (eng_split_force < 0 ? eng_dual : (uint8_t)eng_split_force)
#else
#define ENG_DUAL_NOW() eng_dual
#endif
static void eng_split(synth_t *s, int32_t *bus, uint32_t n)
{
    uint32_t a, b;
    if (ENG_DUAL_NOW() && !eng_c1_failed && synth_balance(s, &a, &b) >= 2u) {
        eng_c1.s = s, eng_c1.mask = b, eng_c1.n = n;
        if (cpu1_post(cpu1_job_voices, &eng_c1) == 0) {
            synth_render_set(s, a, bus, n);
            if (cpu1_join(ENG_C1_JOIN_TICKS) < 0) {   /* late or lost: set B here, one core from now on. A job that
                                           * stopped part way (late) left some of set B advanced by this period: they
                                           * render it again from there, a jump of up to 16 frames in their waveform,
                                           * once (fm1-emu case dualmid: the step stays within the clean run's) */
                eng_c1_failed = 1;
                synth_render_set(s, b, bus, n);
                return;
            }
            fm1_cpu1_sync();              /* (CPU1's writes, its bus and set B's voices, before CPU0 reads them) */
            for (uint32_t i = 0; i < 2u * n; i++) bus[i] += eng_c1.bus[i];
            eng_c1_jobs = eng_c1_jobs + 1u;
            return;
        }
    }
    synth_render_set(s, SYNTH_ALL_VOICES, bus, n);
}
static void eng_c1_cpu(uint32_t budget)   /* (consumer, after the block's joins: CPU1 is idle) */
{
    uint32_t t = eng_c1.ticks, d = t - eng_c1_seen, pct;
    eng_c1_seen = t;
    pct = d >= budget * 10u ? ENG_LOAD_MAX : d * 100u / budget;
    if (pct > ENG_LOAD_MAX) pct = ENG_LOAD_MAX;
    eng_c1_load_q3 += pct - (eng_c1_load_q3 >> 3);
    eng_c1_load = (uint16_t)(eng_c1_load_q3 >> 3);
}
/* main loop: 0 off (DUAL CORE off), 1 on (CPU1 renders), 2 FAIL (on, but CPU1 retired or never started) */
static int eng_c1_status(void)
{
    int st;
    if (!eng_dual) return 0;              /* (off is OFF, whatever happened to CPU1) */
    st = cpu1_state();
    return eng_c1_failed || st == CPU1_FAIL || st == CPU1_OFF ? 2 : 1;
}
#endif

/* The load, for the Globals screen's diagnostics line (consumer writes, main loop reads; each a single aligned word or
 * byte): eng_load the block's load in % of its time (used x 100 / budget, up to 999), smoothed (a one-pole of 8
 * blocks, ~46 ms); eng_peak the highest block's, decaying toward the load with a time constant of 128 blocks
 * (~0.74 s: a peak is gone in about a second); eng_vlimit the CPU guard's voice limit; eng_sheds its sheds since
 * eng_init. */
static void eng_cpu(uint32_t used, uint32_t budget)
{
    uint32_t pct;
    if (!budget) return;
    pct = used >= budget * 10u ? ENG_LOAD_MAX : used * 100u / budget;   /* (32-bit: used < 10 budget, ~1.4 M ticks) */
    if (pct > ENG_LOAD_MAX) pct = ENG_LOAD_MAX;
    eng_load_q3 += pct - (eng_load_q3 >> 3);
    if (pct << 8 >= eng_peak_q8) eng_peak_q8 = pct << 8;
    else eng_peak_q8 -= (eng_peak_q8 - (pct << 8)) >> 7;
    eng_load = (uint16_t)(eng_load_q3 >> 3);
    eng_peak = (uint16_t)((eng_peak_q8 + 128u) >> 8);
    eng_vlimit = eng_synth.vlimit;
    eng_sheds = eng_synth.sheds;
}

/* eng_render's 'used' is wall-clock ticks around synth_render: it includes the time of any interrupt that preempted
 * it (USB, the key scan), which is time the block did not have either, so the guard counts it too. */
static void eng_render(int32_t *lr, uint32_t frames)   /* app_audio: adds into L R pairs, Q15 (then the fade) */
{
    int32_t g = eng_fade_g, tgt;
    if (!eng_ready) return;
    eng_drain();
    uint32_t t0 = eng_ticks();
    synth_render(&eng_synth, lr, frames);
#ifdef DINGHY_HOST
    eng_fake_ticks += eng_fake_per_voice * (uint32_t)synth_active(&eng_synth);
#endif
    uint32_t used = eng_ticks() - t0, budget = frames * ENG_TICKS_PER_FRAME_X10 / 10u;
#if KNOBI_CPU1_PROBE
    used += eng_probe_ticks;               /* (the CPU1 probe's checks of the block before: in the CPU line and the
                                            * guard, so a crackle from the probe's cost sheds voices, as any load) */
    eng_probe_ticks = 0;
#endif
    synth_guard(&eng_synth, used, budget);
    eng_cpu(used, budget);
#if KNOBI_CPU1
    eng_c1_cpu(budget);
#endif
    eng_voices = (uint8_t)synth_active(&eng_synth);
    tgt = eng_fade_out ? 0 : ENG_FADE_ONE;
    if (g == ENG_FADE_ONE && tgt == ENG_FADE_ONE) return;   /* (the usual case: nothing to do) */
    if (g == 0 && tgt == 0) {                  /* silent the whole block */
        for (uint32_t i = 0; i < 2u * frames; i++) lr[i] = 0;
        if (eng_fade_quiet < 255u) eng_fade_quiet = (uint8_t)(eng_fade_quiet + 1u);
        return;
    }
    eng_fade_quiet = 0;
    for (uint32_t i = 0; i < frames; i++) {    /* a straight line to the target, a step a frame */
        g = g < tgt ? (g + ENG_FADE_STEP > tgt ? tgt : g + ENG_FADE_STEP) : g > tgt ? (g - ENG_FADE_STEP < tgt ? tgt : g - ENG_FADE_STEP) : g;
        lr[2u * i] = (int32_t)((int64_t)lr[2u * i] * g >> 15);   /* (64-bit: whatever lr holds, no overflow) */
        lr[2u * i + 1u] = (int32_t)((int64_t)lr[2u * i + 1u] * g >> 15);
    }
    eng_fade_g = g;
}
#endif
