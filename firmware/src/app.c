/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX: a standalone OB-6-inspired polysynth for the M-Vave SMK-37 Pro, on the SMK BASE platform (platform.c
 * below does the rest: updates, the USB rescue, MIDI, USB audio, the keys, pads, knobs, faders, wheels, LEDs, the
 * screen, flash).
 *
 *   keys       the synth (synth/, engine.c), with the key's velocity; the notes go out over USB-MIDI (MIDI CH)
 *   OCTAVE-/+  the octave, -2..+2 (kept in flash: settings_save)
 *   K1         the volume (platform.c)
 *   K2..K8, faders, pads, the other buttons: ui/controls.c (the map: docs/CONTROLS.md): the screen's knobs, the
 *              faders through the pot mode, top pads = switches, bottom pads = pressure (aftertouch), sections
 *   pitch      the wheel bends +-BEND RANGE (the program's) at its ends (plus MIDI pitch bend)
 *   mod wheel  0..127: adds LFO depth (the higher of it and MIDI CC 1)
 *   buttons    lit while held
 *   MIDI in    ui/midi_map.c (docs/MIDI.md): notes, pressure, bend, CCs (own map), program change (Task 10 stub)
 *   screen     the panels (ui/screens.c, panel_gfx.c): a screen a section, its knobs, switches and faders as bound;
 *              the header shows the octave and the last value edited; Globals shows the raw wheels and faders
 *
 * The synth is not thread-safe: the main loop talks to it only through engine.c's event queue (eng_*), which
 * app_audio drains before each render. The program at boot: program_init (every default).
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division.
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

static void app_audio(int32_t *out, uint32_t n)
{
    eng_render(out, n);                         /* the queued events, then n frames added into out */
}

/* ---- notes (main loop) */
static void play(uint32_t note, uint32_t vel) { if (note <= 127u && vel) eng_note((int)note, (int)vel); }
static void stop(uint32_t note) { if (note <= 127u) eng_note((int)note, 0); }

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
static struct { int8_t oct; } set;              /* what survives a power-off (add yours, up to SECTOR_MAX bytes) */
static uint32_t idle_ok;

static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2))             /* ui/midi_map.c: the MIDI CH global, the CC map */
        controls_midi(st, d1, d2);
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
    uint32_t prev = 0, k, t_change = 0, dirty = 0;
    smk_pw_t wheel = {0};
    eng_init();                                 /* the synth with the init program; app_audio renders from now on */
    controls_init();                            /* the pads are the app's (events), the faders' positions now */
    if (settings_load(&set, sizeof set) || set.oct < -2 || set.oct > 2)
        set.oct = 0;

    scr_oct = set.oct;
    screen_draw(screen, &eng_edit, &eng_synth, 1);   /* the boot screen, whole, before the sound starts mattering */
    while (scr_strip < 240u) screen_draw(screen, &eng_edit, &eng_synth, 0);

    for (;;) {
        uint32_t keys, down, up, b, base = (uint32_t)((int32_t)KEY0 + 12 * set.oct), ch;
        int32_t e;
        platform_poll();
        controls_poll(0, &eng_edit, &screen);   /* encoders K2..K8, faders, pads, buttons: through the queue */
        ch = controls_tx_ch();

        keys = fm1_in.notes;                    /* bit n: keybed key F3 + n (n = 0..26), the middle of the keybed */
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {             /* (a key stops the note it started: the octave may change meanwhile) */
            uint32_t key = SMK_KEY_F3 + k, vel = fm1_in.key_vel[key];
            if (down >> k & 1u) {
                key_note[key] = (uint8_t)(base + k);
                play(key_note[key], vel);
                controls_key(key_note[key]);    /* (chord memory capture while CHORD is held) */
                midi_send(0x90u | ch, key_note[key], vel);
            }
            if (up >> k & 1u && key_note[key]) { stop(key_note[key]); midi_send(0x80u | ch, key_note[key], 0); key_note[key] = 0; }
        }
        while ((e = smk_extkey_take()) >= 0) {  /* the keys left and right of those 27: key << 8 | velocity (0 = up) */
            uint32_t key = (uint32_t)e >> 8, vel = (uint32_t)e & 0x7Fu;
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
        if (b & (1u << BTN_OCT_DOWN) && set.oct > -2) { set.oct--; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && set.oct < 2) { set.oct++; dirty = 1; t_change = fm1_ms; }
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

        if (dirty && !eng_active() && fm1_ms - t_change > 2000u) {   /* 2 s after the change, when silent (an erase stalls the sound) */
            settings_save(&set, sizeof set);
            dirty = 0;
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
    else if (!strcmp(name, "prgreq")) *got = ctl_prg_req;              /* the last program change (Task 10 stub) */
    else if (!strcmp(name, "prgbank")) *got = ctl_prg_bank;            /* its bank select */
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
