/* SPDX-License-Identifier: GPL-3.0-only */
/* knobi_core (knobi_core.h): the firmware's behaviour code built for a computer, a unity file like app.c but outside
 * platform.c. What it compiles, unchanged, in app.c's order:
 *   params/params.c, params/factory.h + factory.c (the factory programs: none unless KNOBI_FACTORY_LOCAL / KNOBI_FACTORY_H),
 *   synth/seq.c, params/prog_seq.c (the edit buffer's sequence, program + sequence paired), params/globals.c (the
 *   tables and curves only: GLOBALS_PURE), synth/ (osc, filter, env, mod, voice, voices), engine.c (with clock, arp,
 *   seqplay), ui/seq_rec.c, ui/controls.c and ui/midi_map.c.
 * controls.c is here because midi_map.c (MIDI in) and the program and sequencer paths live on its state (ctl_glob,
 * ctl_prog_select, ctl_set, the recorder, HOLD / sustain, the MIDI wheel / pressure / bend levels) and its pass
 * (controls_poll) hands those levels to the engine: the core runs that pass with no panel (no buttons, encoders, pads
 * or faders: the stubs below), so the panel code is compiled but never acts.
 * What it does not compile: platform.c, tests/host.c, the screens (screens.c, panel_gfx.c, gfx.c, lcd.c), USB, the
 * flash stores (params/store_prog.c, params/seq_store.c: in their place the bank below), globals persistence.
 *
 * The main loop (app.c app_main / app_audio) becomes kc_block, once per KC_BLOCK frames: the MIDI queued (app.c
 * midi_in -> controls_midi), then one pass (controls_poll; the edit buffer's sequence to the player; the arp's MIDI out
 * ring read and dropped; the MIDI bend and wheel levels, the SMK's wheels at rest), then eng_render and app_audio's MONO.
 * On the firmware the pass runs every millisecond and a block every 5.8 ms; passes with nothing new change nothing, so
 * one pass after the messages is the same as the host build's when its next pass also comes before the block
 * (tests/scripts/core_ref.txt). Then the host build's clip to +-32767 (tests/host.c: the USB audio at full MASTER).
 *
 * Per instance (plan Task 2): every instance owns all mutable state. The firmware modules' file-scope state stays
 * declared where it is (the device build unchanged), in blocks wrapped "#ifdef KNOBI_CORE / #include
 * core/state/<block>.h / #else / the declarations / #endif": tools/core_state.py writes each header from the
 * declarations (a struct per block, its initial values, a macro per variable naming the field of the instance this
 * thread runs: KC_ST(block), through the instance's slot table). The core's own (the platform stubs' state, the bank in
 * use) is kc_st_core below, slot 0, the same way. Every kc_* call binds its instance to the calling thread (kc_bind: a
 * pthread key's value set to the instance's slots, the previous value put back at the end), so instances on different
 * threads never meet, and one thread may run several in turn. A pthread key, not _Thread_local: on macOS a thread-local
 * in a loaded bundle is allocated (malloc) on its first use by each thread, i.e. in the host's audio callback;
 * pthread_setspecific / getspecific never allocate (tests/unit/test_core.c first_render_no_alloc). Function statics in the shared code are scratch copies there (locals under KNOBI_CORE); voice_c4_q16 is a
 * constant. tests/unit/test_core.c no_mutable_globals checks the object has no other writable data.
 *
 * Not for the device: built with DINGHY_HOST (the firmware's host paths: no .pool sections, the CPU guard's clock is
 * the host's fake one, so the guard never sheds) and KNOBI_CPU1 0. */
#ifndef DINGHY_HOST
#define DINGHY_HOST 1
#endif
#ifndef BOARD_SMK37
#define BOARD_SMK37 1                           /* (the SMK-37 Pro's build) */
#endif
#undef KNOBI_CPU1
#define KNOBI_CPU1 0
#define KNOBI_CORE 1                            /* (the firmware's state blocks: per instance, core/state/) */
#if !defined KNOBI_FACTORY_LOCAL && !defined KNOBI_FACTORY_H && !defined KNOBI_NO_FACTORY
#define KNOBI_NO_FACTORY 1                      /* no OB-6 data unless asked for (plugin: KNOBI_FACTORY_LOCAL=ON) */
#endif
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include "knobi_core.h"
#include "state/list.h"                         /* (the blocks: KC_STATE_BLOCKS, KC_SLOTS) */

