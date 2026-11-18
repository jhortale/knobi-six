/* SPDX-License-Identifier: GPL-3.0-only */
/* KNOBI-SIX sequence store (Phase 3 spec, "Storage"): João's written sequences, one log-structured, power-safe store
 * in the free FL_DATA sectors (store.h: SEQST_A_AT 0xA6000..0xAEFFF, 9 sectors, and SEQST_B_AT 0xB0000..0xBAFFF, 11
 * sectors, around the owner tag at 0xAF000). Keyed by program 0..SEQST_PROGS-1; every entry carries a generation, so
 * a program record (Task 3) names exactly the entry it was written with. Main loop only (erases hold the interrupts
 * ~50 ms a sector, or slice with store_sliced, as the program store). SMK-37 only: on the FM-1 board the store is
 * compiled out (seqst_total() 0, seqst_put refuses, seqst_get finds nothing).
 *
 * Layout: SEQST_SECTORS (20) sectors of 4 KiB, addressed by index (SEQST_SECTOR_AT: the two regions are one list; no
 * record crosses a sector, so the gap at the owner tag costs nothing). Every in-use sector starts with a 16-byte header:
 *   0 magic "KSQ1" (LE u32)   4 seq (u32, the sector's age; never reused)   8 src (u32: the seq of the sector this one
 *   is a compacted copy of; 0xFFFFFFFF none)   12 version 1   13 check (0xA5 - sum of bytes 0..12)   14 0xFF
 *   15 commit (a compacted copy: 0xFF while copying, 0x00 once complete; else unused)
 * then records, each at a 4-byte aligned offset, appended in order (the erased bytes after the last one: 0xFF):
 *   0 tag (u16: 0x5153 "SQ" an entry, 0x4C52 "RL" a release)   2 program (u16)   4 generation (u16, 1..65535)
 *   6 length (u16: the payload, 0..SEQ_PACKED_MAX; a release: 0)   8 CRC32 of bytes 0..7 and the payload
 *   12 payload: seq_pack's bytes (an empty sequence: the one byte 00, an explicit "empty" entry)
 * A record (4-byte aligned) takes 12 + length bytes: an empty entry 16, a full 64 x 6 one 848, a release 12.
 *
 * Generations: per program, 1..65535, the next = the newest seen + 1 (65535 wraps to 1; 0 is "none", a program
 * record without a sequence). Newer/older is the serial difference (int16): fine while the live generations of one
 * program are within 32767 of each other, which they always are (a put supersedes; compaction reclaims old ones long
 * before: 32767 puts of one program are > 400 KB of log). The newest generation is never lost (its entry, or the
 * release that names it, survives compaction), so a generation is not handed out twice for a program in practice.
 *
 * Liveness: seqst_release(p, keep) writes a release record; every entry of p older than keep is then dead, keep
 * and anything newer (a put whose program record never got written) stays live. Compaction copies the live entries
 * and the newest release of each program, nothing else.
 *
 * Power-safe (a cut after, or in the middle of, any erase or page program):
 *   - a put writes one record past the end of the log; the CRC decides: a cut put is absent (or complete), the others
 *     are untouched. A broken record is skipped by its length; garbage ends the sector's log (it is closed).
 *   - a new sector: erase, then its header. A sector without a valid header is free (erased before use).
 *   - compaction (when only the spare sector is free): the oldest sector T is copied, live records only, into the
 *     spare S (header with src = T's seq, commit 0xFF), then S's commit byte is programmed 0x00, then T is erased.
 *     At init: S without commit is erased (T is intact); S committed and T still there: T is erased (S is complete).
 * Wear: the sectors are used round robin and compaction always takes the oldest: every sector is erased once per
 * pass over the store, never once per save.
 *
 * RAM: a directory of 8 B a program (newest gen, released gen, where their entries are: 4000 B) and 12 B a sector;
 * the record buffer (848 B) is in the pool on the device. */
#ifndef KNOBI_SEQ_STORE_H
#define KNOBI_SEQ_STORE_H
#include <stdint.h>
#include "../store.h"
#include "../synth/seq.h"

#define SEQST_PROGS 500u                                 /* programs 0..499 (store_prog.h PROG_COUNT) */
#define SEQST_SECT_HDR 16u
#define SEQST_REC_HDR 12u
#define SEQST_ROOM (0x1000u - SEQST_SECT_HDR)            /* 4080 B of records a sector */
#define SEQST_REC_MAX ((SEQST_REC_HDR + SEQ_PACKED_MAX + 3u) & ~3u)   /* 848 */
#define SEQST_RESERVE 1024u                              /* kept for empty entries and releases: a full store can
                                                          * still be cleared (a non-empty put is refused first) */

int seqst_init(void);                                    /* scan, finish a cut compaction, rebuild the directory; 0 */
int seqst_get(uint16_t prog, uint16_t gen, seq_t *out);  /* 1 found (an empty entry too: out->len 0), 0 none */
int seqst_put(uint16_t prog, const seq_t *s, uint16_t *gen_out);   /* 0 written, *gen_out its generation; -1 full
                                                          * (or the flash refused): nothing written */
void seqst_release(uint16_t prog, uint16_t keep_gen);    /* entries of prog older than keep_gen reclaimable; call it
                                                          * after the program record naming keep_gen is committed */
uint32_t seqst_used(void);                               /* bytes of live records */
uint32_t seqst_total(void);                              /* bytes of records the store holds (the spare excluded) */

#endif
