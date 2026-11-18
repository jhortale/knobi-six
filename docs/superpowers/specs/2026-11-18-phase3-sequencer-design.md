# KNOBI-SIX Phase 3 — sequencer design

Date: 2026-11-18 · Status: ready for approval · Extends `2026-11-16-standalone-firmware-design.md` (still binding) and
`2026-11-17-phase2-design.md`. Binding reference: `docs/ob6-reference.md` §2.13 and §5 "Sequencer".

## Intent

João wants the OB-6 step sequencer on KNOBI-SIX for two uses: **playing the factory programs' own sequences** (all
500 OB-6 factory programs carry one, imported locally) and **writing his own**. Phase 3 does not depend on the device
numbers (the sequencer is cheap in CPU), unlike the effects (2C), which wait for the CPU1 probe run.

## Goals

- One polyphonic step sequence per program, for every one of the 500 programs: up to 64 steps, up to 6 notes per step
  with a velocity each, rests and ties (reference §5).
- Step-entry recording, playback looping at the clock (BPM / division, internal or MIDI clock), transpose while
  playing, external start/stop (MIDI Start/Stop/Continue; the footswitch where the platform has one).
- The importer (`tools/ob6_import.py`) carries the factory sequences into the local, never-committed build.
- Sequences survive power cuts the way programs do.

## Non-goals / anti-goals

- No real-time recording (the OB-6 has none).
- No per-step gate, swing beyond the clock's divisions, or a length setting separate from the recorded length
  (the reference leaves them unspecified; the OB-6 doesn't offer them).
- No OB-6 sequence data or program names committed or in a published build — same rule as the factory bank.
- A power cut or a full store must never lose or corrupt a stored program or sequence, and never pair a program with a
  sequence that wasn't written with it.
- The sequencer never runs on CPU1; it adds no audio-interrupt flash access.

## Constraints (measured)

- Program records are fixed-stride (~221 B), 74 per 16 KB slot, 500 programs in 7 logical slots + 1 spare; a full
  sequence (64 × 6 × note + velocity = 768 B) cannot go inside a record. The record has reserved phase-2/3 values.
- Factory sequences (measured locally from João's bank, counts only): every program has one, 2–64 steps, median 26;
  steps with 1 note dominate (≈11,300), 6-note steps are rare (72). Total ≈ 19,500 notes; packed ≈ 50 KB.
- Free flash inside the app's own data window `FL_DATA` (0x9D000–0xBAFFF, already allow-listed by window): about
  80 KB not used by settings, blobs 0..7 or the owner tag (0xA6000–0xAEFFF and 0xB0000–0xBAFFF; the builder confirms
  against `store.h` and the owner tag at 0xAF000). Protected ranges (head, BTIF/USRTRIM, USR, key_mac) stay untouchable.
- Flash erase holds interrupts ~50 ms a sector (or sliced saves): save only when silent or with the WRITE fade, as
  programs do. RAM: the edit buffer's sequence (≤ 768 B + a COMPARE copy) must fit the RAM budget in docs/API.md
  "Memory and flash"; the builder checks it.

## Approaches considered

- **A. Separate packed sequence store (chosen by João).** Every program has a sequence; stored edits take only the
  bytes their steps need, in the free ~80 KB. Cost: the most complex storage; a shared total.
- B. ~100 fixed full-size sequence slots that programs point to: simple, but loses most factory sequences.
- C. Sequence inside the program record (≈1 KB records): only ~100 programs fit; drops 400 factory programs.

## Design

### Where sequences come from

- **Factory sequences stay in the factory bank, read-only, in the app image** — exactly like factory programs. A
  program never written plays its factory sequence (empty in builds without the factory bank). RESTORE FACTORY restores
  the factory sequence into the edit buffer.
- **The sequence store holds only João's written sequences**, so all ~80 KB is for his own (≈ 100 full-size 64-step
  6-note sequences; far more typical ones). No first-boot seeding; a firmware update with a different factory bank
  needs no migration.

### Behaviour (reference §5; binding)

- **Recording** (step entry): SEQ REC arms recording on the edit buffer's sequence, from step 1, replacing it. Input:
  the keybed and MIDI-in notes (as chord memory). Each key press (chord: keep one key held to add notes) records a step:
  the played note **after the octave buttons' shift** (baked into the stored note), not the unison / chord-memory
  expansion (that is applied at playback like live playing), with its velocity; notes outside 12–108 are not recorded.
  INC = rest; holding notes and pressing INC repeatedly = tie (extend). DEC = step back and erase. 64 steps max;
  recording stops at 64. SEQ REC again (or SEQ PLAY) ends recording.
- **Playback**: SEQ PLAY toggles; loops the recorded length at the clock's BPM and division. Start follows the arp's
  Phase 2 rules: internal clock with ARP BEAT SYNC off → starts at once (clock restarted); BEAT SYNC on, or MIDI CLOCK
  OUT → waits for the next step of the division (no clock restart, no 0xFA under OUT); MIDI CLOCK In / Slave Thru →
  PLAY arms, and the sequence runs only while the external clock runs (Start = from step 1, Continue = from the held
  step, Stop = pause; silent without a clock). Stopping with SEQ PLAY resets to step 1.
- **Transpose**: while playing, hold SEQ REC and press a key — middle C = no transposition; with Unison on, no hold
  needed (OS 1.5.5+ behaviour). Transpose is not stored.
- **Arp and sequencer exclusive**: turning one on turns the other off.
- Live-played notes compete with the sequence for the 6 voices (normal voice allocation).
- Sequence notes go to MIDI out under the arp's rules (PARAM TX on and LOCAL on; every note-on paired with its off on
  the same channel).
