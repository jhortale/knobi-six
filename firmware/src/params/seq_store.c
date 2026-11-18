/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX sequence store (seq_store.h: the layout, the generations, the power-cut rules). Included by app.c after
 * platform.c (its flash: st_read, fl_erase4k / fl_sliced, fl_write / fl_write_sliced, store_sliced, flash_ok) and
 * synth/seq.c; on the host (DINGHY_HOST: tests/unit/test_seq_store.c, tests/host.c) the includer supplies
 * seqst_fl_read / seqst_fl_erase / seqst_fl_prog. Main loop only. Integers only, no allocation, no recursion.
 *
 * In RAM: per program the newest generation seen (seqst_top), the newest released one (seqst_rel) and where the
 * entries with those two generations are (seqst_loc_top, seqst_loc_rel: sector << 10 | offset / 4; 0xFFFF not
 * known: seqst_get then walks the flash); per sector its state, seq, src, where its log ends (fill; 0x1000: closed)
 * and the bytes of live records in it. Any flash error in a put, a release or a compaction rebuilds all of it from
 * the flash (seqst_init), which is what a power cut at that moment would have left. */
#ifndef KNOBI_SEQ_STORE_C
#define KNOBI_SEQ_STORE_C
#ifdef DINGHY_HOST
#include <string.h>
#endif                                  /* (the device: memset / memcpy from libc.c, through platform.c) */
#include "seq_store.h"

#if BOARD_SMK37

#ifndef DINGHY_HOST                     /* the device: platform.c's flash, as the program store's saves use it */
static int seqst_fl_read(uint32_t off, void *p, uint32_t n) { return flash_ok ? st_read(off, p, n) : -1; }
static int seqst_fl_erase(uint32_t off)
{
    uint32_t took;
    if (!flash_ok) return -1;
    fm1_wdt_feed();                     /* (~50 ms an erase) */
    return store_sliced ? fl_sliced(FLS_ERASE, off, 0, 4096u) : fl_erase4k(off, &took);
}
static int seqst_fl_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!flash_ok) return -1;
    return (store_sliced ? fl_write_sliced : fl_write)(off, (const uint8_t *)p, n);
}
#endif

#define SEQST_MAGIC 0x3151534Bu         /* "KSQ1" */
#define SEQST_VERSION 1u
#define SEQST_NONE 0xFFFFFFFFu          /* src: not a compacted copy */
#define SEQST_FORMAT 0xFFFFFFFEu        /* src: a format's first sector: every older sector is gone */
#define SEQST_AGE_MAX (2u * SEQST_SECTORS)   /* a sector this many sector-opens old is compacted next, live or not */
#define SEQST_TAG_SEQ 0x5153u           /* "SQ": an entry */
#define SEQST_TAG_REL 0x4C52u           /* "RL": a release */
#define SEQST_NOLOC 0xFFFFu
enum { SEQST_FREE = 0, SEQST_USED = 1 };   /* (a free sector: no valid header; blank or to be erased) */

static uint16_t seqst_top[SEQST_PROGS], seqst_rel[SEQST_PROGS], seqst_loc_top[SEQST_PROGS], seqst_loc_rel[SEQST_PROGS];
static uint32_t seqst_seq[SEQST_SECTORS], seqst_src[SEQST_SECTORS];
static uint16_t seqst_fill[SEQST_SECTORS], seqst_live[SEQST_SECTORS];
static uint8_t seqst_state[SEQST_SECTORS], seqst_blank[SEQST_SECTORS];
static int32_t seqst_head = -1;         /* the sector puts append to (the newest), -1 none */
static uint32_t seqst_maxseq;           /* the highest seq or src seen: the next sector gets one more */
static uint8_t seqst_ready;
#ifdef DINGHY_HOST
static uint8_t seqst_buf[SEQST_REC_MAX];
#else
static uint8_t seqst_buf[SEQST_REC_MAX] __attribute__((section(".pool")));   /* (the pool: not in .bss) */
#endif

