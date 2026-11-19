# KNOBI-SIX — Phase 3 (step sequencer) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The OB-6 step sequencer — one polyphonic 64-step sequence per program, step-entry recording, clocked playback with transpose — with factory sequences read-only in the factory bank and João's own in a power-safe packed store.

**Architecture:** A pure sequence model (`synth/seq.{c,h}`) records and plays steps on the Phase 2 clock's step events, like the arp. A log-structured store (`params/seq_store.{c,h}`) in the free part of `FL_DATA` keeps written sequences keyed by program number with a generation number; the program record (version 3) carries the generation, so a WRITE pairs program and sequence atomically (sequence first, record second). Factory sequences ride in the local factory bank header.

**Tech Stack:** C (JieLi pi32v2 clang, `-Os`, `-mfprev1`), host unit tests (`make -C tests unit`), host scripts (`tests/run_host.sh`), host flash model for power-cut tests, fm1-emu (pinned `60645a5`), Python importer (`tools/ob6_import.py`), GitHub Actions (firmware, fm1-emu, cpu1-emu).

**Spec:** `docs/superpowers/specs/2026-11-18-phase3-sequencer-design.md` (extends the two earlier specs, still binding). Reference: `docs/ob6-reference.md` §2.13 and §5 "Sequencer".

## Global Constraints

- Everything from the Phase 1 and Phase 2 plans' Global Constraints still holds (chip rules: no libgcc, no 64-bit division, audio interrupt short — no flash, no screen; identity IDENT 956; never install or flash without João; no OB-6 program data or names committed or in a published build; `firmware/gen/factory_bank.h` never committed).
- 64 steps max, 6 notes per step max, notes 12–108, velocities 0–127, rests and ties (reference §5).
- Factory sequences read-only in the factory bank; the store holds only written sequences; no entry = factory sequence; an explicit empty entry beats the factory one.
- One WRITE = program + sequence, paired by a generation: sequence entry first, program record second; a power cut leaves the old pair or the new pair, never a mix; a full store refuses the whole WRITE (`SEQ FULL`), nothing changes.
- The store lives only in free `FL_DATA` sectors (builder confirms 0xA6000–0xAEFFF and 0xB0000–0xBAFFF against `store.h`, the owner tag at 0xAF000), added to `FL_RANGE_OK` explicitly; protected ranges stay refused.
- Saves only when silent or with the WRITE fade; no flash access in the audio interrupt; the sequencer never runs on CPU1.
- Gate 50 % of the step; ties extend the gate; playback start/stop follows the arp's Phase 2 clock rules (no clock restart and no 0xFA under MIDI CLOCK OUT); arp and sequencer exclusive; sequence notes to MIDI out only with PARAM TX and LOCAL on, every note-on paired with its off on the same channel.
- Default build (`KNOBI_CPU1=0`) and the KNOBI_CPU1 build both stay green in CI; no `.github/workflows` change unless a task says so.
- Branch `phase3` from `phase1`; push to origin only that branch.

## Review Focus

1. **Power cut during WRITE** at every flash step (sequence entry, compaction, program record) — after restart the program and its sequence are a matching pair (old or new), never a mix, never a lost program. Test owner: Task 3 (`write_powercut_pairs`).
2. **Store full / near full** — a refused WRITE changes nothing (program record untouched); compaction never drops a live entry. Test owner: Task 2 (`store_full_refuses`, `compact_keeps_live`) and Task 3 (`write_full_refuses_whole`).
3. **Stuck notes** — stopping, program change, arp switched on, SEQ REC during playback, external clock stop, CC123/panic: every sequence note-on gets its note-off (locally and on MIDI out). Test owner: Task 4 (`seq_no_stuck_notes`) and a host script.
4. **External clock** — MIDI In / Slave Thru: Start from step 1, Continue from the held step, Stop pauses, no clock → silent; OUT: no restart. Test owner: Task 4 (`seq_ext_clock`).
5. **Old records and the factory fallback** — Phase 1/2 records (no generation) load with the factory sequence (or empty without the bank); an explicit empty entry suppresses the factory one; RESTORE FACTORY restores it. Test owner: Task 3 (`load_fallbacks`).

---

### Task 1: Sequence model and packed encoding (host only)

**Files:** Create `firmware/src/synth/seq.{c,h}`, `tests/unit/test_seq.c`; include `seq.c` from `app.c` (unity build).
**Interfaces — Produces:** `seq_t` (length 0..64; per step: count 0..6 (0 = rest), tie flag, notes[6], vels[6]); `void seq_clear(seq_t*)`; recording ops `int seq_rec_step(seq_t*, const uint8_t *notes, const uint8_t *vels, int n)`, `int seq_rec_rest(seq_t*)`, `int seq_rec_tie(seq_t*)`, `int seq_rec_back(seq_t*)` (return the new length, −1 when refused: full at 64, out-of-range notes dropped before counting); `int seq_pack(const seq_t*, uint8_t *buf, uint32_t max)` (bytes or −1; empty → a valid 1-byte "length 0"), `int seq_unpack(seq_t*, const uint8_t *buf, uint32_t n)` (0 / −1), `uint32_t seq_packed_size(const seq_t*)`; `#define SEQ_PACKED_MAX` (the worst case, 64 × 6 notes).
- [ ] Step 1: failing tests `seq_roundtrip` (random sequences incl. empty, 64×6 full, rests, ties → pack → unpack identical), `seq_packed_size_matches`, `seq_rec_ops` (chord step, rest, tie extends previous step, back erases, 64 limit, notes outside 12..108 dropped), `seq_unpack_rejects` (truncated, bad length, count > 6).
- [ ] Step 2–4: fail, implement (encoding: length byte, then per step a header byte = count | tie<<3, then count × (note, vel); document it in seq.h), pass.
- [ ] Step 5: commit `feat(seq): sequence model and packed encoding`.