- A program change stops playback (notes released) and loads that program's sequence.

### Storage

- **Sequence store** in the free FL_DATA space, keyed by program number 0..499. Each entry is either a packed sequence
  or an explicit **empty** entry (a written program whose sequence is empty — so the factory sequence does not come
  back). No entry = never written = the factory sequence. Layout is the builder's within these rules: power-safe (a cut
  leaves the old or the new entry, never neither — e.g. log-structured with a spare / compaction sector); wear-aware
  (no sector erased on every save); its own magic, version, CRC; entries carry a **generation** number.
- **Encoding**: length, then per step: note count (0 = rest), tie flag, notes (12–108) with velocities (0–127).
- **One WRITE = program + sequence, paired**:
  1. The sequence entry is written first with a new generation (the old entry stays valid).
  2. The program record is written with that generation in one of its reserved values (record layout unchanged; a
     record version bump only if the builder finds it needed).
  3. On load, a program uses the entry whose generation matches its record; a record with no generation (Phase 1/2
     records, never-written) uses the factory sequence. Superseded entries are reclaimed by compaction.
  A cut between 1 and 2 leaves the old program with its old sequence; after 2, both new.
- **Store full**: the WRITE is refused as a whole — nothing changes, the screen says `SEQ FULL` (and how to free room:
  clear sequences of programs you don't need, or RESTORE FACTORY and write).
- **Edit buffer**: the sequence is part of the edit buffer like any parameter: COMPARE, RESTORE FACTORY, a program
  change and a power cut behave as for program edits (lost without WRITE; not autosaved).
- **Flash allow-list**: the store's sectors are added to `FL_RANGE_OK` explicitly; flash-map checks prove every
  protected range stays refused.
- **Factory import**: the importer decodes the OB-6 sequence bytes (offset 128: 6 lanes × 64 notes + 64 velocities;
  the builder confirms the rest/tie encoding — values ≥128 appear in the data) and the factory bank header carries the
  packed sequences.

### UI

- Buttons: **SEQ REC** = record; **SEQ PLAY** = play/stop; **SEQ** = the Sequencer screen. INC / DEC during recording:
  the builder picks two buttons free or contextual while recording (e.g. `<` / `>`) and documents them.
- Sequencer screen: step grid (64 steps, current step, notes per step), length, play/record/armed state, transpose,
  store usage (`SEQ 12 KB / 80 KB`). Follows the existing screen style (docs/design/standalone-ui/).
- Play screen header shows `SEQ` / `REC` state.

### Testing

- Unit: encoding round-trip; store save/load/compaction; power-cut at every write step (host flash model) leaves old or
  new, and program/sequence generations always match; full store refuses the whole WRITE; empty entry beats factory;
  record (notes, chords, rest, tie, step-back, 64 limit, octave shift baked, out-of-range dropped, MIDI-in notes);
  playback timing against the clock (internal, OUT, MIDI In start/continue/stop, no clock → silent); transpose;
  arp/seq exclusive; program change stops and releases.
- Host scripts: record → play → WRITE → power-cycle → play; MIDI out pairing; external clock stopped → silent.
- Importer: synthetic OB-6 dumps with known sequences (no Sequential data in tests).
- fm1-emu: CPU delta of playback (expected negligible); WRITE with a sequence (fade, no fault); KNOBI_CPU1 build green.

## Decided by João

- Phase 3 now, before the device probe; the sequencer serves both the factory sequences and his own.
- Approach A: a separate packed sequence store.

## Our calls (correct if wrong)

- Factory sequences read-only in the factory bank; the store holds only written sequences (≈ 80 KB for João's own).
- SEQ REC starts a new recording from step 1 (replaces the sequence); no overdub/insert.
- Recording takes keys and MIDI in; the octave shift is baked in; unison/chord expansion is not recorded.
- Gate per step: 50 % of the step (same as the arp); ties extend the gate across steps.
- Playback start/stop follows the arp's Phase 2 clock rules; Stop via SEQ PLAY resets to step 1.
- Sequence saved only with WRITE, paired with the program by a generation number; a full store refuses the whole WRITE.
- Sequence notes follow the arp's MIDI-out rules (PARAM TX and LOCAL ON).

## Left to the builder

The store layout (directory, compaction, wear levelling), the INC/DEC button mapping while recording, screen layout
details, and the exact OB-6 rest/tie byte decoding (verified against the factory data locally).