static uint32_t seqst_get16(const uint8_t *a) { return (uint32_t)a[0] | (uint32_t)a[1] << 8; }
static uint32_t seqst_get32(const uint8_t *a) { return seqst_get16(a) | seqst_get16(a + 2) << 16; }
static void seqst_put16(uint8_t *a, uint32_t v) { a[0] = (uint8_t)v, a[1] = (uint8_t)(v >> 8); }
static void seqst_put32(uint8_t *a, uint32_t v) { seqst_put16(a, v), seqst_put16(a + 2, v >> 16); }
static uint32_t seqst_crc(uint32_t c, const uint8_t *p, uint32_t n)   /* CRC32 (platform.c's), c: ~0 to start */
{
    uint32_t k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++) c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return c;
}
static int seqst_newer(uint32_t a, uint32_t b) { return b == 0 || (a && (int16_t)(uint16_t)(a - b) > 0); }   /* gen a
                                                 * newer than b (0: none, older than all) */
static int seqst_live_entry(uint32_t p, uint32_t g) { return !seqst_rel[p] || !seqst_newer(seqst_rel[p], g); }
static uint32_t seqst_size(uint32_t len) { return (SEQST_REC_HDR + len + 3u) & ~3u; }
static uint16_t seqst_loc(uint32_t i, uint32_t off) { return (uint16_t)(i << 10 | off >> 2); }

uint32_t seqst_total(void) { return (SEQST_SECTORS - 1u) * SEQST_ROOM; }
uint32_t seqst_used(void)
{
    uint32_t i, u = 0;
    for (i = 0; i < SEQST_SECTORS; i++) if (seqst_state[i] == SEQST_USED) u += seqst_live[i];
    return u;
}
static uint32_t seqst_nfree(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < SEQST_SECTORS; i++) n += seqst_state[i] == SEQST_FREE;
    return n;
}

/* the record at off of sector i into seqst_buf (header + payload when check): 1 valid, 2 broken (a plausible header,
 * the CRC fails: skip *size), 0 the end of the log (erased), -1 garbage (the log of this sector ends here). With
 * check 0 only the header is read and judged (plausible: 1), for walks that skip what they do not need. */
static int seqst_rec(uint32_t i, uint32_t off, int check, uint32_t *size)
{
    uint8_t *h = seqst_buf;
    uint32_t tag, prog, gen, len, k;
    if (off + SEQST_REC_HDR > 0x1000u) return 0;
    if (seqst_fl_read(SEQST_SECTOR_AT(i) + off, h, SEQST_REC_HDR)) return -1;
    tag = seqst_get16(h), prog = seqst_get16(h + 2), gen = seqst_get16(h + 4), len = seqst_get16(h + 6);
    if (tag == 0xFFFFu) {
        for (k = 2; k < SEQST_REC_HDR && h[k] == 0xFFu; k++) {}
        return k == SEQST_REC_HDR ? 0 : -1;
    }
    if ((tag != SEQST_TAG_SEQ && tag != SEQST_TAG_REL) || prog >= SEQST_PROGS || !gen ||
        (tag == SEQST_TAG_SEQ ? len < 1u || len > SEQ_PACKED_MAX : len != 0) || off + SEQST_REC_HDR + len > 0x1000u)
        return -1;
    *size = seqst_size(len);
    if (!check) return 1;
    if (len && seqst_fl_read(SEQST_SECTOR_AT(i) + off + SEQST_REC_HDR, h + SEQST_REC_HDR, len)) return -1;
    k = seqst_crc(~0u, h, 8u);
    k = ~seqst_crc(k, h + SEQST_REC_HDR, len);
    return k == seqst_get32(h + 8) ? 1 : 2;
}
static uint32_t seqst_tag(void) { return seqst_get16(seqst_buf); }
static uint32_t seqst_prog(void) { return seqst_get16(seqst_buf + 2); }
static uint32_t seqst_gen(void) { return seqst_get16(seqst_buf + 4); }
static int seqst_rec_live(void)           /* the record in seqst_buf is live: copied by a compaction, counted used */
{
    uint32_t p = seqst_prog(), g = seqst_gen();
    return seqst_tag() == SEQST_TAG_SEQ ? seqst_live_entry(p, g) : g == seqst_rel[p];
}

static int seqst_blank_at(uint32_t at, uint32_t n)   /* [at, at + n) all 0xFF */
{
    uint8_t b[64];
    uint32_t k, j;
    while (n) {
        k = n > sizeof b ? sizeof b : n;
        if (seqst_fl_read(at, b, k)) return 0;
        for (j = 0; j < k; j++) if (b[j] != 0xFFu) return 0;
        at += k, n -= k;
    }
    return 1;
}
static int seqst_erase(uint32_t i)
{
    seqst_state[i] = SEQST_FREE, seqst_blank[i] = 0, seqst_live[i] = 0;
    if (seqst_fl_erase(SEQST_SECTOR_AT(i))) return -1;
    seqst_blank[i] = 1;
    return 0;
}

