/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX screens: one panel per screen_id (ui/controls.h), in the panel's visual language (panel_gfx.c).
 *
 * A screen is a table of widgets (a title, a knob, a pad's button, an LED list, a 7-segment display...), each with its
 * rectangle. A widget that shows a control names the control's slot, not a parameter: knob 2 is "encoder slot 2", and
 * the parameter it shows comes from BINDINGS (ctl_binding), so what is drawn is always what that encoder or pad edits.
 * The encoders' widgets are listed in reading order, slot 0, 1, 2...: screen_check() (host probe "scrcheck") proves it
 * for every screen and row, and that every label fits its widget.
 *
 * Redraw only what changed: each widget keeps the value it was last drawn with; screen_draw() compares, and draws
 * again only the rectangles of the widgets whose value moved (a knob turned: its 56 x 40 cell and the header's value,
 * about 6 k pixels). A new screen or row is drawn whole, in strips of 240 x CV_MAX / 240 (64 rows), one strip per call,
 * so the main loop (keys, MIDI) never waits for more than a strip. Main loop only; integers only. */

#define SCR_IX 7                                  /* the panel between the wood cheeks: x 7 .. 232 */
#define SCR_IW 226
#define SCR_STRIP (CV_MAX / 240u)                 /* rows of a full-redraw strip */
#define SCR_WMAX 40                               /* widgets a screen at most */

enum { W_END, W_HEAD, W_HVAL, W_FOOT, W_TITLE, W_RULE, W_HINT, W_KNOB, W_ENCBTN, W_LIST, W_SEGENC, W_PAD, W_ROWLED,
       W_ROWSEL, W_ENVG, W_PROG, W_DIGIT, W_NAME, W_BIGNAME, W_VOICES, W_DIAG, W_WORD, W_VER, W_BOOTTXT, W_VERTXT,
       W_WSTAT, W_BPM, W_LIST2, W_CLKSTAT, W_SEQSTAT, W_SEQROW, W_SEQUSE, W_SEQHINT, W_SEQTAG };
/* (Task 5) W_BPM: an encoder's BPM on a 3-digit display (the tempo followed: an external clock's when one is); W_LIST2:
 * a select's LED list in two columns; W_CLKSTAT: what the clock follows, in words.
 * (Phase 3 Task 5, the Sequencer) W_SEQSTAT: the state in words (STOP, PLAY, ARMED, REC) and three displays: the step
 * (playing: the one played last; recording: the next to record), the length, the transpose; W_SEQROW: 16 steps of the
 * grid (a = the row, 0..3: steps 16a+1 .. 16a+16); W_SEQUSE: the sequence store's use; W_SEQHINT: what the buttons do
 * now, in words; W_SEQTAG: Play's SEQ / ARM / REC tag */
typedef struct {
    uint8_t t, x, y, w, h;
    uint8_t a;                                    /* encoder slot 0..6 (K2..K8) or top pad 0..7 */
    uint8_t b;                                    /* a pad's row: 0, 1, or 2 = the screen's row (whichever it is) */
    const char *s;                                /* label; '%' = the value (a knob's number, a select's choice) */
} wdg_t;

#define HEADER {W_HEAD, 7, 0, 143, 15, 0, 0, 0}, {W_HVAL, 150, 0, 83, 15, 0, 0, 0}
#define FOOTER {W_FOOT, 7, 219, 226, 21, 0, 0, 0}
#define PADHINT(y) {W_HINT, 7, y, 226, 9, 0, 0, "PADS 1 2: < SECTION >"}
#define KNOB(x, y, w, a, s) {W_KNOB, x, y, w, 41, a, 0, s}   /* (h: the label's last glyph row is y + h - 1: a row
                                                                     * outside the cell is never cleared on a repaint) */
#define PAD(x, y, w, h, a, b, s) {W_PAD, x, y, w, h, a, b, s}
#define END {W_END, 0, 0, 0, 0, 0, 0, 0}

static const wdg_t SCR_BOOT_W[] = {
    HEADER,
    {W_HINT, 7, 56, 226, 9, 0, 0, "POLYPHONIC SYNTHESIZER"},
    {W_RULE, 17, 72, 206, 2, 0, 0, 0},
    {W_WORD, 7, 82, 226, 50, 0, 0, "KNOBI-SIX"},
    {W_RULE, 17, 138, 206, 2, 0, 0, 0},
    {W_VER, 7, 150, 226, 40, 0, 0, 0},
    {W_BOOTTXT, 7, 202, 226, 9, 0, 0, 0},
    END};
static const wdg_t SCR_PLAY_W[] = {
    HEADER,
    {W_PROG, 11, 17, 90, 44, 0, 0, 0},
    {W_NAME, 104, 21, 125, 16, 0, 0, 0},
    {W_SEQTAG, 104, 44, 24, 10, 0, 0, 0},         /* (the sequencer: SEQ playing, ARM armed, REC recording) */
    {W_VOICES, 128, 44, 101, 10, 0, 0, 0},
    {W_TITLE, 7, 64, 226, 19, 0, 0, "FILTER"},
    KNOB(7, 87, 56, 0, "FREQ"), KNOB(63, 87, 56, 1, "RESO"), KNOB(119, 87, 56, 2, "MODE"),
    KNOB(175, 87, 58, 3, "ENV AMT"),
    {W_TITLE, 7, 133, 226, 19, 0, 0, "PERFORMANCE"},
    PAD(7, 158, 42, 22, 0, 2, "SYNC"), PAD(49, 158, 42, 22, 1, 2, "UNISON"), PAD(91, 158, 42, 22, 2, 2, "ARP"),
    PAD(133, 158, 42, 22, 3, 2, "HOLD"), KNOB(175, 154, 58, 4, "DIST"),   /* (K6: DISTORTION, Task 7) */
    {W_HINT, 7, 196, 226, 9, 0, 0, "TOP PADS: SWITCHES"},
    {W_HINT, 7, 207, 226, 9, 0, 0, "BOTTOM PADS: PRESSURE"},
    FOOTER, END};
static const wdg_t SCR_VCO_W[] = {
    HEADER,
    {W_TITLE, 7, 17, 111, 19, 0, 0, "VCO 1"}, {W_TITLE, 122, 17, 111, 19, 0, 0, "VCO 2"},
    KNOB(7, 41, 56, 0, "FREQ"), KNOB(63, 41, 55, 1, "SHAPE"), KNOB(7, 87, 56, 2, "PW"),
    PAD(63, 95, 55, 22, 2, 2, "SYNC"),
    KNOB(122, 41, 55, 3, "FREQ"), KNOB(177, 41, 56, 4, "FINE"), KNOB(122, 87, 55, 5, "SHAPE"),
    KNOB(177, 87, 56, 6, "PW"),
    PAD(122, 137, 55, 22, 3, 2, "LOW FREQ"), PAD(177, 137, 56, 22, 4, 2, "KEYBOARD"),
    PADHINT(204), FOOTER, END};
static const wdg_t SCR_MIX_W[] = {
    HEADER,
    {W_TITLE, 7, 17, 111, 19, 0, 0, "MIXER"}, {W_TITLE, 122, 17, 111, 19, 0, 0, "FILTER"},
    KNOB(7, 41, 56, 0, "VCO 1"), KNOB(63, 41, 55, 1, "VCO 2"), KNOB(7, 87, 56, 2, "SUB OCT"), KNOB(63, 87, 55, 3, "NOISE"),
    KNOB(122, 41, 55, 4, "FREQ"), KNOB(177, 41, 56, 5, "RESO"), KNOB(122, 87, 55, 6, "MODE"),
    PAD(177, 95, 56, 22, 2, 2, "BP"), PAD(122, 137, 111, 22, 3, 2, "KEY TRACK %"),
    PADHINT(204), FOOTER, END};
static const wdg_t SCR_ENV_W[] = {
    HEADER,
    {W_TITLE, 7, 17, 226, 19, 0, 0, "ENVELOPES"},
    {W_ROWSEL, 7, 40, 226, 11, 0, 0, 0},
    KNOB(7, 56, 45, 0, "ATK"), KNOB(52, 56, 45, 1, "DEC"), KNOB(97, 56, 45, 2, "SUS"),
    KNOB(142, 56, 45, 3, "REL"), KNOB(187, 56, 46, 4, "AMOUNT"),
    {W_ENVG, 17, 101, 206, 50, 0, 0, 0},
    PAD(7, 159, 75, 22, 2, 2, "VEL > FILTER"), PAD(82, 159, 75, 22, 3, 2, "VEL > AMP"), PAD(157, 159, 76, 22, 4, 2, "ROW"),
    {W_HINT, 7, 194, 226, 9, 0, 0, "SCALE: FILTER / LOUDNESS"},
    PADHINT(205), FOOTER, END};