### Task 2: Sequence store in FL_DATA (power-safe, generations)

**Files:** Create `firmware/src/params/seq_store.{c,h}`, `tests/unit/test_seq_store.c` (host flash model: sector erase/program with injectable power cut after any write); Modify `firmware/src/store.h` (`FL_RANGE_OK` adds the store's sectors; compile-time asserts it's inside FL_DATA and clear of settings/blobs/owner tag), flash-map test (`tests/smk_flash_test.c` or the existing equivalent).
**Interfaces — Consumes:** `seq_pack`/`seq_unpack` (Task 1). **Produces:** `int seqst_init(void)` (scan, rebuild the in-RAM directory; 0 ok); `int seqst_get(uint16_t prog, uint16_t gen, seq_t *out)` (1 found — incl. empty entries —, 0 none); `int seqst_put(uint16_t prog, const seq_t *s, uint16_t *gen_out)` (writes a new entry with a new generation, old entries stay readable until reclaimed; −1 = full, nothing written); `void seqst_release(uint16_t prog, uint16_t keep_gen)` (marks older generations of prog reclaimable — called after the program record is committed); `uint32_t seqst_used(void)`, `uint32_t seqst_total(void)`. Main loop only.
- [ ] Step 1: failing tests `store_put_get`, `store_empty_entry`, `store_generation_select` (two generations of one program, get by gen), `store_powercut_every_write` (cut after each flash write during put and during compaction → after `seqst_init` every previously committed entry readable, the in-flight one either complete or absent), `store_full_refuses`, `compact_keeps_live` (fill, release, compaction reclaims only released entries), `store_wear` (N puts erase each sector at most ⌈N/k⌉ times — no sector per save), `store_crc_rejects` (a corrupted entry is skipped, not trusted).
- [ ] Step 2–4: fail, implement (log-structured entries with magic/version/prog/gen/len/CRC; one spare sector for compaction; layout documented in seq_store.h), pass; flash-map test proves protected ranges still refused.
- [ ] Step 5: commit `feat(store): power-safe sequence store`.

### Task 3: Program ⇄ sequence pairing (record v3, WRITE, load, factory)

**Files:** Modify `firmware/src/params/params.{c,h}` (record version 3: a stored sequence generation, 0 = none; `PROGRAM_VERSION_TABLE` entry; still fits `PROGRAM_PACKED_MAX`/stride — assert it), `params/store_prog.{c,h}` (WRITE: `seqst_put` → program record with gen → `seqst_release`; load: record gen → `seqst_get`, else factory sequence, else empty), the edit buffer (sequence part of it: COMPARE, RESTORE FACTORY, program change), `tools/ob6_import.py`'s header format hook only as far as the factory accessor needs (`factory_seq(prog, seq_t*)`, empty when the bank has no sequences — the importer itself is Task 6); `tests/unit/test_store.c` (or a new `test_pairing.c`).
**Interfaces — Produces:** `int prog_write(uint16_t prog)` returns 0, or `PROG_ERR_SEQ_FULL` (nothing changed); `const seq_t *edit_seq(void)` / `seq_t *edit_seq_mut(void)`; `int factory_seq(uint16_t prog, seq_t *out)`.
- [ ] Step 1: failing tests `write_pairs` (write, reload: same program and sequence), `write_powercut_pairs` (cut at every flash write of a WRITE → reload gives old program + old sequence or new + new), `write_full_refuses_whole` (store full → record unchanged byte for byte), `load_fallbacks` (v1/v2 records → factory sequence; no bank → empty; explicit empty entry beats factory; RESTORE FACTORY restores the factory sequence into the edit buffer), `edit_lost_without_write` (program change drops the edit-buffer sequence), `record_v3_compat` (v2 record loads with gen 0; v3 packs within the stride).
- [ ] Step 2–4: fail, implement, pass (all existing store tests unchanged).
- [ ] Step 5: commit `feat(store): sequence paired with the program on WRITE`.

### Task 4: Playback on the clock