/* program and read back: 0 the same; -1 a weak cell reads otherwise; -2 the flash refused */
static int seqst_prog_ok(uint32_t addr, const uint8_t *p, uint32_t n)
{
    uint8_t b[64];
    uint32_t o, k, j;
    if (seqst_fl_prog(addr, p, n)) return -2;
    for (o = 0; o < n; o += k) {
        k = n - o > sizeof b ? (uint32_t)sizeof b : n - o;
        if (seqst_fl_read(addr + o, b, k)) return -2;
        for (j = 0; j < k; j++) if (b[j] != p[o + j]) return -1;
    }
    return 0;
}

/* a sector's header made invalid (its magic programmed to 0: one page program, no erase): free, erased before use.
 * Read back: a magic that still reads whole (a weak or refused program) is erased instead. -1: neither took */
static int seqst_kill(uint32_t i)
{
    static const uint8_t z[4] = {0, 0, 0, 0};
    uint8_t m[4];
    int r = 0;
    (void)seqst_fl_prog(SEQST_SECTOR_AT(i), z, sizeof z);
    if (seqst_fl_read(SEQST_SECTOR_AT(i), m, sizeof m) || seqst_get32(m) == SEQST_MAGIC)
        r = seqst_fl_erase(SEQST_SECTOR_AT(i)) ? -1 : 0;
    seqst_state[i] = SEQST_FREE, seqst_blank[i] = 0, seqst_live[i] = 0, seqst_fill[i] = 0x1000u;
    return r;
}

/* walk sector i's log: pass 1 (the generations), pass 2 (live bytes, where the entries are, where the log ends) */
static void seqst_walk(uint32_t i, int pass)
{
    uint32_t off = SEQST_SECT_HDR, size = 0, p, g;
    int r;
    for (;;) {
        r = seqst_rec(i, off, 1, &size);
        if (r <= 0) break;
        if (r == 2 && pass == 1 && seqst_tag() == SEQST_TAG_SEQ && seqst_newer(seqst_gen(), seqst_top[seqst_prog()]))
            seqst_top[seqst_prog()] = (uint16_t)seqst_gen();   /* a cut put's generation is spent: if its weak bytes
                                                 * ever read whole, it still names only that sequence */
        if (r == 1) {
            p = seqst_prog(), g = seqst_gen();
            if (pass == 1) {
                if (seqst_newer(g, seqst_top[p])) seqst_top[p] = (uint16_t)g;
                if (seqst_tag() == SEQST_TAG_REL && seqst_newer(g, seqst_rel[p])) seqst_rel[p] = (uint16_t)g;
            } else if (seqst_rec_live()) {
                seqst_live[i] = (uint16_t)(seqst_live[i] + size);
                if (seqst_tag() == SEQST_TAG_SEQ) {
                    if (g == seqst_top[p]) seqst_loc_top[p] = seqst_loc(i, off);
                    if (g == seqst_rel[p]) seqst_loc_rel[p] = seqst_loc(i, off);
                }
            }
        }
        off += size;
    }
    if (pass == 2)                      /* appendable only if all after the end is erased */
        seqst_fill[i] = (uint16_t)(r == 0 && seqst_blank_at(SEQST_SECTOR_AT(i) + off, 0x1000u - off) ? off : 0x1000u);
}

