/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX: a standalone OB-6-inspired polysynth for the M-Vave SMK-37 Pro, on the SMK BASE platform (platform.c
 * below does the rest: updates, the USB rescue, MIDI, USB audio, the keys, pads, knobs, faders, wheels, LEDs, the
 * screen, flash).
 *
 *   keys       the synth (synth/, engine.c), with the key's velocity; the notes go out over USB-MIDI, channel 1
 *   OCTAVE-/+  the octave, -2..+2 (kept in flash: settings_save)
 *   K1         the volume (platform.c)
 *   pitch      the wheel bends +-BEND RANGE (the program's) at its ends
 *   mod wheel  0..127: adds LFO depth
 *   pads       not the app's yet (the platform's default)
 *   buttons    lit while held
 *   MIDI in    notes on any channel play the synth, with their velocity; channel pressure is the aftertouch
 *   screen     the last control touched, by its hardware id (keys 0..36, buttons 0..17, pads 0..15, faders 0..3,
 *              wheels 0..1; the encoders by their printed name, K1..K8), the encoders' positions, faders and wheels
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
 * counts at the rest. To check on the hardware (the screen shows the raw value: WHEEL 1) */
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

/* ---- the screen: the name, the last control touched, the encoders, faders and wheels, the octave */
#define C_ACC RGB(180, 140, 255)                /* lilac */
#define C_TXT RGB(210, 210, 210)
#define C_OFF RGB(90, 90, 90)
static const char *const BTN_NAME[18] = {"KNOB BANK", "FADER BANK", "<", ">", "OCTAVE-", "OCTAVE+", "ARP",
                                         "NOTE REPEAT", "SCALE", "CHORD", "GLOBE", "BT", "PATCH", "PARA", "FX",
                                         "SEQ", "SEQ PLAY", "SEQ REC"};
#ifdef DINGHY_HOST
#define ENC0 0u                                 /* the host has no MASTER reader: encoder 0 is the app's too */
#else
#define ENC0 1u                                 /* encoder 0 (K1) is the volume, seen through master_q12 */
#endif
static char ev[24] = "-", ev2[32] = "touch a control";
static uint32_t ev_n;
static int32_t enc_pos[8];