**Files:** Modify `firmware/src/synth/seq.{c,h}` (player), `firmware/src/engine.c` (seq fed by clock step events like the arp; sub-block split at steps and gate ends shared with `eng_render_arp`; arp/seq exclusive; program change, CC123/panic → silence), `firmware/src/ui/midi_map.c` (sequence notes out with the arp's rules and pairing; extend `ctl_arp_out` or share its ring), `tests/unit/test_seq.c`, `tests/scripts/seq.txt`.
**Interfaces — Consumes:** Phase 2 clock API (`clock_tick` step events `.n/.at[]/.step[]`, `clock_running`, `clock_restart`), arp start rules (`eng_arp_start` logic: restart only with BEAT SYNC off and MIDI CLOCK not OUT). **Produces:** `void seq_play(seqp_t*, int on)`, `void seq_step(seqp_t*, synth_t*)`, `void seq_run(seqp_t*, synth_t*, uint32_t frames)` (gate end), `void seq_silence(seqp_t*, synth_t*)`, `void seq_transpose(seqp_t*, int semis)`, `void seq_ext(seqp_t*, uint8_t status)` (FA from step 1, FB continue, FC pause).
- [ ] Step 1: failing tests `seq_play_timing` (steps on clock steps, sample-exact like the arp; 50 % gate; ties hold across steps; rests silent; loops the length), `seq_start_rules` (BEAT SYNC off/on, OUT: no restart / no 0xFA), `seq_ext_clock` (In/Thru: armed until clock; Start/Continue/Stop; no clock → silent), `seq_transpose` (hold SEQ REC + key; middle C = 0; Unison on → no hold), `seq_arp_exclusive`, `seq_no_stuck_notes` (stop, program change, arp on, CC123, external stop, voice shed), `seq_midi_out_paired` (PARAM TX + LOCAL rules, channel kept).
- [ ] Step 2–4: fail, implement, pass; host script `seq.txt` (play, transpose, external clock, MIDI out log) passes.
- [ ] Step 5: commit `feat(seq): playback on the clock`.

### Task 5: Recording, buttons and the Sequencer screen

**Files:** Modify `firmware/src/ui/controls.{c,h}` (BTN_SEQ → Sequencer screen; BTN_PLAY = SEQ PLAY; BTN_REC = SEQ REC; INC/DEC while recording — pick and document, e.g. `<` / `>`; keys and MIDI-in notes to the recorder with the octave shift baked in, unison/chord expansion not recorded), `firmware/src/ui/screens.c` (Sequencer screen: 64-step grid, current step, notes per step, length, PLAY/REC/ARMED, transpose, `SEQ n KB / m KB`; Play header `SEQ` / `REC`; WRITE shows `SEQ FULL` with the hint), `firmware/src/app.c` (WRITE path result), `docs/CONTROLS.md`, `docs/MIDI.md`, host scripts (`tests/scripts/seq_rec.txt`, screen snapshots per the suite's convention).
**Interfaces — Consumes:** Tasks 1, 3, 4.
- [ ] Step 1: failing host scripts / unit tests `rec_keys_and_midi` (chord with one key held adds notes; MIDI-in notes record), `rec_octave_baked`, `rec_inc_dec` (rest, tie by repeated INC while holding, back), `rec_stop_at_64`, `rec_then_write_then_reload` (record → WRITE → power-cycle in the host → same sequence plays), `write_full_message`.
- [ ] Step 2–4: fail, implement, pass; screens match the existing style (docs/design/standalone-ui/).
- [ ] Step 5: commit `feat(ui): sequencer recording and screen`.

### Task 6: Factory sequences in the importer (local only)

**Files:** Modify `tools/ob6_import.py` (decode offset 128: 6 lanes × 64 notes + 64 velocities; decode rests/ties — values ≥ 128 appear: determine the encoding against João's local file and document it in docs/FACTORY_BANK.md without copying data; emit packed sequences in `factory_bank.h` behind the accessor Task 3 defined), the importer's tests (synthetic dumps with known sequences — no Sequential data), `docs/FACTORY_BANK.md`, README import section (one line: sequences now included).
- [ ] Step 1: failing tests: synthetic dump → expected packed sequence (notes, chords, rests, ties, lengths 2..64); empty sequence → empty; the report counts sequences carried.
- [ ] Step 2–4: fail, implement, pass; run the importer locally on `~/Downloads/OB-6-Factory-Programs/OB6_Programs_v1.01.syx` into the git-ignored header and report only counts (programs with sequences, total bytes) — never commit the output.
- [ ] Step 5: commit `feat(import): factory sequences`.

### Task 7: Phase 3 wrap-up

- [ ] fm1-emu: CPU delta of playback (musical stress + a 6-note sequence at 1/16 at 250 BPM) and a WRITE with a sequence (fade, no fault, KNOBI_CPU1 build too); record in `docs/MEASUREMENTS.md`.
- [ ] Update `docs/CONTROLS.md`, `docs/MIDI.md`, README (Phase 3 section); `docs/SMOKE_TEST.md`: sequencer items for the device checklist (record, play, WRITE, power-cycle, `SEQ FULL` not reachable in normal use); list what only the device can confirm (store on real flash, WRITE gap with a sequence).
- [ ] CI green (firmware, fm1-emu, cpu1-emu). Commit `docs: Phase 3 status`.