int seqst_init(void)
{
    uint8_t h[SEQST_SECT_HDR];
    uint32_t i, j, k, sum, fseq = 0;
    seqst_ready = 1, seqst_head = -1, seqst_maxseq = 0;
    memset(seqst_top, 0, sizeof seqst_top), memset(seqst_rel, 0, sizeof seqst_rel);
    memset(seqst_loc_top, 0xFF, sizeof seqst_loc_top), memset(seqst_loc_rel, 0xFF, sizeof seqst_loc_rel);
    for (i = 0; i < SEQST_SECTORS; i++) {       /* the headers */
        seqst_state[i] = SEQST_FREE, seqst_blank[i] = 0, seqst_live[i] = 0, seqst_fill[i] = 0x1000u;
        if (seqst_fl_read(SEQST_SECTOR_AT(i), h, sizeof h)) continue;
        for (k = 0, sum = 0; k < 13u; k++) sum += h[k];
        if (seqst_get32(h) == SEQST_MAGIC && h[12] == SEQST_VERSION && h[13] == (uint8_t)(0xA5u - sum)) {
            seqst_state[i] = SEQST_USED, seqst_seq[i] = seqst_get32(h + 4), seqst_src[i] = seqst_get32(h + 8);
            if (seqst_src[i] == SEQST_FORMAT && seqst_seq[i] > fseq) fseq = seqst_seq[i];
            if (seqst_src[i] < SEQST_FORMAT) {
                if (h[15] == 0xFFu) { (void)seqst_erase(i); continue; }   /* a compaction cut while copying: T
                                                 * still holds it all */
                if (h[15] != 0u) { uint8_t z = 0; (void)seqst_fl_prog(SEQST_SECTOR_AT(i) + 15u, &z, 1u); }
            }
            if (seqst_seq[i] > seqst_maxseq) seqst_maxseq = seqst_seq[i];
            if (seqst_src[i] < SEQST_FORMAT && seqst_src[i] > seqst_maxseq) seqst_maxseq = seqst_src[i];
        }                               /* (a free sector is erased before use even if it reads blank: a cut erase
                                         * can leave cells that read 1 and are not) */
    }
    for (i = 0; i < SEQST_SECTORS; i++)         /* a format: everything older than its sector is gone for good */
        if (seqst_state[i] == SEQST_USED && seqst_seq[i] < fseq) (void)seqst_kill(i);   /* (one that will not die
                                                 * is killed again at every init while the format sector lasts) */
    for (i = 0; i < SEQST_SECTORS; i++)         /* a compaction cut before its source was erased: erase it now */
        if (seqst_state[i] == SEQST_USED && seqst_src[i] < SEQST_FORMAT)
            for (j = 0; j < SEQST_SECTORS; j++)
                if (j != i && seqst_state[j] == SEQST_USED && seqst_seq[j] == seqst_src[i]) (void)seqst_erase(j);
    for (i = 0; i < SEQST_SECTORS; i++) if (seqst_state[i] == SEQST_USED) seqst_walk(i, 1);
    for (i = 0; i < SEQST_SECTORS; i++)
        if (seqst_state[i] == SEQST_USED) {
            seqst_walk(i, 2);
            if (seqst_head < 0 || seqst_seq[i] > seqst_seq[seqst_head]) seqst_head = (int32_t)i;
        }
    return 0;
}

/* open sector i (free) as the newest: erase it unless known blank, then its header (src: a compaction's source) */
static int seqst_open(uint32_t i, uint32_t src)
{
    uint8_t h[SEQST_SECT_HDR];
    uint32_t k, sum;
    if (!seqst_blank[i] && seqst_erase(i)) return -1;
    seqst_blank[i] = 0;
    seqst_maxseq++;
    seqst_put32(h, SEQST_MAGIC), seqst_put32(h + 4, seqst_maxseq), seqst_put32(h + 8, src);
    h[12] = SEQST_VERSION;
    for (k = 0, sum = 0; k < 13u; k++) sum += h[k];
    h[13] = (uint8_t)(0xA5u - sum), h[14] = 0xFFu, h[15] = 0xFFu;
    if (seqst_prog_ok(SEQST_SECTOR_AT(i), h, sizeof h)) return -1;
    seqst_state[i] = SEQST_USED, seqst_seq[i] = seqst_maxseq, seqst_src[i] = src;
    seqst_fill[i] = SEQST_SECT_HDR, seqst_live[i] = 0;
    seqst_head = (int32_t)i;
    return 0;
}
static int32_t seqst_free_after(uint32_t from)   /* a free sector, round robin from `from` (blank ones first) */
{
    uint32_t k, i;
    int32_t any = -1;
    for (k = 0; k < SEQST_SECTORS; k++) {
        i = (from + k) % SEQST_SECTORS;
        if (seqst_state[i] != SEQST_FREE) continue;
        if (seqst_blank[i]) return (int32_t)i;
        if (any < 0) any = (int32_t)i;
    }
    return any;
}
static int32_t seqst_oldest(void)
{
    uint32_t i;
    int32_t o = -1;
    for (i = 0; i < SEQST_SECTORS; i++)
        if (seqst_state[i] == SEQST_USED && (o < 0 || seqst_seq[i] < seqst_seq[o])) o = (int32_t)i;
    return o;
}
/* what to compact for size bytes: the sector with the fewest live bytes (the oldest of equals), the head only when
 * no other one makes the room; -1 none does */