static const wdg_t SCR_LFO_W[] = {
    HEADER,
    {W_TITLE, 7, 16, 226, 19, 0, 0, "LFO"}, {W_ROWLED, 192, 19, 41, 9, 0, 0, 0},
    {W_KNOB, 7, 40, 56, 53, 0, 0, "FREQ"}, {W_ENCBTN, 63, 40, 56, 53, 1, 0, "SYNC"},
    {W_LIST, 119, 39, 56, 54, 2, 0, "SHAPE"}, {W_KNOB, 175, 40, 58, 53, 3, 0, "AMOUNT"},
    PAD(7, 95, 37, 22, 2, 0, "VCO1"), PAD(44, 95, 37, 22, 3, 0, "VCO2"), PAD(81, 95, 38, 22, 4, 0, "PW"),
    PAD(119, 95, 38, 22, 5, 0, "AMP"), PAD(157, 95, 38, 22, 6, 0, "F FRQ"), PAD(195, 95, 38, 22, 7, 0, "F MOD"),
    {W_TITLE, 7, 119, 226, 19, 0, 0, "X-MOD"}, {W_ROWLED, 192, 122, 41, 9, 0, 1, 0},
    KNOB(7, 142, 56, 4, "FILT ENV"), KNOB(63, 142, 56, 5, "VCO 2"),
    PAD(119, 142, 38, 20, 2, 1, "VCO1"), PAD(157, 142, 38, 20, 3, 1, "SHAPE1"), PAD(195, 142, 38, 20, 4, 1, "PW1"),
    PAD(119, 164, 38, 20, 5, 1, "F FRQ"), PAD(157, 164, 38, 20, 6, 1, "F MOD"), PAD(195, 164, 38, 20, 7, 1, "BP"),
    {W_HINT, 7, 196, 226, 9, 0, 0, "SCALE: PADS LFO / X-MOD"},
    PADHINT(207), FOOTER, END};
static const wdg_t SCR_AT_W[] = {
    HEADER,
    {W_TITLE, 7, 16, 226, 19, 0, 0, "AFTERTOUCH"}, {W_ROWLED, 192, 19, 41, 9, 0, 0, 0},
    KNOB(7, 38, 56, 0, "AMOUNT"),
    PAD(63, 38, 56, 20, 2, 0, "VCO1"), PAD(119, 38, 56, 20, 3, 0, "VCO2"), PAD(175, 38, 58, 20, 4, 0, "LFO AMT"),
    PAD(63, 60, 56, 20, 5, 0, "AMP"), PAD(119, 60, 56, 20, 6, 0, "F FRQ"), PAD(175, 60, 58, 20, 7, 0, "F MOD"),
    {W_TITLE, 7, 84, 226, 19, 0, 0, "MISC"}, {W_ROWLED, 192, 87, 41, 9, 0, 1, 0},
    KNOB(7, 107, 56, 1, "DETUNE"), KNOB(63, 107, 56, 2, "SPREAD"), KNOB(119, 107, 56, 3, "BEND % ST"),
    {W_LIST, 175, 107, 58, 64, 4, 0, "KEY MODE"},
    KNOB(7, 153, 56, 5, "PRG VOL"), KNOB(63, 153, 56, 6, "PORTA RT"),
    PAD(119, 153, 56, 20, 2, 1, "UNISON"), PAD(119, 175, 56, 20, 3, 1, "VOX %"),
    PAD(175, 173, 58, 20, 4, 1, "PORTA"), PAD(175, 195, 58, 20, 5, 1, "%"),
    FOOTER, END};
static const wdg_t SCR_CLOCK_W[] = {             /* ARP + CLOCK (the mockup's "Arp + Clock + Program" page, its upper
                                                  * two sections: Program has a screen of its own) */
    HEADER,
    {W_TITLE, 7, 17, 226, 19, 0, 0, "CLOCK"},
    {W_BPM, 7, 40, 111, 56, 0, 0, "BPM"}, {W_LIST2, 122, 40, 111, 56, 1, 0, "DIVISION"},
    PAD(7, 100, 75, 22, 2, 2, "TAP"), PAD(82, 100, 75, 22, 3, 2, "CLK %"), PAD(157, 100, 76, 22, 4, 2, "BEAT SYNC"),
    {W_CLKSTAT, 7, 126, 226, 20, 0, 0, 0},
    {W_TITLE, 7, 148, 226, 19, 0, 0, "ARPEGGIATOR"},
    {W_LIST2, 7, 171, 111, 38, 2, 0, "MODE"}, {W_LIST, 118, 171, 40, 38, 3, 0, "OCT"},
    PAD(158, 171, 75, 20, 5, 2, "ARP"), PAD(158, 193, 75, 20, 6, 2, "HOLD"),
    FOOTER, END};
static const wdg_t SCR_SEQ_W[] = {               /* the Sequencer (Phase 3 Task 5): the state, the 64-step grid (16 a
                                                  * row: the notes of a step as dots, a tie joined to the step before,
                                                  * a rest a dash, the marked step framed), the pads, the store */
    HEADER,
    {W_TITLE, 7, 17, 226, 19, 0, 0, "SEQUENCER"},
    {W_SEQSTAT, 7, 39, 226, 30, 0, 0, 0},
    {W_SEQROW, 16, 71, 208, 23, 0, 0, 0}, {W_SEQROW, 16, 94, 208, 23, 1, 0, 0},
    {W_SEQROW, 16, 117, 208, 23, 2, 0, 0}, {W_SEQROW, 16, 140, 208, 23, 3, 0, 0},
    PAD(7, 166, 75, 22, 2, 2, "SEQ PLAY"), PAD(82, 166, 75, 22, 3, 2, "SEQ REC"),
    {W_SEQUSE, 7, 190, 226, 9, 0, 0, 0},
    {W_SEQHINT, 7, 200, 226, 19, 0, 0, 0},
    FOOTER, END};
static const wdg_t SCR_PROGRAM_W[] = {
    HEADER,
    {W_TITLE, 7, 17, 226, 19, 0, 0, "PROGRAM"},
    {W_DIGIT, 37, 42, 56, 50, 0, 0, "BANK"}, {W_DIGIT, 125, 42, 39, 50, 1, 0, "TENS"},
    {W_DIGIT, 164, 42, 39, 50, 2, 0, "ONES"},
    {W_BIGNAME, 7, 98, 226, 32, 0, 0, 0},
    PAD(7, 136, 75, 22, 2, 2, "WRITE"), PAD(82, 136, 75, 22, 3, 2, "ENTER"), PAD(157, 136, 76, 22, 4, 2, "MANUAL"),
    {W_WSTAT, 7, 166, 226, 20, 0, 0, 0},
    PADHINT(204), FOOTER, END};
static const wdg_t SCR_GLOBALS_W[] = {
    HEADER,
    {W_TITLE, 7, 17, 226, 19, 0, 0, "GLOBALS"},
    {W_SEGENC, 7, 40, 56, 39, 0, 0, "MIDI CH"}, {W_LIST, 63, 40, 56, 39, 1, 0, "POT MODE"},
    {W_LIST, 119, 40, 56, 39, 2, 0, "PARAM TX"}, {W_LIST, 175, 40, 58, 39, 3, 0, "PARAM RX"},
    {W_SEGENC, 7, 81, 75, 39, 4, 0, "TRANSPOSE"}, {W_SEGENC, 82, 81, 75, 39, 5, 0, "TUNE"},
    {W_SEGENC, 157, 81, 76, 39, 6, 0, "VEL CURVE"},
    PAD(7, 123, 56, 22, 2, 2, "LOCAL %"), PAD(63, 123, 56, 22, 3, 2, "%"), PAD(119, 123, 56, 22, 4, 2, "SUS %"),
    PAD(175, 123, 58, 22, 5, 2, "AT CRV %"),
#if KNOBI_CPU1_PROBE
    {W_DIAG, 7, 147, 226, 63, 0, 0, 0},           /* (the CPU1 probe's four lines over the usual three; no version line) */
    PADHINT(210), FOOTER, END};
#else
    {W_DIAG, 7, 148, 226, 30, 0, 0, 0},
    {W_VERTXT, 7, 182, 226, 9, 0, 0, 0},
    PADHINT(204), FOOTER, END};
#endif
static const wdg_t *const SCR_W[SCR_COUNT] = {SCR_BOOT_W, SCR_PLAY_W, SCR_VCO_W, SCR_MIX_W, SCR_ENV_W, SCR_LFO_W,
                                              SCR_AT_W, SCR_CLOCK_W, SCR_SEQ_W, SCR_PROGRAM_W, SCR_GLOBALS_W};

/* ---- state */
static int scr_oct;                               /* the keys' octave (app.c sets it) */
static int scr_on = -1, scr_on_row;               /* the screen and row drawn */
static uint32_t scr_strip = ~0u;                  /* a full redraw's next strip (done when past the screen) */
static int32_t scr_cache[SCR_WMAX];               /* each widget's value as drawn */
static uint32_t scr_px_last, scr_px_acc, scr_fulls;   /* pixels of the last update (a full one: all its strips) */
static const program_t *scr_p;                    /* the draw's arguments, for the widgets */
static const synth_t *scr_s;

