/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX panel drawing: the parts of the panel's visual language (docs/design/standalone-ui/, a style guide) on the
 * 240 x 240 screen: charcoal ground, white bold titles over blue rules, black knobs with a white dot at the value's
 * angle (300 degrees of travel), small white labels (FONT_XS, firmware/gen/small_font.h), red 7-segment digits drawn
 * segment by segment (no font), red LEDs, lit red buttons, wood cheeks, the outlined wordmark.
 *
 * Everything draws into the canvas (gfx.c's cv_px) in screen coordinates: pg_begin(x, y, w, h) makes the canvas that
 * rectangle of the screen (cleared to the ground), the pg_* calls clip to it, pg_end() sends it. So any rectangle of a
 * screen can be drawn again by itself (screens.c: a widget that changed) or the whole screen in strips. Main loop
 * only; integers only. Included by app.c after gfx.c (platform.c) and controls.c. */
#include "small_font.h"

#define PC_BG RGB(0x1d, 0x1e, 0x22)          /* the panel */
#define PC_BLUE RGB(0x2f, 0x6b, 0xe0)        /* the rules */
#define PC_WHITE RGB(0xf4, 0xf4, 0xf4)       /* titles, labels, the knob's dot */
#define PC_LABEL RGB(0xcf, 0xd3, 0xdb)       /* captions */
#define PC_DIM RGB(0x9a, 0xa0, 0xaa)         /* hints */
#define PC_FAINT RGB(0x5c, 0x60, 0x68)       /* what the controls do not reach now */
#define PC_RED RGB(0xff, 0x2a, 0x1a)         /* 7-segment lit */
#define PC_SEG_OFF RGB(0x3a, 0x0e, 0x0a)     /* 7-segment unlit (the ghost of the segment) */
#define PC_SEG_BG RGB(0x12, 0x06, 0x06)      /* the display window */
#define PC_LED_ON RGB(0xff, 0x35, 0x24)
#define PC_LED_OFF RGB(0x3c, 0x0f, 0x0b)
#define PC_BTN_ON RGB(0xff, 0x4a, 0x2a)
#define PC_BTN_OFF RGB(0x2a, 0x0a, 0x07)
#define PC_KNOB_HI RGB(0x4a, 0x4b, 0x50)     /* the knob's lit side */
#define PC_KNOB_LO RGB(0x14, 0x14, 0x16)     /* and its dark side */
#define PC_WOOD_D RGB(0x5a, 0x2a, 0x14)      /* the cheeks: dark edge */
#define PC_WOOD_L RGB(0x8a, 0x4a, 0x26)      /* and light middle */
#define PC_GRAPH_BG RGB(0x14, 0x15, 0x18)

static int32_t pg_x0, pg_y0;                 /* the canvas's place on the screen */
static uint32_t pg_sent;                     /* pixels sent to the screen (screens.c reports them) */

static void pg_begin(int32_t x, int32_t y, int32_t w, int32_t h)
{
    cv_begin((uint32_t)w, (uint32_t)h, PC_BG);   /* (h shrinks to CV_MAX / w: screens.c never asks more) */
    cv_oy = 0;
    pg_x0 = x, pg_y0 = y;
}
static void pg_end(void)
{
    cv_blit((uint32_t)pg_x0, (uint32_t)pg_y0);
    pg_sent += cv_w * cv_h;
}

static inline void pg_px(int32_t x, int32_t y, uint16_t c)
{
    x -= pg_x0, y -= pg_y0;
    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h) cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16(c);
}
static uint16_t pg_mix(uint16_t c0, uint16_t c1, uint32_t a)   /* c0 .. c1, a = 0..256 */
{
    uint32_t r0 = c0 >> 11, g0 = c0 >> 5 & 63u, b0 = c0 & 31u, r1 = c1 >> 11, g1 = c1 >> 5 & 63u, b1 = c1 & 31u;
    if (a > 256u) a = 256u;
    return (uint16_t)(((r0 * (256u - a) + r1 * a) >> 8) << 11 | ((g0 * (256u - a) + g1 * a) >> 8) << 5 |
                      ((b0 * (256u - a) + b1 * a) >> 8));
}
static void pg_blend(int32_t x, int32_t y, uint16_t c, uint32_t a)   /* c over what is there, a = 0..256 */
{
    uint32_t i;
    x -= pg_x0, y -= pg_y0;
    if ((uint32_t)x >= cv_w || (uint32_t)y >= cv_h || !a) return;
    i = (uint32_t)y * cv_w + (uint32_t)x;
    cv_px[i] = swap16(pg_mix(swap16(cv_px[i]), c, a));
}
static void pg_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1, y1, i;
    uint16_t sc = swap16(c);
    x -= pg_x0, y -= pg_y0;
    x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > (int32_t)cv_w) x1 = (int32_t)cv_w;
    if (y1 > (int32_t)cv_h) y1 = (int32_t)cv_h;
    for (; y < y1; y++)
        for (i = x; i < x1; i++) cv_px[(uint32_t)y * cv_w + (uint32_t)i] = sc;
}
static int pg_hits(int32_t x, int32_t y, int32_t w, int32_t h)   /* does the rectangle touch the canvas? */
{
    return x < pg_x0 + (int32_t)cv_w && x + w > pg_x0 && y < pg_y0 + (int32_t)cv_h && y + h > pg_y0;
}
static void pg_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 1000;
    while (guard--) {
        int32_t e2 = 2 * err;
        pg_px(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        if (e2 >= dy) err += dy, x0 += sx;
        if (e2 <= dx) err += dx, y0 += sy;
    }
}