#if defined __GNUC__
#pragma GCC diagnostic ignored "-Wunused-function"   /* (the panel's and the stores' helpers the core never calls) */
#endif
/* Bit-exact with the firmware's host build needs the same float results from the filter's float path: no fused
 * multiply-add (a compiler fuses differently in different files and inlining contexts). Build the core, and the reference
 * it is compared with, with -ffp-contract=off (tests/Makefile FP_EXACT); clang also takes it from here */
#if defined __clang__
#pragma STDC FP_CONTRACT OFF
#endif

/* ---- what platform.c gives the shared code, with no hardware: the SMK-37's button ids, the millisecond clock
 * (kc_block advances it by the frames rendered), no button edges, encoders, pads or faders, no LEDs, no MIDI out */
enum { BTN_KNOB_BANK = 0, BTN_FADER_BANK = 1, BTN_LEFT = 2, BTN_RIGHT = 3, BTN_OCT_DOWN = 4, BTN_OCT_UP = 5,
       BTN_ARP = 6, BTN_LFO = 7, BTN_SEL = 8, BTN_ENV = 9, BTN_GLO = 10, BTN_SAVE = 11, BTN_HOME = 12, BTN_EDIT = 13,
       BTN_FX = 14, BTN_SEQ = 15, BTN_PLAY = 16, BTN_REC = 17 };   /* (platform.c, BOARD_SMK37) */
struct kc_st_core {                             /* the core's own state, per instance (as core/state/<block>.h) */
    uint32_t fm1_ms;                            /* platform.c's millisecond clock */
    struct { uint32_t notes, buttons; uint8_t fader[4], pad_press[16]; } fm1_in;   /* (platform.c fm1_in: at rest) */
    uint8_t smk_pad_events, store_sliced;
    uint32_t eng_fake_ticks, eng_fake_per_voice;   /* (engine.c's host CPU guard clock: never advanced here) */
    const kc_bank_t *bank;                      /* kc_bank: the bank this call reads (kc_bind: the newest kc_bank_set) */
};
/* the binding: kc_key's value on a thread is the slot table of the instance it runs (kc_bind), slot KC_IX_<block> the
 * instance's block. kc_key*: written once (pthread_once, kc_create), read-only after: no per-instance state */
static pthread_key_t kc_key;
static pthread_once_t kc_key_once = PTHREAD_ONCE_INIT;
static int kc_key_err;
static void kc_key_make(void) { kc_key_err = pthread_key_create(&kc_key, NULL); }
static inline void **kc_slots(void) { return (void **)pthread_getspecific(kc_key); }
#define KC_ST(b) ((struct kc_st_##b *)kc_slots()[KC_IX_##b])
#define KC_IX_core 0
#define fm1_ms (KC_ST(core)->fm1_ms)
#define fm1_in (KC_ST(core)->fm1_in)
#define smk_pad_events (KC_ST(core)->smk_pad_events)
#define store_sliced (KC_ST(core)->store_sliced)
#define eng_fake_ticks (KC_ST(core)->eng_fake_ticks)
#define eng_fake_per_voice (KC_ST(core)->eng_fake_per_voice)
#define kc_bank (KC_ST(core)->bank)
static uint32_t fm1_input_edges(uint32_t *released) { *released = 0; return 0; }
static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static int32_t smk_pad_take(void) { return -1; }
static void padled_set(uint32_t pad, uint8_t r, uint8_t g, uint8_t b) { (void)pad; (void)r; (void)g; (void)b; }
static void padled_flush(void) {}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }   /* (no MIDI out) */

/* ---- the programs (store_prog.h's calls, instead of params/store_prog.c's flash slots): the user layer
 * (kc_bank_set), else the factory program, else init, as prog_load reads a program never written. The sequence store
 * (seq_store.h's calls, instead of params/seq_store.c): a user program's sequence is its bank entry's (named by
 * generation KC_BANK_GEN); nothing is ever put (WRITE is the plugin's library's) */