static char *scr_cat(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }
static char *scr_num(char *d, int32_t v)
{
    char b[12];
    uint32_t n = 0, u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
    do { b[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    if (v < 0) *d++ = '-';
    while (n) *d++ = b[--n];
    *d = 0;
    return d;
}

/* ---- a binding's value, range and choices (params, globals, actions) */
static const char *const SCR_POT[3] = {"RELATIVE", "PASSTHRU", "JUMP"};
static const char *const SCR_TXRX[2] = {"OFF", "CC"};
static const char *const SCR_ONOFF[2] = {"OFF", "ON"};
static const char *const SCR_STEREO[2] = {"STEREO", "MONO"};
static const char *const SCR_SUS[2] = {"N.O.", "N.C."};
static const char *const SCR_MCLK[CLK_MIDI_COUNT] = {"OFF", "OUT", "IN", "THRU", "IN NO S/S"};   /* MIDI CLOCK */
static const char *const SCR_MCLK7[CLK_MIDI_COUNT] = {"OFF", "Out", "In", "thru", "In.nS"};      /* (the header's) */
static int32_t scr_prog_shown(void) { return ctl_manual ? -1 : ctl_prog; }   /* the program on the displays, -1 MANUAL */
/* the diagnostics line's numbers, quantised (what is drawn is what is compared): the load and its peak in 5 % steps
 * (0..199: up to 995 %), the voice limit, the sheds (up to 999) */
static int32_t scr_cpu_q(void)
{
    uint32_t ld = (eng_load + 2u) / 5u, pk = (eng_peak + 2u) / 5u, sh = eng_sheds;
    return (int32_t)((ld > 199u ? 199u : ld) | (pk > 199u ? 199u : pk) << 8 | (uint32_t)(eng_vlimit & 15u) << 16 |
                     (sh > 999u ? 999u : sh) << 20);
}
#if KNOBI_CPU1
/* the second core on the diagnostics line (engine.c eng_c1_status), quantised: 0 OFF (DUAL CORE off), 1 FAIL (on, but
 * CPU1 is not running: retired, or it never started), 2 + its job load in 5 % steps (on: "C1 ON 40%") */
static int32_t scr_c1_q(void)
{
    int st = eng_c1_status();
    uint32_t ld = (eng_c1_load + 2u) / 5u;
    return st == 0 ? 0 : st == 2 ? 1 : 2 + (int32_t)(ld > 199u ? 199u : ld);
}
static void scr_c1(char *d)
{
    int32_t q = scr_c1_q();
    d = scr_cat(d, "  C1 ");
    if (q < 2) scr_cat(d, q ? "FAIL" : "OFF");
    else scr_cat(scr_num(scr_cat(d, "ON "), (q - 2) * 5), "%");
}
#endif
#if KNOBI_CPU1_PROBE
/* the CPU1 probe's lines (cpu1_probe.c), on top of the diagnostics:
 *   C1 PING n  ERR n  MISREAD n
 *   FPU OK|BAD|TRAP|--  DUAL=SINGLE OK|BAD|--  [BIT xxxxxxxx: the bits misread]
 *   LOAD 1C x%  2C y%  JOB z%     (the test patch on one core, on two (CPU0), CPU1's job; -- until measured)
 *   PROBE n%  C1 IDLE|BUSY|OFF|FAIL [why]  DUAL OFF|ON n%|FAIL   (n: the probe's own time, the last second's
 *                                    peak, % of a block; why: PATTERN, FPU, DUAL for the probe's own, else the HAL's)
 * what is drawn is what is compared (the widget's value) */
static const char *const SCR_C1ST[4] = {"OFF", "IDLE", "BUSY", "FAIL"};
static const char *const SCR_C1WHY[6] = {"", " MISREAD", " LATE", " BARRED", " FAULT", " SILENT"};
static const char *const SCR_CHK[4] = {"--", "OK", "BAD", "TRAP"};
static const char *const SCR_PRFAIL[4] = {"", " PATTERN", " FPU", " DUAL"};
static char *scr_hex8(char *d, uint32_t v)
{
    for (int i = 7; i >= 0; i--) *d++ = "0123456789ABCDEF"[v >> 4 * i & 15u];
    *d = 0;
    return d;
}
static uint32_t scr_probe_q(void)
{
    uint32_t h = c1probe.pings;
    h = h * 131u + c1probe.errs;
    h = h * 131u + c1probe_misreads();
    h = h * 131u + c1probe_bits();
    h = h * 7u + (uint32_t)c1probe_fpu();
    h = h * 7u + (uint32_t)c1probe_dual();
    h = h * 1009u + c1probe.load1 * 1000003u + c1probe.load2 * 999983u + c1probe.loadj * 7919u + c1probe.passes * 31u;
    h = h * 17u + (c1probe.state & 3u) * 8u + (c1probe.why & 7u);
    h = h * 37u + (c1probe.fail & 3u) + c1probe.cost_peak * 4u;
    return h;
}
static int32_t scr_probe_lines(int32_t cx, int32_t y)   /* draws them from y; returns the y after them */
{
    char t[48], *p;
    uint32_t mis = c1probe_misreads(), bits = c1probe_bits(), st = c1probe.state & 3u, why = c1probe.why;
    int fpu = c1probe_fpu(), dual = c1probe_dual(), c1 = eng_c1_status();
    p = scr_num(scr_cat(t, "C1 PING "), (int32_t)c1probe.pings);
    p = scr_num(scr_cat(p, "  ERR "), (int32_t)c1probe.errs);
    scr_num(scr_cat(p, "  MISREAD "), (int32_t)mis);
    pg_text_c(cx, y, &FONT_XS, t, c1probe.errs || mis ? PC_RED : PC_WHITE);
    p = scr_cat(scr_cat(t, "FPU "), SCR_CHK[fpu]);
    p = scr_cat(scr_cat(p, "  DUAL=SINGLE "), SCR_CHK[dual]);
    if (bits) scr_hex8(scr_cat(p, "  BIT "), bits);
    pg_text_c(cx, y + 9, &FONT_XS, t, fpu >= 2 || dual >= 2 || bits ? PC_RED : PC_WHITE);
    p = scr_cat(t, "LOAD 1C ");
    if (!c1probe.passes) scr_cat(p, "--  2C --  JOB --");
    else {
        p = scr_num(p, (int32_t)c1probe.load1);
        p = scr_num(scr_cat(p, "%  2C "), (int32_t)c1probe.load2);
        scr_cat(scr_num(scr_cat(p, "%  JOB "), (int32_t)c1probe.loadj), "%");
    }
    pg_text_c(cx, y + 18, &FONT_XS, t, PC_WHITE);
    p = scr_num(scr_cat(t, "PROBE "), (int32_t)(c1probe.cost_peak > 999u ? 999u : c1probe.cost_peak));
    p = scr_cat(scr_cat(p, "%  C1 "), SCR_C1ST[st]);
    p = scr_cat(p, c1probe.fail ? SCR_PRFAIL[c1probe.fail & 3u] : why < 6u ? SCR_C1WHY[why] : " ?");
    p = scr_cat(p, "  DUAL ");
    if (c1 == 1) scr_cat(scr_num(scr_cat(p, "ON "), (int32_t)eng_c1_load), "%");
    else scr_cat(p, c1 ? "FAIL" : "OFF");
    pg_text_c(cx, y + 27, &FONT_XS, t, st == CPU1_FAIL ? PC_RED : PC_WHITE);
    return y + 36;
}
#endif
static int32_t scr_val(uint16_t id)
{
    if (P_IS_LIVE(id)) return scr_p->v[id];
    if (B_IS_GLOBAL(id) && (id & 0xFFu) < G_COUNT) return ctl_glob[id & 0xFFu];
    if (B_IS_PROG(id)) {                           /* a digit of the program (WRITE armed: of its destination) */
        int32_t n = ctl_wr == 1 ? ctl_wr_dest : scr_prog_shown();
        if (n < 0) return -1;
        return (id & 0xFFu) == PRG_BANK ? n / 100 : (id & 0xFFu) == PRG_TENS ? n / 10 % 10 : n % 10;
    }
    if (id == B_ACT(ACT_HOLD)) return ctl_hold;
    if (id == B_ACT(ACT_WRITE)) return ctl_wr == 2 || (ctl_wr == 1 && (fm1_ms >> 8 & 1u));   /* armed: blinks */
    if (id == B_ACT(ACT_ENTER)) return ctl_wr == 1;
    if (id == B_ACT(ACT_MANUAL)) return ctl_manual;
    if (id == B_ACT(ACT_TAP)) return ctl_tap_lit();   /* (flashes on the beat) */
    if (id == B_ACT(ACT_SEQPLAY)) return ctl_seq_on();
    if (id == B_ACT(ACT_SEQREC)) return ctl_rec.on || ctl_rec_arm;
    return 0;
}
/* ---- the sequencer's state, for the screens: 0 stopped, 1 playing, 2 armed (playing, waiting for a MIDI clock), 3
 * recording; the step marked (playing: the one played last; recording: the next to record), -1 none */
static int scr_seq_state(void)
{
    if (ctl_rec.on || ctl_rec_arm) return 3;
    if (!ctl_seq_on()) return 0;
    return eng_seq_state == 2 ? 2 : 1;
}
static const seq_t *scr_seq(void)                 /* the sequence shown: a fresh recording (nothing entered yet; it
                                                  * replaces the sequence at its first entry) as empty */
{
    static seq_t none;                            /* (zero: empty) */
    return seqrec_fresh(&ctl_rec) ? &none : edit_seq();
}
static int32_t scr_seq_mark(void)
{
    int st = scr_seq_state();
    if (st == 3) return scr_seq()->len < SEQ_MAX_STEPS ? scr_seq()->len : -1;
    if (st == 1 && eng_seq_cur != SEQ_NO_STEP && eng_seq_cur < scr_seq()->len) return eng_seq_cur;
    return -1;
}
static int32_t scr_kb(uint32_t b) { return (int32_t)((b + 1023u) / 1024u); }   /* bytes -> KB, rounded up */
static const char *const SCR_SEQHINT[3][2] = {    /* the Sequencer's hint lines: stopped, playing (or armed), recording */
    {"SEQ REC: RECORD   SEQ PLAY: PLAY", "SEQ: BACK   WRITE KEEPS THE SEQUENCE"},
    {"SEQ REC HELD + KEY: TRANSPOSE", "MIDDLE C: NONE   SEQ REC: RECORD"},
    {"KEYS: A STEP (A CHORD: KEEP ONE HELD)", ">: REST / TIE   <: BACK   SEQ REC: END"}};
/* the BPM shown: the program's, or (MIDI CLOCK following one) the tempo received, rounded (0: none yet) */
static int32_t scr_bpm_shown(uint16_t id)
{
    if (CLOCK_EXTERNAL(ctl_glob[G_MIDI_CLOCK])) return (eng_clk_bpm10 + 5) / 10;
    return scr_val(id);
}
static int32_t scr_lo(uint16_t id)
{
    if (P_IS_LIVE(id)) return param_get(id)->min;
    if (B_IS_GLOBAL(id) && (id & 0xFFu) < G_COUNT) return GLOB_MIN[id & 0xFFu];
    return 0;
}
static int32_t scr_hi(uint16_t id)
{
    if (P_IS_LIVE(id)) return param_get(id)->max;
    if (B_IS_GLOBAL(id) && (id & 0xFFu) < G_COUNT) return GLOB_MAX[id & 0xFFu];
    if (B_IS_PROG(id)) return (id & 0xFFu) == PRG_BANK ? (int32_t)PROG_BANKS - 1 : 9;
    return 1;
}
static const char *scr_choice(uint16_t id, int32_t v)   /* a select's choice, else 0 */
{
    if (P_IS_LIVE(id)) {
        const param_t *p = param_get(id);
        return p->kind == PK_SELECT && v >= p->min && v <= p->max ? p->labels[v - p->min] : 0;
    }
    if (id == B_GLOBAL(G_POT_MODE)) return SCR_POT[v % 3];
    if (id == B_GLOBAL(G_MIDI_CLOCK)) return SCR_MCLK[(uint32_t)v % CLK_MIDI_COUNT];
    if (id == B_GLOBAL(G_ARP_SYNC)) return SCR_ONOFF[v & 1];
    if (id == B_GLOBAL(G_PARAM_TX) || id == B_GLOBAL(G_PARAM_RX)) return SCR_TXRX[v & 1];
    if (id == B_GLOBAL(G_LOCAL)) return SCR_ONOFF[v & 1];
#if KNOBI_CPU1
    if (id == B_GLOBAL(G_DUAL)) return SCR_ONOFF[v & 1];
#endif
    if (id == B_GLOBAL(G_STEREO)) return SCR_STEREO[v & 1];
    if (id == B_GLOBAL(G_SUSTAIN)) return SCR_SUS[v & 1];
    return 0;
}
static void scr_label(char *d, const char *s, uint16_t id, int32_t v)   /* '%': the value */
{
    for (; *s; s++) {
        if (*s != '%') { *d++ = *s; continue; }
        if (scr_choice(id, v)) d = scr_cat(d, scr_choice(id, v));
        else d = scr_num(d, v);
    }
    *d = 0;
}
static const char *scr_short(uint16_t id)          /* a fader's name in the footer */
{
    switch (id) {
    case P_FLT_FREQ: return "FREQ";
    case P_FLT_RESO: return "RESO";
    case P_FLT_MODE: return "MODE";
    case P_FLT_ENV_AMT: return "ENV AMT";
    case P_VCO1_SHAPE: return "V1 SHAPE";
    case P_VCO1_PW: return "V1 PW";
    case P_VCO2_SHAPE: return "V2 SHAPE";
    case P_VCO2_PW: return "V2 PW";
    case P_FENV_A: case P_LENV_A: return "ATTACK";
    case P_FENV_D: case P_LENV_D: return "DECAY";
    case P_FENV_S: case P_LENV_S: return "SUSTAIN";
    case P_FENV_R: case P_LENV_R: return "RELEASE";
    case P_LFO_FREQ: return "LFO FREQ";
    case P_LFO_AMT: return "LFO AMT";
    case P_XM_FENV: return "XM F ENV";
    case P_XM_VCO2: return "XM VCO2";
    case P_AT_AMT: return "AT AMT";
    case P_DETUNE: return "DETUNE";
    case P_PORTA_RATE: return "PORTA RT";
    case P_PRG_VOL: return "PRG VOL";
    }
    return P_IS_LIVE(id) ? param_get(id)->name : "-";
}

/* the binding a widget shows: an encoder slot of the screen's row, or a top pad of its own row */
static uint16_t scr_wid(int screen, int row, const wdg_t *w)
{
    if (w->t == W_PAD) return ctl_binding(screen, w->b == 2 ? row : w->b)->pad_top[w->a & 7u];
    if (w->t == W_KNOB || w->t == W_ENCBTN || w->t == W_LIST || w->t == W_SEGENC || w->t == W_DIGIT || w->t == W_BPM ||
        w->t == W_LIST2)
        return ctl_binding(screen, row)->enc[w->a % 7u];
    return B_NONE;
}

/* the value a widget is drawn from: when it changes, the widget is drawn again */
static int32_t scr_wval(int screen, int row, const wdg_t *w)
{
    uint16_t id = scr_wid(screen, row, w);
    uint32_t h = 0, i;
    switch (w->t) {
    case W_KNOB: case W_ENCBTN: case W_LIST: case W_SEGENC: case W_DIGIT: case W_LIST2: return scr_val(id);
    case W_BPM: return scr_bpm_shown(id) | (int32_t)CLOCK_EXTERNAL(ctl_glob[G_MIDI_CLOCK]) << 12;
    case W_CLKSTAT: return ctl_glob[G_MIDI_CLOCK] | eng_clk_run << 4 | eng_clk_alive << 5;
    case W_SEQSTAT: {
        int32_t m = scr_seq_mark();
        return scr_seq_state() | (m + 1) << 4 | (int32_t)scr_seq()->len << 12 | (eng_last_seqxp + 128) << 20;
    }
    case W_SEQROW: {                               /* its 16 steps as drawn, and the mark if it is in this row */
        const seq_t *q = scr_seq();
        int32_t m = scr_seq_mark();
        for (i = 16u * w->a; i < 16u * w->a + 16u; i++)
            h = h * 37u + (i < q->len ? 16u + q->step[i].count + 8u * q->step[i].tie : 0u);
        return (int32_t)(h * 67u + (uint32_t)(m >= 16 * w->a && m < 16 * w->a + 16 ? m - 16 * w->a + 1 : 0) +
                         (uint32_t)(scr_seq_state() == 3) * 33u);
    }
    case W_SEQUSE: return scr_kb(seqst_used()) | scr_kb(seqst_total()) << 12;
    case W_SEQHINT: case W_SEQTAG: return scr_seq_state();
    case W_PAD: return scr_val(id) | (w->b == 2 || w->b == row) << 16;
    case W_ROWLED: case W_ROWSEL: return row;
    case W_HVAL: return (int32_t)(((uint32_t)ctl_last_v & 0xFFFFu) | (uint32_t)(scr_oct + 2) << 16 |      /* v 0..15, octave 16..18, */
                         ((uint32_t)ctl_last_id & 0xFFu) << 19 | ((uint32_t)ctl_last_id >> 13) << 27);   /* id 19..29 */
    case W_VOICES:
        for (i = 0; i < NVOICE; i++) h |= (uint32_t)(scr_s->voice[i].busy != 0) << i;
        return (int32_t)h;
    case W_ENVG: {
        const binding_t *b = ctl_binding(screen, row);
        return scr_p->v[b->enc[0]] | scr_p->v[b->enc[1]] << 7 | scr_p->v[b->enc[2]] << 14 | scr_p->v[b->enc[3]] << 21 |
               row << 28;
    }
    case W_NAME: case W_BIGNAME:
        for (i = 0; i < PROGRAM_NAME_LEN; i++) h = h * 31u + (uint8_t)scr_p->name[i];
        return (int32_t)h;
    case W_PROG: return scr_prog_shown();
    case W_WSTAT:
        return (int32_t)((uint32_t)ctl_wr | (uint32_t)ctl_wr_dest << 2 | (uint32_t)ctl_manual << 12 |
                         (uint32_t)(ctl_wr_rc + 2) << 13 | (uint32_t)scr_prog_shown() << 16 | (uint32_t)ctl_notice << 28);
    case W_DIAG:
        for (i = 0; i < 4u; i++) h = h * 131u + (fm1_in.fader[i] >> 3);   /* (quantised: ADC noise must not repaint) */
        h = h * 1031u + (fm1_in.wheel[0] >> 3) * 7u + (fm1_in.wheel[1] >> 3) * 65599u;
#if KNOBI_CPU1_PROBE
        return (int32_t)(h * 2654435761u + scr_cpu_q() + (uint32_t)scr_c1_q() * 40503u + scr_probe_q() * 2246822519u);
#elif KNOBI_CPU1
        return (int32_t)(h * 2654435761u + scr_cpu_q() + (uint32_t)scr_c1_q() * 40503u);
#else
        return (int32_t)(h * 2654435761u + scr_cpu_q());   /* (the load in 5 % steps: not a repaint a pass) */
#endif
    }
    return 0;
}

/* ---- drawing a widget (from its cached value v) */
static void scr_seg_text(char *d, uint16_t id, int32_t v)   /* the header's display: a value, ON / OFF, a choice no. */
{
    *d = 0;
    if (P_IS_LIVE(id)) {
        if (param_get(id)->kind == PK_SWITCH) scr_cat(d, v ? "On" : "OFF");
        else if (param_get(id)->kind == PK_SELECT) scr_num(d, v - param_get(id)->min + 1);
        else scr_num(d, v);
    } else if (id == B_GLOBAL(G_MIDI_CLOCK)) scr_cat(d, SCR_MCLK7[(uint32_t)v % CLK_MIDI_COUNT]);
    else if (id == B_GLOBAL(G_ARP_SYNC)) scr_cat(d, v ? "On" : "OFF");
    else if (id == B_ACT(ACT_TAP)) scr_num(scr_cat(d, "tAP"), v % 10);   /* (the taps counted so far) */
    else if (id == B_GLOBAL(G_MIDI_CH)) {
        if (v) scr_num(d, v); else scr_cat(d, "ALL");
    } else if (id == B_GLOBAL(G_PARAM_TX) || id == B_GLOBAL(G_PARAM_RX)) scr_cat(d, v ? "CC" : "OFF");
    else if (id == B_GLOBAL(G_LOCAL)) scr_cat(d, v ? "On" : "OFF");
#if KNOBI_CPU1
    else if (id == B_GLOBAL(G_DUAL)) scr_cat(d, v ? "On" : "OFF");
#endif
    else if (id == B_GLOBAL(G_TRANSPOSE) || id == B_GLOBAL(G_TUNE) || id == B_GLOBAL(G_VEL_CURVE) ||
             id == B_GLOBAL(G_AT_CURVE)) scr_num(d, v);
    else if (B_IS_GLOBAL(id)) scr_num(d, v + 1);
    else if (id == B_ACT(ACT_HOLD)) scr_cat(d, v ? "On" : "OFF");
    else if (id == B_NOTICE) scr_cat(d, v == NT_BROKEN ? "Err" : v == NT_RESTORE_ASK ? "SurE" : v == NT_RESTORED ? "FAC" : v == NT_SEQ_MISSING ? "Err" : "");
    else if (id == B_SEQXP) scr_num(d, v);         /* (the transpose, semitones) */
}

static void scr_list(const wdg_t *w, uint16_t id, int32_t v)   /* an LED a choice, the lit one the value */
{
    int32_t lo = scr_lo(id), hi = scr_hi(id), n = hi - lo + 1, i, tw = 0, x;
    for (i = 0; i < n; i++) {
        int32_t t = text_w(&FONT_XS, scr_choice(id, lo + i));
        if (t > tw) tw = t;
    }
    x = w->x + (w->w - (tw + 9)) / 2;
    for (i = 0; i < n; i++) {
        int on = lo + i == v;
        pg_led_s(x + 3, w->y + 3 + 9 * i, on);
        pg_text(x + 9, w->y + 9 * i, &FONT_XS, scr_choice(id, lo + i), on ? PC_WHITE : PC_DIM);
    }
    if (w->s) pg_text_c(w->x + w->w / 2, w->y + w->h - 7, &FONT_XS, w->s, PC_WHITE);
}

static void scr_list2(const wdg_t *w, uint16_t id, int32_t v)   /* scr_list in two columns, the first half left */
{
    int32_t lo = scr_lo(id), hi = scr_hi(id), n = hi - lo + 1, rows = (n + 1) / 2, i, cw = w->w / 2;
    for (i = 0; i < n; i++) {
        int on = lo + i == v;
        int32_t x = w->x + 6 + (i / rows) * cw, y = w->y + 9 * (i % rows);
        pg_led_s(x + 3, y + 3, on);
        pg_text(x + 9, y, &FONT_XS, scr_choice(id, lo + i), on ? PC_WHITE : PC_DIM);
    }
    if (w->s) pg_text_c(w->x + w->w / 2, w->y + w->h - 7, &FONT_XS, w->s, PC_WHITE);
}

static void scr_envg(const wdg_t *w, int32_t v)
{
    int32_t a = v & 127, d = v >> 7 & 127, s = v >> 14 & 127, r = v >> 21 & 127;
    int32_t x0 = w->x + 6, top = w->y + 5, bot = w->y + w->h - 6, hh = bot - top;
    int32_t px[5], py[5], X, k;
    pg_fill(w->x, w->y, w->w, w->h, C_BLACK);
    pg_fill(w->x + 1, w->y + 1, w->w - 2, w->h - 2, PC_GRAPH_BG);
    for (X = w->x + 4; X < w->x + w->w - 4; X += 4) pg_px(X, top + hh / 2, PC_FAINT);
    px[0] = x0, py[0] = bot;
    px[1] = px[0] + 3 + a * 44 / 127, py[1] = top;
    px[2] = px[1] + 3 + d * 44 / 127, py[2] = bot - s * hh / 127;
    px[3] = px[2] + 34, py[3] = py[2];
    px[4] = px[3] + 3 + r * 44 / 127, py[4] = bot;
    for (k = 0; k < 4; k++)                        /* under the curve, dim red */
        for (X = px[k]; X <= px[k + 1]; X++) {
            int32_t dx = px[k + 1] - px[k], y = dx ? py[k] + (py[k + 1] - py[k]) * (X - px[k]) / dx : py[k + 1];
            pg_fill(X, y, 1, bot - y + 1, PC_SEG_OFF);
        }
    for (k = 0; k < 4; k++) {                      /* the curve, 2 px */
        pg_line(px[k], py[k], px[k + 1], py[k + 1], PC_RED);
        pg_line(px[k], py[k] - 1, px[k + 1], py[k + 1] - 1, PC_RED);
    }
    pg_text(w->x + w->w - 6 - text_w(&FONT_XS, "A D S R"), w->y + 4, &FONT_XS, "A D S R", PC_DIM);
}

static void scr_paint_w(int screen, int row, const wdg_t *w, int32_t v)
{
    uint16_t id = scr_wid(screen, row, w);
    char t[64];
    int32_t cx = w->x + w->w / 2, i, x;
    switch (w->t) {
    case W_HEAD:                                   /* the mark, and where we are: an LED a section */
        pg_led(w->x + 5, 7, 1);
        pg_text(w->x + 11, 4, &FONT_XS, "KNOBI-SIX", PC_WHITE);
        pg_text(w->x + 12, 4, &FONT_XS, "KNOBI-SIX", PC_WHITE);
        for (i = SCR_PLAY; i < SCR_COUNT; i++) pg_disc(4 * (w->x + 76 + 6 * i) + 2, 30, 7, i == screen ? PC_LED_ON : PC_LED_OFF, 256u);
        break;
    case W_HVAL:
        t[0] = 'O', t[1] = 'C', t[2] = 'T', t[3] = scr_oct < 0 ? '-' : '+', t[4] = (char)('0' + (scr_oct < 0 ? -scr_oct : scr_oct)), t[5] = 0;
        pg_text(w->x + 2, 4, &FONT_XS, t, scr_oct ? PC_WHITE : PC_DIM);
        scr_seg_text(t, ctl_last_id, ctl_last_v);
        pg_seg(w->x + 38, 1, 4, 6, 9, 1, t);
        break;
    case W_FOOT: {
        const binding_t *b = ctl_binding(screen, row);
        pg_fill(w->x + 2, w->y, w->w - 4, 2, PC_BLUE);
        for (i = 0; i < 4; i++)
            pg_text_c(w->x + w->w * (2 * i + 1) / 8, w->y + 8, &FONT_XS, scr_short(b->fader[i]),
                      b->fader[i] == B_NONE ? PC_FAINT : PC_WHITE);
        break;
    }
    case W_TITLE: pg_text_bold_c(cx, w->y, &FONT_S, w->s, PC_WHITE); pg_fill(w->x + 2, w->y + 16, w->w - 4, 2, PC_BLUE); break;
    case W_RULE: pg_fill(w->x, w->y, w->w, w->h, PC_BLUE); break;
    case W_HINT: pg_text_c(cx, w->y + 1, &FONT_XS, w->s, PC_DIM); break;
    case W_KNOB: {
        int bip = P_IS_LIVE(id) && param_get(id)->bipolar;
        pg_knob(cx, w->y + w->h - 23, 13, v, scr_lo(id), scr_hi(id), bip);
        if (id == P_FLT_MODE) {                    /* the mode knob's ends */
            pg_text(w->x + 1, w->y + w->h - 15, &FONT_XS, "LP", PC_DIM);
            pg_text(w->x + w->w - 12, w->y + w->h - 15, &FONT_XS, "HP", PC_DIM);
        }
        scr_label(t, w->s, id, v);
        pg_text_c(cx, w->y + w->h - 7, &FONT_XS, t, PC_WHITE);
        break;
    }
    case W_ENCBTN:                                 /* a switch on an encoder: clockwise on, anticlockwise off */
        pg_button(cx - 11, w->y + w->h - 28, 22, 11, v != 0);
        pg_text_c(cx, w->y + w->h - 7, &FONT_XS, w->s, PC_WHITE);
        break;
    case W_LIST: scr_list(w, id, v); break;
    case W_LIST2: scr_list2(w, id, v); break;
    case W_BPM: {                                  /* the tempo on a 3-digit display; following MIDI: its tempo ("---"
                                                    * none yet), "BPM MIDI" under it */
        int32_t b = v & 0xFFF;
        x = pg_seg_w(3, 14, 24);
        if (b) scr_num(t, b); else scr_cat(t, "---");
        pg_seg(cx - x / 2, w->y + 4, 3, 14, 24, 3, t);
        pg_text_c(cx, w->y + w->h - 7, &FONT_XS, v >> 12 & 1 ? "BPM  MIDI" : w->s, PC_WHITE);
        break;
    }
    case W_CLKSTAT: {                              /* what the clock follows (the global MIDI CLOCK), in words */
        int m = v & 15, run = v >> 4 & 1, alive = v >> 5 & 1;
        if (!CLOCK_EXTERNAL(m))
            pg_text_c(cx, w->y + 1, &FONT_XS, m == CLK_MIDI_OUT ? "INTERNAL CLOCK, SENT AS MIDI CLOCK" : "INTERNAL CLOCK",
                      PC_WHITE);
        else if (run) pg_text_c(cx, w->y + 1, &FONT_XS, "FOLLOWING MIDI CLOCK", PC_WHITE);
        else pg_text_c(cx, w->y + 1, &FONT_XS, alive ? "MIDI CLOCK STOPPED" : "NO MIDI CLOCK: THE ARP WAITS", PC_RED);
                                                   /* (stopped: ticks arriving, but 0xFC, or no 0xFA yet under IN) */
        pg_text_c(cx, w->y + 11, &FONT_XS, CLOCK_EXTERNAL(m) ? "BPM AND TAP: THE PROGRAM'S OWN TEMPO" :
                  "CLK: THE GLOBAL MIDI CLOCK", PC_DIM);
        break;
    }
    case W_SEQSTAT: {                              /* the state; STEP, LEN, XP displays */
        static const char *const ST[4] = {"STOP", "PLAY", "ARMED", "REC"};
        int st = v & 15;
        int32_t m = (v >> 4 & 0xFF) - 1, len = v >> 12 & 0xFF, xp = (v >> 20 & 0xFFF) - 128, k;
        pg_text_bold_c(w->x + 32, w->y + 4, &FONT_S, ST[st], st == 0 ? PC_DIM : st == 1 ? PC_WHITE : PC_RED);
        pg_text_c(w->x + 32, w->y + 22, &FONT_XS, "SEQ", PC_LABEL);
        for (k = 0; k < 3; k++) {
            int32_t n = k == 2 ? 3 : 2, x0 = w->x + (k == 0 ? 92 : k == 1 ? 142 : 197) - pg_seg_w(n, 9, 14) / 2;
            if (k == 0) { if (m >= 0) scr_num(t, m + 1); else scr_cat(t, "--"); }
            else if (k == 1) scr_num(t, len);
            else scr_num(t, xp);
            pg_seg(x0, w->y, n, 9, 14, 1, t);
            pg_text_c(x0 + pg_seg_w(n, 9, 14) / 2, w->y + 22, &FONT_XS, k == 0 ? "STEP" : k == 1 ? "LEN" : "XP", PC_LABEL);
        }
        break;
    }
    case W_SEQROW: {                               /* 16 cells of 13 x 23: a frame, the notes as dots from the bottom
                                                    * (one a note, up to 6), a rest a dash, a tie a blue bar from the step
                                                    * before; past the length: an empty frame; the marked step: white */
        const seq_t *q = scr_seq();
        int32_t m = scr_seq_mark(), j, k;
        for (j = 0; j < 16; j++) {
            uint32_t si = 16u * w->a + (uint32_t)j;
            int32_t x0 = w->x + 13 * j, in = si < q->len, mk = (int32_t)si == m;
            pg_fill(x0 + 1, w->y + 1, 11, 21, mk ? PC_WHITE : j % 4 == 0 ? PC_DIM : PC_FAINT);
            pg_fill(x0 + 2, w->y + 2, 9, 19, in ? PC_GRAPH_BG : PC_BG);
            if (!in) continue;
            if (!q->step[si].count) pg_fill(x0 + 4, w->y + 11, 5, 1, PC_DIM);
            for (k = 0; k < q->step[si].count; k++) pg_fill(x0 + 4, w->y + 17 - 3 * (int32_t)k, 5, 2, PC_RED);
            if (q->step[si].tie) pg_fill(x0 + 1, w->y + 3, 5, 2, PC_BLUE);   /* (joined to the step before) */
        }
        break;
    }
    case W_SEQUSE:                                 /* the store: KB used of the KB it has (none: the FM-1) */
        if (!(v >> 12)) { pg_text_c(cx, w->y + 1, &FONT_XS, "NO SEQUENCE STORE ON THIS BOARD", PC_FAINT); break; }
        scr_cat(scr_num(scr_cat(scr_num(scr_cat(t, "SEQ "), v & 0xFFF), " KB / "), v >> 12), " KB");
        pg_text_c(cx, w->y + 1, &FONT_XS, t, PC_LABEL);
        break;
    case W_SEQHINT:                                /* what the buttons do now */
        pg_text_c(cx, w->y + 1, &FONT_XS, SCR_SEQHINT[v == 3 ? 2 : v != 0][0], PC_DIM);
        pg_text_c(cx, w->y + 11, &FONT_XS, SCR_SEQHINT[v == 3 ? 2 : v != 0][1], PC_DIM);
        break;
    case W_SEQTAG:                                 /* Play: the sequencer's state */
        if (v) pg_text(w->x + 1, w->y + 2, &FONT_XS, v == 3 ? "REC" : v == 2 ? "ARM" : "SEQ", v == 1 ? PC_WHITE : PC_RED);
        break;
    case W_SEGENC:
        scr_seg_text(t, id, v);
        pg_seg(cx - pg_seg_w(3, 10, 18) / 2, w->y + 4, 3, 10, 18, 3, t);
        pg_text_c(cx, w->y + w->h - 7, &FONT_XS, w->s, PC_WHITE);
        break;
    case W_PAD: {
        int active = v >> 16 & 1, on;
        int32_t val = (int32_t)(int16_t)(v & 0xFFFF);
        if (P_IS_LIVE(id)) on = val != param_get(id)->min;
        else if (B_IS_GLOBAL(id)) on = val != scr_lo(id);
        else on = val != 0;                        /* HOLD, WRITE (blinks armed), ENTER (armed), MANUAL */
        pg_button(cx - 9, w->y + 1, 18, 8, on);
        if (B_IS_GLOBAL(id)) scr_label(t, w->s, id, val);
        else if (P_IS_LIVE(id) && param_get(id)->kind == PK_SELECT) {
            int has = 0;
            for (i = 0; w->s[i]; i++) has |= w->s[i] == '%';
            if (has) scr_label(t, w->s, id, val);
            else scr_cat(t, val == param_get(id)->min ? w->s : scr_choice(id, val));
        } else scr_cat(t, w->s);
        pg_text_c(cx, w->y + 12, &FONT_XS, t, active ? PC_WHITE : PC_FAINT);
        break;
    }
    case W_ROWLED:                                 /* the pads reach this row now (SCALE) */
        pg_led(w->x + 4, w->y + 4, v == w->b);
        pg_text(w->x + 10, w->y + 1, &FONT_XS, "PADS", v == w->b ? PC_WHITE : PC_FAINT);
        break;
    case W_ROWSEL:                                 /* the encoders and faders edit this envelope now */
        pg_led(w->x + 52, w->y + 5, v == 0);
        pg_text(w->x + 58, w->y + 2, &FONT_XS, "FILTER", v == 0 ? PC_WHITE : PC_FAINT);
        pg_led(w->x + 124, w->y + 5, v == 1);
        pg_text(w->x + 130, w->y + 2, &FONT_XS, "LOUDNESS", v == 1 ? PC_WHITE : PC_FAINT);
        break;
    case W_ENVG: scr_envg(w, v); break;
    case W_PROG:                                   /* bank . program, as the panel's display (MANUAL: dashes) */
        x = pg_seg_w(1, 14, 24) + 6;
        pg_text(w->x + 1, w->y + 1, &FONT_XS, "BANK", PC_LABEL);
        pg_text(w->x + x + 1, w->y + 1, &FONT_XS, v < 0 ? "MANUAL" : "PROGRAM", PC_LABEL);
        t[0] = v < 0 ? '-' : (char)('0' + v / 100 % 10), t[1] = 0;
        pg_seg(w->x, w->y + 11, 1, 14, 24, 3, t);
        t[0] = v < 0 ? '-' : (char)('0' + v / 10 % 10), t[1] = v < 0 ? '-' : (char)('0' + v % 10), t[2] = 0;
        pg_seg(w->x + x, w->y + 11, 2, 14, 24, 3, t);
        break;
    case W_DIGIT:                                  /* a program digit, big, its encoder's name under it */
        t[0] = v < 0 ? '-' : (char)('0' + v % 10), t[1] = 0;
        x = pg_seg_w(1, 18, 30);
        pg_seg(cx - x / 2, w->y, 1, 18, 30, 3, t);
        pg_text_c(cx, w->y + w->h - 7, &FONT_XS, w->s, PC_WHITE);
        break;
    case W_WSTAT: {                                /* what WRITE / MANUAL are doing, in words */
        char *q = t;
        int32_t d = ctl_wr_dest;
        if (ctl_wr == 1 || ctl_wr == 2) {
            q = scr_cat(q, ctl_wr == 1 ? "WRITE TO " : "WRITING ");
            *q++ = (char)('0' + d / 100), *q++ = '-', *q++ = (char)('0' + d / 10 % 10), *q++ = (char)('0' + d % 10), *q = 0;
            if (ctl_wr == 1) scr_cat(q, "?");
            pg_text_c(cx, w->y + 1, &FONT_XS, t, PC_WHITE);
            if (ctl_wr == 1) pg_text_c(cx, w->y + 11, &FONT_XS, "ENTER: STORE   WRITE: CANCEL", PC_DIM);
        } else if (ctl_notice == NT_BROKEN) {
            pg_text_c(cx, w->y + 1, &FONT_XS, "STORED PROGRAM BROKEN: INIT LOADED", PC_RED);
        } else if (ctl_notice == NT_SEQ_MISSING) {
            pg_text_c(cx, w->y + 1, &FONT_XS, "STORED SEQUENCE MISSING: EMPTY LOADED", PC_RED);
            pg_text_c(cx, w->y + 11, &FONT_XS, "WRITE KEEPS IT; AN EDIT REPLACES IT", PC_DIM);
        } else if (ctl_notice == NT_RESTORE_ASK) {
            pg_text_c(cx, w->y + 1, &FONT_XS, "RESTORE FACTORY? EDITS ARE LOST", PC_WHITE);
            pg_text_c(cx, w->y + 11, &FONT_XS, "GLOBE + BT AGAIN WITHIN 2 S", PC_DIM);
        } else if (ctl_notice == NT_RESTORED) {
            pg_text_c(cx, w->y + 1, &FONT_XS, "FACTORY RESTORED: WRITE TO KEEP", PC_WHITE);
        } else if (ctl_manual) {
            pg_text_c(cx, w->y + 1, &FONT_XS, "MANUAL: THE SOUND IS THE PANEL", PC_WHITE);
        } else if (ctl_wr_rc) {
            pg_text_c(cx, w->y + 1, &FONT_XS, ctl_wr_rc > 0 ? "STORED" : ctl_wr_rc == -2 ? "SEQ FULL: NOTHING STORED" :
                      "WRITE FAILED: NOTHING CHANGED", ctl_wr_rc > 0 ? PC_WHITE : PC_RED);
            if (ctl_wr_rc == -2)                   /* (the spec's way to free room) */
                pg_text_c(cx, w->y + 11, &FONT_XS, "CLEAR SEQS OR RESTORE FACTORY+WRITE", PC_DIM);
        } else pg_text_c(cx, w->y + 1, &FONT_XS, "WRITE (OR BT) TO STORE THE SOUND", PC_DIM);
        break;
    }
    case W_NAME: case W_BIGNAME: {
        const felucca_font_t *f = w->t == W_NAME ? &FONT_S : &FONT_L;
        for (i = 0; i < PROGRAM_NAME_LEN && scr_p->name[i]; i++) t[i] = scr_p->name[i];
        t[i] = 0;
        while (i > 0 && t[i - 1] == ' ') t[--i] = 0;
        if (w->t == W_NAME) {
            x = w->x + w->w - text_w(f, t) - 1;
            pg_text(x, w->y, f, t, PC_WHITE);
            pg_text(x + 1, w->y, f, t, PC_WHITE);
        } else pg_text_c(cx, w->y, f, t, PC_WHITE);
        break;
    }
    case W_VOICES:                                 /* an LED a voice, lit while it sounds */
        x = w->x + w->w - text_w(&FONT_XS, "VOICES");
        pg_text(x, w->y + 2, &FONT_XS, "VOICES", PC_LABEL);
        for (i = 0; i < NVOICE; i++) pg_led(x - 8 - 9 * (NVOICE - 1 - i), w->y + 5, v >> i & 1);
        break;
    case W_DIAG: {                                 /* the load and the CPU guard (engine.c eng_cpu); the raw wheels
                                                    * and faders (checking a unit's travel) */
        uint32_t q = (uint32_t)scr_cpu_q();
#if KNOBI_CPU1_PROBE
        int32_t y = scr_probe_lines(cx, w->y + 1);   /* (the probe's lines first; then the usual, 9 rows apart) */
#define SCR_DIAG_Y(n) (y + 9 * (n))
#else
#define SCR_DIAG_Y(n) (w->y + 1 + 10 * (n))
#endif
        char *p = scr_num(scr_cat(t, "CPU "), (int32_t)(q & 0xFFu) * 5);
        p = scr_num(scr_cat(p, "% PEAK "), (int32_t)(q >> 8 & 0xFFu) * 5);
        p = scr_num(scr_cat(p, "%  VOX "), (int32_t)(q >> 16 & 0xFu));
        p = scr_num(scr_cat(p, "/"), NVOICE);
        scr_num(scr_cat(p, "  SHED "), (int32_t)(q >> 20));
        pg_text_c(cx, SCR_DIAG_Y(0), &FONT_XS, t, (q >> 20) ? PC_WHITE : PC_DIM);
        p = scr_cat(t, "WHEELS ");
        p = scr_num(p, fm1_in.wheel[0]);
#if KNOBI_CPU1 && !KNOBI_CPU1_PROBE
        scr_c1(scr_num(scr_cat(p, " "), fm1_in.wheel[1]));   /* + "C1 ON 40%" / "C1 OFF" / "C1 FAIL" */
#else
        scr_num(scr_cat(p, " "), fm1_in.wheel[1]);    /* (the probe: DUAL's state is on its PROBE line) */
#endif
        pg_text_c(cx, SCR_DIAG_Y(1), &FONT_XS, t, PC_DIM);
        p = scr_cat(t, "FADERS");
        for (i = 0; i < 4; i++) p = scr_num(scr_cat(p, " "), fm1_in.fader[i]);
        pg_text_c(cx, SCR_DIAG_Y(2), &FONT_XS, t, PC_DIM);
#undef SCR_DIAG_Y
        break;
    }
    case W_WORD: pg_wordmark(cx - pg_word_w(w->s) / 2, w->y + 1, w->s, PC_WHITE); break;
    case W_VER:
        scr_cat(t, APP_VER);
        for (i = x = 0; t[i]; i++) x += t[i] != '.';
        pg_seg(cx - pg_seg_w(x, 14, 24) / 2, w->y + 2, x, 14, 24, 3, t);
        break;
    case W_BOOTTXT:
        scr_cat(scr_cat(scr_num(t, NVOICE), " VOICES   2 VCOS   "), "SEM-STYLE FILTER");
        pg_text_c(cx, w->y + 1, &FONT_XS, t, PC_DIM);
        break;
    case W_VERTXT:
        scr_cat(scr_cat(t, "KNOBI-SIX " APP_VER "  "), FELUCCA_ID);
        pg_text_c(cx, w->y + 1, &FONT_XS, t, PC_FAINT);
        break;
    }
}

/* the rectangle (x, y, w, h) of the screen, drawn again: the panel, the cheeks, every widget touching it. In strips of
 * at most CV_MAX pixels */
static void scr_paint(int screen, int row, int32_t x, int32_t y, int32_t w, int32_t h)
{
    const wdg_t *ws = SCR_W[screen];
    int32_t rows = (int32_t)(CV_MAX / (uint32_t)w), y1 = y + h;
    for (; y < y1; y += rows) {
        int32_t hh = y1 - y < rows ? y1 - y : rows;
        uint32_t i;
        pg_begin(x, y, w, hh);
        pg_wood(0, 7);
        pg_wood(233, 7);
        for (i = 0; ws[i].t != W_END; i++)
            if (pg_hits(ws[i].x, ws[i].y, ws[i].w, ws[i].h)) scr_paint_w(screen, row, &ws[i], scr_cache[i]);
        pg_end();
    }
}

/* the interface (ui/screens.h's words): draw what changed of screen `screen`, or all of it (full, or a new screen or
 * row). p: the program as edited (eng_edit), s: the synth (its voices' busy flags, read only) */
void screen_draw(int screen, const program_t *p, const synth_t *s, int full)
{
    const wdg_t *ws;
    int row;
    uint32_t i;
    if (screen < 0 || screen >= SCR_COUNT) screen = SCR_BOOT;
    ws = SCR_W[screen];
    row = SCR_ROWS[screen] > 1 ? ctl_row[screen] : 0;
    scr_p = p, scr_s = s;
    if (full || screen != scr_on || row != scr_on_row) {   /* a new screen: its values now, then strip by strip */
        scr_on = screen, scr_on_row = row;
        for (i = 0; ws[i].t != W_END && i < SCR_WMAX; i++) scr_cache[i] = scr_wval(screen, row, &ws[i]);
        scr_strip = 0, scr_px_acc = 0;
        scr_fulls++;
    }
    pg_sent = 0;
    if (scr_strip < 240u) {                        /* one strip a call */
        scr_paint(screen, row, 0, (int32_t)scr_strip, 240, (int32_t)(240u - scr_strip < SCR_STRIP ? 240u - scr_strip : SCR_STRIP));
        scr_strip += SCR_STRIP;
        scr_px_acc += pg_sent;
        if (scr_strip >= 240u) scr_px_last = scr_px_acc;
        return;
    }
    for (i = 0; ws[i].t != W_END && i < SCR_WMAX; i++) {   /* then only what changed */
        int32_t v = scr_wval(screen, row, &ws[i]);
        if (v == scr_cache[i]) continue;
        scr_cache[i] = v;
        scr_paint(screen, row, ws[i].x, ws[i].y, ws[i].w, ws[i].h);
    }
    if (pg_sent) scr_px_last = pg_sent;
}

/* the tables' promises, for every screen and row: the encoder widgets are slots 0, 1, 2... in reading (table) order,
 * one for each bound encoder, of the right kind (a knob for a knob, a button for a switch, a list or display for a
 * select or a global); every pad widget's pad is bound, every bound switch pad has a widget; every label fits its
 * widget (a select's every choice). Returns the faults (0) and prints them on the host */
#ifdef DINGHY_HOST
#define SCR_BAD (bad++, printf("screen_check: screen %d row %d widget %u: line %d\n", sc, row, (unsigned)i, __LINE__))
#else
#define SCR_BAD bad++
#endif
static int screen_check(void)
{
    int bad = 0, sc, row;
    for (sc = 0; sc < SCR_COUNT; sc++)
        for (row = 0; row < SCR_ROWS[sc]; row++) {
            const binding_t *b = ctl_binding(sc, row);
            const wdg_t *ws = SCR_W[sc];
            uint32_t n = 0, nb = 0, padseen = 0, k, i;
            for (k = 0; k < 7u; k++) nb += b->enc[k] != B_NONE;
            for (i = 0; ws[i].t != W_END; i++) {
                const wdg_t *w = &ws[i];
                uint16_t id = scr_wid(sc, row, w);
                char t[40];
                int32_t lim = w->w;
                if (i >= SCR_WMAX) SCR_BAD;
                if (w->x + w->w > 240 || w->y + w->h > 240) SCR_BAD;
                if (w->t == W_KNOB || w->t == W_ENCBTN || w->t == W_LIST || w->t == W_SEGENC || w->t == W_DIGIT ||
                    w->t == W_BPM || w->t == W_LIST2) {
                    int kind = P_IS_LIVE(id) ? param_get(id)->kind : B_IS_GLOBAL(id) || B_IS_PROG(id) ? PK_SELECT : -1;
                    if (w->a != n++ || id == B_NONE) SCR_BAD;
                    if ((w->t == W_KNOB || w->t == W_BPM) != (kind == PK_KNOB) || (w->t == W_ENCBTN) != (kind == PK_SWITCH))
                        SCR_BAD;
                    if (w->t == W_BPM && id != P_BPM) SCR_BAD;
                    if (w->t == W_SEGENC && !B_IS_GLOBAL(id)) SCR_BAD;
                    if ((w->t == W_DIGIT) != (B_IS_PROG(id) != 0)) SCR_BAD;
                }
                if (w->t == W_PAD && (w->b == 2 || w->b == row)) {
                    if (id == B_NONE) SCR_BAD;
                    padseen |= 1u << w->a;
                }
                if (w->s && (w->t == W_KNOB || w->t == W_PAD || w->t == W_LIST || w->t == W_ENCBTN || w->t == W_SEGENC ||
                             w->t == W_HINT || w->t == W_DIGIT || w->t == W_BPM || w->t == W_LIST2)) {
                    int32_t v, lo = id != B_NONE ? scr_lo(id) : 0, hi = id != B_NONE ? scr_hi(id) : 0;
                    for (v = lo; v <= hi; v++) {
                        if (w->t == W_PAD && P_IS_LIVE(id) && param_get(id)->kind == PK_SELECT) {
                            int has = 0;
                            for (k = 0; w->s[k]; k++) has |= w->s[k] == '%';
                            if (has) scr_label(t, w->s, id, v);
                            else scr_cat(t, v == param_get(id)->min ? w->s : scr_choice(id, v));
                        } else scr_label(t, w->s, id, v);
                        if (text_w(&FONT_XS, t) - 1 > lim) {
                            SCR_BAD;
#ifdef DINGHY_HOST
                            printf("screen %d: \"%s\" is %d px in a widget %d wide\n", sc, t, text_w(&FONT_XS, t) - 1, (int)lim);
#endif
                        }
                        if (w->t == W_LIST && scr_choice(id, v) && text_w(&FONT_XS, scr_choice(id, v)) + 8 > lim) SCR_BAD;
                        if (w->t == W_LIST2 && (!scr_choice(id, v) || text_w(&FONT_XS, scr_choice(id, v)) + 8 > lim / 2))
                            SCR_BAD;
                    }
                }
            }
            if (n != nb) SCR_BAD;
            for (k = 0; k < 8u; k++)                /* a bound pad (not the section arrows) has its button */
                if (b->pad_top[k] != B_NONE && b->pad_top[k] != B_ACT(ACT_PREV) && b->pad_top[k] != B_ACT(ACT_NEXT) &&
                    !(padseen >> k & 1u))
                    SCR_BAD;
#ifdef DINGHY_HOST
            if (bad) printf("screen_check: screen %d row %d: %d fault(s) so far\n", sc, row, bad);
#endif
        }
    {                                               /* the Sequencer's words (drawn from state, not a label) fit; Play's
                                                    * tag and the voices' LEDs beside it do not overlap */
        uint32_t i = 0;
        int32_t vx;
        sc = SCR_SEQ, row = 0;
        for (i = 0; i < 6u; i++) if (text_w(&FONT_XS, SCR_SEQHINT[i / 2u][i & 1u]) - 1 > 226) SCR_BAD;
        if (text_w(&FONT_XS, "SEQ 999 KB / 999 KB") - 1 > 226 || text_w(&FONT_XS, "NO SEQUENCE STORE ON THIS BOARD") - 1 > 226)
            SCR_BAD;
        for (i = 0; SCR_PLAY_W[i].t != W_END; i++)
            if (SCR_PLAY_W[i].t == W_SEQTAG) {
                sc = SCR_PLAY;
                if (text_w(&FONT_XS, "REC") + 1 > SCR_PLAY_W[i].w || text_w(&FONT_XS, "ARM") + 1 > SCR_PLAY_W[i].w) SCR_BAD;
            } else if (SCR_PLAY_W[i].t == W_VOICES) {
                vx = SCR_PLAY_W[i].x + SCR_PLAY_W[i].w - text_w(&FONT_XS, "VOICES") - 8 - 9 * (NVOICE - 1);
                if (vx - 6 < SCR_PLAY_W[i].x) SCR_BAD;      /* (the leftmost LED and its glow, 5.5 px, inside the widget) */
            }
    }
    return bad;
}