static int32_t seqst_victim(uint32_t size)
{
    uint32_t i;
    int32_t v = -1;
    for (i = 0; i < SEQST_SECTORS; i++)
        if (seqst_state[i] == SEQST_USED && (int32_t)i != seqst_head &&
            (v < 0 || seqst_live[i] < seqst_live[v] || (seqst_live[i] == seqst_live[v] && seqst_seq[i] < seqst_seq[v])))
            v = (int32_t)i;
    if (v >= 0 && SEQST_ROOM - seqst_live[v] >= size) return v;
    return seqst_head >= 0 && SEQST_ROOM - seqst_live[seqst_head] >= size ? seqst_head : -1;
}

/* compaction of sector t: its live records into the spare, commit, erase t. The spare is the head now */
static int seqst_compact(int32_t t)
{
    int32_t s = t < 0 ? -1 : seqst_free_after((uint32_t)t + 1u);
    uint32_t off = SEQST_SECT_HDR, size = 0, at, p, g;
    uint8_t z = 0;
    int r;
    if (t < 0 || s < 0 || seqst_open((uint32_t)s, seqst_seq[t])) return -1;
    for (;;) {
        r = seqst_rec((uint32_t)t, off, 1, &size);
        if (r <= 0) break;
        if (r == 1 && seqst_rec_live()) {
            at = seqst_fill[s];
            if (seqst_prog_ok(SEQST_SECTOR_AT(s) + at, seqst_buf, SEQST_REC_HDR + seqst_get16(seqst_buf + 6))) return -1;   /* (a
                                                 * copy that does not read back: no commit, init drops S, T stays) */
            p = seqst_prog(), g = seqst_gen();
            if (seqst_tag() == SEQST_TAG_SEQ) {
                if (g == seqst_top[p]) seqst_loc_top[p] = seqst_loc((uint32_t)s, at);
                if (g == seqst_rel[p]) seqst_loc_rel[p] = seqst_loc((uint32_t)s, at);
            }
            seqst_fill[s] = (uint16_t)(at + size), seqst_live[s] = (uint16_t)(seqst_live[s] + size);
        }
        off += size;
    }
    if (seqst_prog_ok(SEQST_SECTOR_AT(s) + 15u, &z, 1u)) return -1;   /* complete: from here S is the copy */
    return seqst_erase((uint32_t)t);
}

/* room for size bytes at the head: a new sector while two are free, else a compaction of the sector with the
 * fewest live bytes (greedy: near full one erase makes the room, not one sector after another). Wear: a sector
 * SEQST_AGE_MAX sector-opens old is compacted first (static data moves on, so every sector takes its share of
 * erases); then at most one greedy compaction. 0 done; -1 full (no flash touched); -2 the flash failed (RAM no
 * longer matches it: seqst_init) */
static int seqst_room(uint32_t size)
{
    int32_t t;
    if (seqst_head >= 0 && 0x1000u - seqst_fill[seqst_head] >= size) return 0;
    if (seqst_nfree() >= 2u) return seqst_open((uint32_t)seqst_free_after((uint32_t)(seqst_head + 1)), SEQST_NONE) ? -2 : 0;
    if (seqst_nfree() == 0u || seqst_victim(size) < 0) return -1;   /* no compaction makes the room: full */
    t = seqst_oldest();
    if (t >= 0 && t != seqst_head && seqst_maxseq - seqst_seq[t] > SEQST_AGE_MAX) {
        if (seqst_compact(t)) return -2;
        if (0x1000u - seqst_fill[seqst_head] >= size) return 0;
    }
    if (seqst_compact(seqst_victim(size))) return -2;
    return 0x1000u - seqst_fill[seqst_head] >= size ? 0 : -2;
}

/* program the record in seqst_buf (n bytes) at the head's end and read it back: 0 there; 1 it did not read back
 * (a weak cell: the head is closed, the caller tries once more elsewhere); -2 the flash failed */