/* ---- text: blended over the ground (gfx.c's cv_text blends onto black) */
static uint32_t pg_alpha(const felucca_font_t *f, uint32_t gi, int32_t gx, int32_t gy)   /* a glyph's pixel, 0..15 */
{
    uint32_t w = f->bw[gi], v;
    if (gx < 0 || gy < 0 || (uint32_t)gx >= w || (uint32_t)gy >= f->h) return 0;
    v = f->data[f->off[gi] + (uint32_t)gy * ((w + 1u) / 2u) + (uint32_t)gx / 2u];
    return (gx & 1) ? (v & 15u) : (v >> 4);
}
static int32_t pg_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    for (; *s; s++) {
        uint32_t gi = glyph(f, (uint8_t)*s), w = f->bw[gi], gx, gy, a;
        if (pg_hits(x - f->pad, y, (int32_t)w, f->h))
            for (gy = 0; gy < f->h; gy++)
                for (gx = 0; gx < w; gx++)
                    if ((a = pg_alpha(f, gi, (int32_t)gx, (int32_t)gy)) != 0)
                        pg_blend(x - f->pad + (int32_t)gx, y + (int32_t)gy, c, a * 256u / 15u);
        x += f->adv[gi];
    }
    return x;
}
static void pg_text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)   /* centred on cx */
{
    pg_text(cx - (text_w(f, s) - 1) / 2, y, f, s, c);
}
static void pg_text_bold_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    int32_t x = cx - text_w(f, s) / 2;
    pg_text(x, y, f, s, c);
    pg_text(x + 1, y, f, s, c);                    /* struck twice: the panel's bold */
}

/* the title of a section: white bold, centred over a blue rule (19 px tall) */
static void pg_title(int32_t x, int32_t y, int32_t w, const char *s)
{
    pg_text_bold_c(x + w / 2, y, &FONT_S, s, PC_WHITE);
    pg_fill(x, y + 16, w, 2, PC_BLUE);
}

/* ---- round things, antialiased: 4 samples a pixel, coordinates in quarter pixels */
static uint32_t pg_cover(int32_t px, int32_t py, int32_t qx, int32_t qy, int32_t qr)   /* 0..4 of pixel (px, py) */
{
    uint32_t n = 0, i;
    int32_t r2 = qr * qr;
    for (i = 0; i < 4u; i++) {
        int32_t dx = 4 * px + 1 + 2 * (int32_t)(i & 1u) - qx, dy = 4 * py + 1 + (int32_t)(i & 2u) - qy;
        if (dx * dx + dy * dy <= r2) n++;
    }
    return n;
}
static void pg_disc(int32_t qx, int32_t qy, int32_t qr, uint16_t c, uint32_t a)   /* a = 0..256 at full cover */
{
    int32_t x0 = (qx - qr) / 4 - 1, x1 = (qx + qr) / 4 + 1, y0 = (qy - qr) / 4 - 1, y1 = (qy + qr) / 4 + 1, x, y;
    if (!pg_hits(x0, y0, x1 - x0 + 1, y1 - y0 + 1)) return;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            uint32_t n = pg_cover(x, y, qx, qy, qr);
            if (n) pg_blend(x, y, c, a * n / 4u);
        }
}
static uint32_t pg_isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) v -= r + b, r = (r >> 1) + b;
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