#include "../params/params.c"
#include "../params/store_prog.h"
#include "../params/factory.h"
#include "../params/factory.c"
#include "../synth/seq.c"
#include "../params/seq_store.h"
#define KC_BANK_GEN 1u
/* a program / sequence through the firmware's own record formats, as its stores keep them: 0 ok, -1 not one it stores */
static int kc_copy_program(const program_t *from, program_t *to)
{
    uint8_t buf[PROGRAM_PACKED_MAX];
    int n = program_pack(from, buf, sizeof buf);
    return n < 0 || program_unpack(to, buf, (uint32_t)n) ? -1 : 0;
}
static int kc_copy_seq(const seq_t *from, seq_t *to)
{
    uint8_t buf[SEQ_PACKED_MAX];
    int n = seq_pack(from, buf, sizeof buf);
    return n < 0 || seq_unpack(to, buf, (uint32_t)n) ? -1 : 0;
}
uint32_t prog_count(void) { return PROG_COUNT; }
int prog_load(uint32_t n, program_t *p)
{
    if (n < PROG_COUNT && kc_bank && kc_bank->used[n]) {
        if (kc_copy_program(&kc_bank->program[n], p)) { program_init(p); return -2; }   /* (as store_prog.c: broken;
                                                 * never here: kc_bank_set refuses such a bank) */
        program_set_seq_gen(p, KC_BANK_GEN);
        return 0;
    }
    return prog_factory(n, p) == 0 ? 1 : -1;    /* (never written: the factory program, else init) */
}
int prog_save(uint32_t n, const program_t *p) { (void)n; (void)p; return -1; }
int seqst_init(void) { return 0; }
int seqst_get(uint16_t prog, uint16_t gen, seq_t *out)
{
    if (prog >= PROG_COUNT || gen != KC_BANK_GEN || !kc_bank || !kc_bank->used[prog]) return 0;
    return kc_copy_seq(&kc_bank->seq[prog], out) == 0;
}
int seqst_put(uint16_t prog, const seq_t *s, uint16_t *gen_out) { (void)prog; (void)s; (void)gen_out; return -1; }
int seqst_put_above(uint16_t prog, const seq_t *s, uint16_t floor, uint16_t *gen_out) { (void)floor; return seqst_put(prog, s, gen_out); }
void seqst_release(uint16_t prog, uint16_t keep_gen) { (void)prog; (void)keep_gen; }
void seqst_drop(uint16_t prog) { (void)prog; }
int seqst_format(void) { return 0; }
uint32_t seqst_used(void) { return 0; }
uint32_t seqst_total(void) { return 0; }

/* ---- the shared code, as app.c includes it */
#include "../params/prog_seq.c"
#define GLOBALS_PURE 1                          /* (the tables and curves: the globals are kept by the plugin) */
#include "../params/globals.c"
#include "../synth/osc.c"
#include "../synth/filter.c"
#include "../synth/env.c"
#include "../synth/mod.c"
#include "../synth/voice.c"
#include "../synth/voices.c"
#include "../engine.c"
#include "../ui/seq_rec.c"
#include "../ui/controls.c"
#include "../ui/midi_map.c"

/* ---- the instance */
#define KC_ST_FIELD(b) struct kc_st_##b b;
struct kc {
    void *slots[KC_SLOTS];                      /* kc_key's value while it runs: &core, then each block's */
    struct kc_st_core core;                     /* all the state the shared code reads and writes, this instance's */
    KC_STATE_BLOCKS(KC_ST_FIELD)
    _Atomic(const kc_bank_t *) bank_next;       /* kc_bank_set's (any thread) */
    _Atomic(const kc_bank_t *) bank_used;       /* core.bank, published for kc_bank_in_use */
    int16_t fifo[2u * KC_BLOCK];                /* the block rendered, handed out from fifo_at */
    uint32_t fifo_at;                           /* KC_BLOCK: empty */
    uint64_t frames;                            /* rendered so far (fm1_ms) */
    uint8_t mq[KC_MIDI_QUEUE][3];
    uint32_t mq_w, mq_r, mq_drops;
    int screen;                                 /* controls_poll's (the Play screen: no panel) */
    uint32_t seq_sent;                          /* the edit sequence's version the player has (app.c seq_sent) */
    program_t base;                             /* the program as loaded: edited = the edit buffer differs */
    uint32_t base_pw;                           /* eng_pw when base was taken */
    uint8_t forced_edit;                        /* loaded as edited (kc_program_set) */
    uint8_t pend;                               /* a kc_program_set the engine could not take yet */
    int32_t pend_n;
    uint8_t pend_edited;
    program_t pend_p;
    seq_t pend_s;
};