static int seqst_append(uint32_t n, uint32_t *at_out)
{
    uint32_t at = seqst_fill[seqst_head];
    int r = seqst_prog_ok(SEQST_SECTOR_AT(seqst_head) + at, seqst_buf, n);
    if (r) {
        if (r == -2) return -2;
        seqst_fill[seqst_head] = 0x1000u;       /* (the weak bytes stay where they are; never appended over) */
        return 1;
    }
    seqst_fill[seqst_head] = (uint16_t)(at + seqst_size(n - SEQST_REC_HDR));
    seqst_live[seqst_head] = (uint16_t)(seqst_live[seqst_head] + seqst_size(n - SEQST_REC_HDR));
    *at_out = at;
    return 0;
}

int seqst_put(uint16_t prog, const seq_t *s, uint16_t *gen_out)
{
    uint32_t len, size, at = 0, c, tries;
    uint16_t g;
    int r = 1;
    if (!seqst_ready || prog >= SEQST_PROGS || !s || (len = seq_packed_size(s)) < 1u || len > SEQ_PACKED_MAX) return -1;
    size = seqst_size(len);
    if (seqst_used() + size > seqst_total() - (s->len ? SEQST_RESERVE : 0u)) return -1;   /* (the reserve: empties) */
    g = (uint16_t)(seqst_top[prog] + 1u);
    if (!g) g = 1u;
    for (tries = 0; tries < 2u && r == 1; tries++) {   /* (a record that did not read back: once more, elsewhere,
                                                 * the same bytes: should the weak one ever read whole it says the same) */
        if ((r = seqst_room(size)) != 0) break;
        if (seq_pack(s, seqst_buf + SEQST_REC_HDR, SEQ_PACKED_MAX) != (int)len) return -2;   /* (an invalid
                                                 * sequence: not "full") */
        seqst_put16(seqst_buf, SEQST_TAG_SEQ), seqst_put16(seqst_buf + 2, prog), seqst_put16(seqst_buf + 4, g);
        seqst_put16(seqst_buf + 6, len);
        c = ~seqst_crc(seqst_crc(~0u, seqst_buf, 8u), seqst_buf + SEQST_REC_HDR, len);
        seqst_put32(seqst_buf + 8, c);
        r = seqst_append(SEQST_REC_HDR + len, &at);
    }
    if (r == -1) return -1;                     /* full: no flash touched */
    if (r != 0) { (void)seqst_init(); return -2; }   /* the flash failed (or two weak writes): nothing that counts
                                                 * written, RAM rebuilt from the flash */
    seqst_top[prog] = g, seqst_loc_top[prog] = seqst_loc((uint32_t)seqst_head, at);
    if (gen_out) *gen_out = g;
    return 0;
}

int seqst_get(uint16_t prog, uint16_t gen, seq_t *out)
{
    uint32_t i, off, size = 0;
    uint16_t loc;
    int r;
    if (!seqst_ready || prog >= SEQST_PROGS || !gen || !out || !seqst_live_entry(prog, gen)) return 0;   /* (a released
                                                 * generation is gone, whether reclaimed yet or not) */
    loc = gen == seqst_top[prog] ? seqst_loc_top[prog] : gen == seqst_rel[prog] ? seqst_loc_rel[prog] : SEQST_NOLOC;
    if (loc != SEQST_NOLOC) {
        r = seqst_rec(loc >> 10, (uint32_t)(loc & 0x3FFu) << 2, 1, &size);
        if (r == 1 && seqst_tag() == SEQST_TAG_SEQ && seqst_prog() == prog && seqst_gen() == gen)
            return seq_unpack(out, seqst_buf + SEQST_REC_HDR, seqst_get16(seqst_buf + 6)) == 0;
    }
    for (i = 0; i < SEQST_SECTORS; i++) {       /* not where the directory says (an older one): walk the flash */
        if (seqst_state[i] != SEQST_USED) continue;
        for (off = SEQST_SECT_HDR; (r = seqst_rec(i, off, 0, &size)) > 0; off += size) {
            if (seqst_tag() != SEQST_TAG_SEQ || seqst_prog() != prog || seqst_gen() != gen) continue;
            if (seqst_rec(i, off, 1, &size) == 1 && seq_unpack(out, seqst_buf + SEQST_REC_HDR, seqst_get16(seqst_buf + 6)) == 0)
                return 1;
        }
    }
    return 0;
}