static const int16_t PG_SIN[91] = {     /* sin(degrees) x 4096 */
    0, 71, 143, 214, 286, 357, 428, 499, 570, 641, 711, 782, 852, 921, 991, 1060, 1129, 1198, 1266, 1334, 1401, 1468,
    1534, 1600, 1666, 1731, 1796, 1860, 1923, 1986, 2048, 2110, 2171, 2231, 2290, 2349, 2408, 2465, 2522, 2578, 2633,
    2687, 2741, 2793, 2845, 2896, 2946, 2996, 3044, 3091, 3138, 3183, 3228, 3271, 3314, 3355, 3396, 3435, 3474, 3511,
    3547, 3582, 3617, 3650, 3681, 3712, 3742, 3770, 3798, 3824, 3849, 3873, 3896, 3917, 3937, 3956, 3974, 3991, 4006,
    4021, 4034, 4046, 4056, 4065, 4074, 4080, 4086, 4090, 4094, 4095, 4096};
static int32_t pg_sin(int32_t d)        /* any whole degrees */
{
    d %= 360;
    if (d < 0) d += 360;
    if (d <= 90) return PG_SIN[d];
    if (d <= 180) return PG_SIN[180 - d];
    if (d <= 270) return -PG_SIN[d - 180];
    return -PG_SIN[360 - d];
}

/* a knob: centre (cx, cy) a pixel's top-left corner (so an even width is centred), radius r px; v in lo..hi turns the
 * white dot from -150 degrees (lo, 7 o'clock) to +150 (hi, 5 o'clock), 0 = straight up. bipolar: a tick at the top */
static void pg_knob(int32_t cx, int32_t cy, int32_t r, int32_t v, int32_t lo, int32_t hi, int bipolar)
{
    int32_t qx = 4 * cx, qy = 4 * cy, qr = 4 * r, hx = qx - qr / 4, hy = qy - qr * 36 / 100, x, y, ang, s, c;
    int32_t span = qr * 13 / 10;
    if (bipolar) pg_fill(cx - 1, cy - r - 4, 2, 2, PC_DIM);
    pg_disc(qx, qy + 8, qr, C_BLACK, 150);         /* the shadow, 2 px down */
    if (pg_hits(cx - r - 1, cy - r - 1, 2 * r + 2, 2 * r + 2))
        for (y = cy - r - 1; y <= cy + r; y++)
            for (x = cx - r - 1; x <= cx + r; x++) {
                uint32_t n = pg_cover(x, y, qx, qy, qr), d;
                int32_t dx, dy;
                if (!n) continue;
                dx = 4 * x + 2 - hx, dy = 4 * y + 2 - hy;   /* the light from the top left */
                d = pg_isqrt((uint32_t)(dx * dx + dy * dy)) * 256u / (uint32_t)span;
                pg_blend(x, y, pg_mix(PC_KNOB_HI, PC_KNOB_LO, d > 165u ? 256u : d * 256u / 165u), 64u * n);
            }
    if (hi <= lo) hi = lo + 1;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    ang = -150 + (v - lo) * 300 / (hi - lo);
    s = pg_sin(ang), c = pg_sin(ang + 90);
    pg_disc(qx + s * (qr - 22) / 4096, qy - c * (qr - 22) / 4096, 9, PC_WHITE, 256u);   /* the dot */
}

static void pg_led(int32_t cx, int32_t cy, int on)   /* centre of pixel (cx, cy) */
{
    int32_t qx = 4 * cx + 2, qy = 4 * cy + 2;
    if (on) {
        pg_disc(qx, qy, 22, PC_LED_ON, 70u);       /* the glow */
        pg_disc(qx, qy, 12, PC_LED_ON, 256u);
        pg_disc(qx - 3, qy - 3, 4, PC_WHITE, 120u);
    } else
        pg_disc(qx, qy, 12, PC_LED_OFF, 256u);
}

static void pg_led_s(int32_t cx, int32_t cy, int on)   /* a list's LED: smaller, no glow past its row */
{
    int32_t qx = 4 * cx + 2, qy = 4 * cy + 2;
    if (on) {
        pg_disc(qx, qy, 15, PC_LED_ON, 90u);
        pg_disc(qx, qy, 10, PC_LED_ON, 256u);
        pg_disc(qx - 2, qy - 2, 3, PC_WHITE, 140u);
    } else
        pg_disc(qx, qy, 10, PC_LED_OFF, 256u);
}

