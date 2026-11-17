/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX: a standalone OB-6-inspired polysynth for the M-Vave SMK-37 Pro, on the SMK BASE platform (platform.c
 * below does the rest: updates, the USB rescue, MIDI, USB audio, the keys, pads, knobs, faders, wheels, LEDs, the
 * screen, flash).
 *
 *   keys       the synth (synth/, engine.c), with the key's velocity through the VEL CURVE global; the notes go out
 *              over USB-MIDI (MIDI CH); LOCAL OFF: MIDI only
 *   OCTAVE-/+  the octave, -2..+2 (kept in flash with the globals)
 *   K1         the volume (platform.c)
 *   K2..K8, faders, pads, the other buttons: ui/controls.c (the map: docs/CONTROLS.md): the screen's knobs, the
 *              faders through the pot mode, top pads = switches, bottom pads = pressure (aftertouch), sections
 *   pitch      the wheel bends +-BEND RANGE (the program's) at its ends (plus MIDI pitch bend)
 *   mod wheel  0..127: adds LFO depth (the higher of it and MIDI CC 1)
 *   buttons    lit while held
 *   MIDI in    ui/midi_map.c (docs/MIDI.md): notes, pressure, bend, CCs (own map), bank select (CC 32), program change
 *   programs   params/store_prog.c: 500 in the flash slots (bank / tens / ones), WRITE (fade out, save, fade in),
 *              MANUAL; the globals (params/globals.c) and the last program in blobs 0 / 1 (A/B), saved after
 *              3 s with nothing played
 *   screen     the panels (ui/screens.c, panel_gfx.c): a screen a section, its knobs, switches and faders as bound;
 *              the header shows the octave and the last value edited; Globals shows the raw wheels and faders
 *
 * The synth is not thread-safe: the main loop talks to it only through engine.c's event queue (eng_*), which
 * app_audio drains before each render. The program at boot: the last one loaded (the globals), else program 0;
 * nothing stored there: the factory program (an optional local factory bank, docs/FACTORY_BANK.md), else
 * program_init (every default).
 * Rules of the road (no libgcc): no double, no 64-bit division; integers, except the filter's float path, which runs on
 * the FPU (the app is built with -mfprev1, tools/build.py APP_FPU; docs/RULES.md, docs/MEASUREMENTS.md).
 * tests/run_host.sh runs this file on a computer: the screen as PNG, the sound as WAV.
 * What platform.c offers: docs/API.md. The chip's rules: docs/RULES.md. */
#define APP_NAME "KNOBI-SIX"
#define STORE_MAGIC 0x36424E4Bu                 /* "KNB6": settings and blobs (store.h); see docs/IDENTITY.md */
static void app_idle(void);
#define LCD_IDLE app_idle                       /* while the screen's DMA runs: MIDI in (platform.c) */
#include "platform.c"
#include "smk_wheel.h"
#ifndef FELUCCA_ID
#define FELUCCA_ID "HOST"                       /* (platform.c gives it to the device build only) */
#endif

/* ---- the sound: the synth, through the event queue (engine.c); app_audio runs in the audio ISR */
#include "params/params.c"
#include "params/store_prog.c"                  /* programs in the flash slots */
#include "params/globals.c"                     /* the globals in blobs 0 and 1, in turn */
#include "synth/osc.c"
#include "synth/filter.c"
#include "synth/env.c"
#include "synth/mod.c"
#include "synth/voice.c"
#include "synth/voices.c"
#include "engine.c"
#include "ui/controls.c"                        /* the panel: screens' bindings, pot modes, pads (after engine.c) */
#include "ui/midi_map.c"                        /* MIDI in and the CC map */
#include "ui/panel_gfx.c"                       /* the panel's drawing parts: knobs, 7-segment digits, wood... */
#include "ui/screens.c"                         /* the screens (after controls.c: they show its bindings) */

static volatile uint8_t app_mono;               /* the STEREO global = MONO: both sides the sum (main loop sets it) */
static void app_audio(int32_t *out, uint32_t n)
{
    eng_render(out, n);                         /* the queued events, then n frames added into out (WRITE's fade) */
    if (app_mono)
        for (uint32_t i = 0; i < n; i++) out[2u * i] = out[2u * i + 1u] = (out[2u * i] + out[2u * i + 1u]) >> 1;
}

/* ---- notes (main loop) */
static void play(uint32_t note, uint32_t vel) { if (note <= 127u && vel && ctl_glob[G_LOCAL]) eng_note((int)note, (int)vel); }
static void stop(uint32_t note) { if (note <= 127u) eng_note((int)note, 0); }   /* (even LOCAL OFF: never a stuck note) */

/* the mod wheel, raw ADC -> 0..127. Self-calibrating like the pitch wheel (smk_wheel.h), as its direction and travel
 * on a unit are not known yet: 0 at the first reading (where the wheel rests at power-up: its bottom, we assume),
 * 127 at the farthest reading seen from there in either direction (at least MW_SPAN0 counts), a dead band of MW_DEAD
 * counts at the rest. To check on the hardware (the Globals screen shows the raw values: WHEELS) */
#define MW_SPAN0 160
#define MW_DEAD 8
static int32_t mw_rest = -1, mw_span = MW_SPAN0;
static int32_t mod_wheel(int32_t raw)
{
    int32_t d;
    if (mw_rest < 0) mw_rest = raw;
    d = raw - mw_rest;
    if (d < 0) d = -d;
    if (d > mw_span) mw_span = d;
    d -= MW_DEAD;
    return d <= 0 ? 0 : d * 127 / (mw_span - MW_DEAD);
}

/* ---- the screen: the panels (ui/screens.c), drawn by the main loop, only what changed */
static int screen = SCR_BOOT;                   /* enum screen_id (ui/controls.h) */
#define enc_pos ctl_enc_pos                     /* detents per encoder (controls.c counts K2..K8, the app K1 on the host) */

/* ---- MIDI in: from the main loop, and from app_idle while the screen draws (idle_ok) */
static globals_t set;                           /* what survives a power-off: the globals, the octave, the last program */
static uint32_t idle_ok, set_saves;
static volatile uint32_t set_busy_ms;           /* the last time anything played: a voice, a key or pad, a MIDI note */
#define SET_PROG_MS 30000u                      /* the last program alone: saved at most this often */
#define SET_QUIET_MS 3000u                      /* a save (a ~50 ms flash erase stalls the sound) waits for this long
                                                 * with nothing played: not in the gap between two staccato notes */
static void set_now(globals_t *g)               /* the globals as they are now */
{
    for (unsigned i = 0; i < G_COUNT; i++) g->v[i] = ctl_glob[i];
    g->oct = set.oct;
    g->last_prog = (uint16_t)(ctl_prog >= 0 && ctl_prog < (int32_t)PROG_COUNT ? ctl_prog : 0);
}
static int set_same(const globals_t *a, const globals_t *b)
{
    for (unsigned i = 0; i < G_COUNT; i++) if (a->v[i] != b->v[i]) return 0;
    return a->oct == b->oct && a->last_prog == b->last_prog;
}

static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2)) {           /* ui/midi_map.c: the MIDI CH global, the CC map */
        if ((st & 0xE0u) == 0x80u) set_busy_ms = fm1_ms;   /* a note on or off (any channel): playing */
        controls_midi(st, d1, d2);
    }
}
static void app_idle(void)
{
    if (idle_ok) midi_in();
}