void seqst_release(uint16_t prog, uint16_t keep_gen)
{
    uint32_t i, off, size = 0, c, old, was, at, tries;
    int r = 1;
    if (!seqst_ready || prog >= SEQST_PROGS || !keep_gen || !seqst_newer(keep_gen, seqst_rel[prog])) return;   /* (same or older:
                                                 * nothing to do) */
    old = seqst_rel[prog], seqst_loc_rel[prog] = SEQST_NOLOC;
    for (i = 0; i < SEQST_SECTORS; i++) {       /* what dies: prog's entries older than keep, its older release */
        if (seqst_state[i] != SEQST_USED) continue;
        for (off = SEQST_SECT_HDR; (r = seqst_rec(i, off, 0, &size)) > 0; off += size) {
            if (seqst_prog() != prog || seqst_rec(i, off, 1, &size) != 1) continue;
            seqst_rel[prog] = (uint16_t)old, was = seqst_rec_live();
            seqst_rel[prog] = keep_gen;
            if (was && !seqst_rec_live()) seqst_live[i] = (uint16_t)(seqst_live[i] - size);
            if (seqst_tag() == SEQST_TAG_SEQ && seqst_gen() == keep_gen) seqst_loc_rel[prog] = seqst_loc(i, off);
        }
    }
    seqst_rel[prog] = keep_gen;
    if (seqst_newer(keep_gen, seqst_top[prog])) seqst_top[prog] = keep_gen, seqst_loc_top[prog] = SEQST_NOLOC;
    for (tries = 0, r = 1; tries < 2u && r == 1; tries++) {   /* (not written: the older ones stay live after a power
                                                 * cycle, which only costs room) */
        if ((r = seqst_room(SEQST_REC_HDR)) != 0) break;
        seqst_put16(seqst_buf, SEQST_TAG_REL), seqst_put16(seqst_buf + 2, prog), seqst_put16(seqst_buf + 4, keep_gen);
        seqst_put16(seqst_buf + 6, 0);
        c = ~seqst_crc(~0u, seqst_buf, 8u);
        seqst_put32(seqst_buf + 8, c);
        r = seqst_append(SEQST_REC_HDR, &at);
    }
    if (r < -1) (void)seqst_init();
}

void seqst_drop(uint16_t prog)
{
    uint16_t g;
    if (!seqst_ready || prog >= SEQST_PROGS || !seqst_top[prog]) return;
    if (seqst_top[prog] == seqst_rel[prog] && seqst_loc_rel[prog] == SEQST_NOLOC) return;   /* nothing live */
    g = (uint16_t)(seqst_top[prog] + 1u);
    if (!g) g = 1u;
    seqst_release(prog, g);                     /* a release past the newest: every entry older, all of them dead */
}

int seqst_format(void)
{
    int32_t f;
    uint32_t i;
    int bad = 0;
    (void)seqst_init();
    f = seqst_free_after(0);
    if (f >= 0 && seqst_open((uint32_t)f, SEQST_FORMAT)) { (void)seqst_init(); return -1; }   /* (cut before its
                                                 * header: nothing changed) */
    for (i = 0; i < SEQST_SECTORS; i++)         /* (from here the format holds: init kills what is older) */
        if (seqst_state[i] == SEQST_USED && (int32_t)i != f) bad |= seqst_kill(i);
    (void)seqst_init();
    return bad ? -1 : 0;                        /* -1: a sector would neither program nor erase (formatted all the
                                                 * same; init keeps killing it while the format sector lasts) */
}

#else /* the FM-1: no room in its flash map for it; the store is compiled out */

int seqst_init(void) { return 0; }
int seqst_get(uint16_t prog, uint16_t gen, seq_t *out) { (void)prog, (void)gen, (void)out; return 0; }
int seqst_put(uint16_t prog, const seq_t *s, uint16_t *gen_out) { (void)prog, (void)s, (void)gen_out; return -1; }
void seqst_release(uint16_t prog, uint16_t keep_gen) { (void)prog, (void)keep_gen; }
void seqst_drop(uint16_t prog) { (void)prog; }
int seqst_format(void) { return 0; }
uint32_t seqst_used(void) { return 0; }
uint32_t seqst_total(void) { return 0; }

#endif /* BOARD_SMK37 */
#endif /* KNOBI_SEQ_STORE_C */