static void pg_button(int32_t x, int32_t y, int32_t w, int32_t h, int on)   /* rounded corners, lit red when on */
{
    if (on) {
        pg_fill(x - 2, y - 1, w + 4, h + 2, pg_mix(PC_BG, PC_BTN_ON, 60u));   /* the glow */
        pg_fill(x - 1, y - 2, w + 2, h + 4, pg_mix(PC_BG, PC_BTN_ON, 60u));
        pg_fill(x, y, w, h, PC_BTN_ON);
        pg_fill(x + 1, y + 1, w - 2, 1, pg_mix(PC_BTN_ON, PC_WHITE, 110u));
    } else {
        pg_fill(x, y, w, h, C_BLACK);
        pg_fill(x + 1, y + 1, w - 2, h - 2, PC_BTN_OFF);
    }
    pg_px(x, y, PC_BG), pg_px(x + w - 1, y, PC_BG), pg_px(x, y + h - 1, PC_BG), pg_px(x + w - 1, y + h - 1, PC_BG);
}

/* ---- 7-segment digits, drawn segment by segment: a b c d e f g = bits 0..6 */
static uint32_t pg_segbits(char ch)
{
    static const uint8_t DIG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    if (ch >= '0' && ch <= '9') return DIG[ch - '0'];
    switch (ch) {
    case '-': return 0x40;
    case '_': return 0x08;
    case 'A': case 'a': return 0x77;
    case 'B': case 'b': return 0x7C;
    case 'C': return 0x39;
    case 'c': return 0x58;
    case 'D': case 'd': return 0x5E;
    case 'E': case 'e': return 0x79;
    case 'F': case 'f': return 0x71;
    case 'G': case 'g': return 0x3D;
    case 'H': return 0x76;
    case 'h': return 0x74;
    case 'I': case 'i': return 0x30;
    case 'J': case 'j': return 0x1E;
    case 'L': case 'l': return 0x38;
    case 'N': case 'n': return 0x54;
    case 'O': return 0x3F;
    case 'o': return 0x5C;
    case 'P': case 'p': return 0x73;
    case 'R': case 'r': return 0x50;
    case 'S': case 's': return 0x6D;
    case 'T': case 't': return 0x78;
    case 'U': return 0x3E;
    case 'u': case 'V': case 'v': return 0x1C;
    case 'Y': case 'y': return 0x6E;
    }
    return 0;
}
static int32_t pg_iabs(int32_t v) { return v < 0 ? -v : v; }
/* one digit in a w x h cell at (x, y), segments t px thick (odd), leaning right like the panel's displays */
static void pg_seg_digit(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint32_t bits)
{
    int32_t half = t / 2, xl = half, xr = w - 1 - half, ya = half, yg = h / 2, yd = h - 1 - half, i, j;
    if (!pg_hits(x, y, w + h / 6 + 1, h)) return;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            int32_t seg = -1, di, dj;
            dj = pg_iabs(j - ya);
            if (dj <= half && i >= xl + 1 + dj && i <= xr - 1 - dj) seg = 0;
            dj = pg_iabs(j - yg);
            if (dj <= half && i >= xl + 1 + dj && i <= xr - 1 - dj) seg = 6;
            dj = pg_iabs(j - yd);
            if (dj <= half && i >= xl + 1 + dj && i <= xr - 1 - dj) seg = 3;
            di = pg_iabs(i - xr);
            if (di <= half && j >= ya + 1 + di && j <= yg - 1 - di) seg = 1;
            if (di <= half && j >= yg + 1 + di && j <= yd - 1 - di) seg = 2;
            di = pg_iabs(i - xl);
            if (di <= half && j >= ya + 1 + di && j <= yg - 1 - di) seg = 5;
            if (di <= half && j >= yg + 1 + di && j <= yd - 1 - di) seg = 4;
            if (seg >= 0) pg_px(x + i + (h - 1 - j) / 6, y + j, bits >> seg & 1u ? PC_RED : PC_SEG_OFF);
        }
}
/* a display window of n digits w x h: its width (its height is h + 2 * pad) */
static int32_t pg_seg_pad(int32_t h) { return h > 14 ? 4 : 2; }
static int32_t pg_seg_w(int32_t n, int32_t w, int32_t h)
{
    int32_t gap = pg_seg_pad(h);
    return 2 * pg_seg_pad(h) + n * (w + gap) - gap + h / 6 + 1;
}
/* the window at (x, y), the text right-aligned in it ('.' lights the decimal point of the digit before it) */
static void pg_seg(int32_t x, int32_t y, int32_t n, int32_t w, int32_t h, int32_t t, const char *s)
{
    int32_t pad = pg_seg_pad(h), pitch = w + pad, bw = pg_seg_w(n, w, h), len = 0, k, i;
    uint8_t bits[12], dp[12];
    for (i = 0; s[i] && len < 12; i++) {
        if (s[i] == '.' && len) { dp[len - 1] = 1; continue; }
        bits[len] = (uint8_t)pg_segbits(s[i]), dp[len] = 0, len++;
    }
    pg_fill(x, y, bw, h + 2 * pad, C_BLACK);
    pg_fill(x + 1, y + 1, bw - 2, h + 2 * pad - 2, PC_SEG_BG);
    for (k = 0; k < n; k++) {
        int32_t ci = k - (n - len), dx = x + pad + k * pitch;
        pg_seg_digit(dx, y + pad, w, h, t, ci >= 0 ? bits[ci] : 0u);
        if (ci >= 0 && dp[ci]) pg_fill(dx + w, y + pad + h - t, t, t, PC_RED);
    }
}