/* the instance k runs on this thread until kc_unbind (every kc_* call that touches the shared code: bound on entry,
 * the previous binding put back on return); it takes the newest bank set. The bank: the one published in bank_used is
 * the one read; the loop republishes until bank_next has not moved meanwhile (all seq_cst), so a bank replaced before
 * kc_bank_in_use returned something else is never read again (knobi_core.h kc_bank_in_use) */
typedef void *kc_binding_t;
static kc_binding_t kc_bind(const kc_t *kk)
{
    kc_t *k = (kc_t *)kk;                       /* (a const call binds too: the shared code's reads are macros) */
    kc_binding_t was = pthread_getspecific(kc_key);
    const kc_bank_t *b;
    pthread_setspecific(kc_key, k->slots);
    do {
        b = atomic_load(&k->bank_next);
        atomic_store(&k->bank_used, b);
    } while (atomic_load(&k->bank_next) != b);
    k->core.bank = b;
    return was;
}
static void kc_unbind(kc_binding_t was) { pthread_setspecific(kc_key, was); }

static void kc_track_load(kc_t *k)             /* a program load queued since: it is the new base */
{
    if (eng_pw == k->base_pw) return;
    k->base = eng_edit, k->base_pw = eng_pw, k->forced_edit = 0;
}
static int kc_edited(const kc_t *k)
{
    if (k->pend) return k->pend_edited;
    if (k->forced_edit || es_dirty || es_hash() != es_sum) return 1;
    for (uint32_t id = 0; id < P_COUNT_RESERVED; id++) if (P_IS_LIVE(id) && eng_edit.v[id] != k->base.v[id]) return 1;
    return memcmp(eng_edit.chord, k->base.chord, sizeof eng_edit.chord) != 0;
}

/* kc_program_set's load, as ctl_prog_select does one (the engine busy with another load: next block) */
static void kc_apply_pending(kc_t *k)
{
    if (!k->pend || ctl_wr != 0 || eng_program(&k->pend_p)) return;
    ctl_rec_forget();
    ctl_prog = k->pend_n, ctl_manual = 0, ctl_want_factory = 0, ctl_restore_ask = -1, ctl_prog_want = -1;
    ctl_notice = NT_NONE;
    es_seq = k->pend_s;
    es_set(k->pend_n, 0, 0, 0);
    k->pend = 0;
    kc_track_load(k);
    k->forced_edit = k->pend_edited;
}

static void kc_pass(kc_t *k)                   /* app.c app_main's pass, without the panel, the keys and the screen */
{
    kc_apply_pending(k);
    controls_poll(0, &eng_edit, &k->screen);
    kc_track_load(k);
    if (edit_seq_ver() != k->seq_sent && eng_seq_load(edit_seq()) == 0) k->seq_sent = edit_seq_ver();
    ctl_arp_out();                              /* (the ring read; the notes not sent) */
    eng_bend(8192 + ctl_midi_bend);             /* (the SMK's pitch wheel at rest: 0) */
    eng_wheel(ctl_midi_wheel);                  /* (its mod wheel at rest: 0; the higher of the two) */
}