/* ---- the main loop */
#define KEY0 53u                                /* the note of F3, key 5 of the keybed, at octave 0 */
static void app_main(void)
{
    uint8_t key_note[FM1_NKEY] = {0};           /* the note each key started (0: none) */
    uint32_t prev = 0, k, t_change = 0, dirty = 0, t_saved = 0;
    globals_t saved, now, last;
    smk_pw_t wheel = {0};
    eng_init();                                 /* the synth with the init program; app_audio renders from now on */
    controls_init();                            /* the pads are the app's (events), the faders' positions now */
    globals_load(&set);                         /* (nothing saved or broken: the defaults) */
    for (k = 0; k < G_COUNT; k++) ctl_glob[k] = set.v[k];
    ctl_prog_select(set.last_prog);             /* the program last loaded (nothing stored there: init) */
    saved = last = set;

    scr_oct = set.oct;
    screen_draw(screen, &eng_edit, &eng_synth, 1);   /* the boot screen, whole, before the sound starts mattering */
    while (scr_strip < 240u) screen_draw(screen, &eng_edit, &eng_synth, 0);

    for (;;) {
        uint32_t keys, down, up, b, base = (uint32_t)((int32_t)KEY0 + 12 * set.oct), ch, vc = (uint32_t)ctl_glob[G_VEL_CURVE];
        int32_t e;
        platform_poll();
        controls_poll(0, &eng_edit, &screen);   /* encoders K2..K8, faders, pads, buttons: through the queue */
        ch = controls_tx_ch();

        keys = fm1_in.notes;                    /* bit n: keybed key F3 + n (n = 0..26), the middle of the keybed */
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {             /* (a key stops the note it started: the octave may change meanwhile) */
            uint32_t key = SMK_KEY_F3 + k, vel = (uint32_t)glob_vel_curve((int)vc, fm1_in.key_vel[key]);
            if (down >> k & 1u) {
                key_note[key] = (uint8_t)(base + k);
                play(key_note[key], vel);
                controls_key(key_note[key]);    /* (chord memory capture while CHORD is held) */
                midi_send(0x90u | ch, key_note[key], vel);
            }
            if (up >> k & 1u && key_note[key]) { stop(key_note[key]); midi_send(0x80u | ch, key_note[key], 0); key_note[key] = 0; }
        }
        while ((e = smk_extkey_take()) >= 0) {  /* the keys left and right of those 27: key << 8 | velocity (0 = up) */
            uint32_t key = (uint32_t)e >> 8, vel = (uint32_t)glob_vel_curve((int)vc, (int)((uint32_t)e & 0x7Fu));
            if (key >= FM1_NKEY) continue;
            if (vel) {
                key_note[key] = (uint8_t)(base + key - SMK_KEY_F3);
                play(key_note[key], vel);
                controls_key(key_note[key]);
                midi_send(0x90u | ch, key_note[key], vel);
            } else if (key_note[key]) { stop(key_note[key]); midi_send(0x80u | ch, key_note[key], 0); key_note[key] = 0; }
        }
        {                                       /* the wheels: queued only when the value changes */
            int32_t bd = smk_pw_bend(&wheel, fm1_in.wheel[0], fm1_ms), mw = mod_wheel(fm1_in.wheel[1]);   /* 1/4096 semitone, +-2 at the ends */
            eng_bend(8192 + bd + ctl_midi_bend);   /* the ends = 0 and 16383: +-BEND RANGE; plus MIDI's bend */
            eng_wheel(mw > ctl_midi_wheel ? mw : ctl_midi_wheel);
        }

        b = ctl_pressed;                        /* buttons pressed since the last pass (controls_poll took them) */
        if (b & (1u << BTN_OCT_DOWN) && set.oct > -2) set.oct--;
        if (b & (1u << BTN_OCT_UP) && set.oct < 2) set.oct++;
        app_mono = (uint8_t)(ctl_glob[G_STEREO] != 0);
#ifdef DINGHY_HOST
        if ((e = fm1_enc_take(0)) != 0) enc_pos[0] += e;   /* (K1: no MASTER on the host) */
#endif

        midi_in();

        for (k = 0; k < 18u; k++)               /* a button lit while it is held */
            fm1_led_key(k, fm1_in.buttons >> k & 1u);

        idle_ok = 1;                            /* the drawing below may read MIDI meanwhile (LCD_IDLE) */
        scr_oct = set.oct;
        screen_draw(screen, &eng_edit, &eng_synth, 0);   /* what changed, or a strip of a new screen */
        idle_ok = 0;

        for (k = 0; k < FM1_NKEY && !key_note[k]; k++) {}
        if (eng_active() || k < FM1_NKEY || fm1_in.notes) set_busy_ms = fm1_ms;   /* a voice sounding, a key held */
        for (k = 0; k < 16u; k++) if (fm1_in.pad_press[k]) set_busy_ms = fm1_ms; /* a pad pressed (pressure) */

        set_now(&now);                          /* a global, the octave or the program changed: save it later */
        if (!set_same(&now, &last)) { last = now; t_change = fm1_ms; }
        dirty = !set_same(&now, &saved);
        if (dirty) {                            /* only the program changed: at most once in SET_PROG_MS (wear: a
                                                 * player stepping through programs would erase on every pause) */
            uint16_t lp = saved.last_prog;
            saved.last_prog = now.last_prog;
            dirty = !set_same(&now, &saved) || !set_saves || fm1_ms - t_saved >= SET_PROG_MS;
            saved.last_prog = lp;
        }
        if (dirty && !ctl_wr && fm1_ms - t_change > 2000u && fm1_ms - set_busy_ms >= SET_QUIET_MS) {
                                                /* 2 s after the last change, after 3 s with nothing played (an erase
                                                 * stalls the sound) */
            now.seq = set.seq;
            if (globals_save(&now) == 0) {      /* (the other blob of the two; now.seq the saved one's) */
                set = saved = now;
                t_saved = fm1_ms;
                set_saves++;
            } else t_change = fm1_ms;           /* failed: still dirty, try again in 2 s (not every pass) */
        }
    }
}