/* ---- the wood cheeks: a brown cylinder, with grain, the full height of the screen at x .. x + w - 1 */
static void pg_wood(int32_t x, int32_t w)
{
    int32_t i, y, y0 = pg_y0 < 0 ? 0 : pg_y0, y1 = pg_y0 + (int32_t)cv_h;
    if (!pg_hits(x, 0, w, 240)) return;
    for (i = 0; i < w; i++) {
        int32_t edge = pg_iabs(2 * i - (w - 1)) * 256 / (w - 1);   /* 0 in the middle, 256 at the edges */
        uint16_t base = pg_mix(PC_WOOD_L, PC_WOOD_D, (uint32_t)(edge * edge / 256));
        for (y = y0; y < y1 && y < 240; y++) {
            int32_t g = ((x + i) * 7 + (y >> 4) * 3 + ((y * 5 + (x + i) * 11) >> 3) % 3) % 5;   /* streaks */
            pg_px(x + i, y, g == 0 ? pg_mix(base, C_BLACK, 40u) : g == 3 ? pg_mix(base, PC_WOOD_L, 50u) : base);
        }
    }
}

/* ---- the wordmark: FONT_L at 3/2 scale, outlined (the inside of each letter left as the panel) */
static int pg_word_on(const char *s, int32_t X, int32_t Y)   /* is pixel (X, Y) of the scaled word inside a letter? */
{
    int32_t gx = X * 2 / 3, gy = Y * 2 / 3, pen = 0;
    if (X < 0 || Y < 0) return 0;
    for (; *s; s++) {
        uint32_t gi = glyph(&FONT_L, (uint8_t)*s);
        if (gx < pen + FONT_L.adv[gi]) return pg_alpha(&FONT_L, gi, gx - pen + FONT_L.pad, gy) >= 8u;
        pen += FONT_L.adv[gi];
    }
    return 0;
}
static int32_t pg_word_w(const char *s) { return text_w(&FONT_L, s) * 3 / 2; }
static void pg_wordmark(int32_t x, int32_t y, const char *s, uint16_t c)   /* 48 px tall */
{
    int32_t w = pg_word_w(s), X, Y;
    if (!pg_hits(x - 1, y - 1, w + 2, 50)) return;
    for (Y = -1; Y < 49; Y++)
        for (X = -1; X <= w; X++) {
            if (!pg_hits(x + X, y + Y, 1, 1) || pg_word_on(s, X, Y)) continue;
            if (pg_word_on(s, X - 1, Y) || pg_word_on(s, X + 1, Y) || pg_word_on(s, X, Y - 1) || pg_word_on(s, X, Y + 1) ||
                pg_word_on(s, X - 1, Y - 1) || pg_word_on(s, X + 1, Y - 1) || pg_word_on(s, X - 1, Y + 1) ||
                pg_word_on(s, X + 1, Y + 1))
                pg_px(x + X, y + Y, c);
        }
}
