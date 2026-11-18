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
 *   is a compacted copy of; 0xFFFFFFFF none; 0xFFFFFFFE a format: every sector older than this one is gone)   12 version 1   13 check (0xA5 - sum of bytes 0..12)   14 0xFF
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
 * A cut put whose header made it (CRC failing) spends its generation too: should its weak bytes ever read whole, that
 * generation still names only that sequence. After seqst_format generations start again at 1 (the program records
 * were cleared with it: see below).
 *
 * Liveness: seqst_release(p, keep) writes a release record; every entry of p older than keep is then dead, keep
 * and anything newer (a put whose program record never got written) stays live. seqst_drop(p) is a release past p's
 * newest generation: all of p dead (a program written with no sequence generation, e.g. RESTORE FACTORY). seqst_get
 * serves live entries only. Compaction copies the live entries and the newest release of each program, nothing else.
 *
 * Power-safe (a cut after, or in the middle of, any erase or page program):
 *   - a put writes one record past the end of the log; the CRC decides: a cut put is absent (or complete), the others
 *     are untouched. A broken record is skipped by its length; garbage ends the sector's log (it is closed).
 *   - a new sector: erase, then its header. A sector without a valid header is free (erased before use).
 *   - compaction (when only the spare sector is free): a sector T is copied, live records only, into the spare S
 *     (header with src = T's seq, commit 0xFF), then S's commit byte is programmed 0x00, then T is erased. At init:
 *     S without commit is erased (T is intact); S committed and T still there: T is erased (S is complete).
 *   - format: a fresh sector F with src 0xFFFFFFFE (the commit point), then every other sector's magic programmed to
 *     0. Cut before F's header: nothing changed; after: init kills whatever is older than F. All or nothing.
 *   - weak cells: every record, copy, sector header and commit byte is read back after programming. A put or release
 *     that does not read back closes the head and is written once more in a new place (same bytes); a copy that does
 *     not read back aborts the compaction before its commit. A free sector is erased before use even if it reads
 *     blank (a cut erase can leave cells reading 1 that are not): after a boot the first sector opened costs an erase.
 * Which sector to compact: the one with the fewest live bytes (the oldest of equals; the head only if nothing else
 * makes the room), so near full one erase frees the room (measured at 86 % full: 0.35 erases a WRITE, worst 2). Wear:
 * a sector SEQST_AGE_MAX (40) sector-opens old is compacted first, live or not, so static data moves on and every
 * sector takes its share; a put then compacts at most once more (worst 2 erases; the first compaction after a boot
 * may add the spare's erase). Never one erase per save.
 *
 * Init: seqst_init (or seqst_format) must run once at boot (main loop) before anything else; before it seqst_get
 * finds nothing and seqst_put / release / drop do nothing, never touching the flash.
 * The owner tag (platform.c smk_owner_check): when the store is not ours, the boot erases these 20 sectors with the
 * settings, the blobs and the slots' heads (the program records that name the entries), all before it writes the tag,
 * so a power cut in the middle clears them again at the next boot; seqst_init then finds them free. (seqst_format is
 * the in-app "clear every sequence"; nothing calls it at boot.)
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

int seqst_init(void);                                    /* at boot: scan, finish a cut compaction / format, rebuild
                                                          * the directory; 0 */
int seqst_get(uint16_t prog, uint16_t gen, seq_t *out);  /* 1 found (an empty entry too: out->len 0), 0 none */
int seqst_put(uint16_t prog, const seq_t *s, uint16_t *gen_out);   /* 0 written, *gen_out its generation; -1 full
                                                          * (or not ready / a bad argument): nothing written, no flash
                                                          * touched; -2 the flash failed (refused, or a record that
                                                          * twice did not read back): nothing that counts written */
void seqst_release(uint16_t prog, uint16_t keep_gen);    /* entries of prog older than keep_gen reclaimable; call it
                                                          * after the program record naming keep_gen is committed */
void seqst_drop(uint16_t prog);                          /* every generation of prog reclaimable (its record names
                                                          * none: RESTORE FACTORY written) */
int seqst_format(void);                                  /* empty the store, all or nothing (0; -1 the flash
                                                          * failed: nothing changed, or an old sector would neither
                                                          * program nor erase); also does seqst_init's work */
uint32_t seqst_used(void);                               /* bytes of live records */
uint32_t seqst_total(void);                              /* bytes of records the store holds (the spare excluded) */

#endif