static char *cat(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *catn(char *p, int32_t v)           /* decimal (libc.c's fmt_int is not in the host build) */
{
    char b[12];
    uint32_t n = 0, u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
    do { b[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    if (v < 0) *p++ = '-';
    while (n) *p++ = b[--n];
    *p = 0;
    return p;
}

static void event_name(const char *what, uint32_t id, const char *name)   /* "WHAT id" over the name */
{
    catn(cat(ev, what), (int32_t)id);
    cat(ev2, name);
    ev_n++;
}
static void event(const char *what, uint32_t id, const char *lab, int32_t v)   /* "WHAT id" over "lab v" */
{
    event_name(what, id, "");
    catn(cat(ev2, lab), v);
}

static void draw_events(void)
{
    char s[72], *p;                             /* (four "  K8:-2147483648": 64) */
    uint32_t i;
    draw_text_box(0, 82, 240, &FONT_L, ev, C_WHITE, 1);
    draw_text_box(0, 118, 240, &FONT_S, ev2, C_ACC, 1);
    for (i = 0; i < 8u; i += 4u) {              /* the encoders' positions, two rows of four */
        uint32_t j;
        p = s;
        for (j = i; j < i + 4u; j++) {
            p = catn(cat(p, j == i ? "K" : "  K"), (int32_t)j + 1);
            p = catn(cat(p, ":"), enc_pos[j]);
        }
        draw_text_box(0, 146 + 20 * (i / 4u), 240, &FONT_S, s, C_TXT, 1);
    }
    p = cat(s, "F");
    for (i = 0; i < 4u; i++)
        p = catn(cat(p, " "), fm1_in.fader[i]);
    p = catn(cat(p, "  W "), fm1_in.wheel[0]);
    catn(cat(p, " "), fm1_in.wheel[1]);
    draw_text_box(0, 186, 240, &FONT_S, s, C_TXT, 1);
}

static void draw_octave(int32_t oct)
{
    char s[8] = "OCT +0";
    s[4] = oct < 0 ? '-' : '+';
    s[5] = (char)('0' + (oct < 0 ? -oct : oct));
    draw_text_box(0, 214, 240, &FONT_S, s, C_OFF, 1);
}

/* ---- MIDI in: from the main loop, and from app_idle while the screen draws (idle_ok) */
static struct { int8_t oct; } set;              /* what survives a power-off (add yours, up to SECTOR_MAX bytes) */
static uint32_t idle_ok;

static void midi_in(void)
{
    uint8_t st, d1, d2;
    while (midi_get(&st, &d1, &d2)) {           /* any channel */
        uint32_t ty = st & 0xF0u;
        if (ty == 0x90u && d2) play(d1, d2);
        else if (ty == 0x80u || ty == 0x90u) stop(d1);
        else if (ty == 0xD0u) eng_pressure(d1);   /* channel pressure (bend, CCs: the controls' task) */
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
    uint32_t prev = 0, k, t_change = 0, dirty = 0, ev_drawn = ~0u, oct_drawn = ~0u;
    uint8_t fader_seen[4] = {fm1_in.fader[0], fm1_in.fader[1], fm1_in.fader[2], fm1_in.fader[3]};
    uint16_t wheel_seen[2] = {fm1_in.wheel[0], fm1_in.wheel[1]};
    smk_pw_t wheel = {0};
#ifndef DINGHY_HOST
    int32_t master_seen = master_q12;
#endif
    eng_init();                                 /* the synth with the init program; app_audio renders from now on */
    if (settings_load(&set, sizeof set) || set.oct < -2 || set.oct > 2)
        set.oct = 0;

    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 18, 240, &FONT_L, APP_NAME, C_ACC, 1);
    draw_text_box(0, 52, 240, &FONT_S, APP_VER "  " FELUCCA_ID, C_OFF, 1);

    for (;;) {
        uint32_t keys, down, up, rel, b, base = (uint32_t)((int32_t)KEY0 + 12 * set.oct);
        int32_t e;
        platform_poll();

        keys = fm1_in.notes;                    /* bit n: keybed key F3 + n (n = 0..26), the middle of the keybed */
        down = keys & ~prev;
        up = prev & ~keys;
        prev = keys;
        for (k = 0; k < 27u; k++) {             /* (a key stops the note it started: the octave may change meanwhile) */
            uint32_t key = SMK_KEY_F3 + k, vel = fm1_in.key_vel[key];
            if (down >> k & 1u) {
                key_note[key] = (uint8_t)(base + k);
                play(key_note[key], vel);
                midi_send(0x90u, key_note[key], vel);
                event("KEY ", key, "vel ", (int32_t)vel);
            }
            if (up >> k & 1u && key_note[key]) { stop(key_note[key]); midi_send(0x80u, key_note[key], 0); key_note[key] = 0; }
        }
        while ((e = smk_extkey_take()) >= 0) {  /* the keys left and right of those 27: key << 8 | velocity (0 = up) */
            uint32_t key = (uint32_t)e >> 8, vel = (uint32_t)e & 0x7Fu;
            if (key >= FM1_NKEY) continue;
            if (vel) {
                key_note[key] = (uint8_t)(base + key - SMK_KEY_F3);
                play(key_note[key], vel);
                midi_send(0x90u, key_note[key], vel);
                event("KEY ", key, "vel ", (int32_t)vel);
            } else if (key_note[key]) { stop(key_note[key]); midi_send(0x80u, key_note[key], 0); key_note[key] = 0; }
        }
        {                                       /* the wheels: queued only when the value changes */
            int32_t bd = smk_pw_bend(&wheel, fm1_in.wheel[0], fm1_ms);   /* 1/4096 semitone, +-2 at the ends */
            eng_bend(8192 + bd);                /* the ends = 0 and 16383: +-BEND RANGE */
            eng_wheel(mod_wheel(fm1_in.wheel[1]));
        }

        b = fm1_input_edges(&rel);              /* buttons pressed since the last call (bit = button id) */
        for (k = 0; k < 18u; k++)
            if (b >> k & 1u) event_name("BTN ", k, BTN_NAME[k]);
        if (b & (1u << BTN_OCT_DOWN) && set.oct > -2) { set.oct--; dirty = 1; t_change = fm1_ms; }
        if (b & (1u << BTN_OCT_UP) && set.oct < 2) { set.oct++; dirty = 1; t_change = fm1_ms; }
        for (k = ENC0; k < 8u; k++)             /* detents turned since the last call */
            if ((e = fm1_enc_take(k)) != 0) { enc_pos[k] += e; event("K", k + 1u, "turned ", enc_pos[k]); }
#ifndef DINGHY_HOST
        if (master_q12 != master_seen) {        /* K1, encoder 0, is the volume: platform.c reads it */
            master_seen = master_q12;
            if (fm1_ms > 1000u) event("K", 1, "volume ", master_q12 * 100 / 4096);
        }
#endif
        for (k = 0; k < 4u; k++)
            if (fm1_in.fader[k] != fader_seen[k]) { fader_seen[k] = fm1_in.fader[k]; event("FADER ", k, "", fm1_in.fader[k]); }
        for (k = 0; k < 2u; k++) {
            int32_t d = (int32_t)fm1_in.wheel[k] - (int32_t)wheel_seen[k];
            if (d > 8 || d < -8) { wheel_seen[k] = fm1_in.wheel[k]; event("WHEEL ", k, "raw ", fm1_in.wheel[k]); }
        }

        midi_in();

        for (k = 0; k < 18u; k++)               /* a button lit while it is held */
            fm1_led_key(k, fm1_in.buttons >> k & 1u);

        idle_ok = 1;                            /* the drawing below may read MIDI meanwhile (LCD_IDLE) */
        if (ev_n != ev_drawn) { ev_drawn = ev_n; draw_events(); }   /* redraw only what changed */
        if ((uint32_t)(set.oct + 2) != oct_drawn) { oct_drawn = (uint32_t)(set.oct + 2); draw_octave(set.oct); }
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
    else if (!strcmp(name, "set") && n >= 2) *got = eng_param(a0, a[1]);   /* a host-only hook, not a probe: "T expect 0
                                                                          set ID V" queues the edit PARAM ID = V as the
                                                                          main loop would (0 = queued); worst.txt's
                                                                          patch, until the controls exist (Task 8) */
    else return -1;                                                    /* a name the app does not know */
    return 0;
}
#endif