static void kc_block(kc_t *k)
{
    int32_t out[2u * KC_BLOCK];
    while (k->mq_r != k->mq_w) {                /* app.c midi_in */
        const uint8_t *m = k->mq[k->mq_r % KC_MIDI_QUEUE];
        controls_midi(m[0], m[1], m[2]);
        kc_track_load(k);
        k->mq_r++;
    }
    kc_pass(k);
    memset(out, 0, sizeof out);
    eng_render(out, KC_BLOCK);
    if (ctl_glob[G_STEREO])                     /* app.c app_audio: MONO, both sides the sum */
        for (uint32_t i = 0; i < KC_BLOCK; i++) out[2u * i] = out[2u * i + 1u] = (out[2u * i] + out[2u * i + 1u]) >> 1;
    for (uint32_t i = 0; i < 2u * KC_BLOCK; i++) {   /* (tests/host.c's clip) */
        int32_t v = out[i];
        k->fifo[i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
    }
    k->fifo_at = 0;
    k->frames += KC_BLOCK;
    fm1_ms = (uint32_t)(k->frames * 1000u / KC_RATE);
}

kc_t *kc_create(void)
{
    kc_t *k;
    kc_binding_t was;
    if (pthread_once(&kc_key_once, kc_key_make) || kc_key_err || !(k = calloc(1, sizeof *k))) return NULL;
    k->slots[KC_IX_core] = &k->core;            /* (calloc: the core's state zero) */
#define KC_INIT(b) kc_st_##b##_init(&k->b), k->slots[KC_IX_##b] = &k->b;   /* (each block at its declarations' values) */
    KC_STATE_BLOCKS(KC_INIT)
#undef KC_INIT
    atomic_init(&k->bank_next, NULL);
    atomic_init(&k->bank_used, NULL);
    k->fifo_at = KC_BLOCK;
    k->screen = SCR_PLAY;
    was = kc_bind(k);
    eng_init();                                 /* (app_main's start, the globals at their defaults: ctl_glob) */
    controls_init();
    prog_seq_boot();
    k->seq_sent = edit_seq_ver() - 1u;
    ctl_prog_select(0);
    kc_track_load(k);
    kc_unbind(was);
    return k;
}

void kc_destroy(kc_t *k) { free(k); }

void kc_render(kc_t *k, int16_t *lr, uint32_t frames)
{
    kc_binding_t was = kc_bind(k);
    while (frames) {
        uint32_t n;
        if (k->fifo_at >= KC_BLOCK) kc_block(k);
        n = KC_BLOCK - k->fifo_at;
        if (n > frames) n = frames;
        memcpy(lr, &k->fifo[2u * k->fifo_at], n * 4u);
        lr += 2u * n, frames -= n, k->fifo_at += n;
    }
    kc_unbind(was);
}

void kc_midi(kc_t *k, const uint8_t *msg, uint32_t len)
{
    uint8_t *m;
    if (!len || msg[0] < 0x80u || (msg[0] >= 0xF0u && msg[0] < 0xF8u)) return;   /* (running status, SysEx, others) */
    if (k->mq_w - k->mq_r >= KC_MIDI_QUEUE) { k->mq_drops++; return; }
    m = k->mq[k->mq_w % KC_MIDI_QUEUE];
    m[0] = msg[0], m[1] = len > 1u ? msg[1] : 0u, m[2] = len > 2u ? msg[2] : 0u;
    k->mq_w++;
}
uint32_t kc_midi_dropped(const kc_t *k) { return k->mq_drops; }

int kc_param_info(uint16_t id, kc_param_info_t *out)
{
    const param_t *p = id < P_COUNT_RESERVED && P_IS_LIVE(id) ? param_get(id) : NULL;
    if (!p) return -1;
    if (out) {
        out->name = p->name;
        out->kind = p->kind == PK_SWITCH ? KC_KIND_SWITCH : p->kind == PK_SELECT ? KC_KIND_SELECT : KC_KIND_KNOB;
        out->min = p->min, out->max = p->max, out->def = p->def;
        out->bipolar = p->bipolar, out->section = p->section;
        out->labels = p->kind == PK_SELECT ? p->labels : NULL;
    }
    return 0;
}
void kc_param_set(kc_t *k, uint16_t id, int32_t v)
{
    kc_binding_t was;
    if (kc_param_info(id, NULL)) return;
    was = kc_bind(k);
    kc_apply_pending(k);                        /* (an edit after a set program: on that program) */
    if (k->pend) k->pend_p.v[id] = param_clamp(id, v);   /* (the engine still busy: the program set takes it) */
    else ctl_set(&eng_edit, id, v, 0);
    kc_unbind(was);
}
int32_t kc_param_get(const kc_t *k, uint16_t id)
{
    kc_binding_t was;
    int32_t v;
    if (kc_param_info(id, NULL)) return 0;
    if (k->pend) return k->pend_p.v[id];
    was = kc_bind(k);
    v = eng_edit.v[id];
    kc_unbind(was);
    return v;
}

int kc_program_valid(const program_t *p, const seq_t *s)
{
    program_t q;
    seq_t t;
    return p && s && kc_copy_program(p, &q) == 0 && kc_copy_seq(s, &t) == 0 ? 0 : -1;
}
int kc_program_set(kc_t *k, int32_t number, const program_t *p, const seq_t *s, int edited)
{
    program_t q;
    seq_t t;
    kc_binding_t was;
    if (number < 0 || number >= (int32_t)PROG_COUNT) return -1;
    /* as the firmware would store them: through its own record formats (a CRC, the version, the ranges; a sequence's
     * notes, velocities and tie rules); refused: nothing changes (a set still pending stays) */
    if (!p || !s || kc_copy_program(p, &q) || kc_copy_seq(s, &t)) return -1;
    k->pend_p = q, k->pend_s = t;
    program_set_seq_gen(&k->pend_p, 0);
    k->pend = 1, k->pend_n = number, k->pend_edited = (uint8_t)(edited != 0);
    was = kc_bind(k);
    kc_apply_pending(k);
    kc_unbind(was);
    return 0;
}
void kc_program_get(const kc_t *k, int32_t *number, program_t *p, seq_t *s, int *edited)
{
    kc_binding_t was = kc_bind(k);
    if (number) *number = k->pend ? k->pend_n : ctl_prog;
    if (p) {
        *p = k->pend ? k->pend_p : eng_edit;
        program_set_seq_gen(p, 0);
    }
    if (s) *s = k->pend ? k->pend_s : *edit_seq();
    if (edited) *edited = kc_edited(k);
    kc_unbind(was);
}
int kc_program_select(kc_t *k, int32_t number)
{
    kc_binding_t was;
    if (number < 0 || number >= (int32_t)PROG_COUNT) return -1;
    was = kc_bind(k);
    k->pend = 0;                                /* (a later load wins) */
    ctl_prog_select(number);                    /* (the engine busy: retried by the next pass, controls_poll) */
    kc_track_load(k);
    kc_unbind(was);
    return 0;
}

/* any thread: validated here, taken by the instance's next call (kc_bind) */
int kc_bank_set(kc_t *k, const kc_bank_t *bank)
{
    if (bank)
        for (uint32_t i = 0; i < KC_PROGRAMS; i++)
            if (bank->used[i] && kc_program_valid(&bank->program[i], &bank->seq[i])) return -1;
    atomic_store(&k->bank_next, bank);
    return 0;
}
const kc_bank_t *kc_bank_in_use(const kc_t *k)
{
    return atomic_load(&((kc_t *)k)->bank_used);
}

void kc_global_set(kc_t *k, int g, int32_t v)
{
    kc_binding_t was;
    if (g < 0 || g >= G_COUNT) return;
    was = kc_bind(k);
    ctl_set(&eng_edit, (uint16_t)B_GLOBAL(g), v, 0);
    kc_unbind(was);
}
int32_t kc_global_get(const kc_t *k, int g)
{
    kc_binding_t was;
    int32_t v;
    if (g < 0 || g >= G_COUNT) return 0;
    was = kc_bind(k);
    v = ctl_glob[g];
    kc_unbind(was);
    return v;
}

void kc_clock_host(kc_t *k, int playing, double bpm, double ppq_pos, uint32_t frame_offset)
{
    (void)k, (void)playing, (void)bpm, (void)ppq_pos, (void)frame_offset;   /* (plan Task 5) */
}