#ifdef DINGHY_HOST
static int host_probe(const char *name, const int *a, int n, int *got)   /* tests/host.c's "T expect V NAME [A..]" */
{
    uint32_t a0 = n > 0 ? (uint32_t)a[0] : 0u;
    if (!strcmp(name, "oct")) *got = set.oct;                          /* the octave, -2..+2 */
    else if (!strcmp(name, "sounding")) {                              /* sounding NOTE: 1 while a voice plays it */
        uint32_t v;                                                    /* with its key down (or latched by HOLD) */
        *got = 0;
        for (v = 0; v < NVOICE; v++)
            if (eng_synth.voice[v].busy && eng_synth.voice[v].note == a0 && (eng_synth.voice[v].key || eng_synth.voice[v].latched))
                *got = 1;
    }
    else if (!strcmp(name, "voices")) *got = eng_active();             /* voices sounding (releases included) */
    else if (!strcmp(name, "enc")) *got = (int)enc_pos[a0 & 7u];       /* enc K: encoder K's position (0 = K1) */
    else if (!strcmp(name, "param")) *got = a0 < P_COUNT_PHASE1 ? eng_edit.v[a0] : -1;   /* param ID: its value */
    else if (!strcmp(name, "bend")) *got = eng_last_bend;              /* the bend sent, 0..16383 */
    else if (!strcmp(name, "wheel")) *got = eng_last_wheel;            /* the mod wheel sent, 0..127 */
    else if (!strcmp(name, "screen")) *got = screen;                   /* the screen, enum screen_id (ui/controls.h) */
    else if (!strcmp(name, "row")) *got = ctl_row[screen];             /* the screen's row (SCALE), 0 or 1 */
    else if (!strcmp(name, "at")) *got = controls_aftertouch();        /* the bottom pads' aftertouch, 0..127 */
    else if (!strcmp(name, "pressure")) *got = eng_last_press;         /* the pressure sent (pads or MIDI) */
    else if (!strcmp(name, "hold")) *got = eng_last_hold;              /* HOLD sent (the pad or sustain) */
    else if (!strcmp(name, "holdpad")) *got = ctl_hold;                /* the HOLD pad's latch (Play pad 3) */
    else if (!strcmp(name, "vol")) *got = eng_last_vol;                /* the MIDI volume sent (CC 7), 0..127 */
    else if (!strcmp(name, "glob")) *got = a0 < G_COUNT ? ctl_glob[a0] : -1;   /* glob G: a global (controls.h G_*) */
    else if (!strcmp(name, "ccout")) *got = midi_cc_sent[a0 & 127u];   /* ccout N: the last value sent on CC N, -1 */
    else if (!strcmp(name, "prgreq")) *got = ctl_prg_req;              /* the last program change received */
    else if (!strcmp(name, "prgbank")) *got = ctl_prg_bank;            /* its bank select (CC 32; -1 none) */
    else if (!strcmp(name, "prog")) *got = ctl_manual ? -1 : ctl_prog; /* the program in play (-1: MANUAL) */
    else if (!strcmp(name, "wr")) *got = ctl_wr;                       /* WRITE: 0 idle, 1 armed, 2 fading / saving */
    else if (!strcmp(name, "wrdest")) *got = ctl_wr_dest;              /* WRITE's destination */
    else if (!strcmp(name, "writes")) *got = (int)ctl_writes;          /* WRITEs stored */
    else if (!strcmp(name, "restores")) *got = (int)ctl_restores;      /* restore-factory loads done */
    else if (!strcmp(name, "notice")) *got = ctl_notice;               /* the notice (controls.h NT_*) */
    else if (!strcmp(name, "factory")) *got = (int)prog_factory_count();   /* factory programs in this build */
    else if (!strcmp(name, "edit")) *got = a0 < P_COUNT_RESERVED ? eng_edit.v[a0] : -1;   /* edit ID: the edit buffer */
    else if (!strcmp(name, "name")) *got = a0 < PROGRAM_NAME_LEN ? (uint8_t)eng_edit.name[a0] : -1;   /* name I: char */
    else if (!strcmp(name, "fade")) *got = eng_fade_g;                 /* the output gain, 32768 = full */
    else if (!strcmp(name, "vlimit")) *got = eng_vlimit;               /* the CPU guard's voice limit (published) */
    else if (!strcmp(name, "sheds")) *got = (int)eng_sheds;            /* its sheds since start */
    else if (!strcmp(name, "load")) *got = eng_load;                   /* the load, % of the block (smoothed) */
    else if (!strcmp(name, "peak")) *got = eng_peak;                   /* its peak, % (decaying, ~1 s) */
    else if (!strcmp(name, "stored")) {                                /* stored N ID: param ID of program N in flash */
        static program_t p;
        prog_load(a0, &p);
        *got = n >= 2 && a[1] >= 0 && a[1] < P_COUNT_RESERVED ? p.v[a[1]] : -1;
    }
    else if (!strcmp(name, "saves")) *got = (int)set_saves;            /* the globals saved (blobs 0 / 1) so far */
    else if (!strcmp(name, "tune")) *got = eng_last_tune;              /* the master tune sent, cents */
    else if (!strcmp(name, "mono")) *got = app_mono;                   /* STEREO global = MONO */
    else if (!strcmp(name, "chord")) *got = eng_edit.chord[a0 % 6u];   /* chord I: chord memory slot I */
    else if (!strcmp(name, "pending")) *got = eng_flush();             /* events the queue still owes (0) */
    else if (!strcmp(name, "scrcheck")) *got = screen_check();         /* the screens' tables: faults (0) */
    else if (!strcmp(name, "scrpx")) *got = (int)scr_px_last;          /* pixels sent by the last screen update */
    else if (!strcmp(name, "scrfull")) *got = (int)scr_fulls;          /* whole-screen redraws so far */
    else if (!strcmp(name, "set") && n >= 2) *got = eng_param(a0, a[1]);   /* a host-only hook, not a probe: "T expect 0
                                                                          set ID V" queues the edit PARAM ID = V as the
                                                                          main loop would (0 = queued); worst.txt's
                                                                          patch (quicker than the panel's gestures) */
    else return -1;                                                    /* a name the app does not know */
    return 0;
}
#endif
