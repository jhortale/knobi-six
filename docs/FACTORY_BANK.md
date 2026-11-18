# The factory bank (optional, local only)

## NOTICE

The OB-6 factory programs (OB-6 Factory Programs v1.01, file `OB6_Programs_v1.01.syx`, 2016) belong to
**Sequential / Dave Smith Instruments**. **They are not part of KNOBI-SIX**: no program data, converted or not, is in
this repository, and no build we publish contains them. KNOBI-SIX ships with empty banks (every program = the init
program).

`tools/ob6_import.py` lets an owner of that file build a firmware for their own keyboard with the programs as a
read-only factory bank. The header it writes, `firmware/gen/factory_bank.h`, holds Sequential's data: it is in
`.gitignore`, and it must **never be committed or redistributed**, nor a firmware image built with it. Any use beyond
that (publishing the data, the names, a build with them) needs João's explicit decision first: it raises the rights to
the programs, and the program names that contain "OB" / "Ober" clash with the project's rule of its own names.

Where to get the file and the steps: README.md, "Importing the OB-6 factory programs".

**The build enforces it** (`tools/build.py`, `factory_guard`; tested by `tests/build_factory_test.py`):
- `--release` (what CI's release workflow and anything we publish use) **refuses to run** while
  `firmware/gen/factory_bank.h` is there: move it away or delete it, then build the release again.
- Any other build with it there names the package **`build/knobi-six-local.fwsc`** (`knobi-six-local-elite.fwsc`,
  `-local-mke.fwsc`) instead of `knobi-six.fwsc`, and warns, before and after building, that the package contains your
  own imported bank and must not be shared. Install it on your own keyboard only.

Names in the v1.01 bank that contain "OB" / "Ober": 18 of 500. They are imported as they are; the importer's local
report (`build/factory_bank_report.txt`) lists them for whoever runs it on their own file.

## How it behaves

- Banks 0..4 (programs 000..499) stay the editable user programs in the flash slots (`store_prog.c`). The factory
  bank is a second, read-only copy baked into the image, like the OB-6's banks 5..9.
- A program **never written** (its record all zeros) loads as **factory program N**; past the bank, or with no bank,
  the init program. On the first boot the store is empty, so program 000 is factory 000.
- A stored record that is **broken** (CRC) or of an unknown version loads as the **init program**, never silently as
  the factory one, with a notice: `Err` in the header, STORED PROGRAM BROKEN: INIT LOADED on Program
  (`prog_load` returns -2).
- **WRITE** stores into the user slot only; the factory bank never changes. A written program plays instead of its
  factory version from then on.
- **Restore factory: hold GLOBE and press BT (WRITE), twice within 2 s.** It throws the edits away, so the first press
  only asks (`SurE` in the header; RESTORE FACTORY? EDITS ARE LOST on Program); the second, on the same program within
  2 s, puts factory program N (N = the current program) into the edit buffer (`FAC`); not confirmed in time, the ask
  is forgotten. Nothing is stored until WRITE + ENTER. GLOBE's press opened Globals: the gesture closes it again.
  Ignored while a WRITE is armed. With no factory bank it loads the init program.
- The build: `firmware/src/params/store_prog.c` includes `factory_bank.h` when it exists (`__has_include`), else
  `FACTORY_COUNT` is 0. `-DKNOBI_NO_FACTORY` builds without it even when it is there (the host tests do, since they
  expect the init program); `-DKNOBI_FACTORY_H='"file.h"'` takes another bank (the unit tests' synthetic one,
  `tests/unit/factory_test_bank.h`, three made-up programs). `FACTORY=1 tests/run_host.sh SCRIPT` runs the host app
  with the local bank.
- Size: 500 records x 201 B = 100,500 B of flash (record version 2, `program_pack`'s format), plus the sequences
  (about 61 KB for v1.01); no compression needed: the image stays below its 619,520 B limit (CI measures it with a
  500-program synthetic bank with sequences of the same size).

## The record

The bank's records are `program_pack` records (`params.c`), version 2: version, name[20], 86 values as little-endian
int16 (the 66 phase-1 parameters, then the 20 reserved phase-2/3 ids of `params.h`: P_DISTORTION .. P_ARP_HOLD),
chord[6], CRC-16/CCITT-FALSE. Version-1 records still load (the reserved values take their defaults). Since Phase 3,
WRITE stores **version 3**: the same plus one more value, P_SEQ_GEN, the generation of the program's sequence in the
sequence store (0 = none: the factory sequence). A version-2 record (the bank's, or a Phase 2 WRITE) loads with
generation 0. A firmware older than one record version reads the newer records as broken (init).

## Sequences (Phase 3)

The factory sequences stay in the factory bank, read only, like the programs (`firmware/src/params/prog_seq.h`). The
header may define, beside `factory_bank[][]`:

```c
#define FACTORY_SEQ_COUNT N                             /* programs 0..N-1 have an entry (need not be FACTORY_COUNT) */
static const uint32_t factory_seq_at[N + 1];            /* program p: factory_seq_data[at[p] .. at[p + 1]) */
static const uint8_t factory_seq_data[];                /* the sequences, back to back */
```

Each sequence is `seq_pack`'s format exactly (`synth/seq.h`: length, then per step a header byte count | tie << 3 and
the (note, velocity) pairs; an empty sequence is the one byte `00`). An empty span means "no factory sequence"; a span
that does not unpack to exactly its own length reads as none. Without `FACTORY_SEQ_COUNT` (no bank, or a bank of
programs only, as every bank before Phase 3's importer) every factory sequence is empty. `factory_seq(prog, seq_t *)`
reads them. `tools/ob6_import.py` emits these symbols (when at least one program has a sequence: a bank without any
stays a bank of programs only), `FACTORY_SEQ_COUNT` = the last program + 1, an empty sequence or a missing program as
an empty span; `tests/unit/factory_test_seq.h` is a hand-written example, `tests/unit/factory_test_import.h` the
importer's own output for made-up dumps (`tests/unit/test_factory_import.c` loads every span with `seq_unpack`). With
the v1.01 file every program carries one: about 61 KB of flash beside the 100.5 KB of programs.

How a program's sequence is chosen: its record names a generation -> that stored entry (an empty one too); it names
none (a version-1/2 record, a never-written program, or a program written with its factory sequence unedited) -> the
factory sequence; a broken record -> empty. RESTORE FACTORY puts the factory sequence into the edit buffer with the
factory program; WRITE after it names no entry and frees the program's stored ones.

## The OB-6 dump and the conversion (tools/ob6_import.py)

A program dump is `F0 01 2E 02 <bank> <program> <1171 bytes> F7`; the 1171 bytes unpack ("packed MS bit", groups of 8)
to 1024. **The unpacked bytes are not in NRPN order** (the name is at 107..126, NRPN 236..255): they follow the OB-6's
internal parameter order, which is the Prophet-6's (Sequential's "P6 packed parameter data assignments", posted on
forum.sequential.com), with the Prophet-6's high-pass filter slots reused by the OB-6. The importer's `LAYOUT` table
gives every byte; it was checked against the factory file's 500 programs: each mapped byte inside its range
(0 anomalies), the unused bytes always 0, and the switches agree with what the programs' names say they do (arpeggio,
unison, band-pass programs), the envelopes' interleaved order with the OB-6's init program.

| OB-6 bytes | KNOBI-SIX | conversion |
| --- | --- | --- |
| 0 / 1 VCO1 / VCO2 FREQ | VCO1 / VCO2 FREQ | as is (0..60) |
| 2 VCO2 FINE 0..254 | VCO2 FINE -50..+50 cents | (v - 127) x 50 / 127, rounded half away from 0 |
| 3, 4, 5, 6 shapes, PWs | the same | as is |
| 7, 8, 9, 10 levels | mixer | as is |
| 11 SYNC, 12 KEYBOARD, 13 LOW FREQ | the same | as is |
| 14 PORTA RATE, 15 MODE, 16 ON, 17 BEND, 18 DETUNE | the same | as is |
| 19 FILTER FREQ 0..255 | FILTER FREQ 0..164 | v x 164 / 255, rounded (the dump keeps 8 bits; the NRPN 0..164) |
| 20 RESO, 21 KEY TRACK, 22 FILTER VEL | the same | as is |
| 23 FILTER MODE 0..254 | FILTER MODE 0..127 | v x 127 / 254, rounded (the P6's HP cutoff slot) |
| 26 BP | BP | as is (the P6's HP velocity slot) |
| 25 | X-MOD > NORM-BP | as is; **inferred** (the P6's HP key-amount slot; set almost only where an X-MOD amount is) |
| 27 PROGRAM VOLUME, 28 PAN SPREAD | the same | as is |
| 29 FILTER ENV AMOUNT, 70 AT AMOUNT, 77 / 78 X-MOD amounts | bipolar | v - 127 (0..254 -> -127..+127) |
| 31 LOUDNESS ENV AMOUNT, 43 its VELOCITY | the same | as is |
| 35..42 envelopes | filter / loudness A D S R | interleaved: filter A, loudness A, filter D, loudness D, ... |
| 59 LFO FREQ, 61 SYNC, 62 SHAPE, 63 AMOUNT | the same | as is (shape order sine, saw, rev saw, square, random) |
| 64, 65, 66 (0..3), 67, 68, 69 LFO dests | VCO1, VCO2, PW (none/1/2/both), FILTER FREQ, FILTER MODE, AMP | as is |
| 71..76 AT dests | VCO1, VCO2, FILTER FREQ, FILTER MODE, LOUDNESS, LFO AMT | as is |
| 79..83 X-MOD dests | VCO1, SHAPE1, PW1, FILTER FREQ, FILTER MODE | as is |
| 84 UNISON, 85 VOICES (0..5 = 1..6, 6 = chord), 86 KEY MODE | the same | as is |
| 97..102 chord memory: semitones from the root, 127 empty | chord[6], absolute notes | 60 + offset (only the intervals count); all 0 / 127 = none captured: empty |
| 58 distortion, 46 FX on, 44..57 FX A / B (type, mix, P1, P2, sync, sync division) | reserved P_DISTORTION, P_FX_* | as is (FX B has 14 types, 0..13; the NRPN table's 0-5 / 0-9 are wrong) |
| 87 BPM, 92 clock division, 91 ARP ON, 89 MODE, 90 RANGE | reserved P_BPM, P_CLK_DIV, P_ARP_* | as is |
| 88 | reserved P_ARP_HOLD | **inferred** (the P6's arp-hold slot) |
| 60 (LFO retrigger, 0 / 11), 93..96 (sequencer state), 105, 106, 127 (version, editor, end marker) | not carried | |
| 128..895 sequencer: 6 lanes of 64 notes and 64 velocities | the factory sequence (`factory_seq_data`) | see "The OB-6 sequence" below |
| 896..1023 | not carried | not the sequence (see below) |

### The OB-6 sequence (bytes 128..895)

The manual's NRPN table gives the per-step notes (12..108) and velocities (0..127) but not how the program dump marks
a step, a rest or a tie, nor the length. The rules below were found on the v1.01 file and are what `decode_seq`
implements; they describe the format, and no data from the file is reproduced here (only coarse statements, no counts).

- **Layout, lane-major:** lane L (0..5) of step s (0..63): note byte `128 + 128 L + s`, velocity byte `+ 64`.
- **Velocity byte bit 7 = the lane holds a note at this step**; its low 7 bits are the velocity. A lane that holds
  nothing has note 60 and velocity 0 (a filler: 60 is by far the commonest note byte for that reason). In lanes 0..3
  a velocity byte is always either 0 or has bit 7 set.
- **Length:** a step exists while lane 0 holds a note; the sequence is the run of such steps from step 1. In the file
  the steps are contiguous (no lane-0 note after the first step without one), so no separate length is needed; there is
  none elsewhere either: bytes 93..96 ("seq on / record / mode / play mode") are 0 in every program. All 500 programs
  carry a sequence, of very different lengths (up to the 64 steps).
- **Tie: lane 0's note byte with bit 7 set.** Its other bits are the filler (note 60 | 0x80, velocity 0 | 0x80) and
  the other lanes are empty: the step holds the previous one on. Why a tie and not a rest: it never occurs at step 1
  nor after a rest (the OB-6's own rule: a tie extends held notes), and after chords it makes long even runs (a chord
  held for a bar or more), which long rests after a chord would not be. A few steps have all low bits set (note 0xFF,
  velocity 0xFF) instead of the filler: read by the same bit-7 rule as ties (they too follow a note or a tie); the
  importer counts those programs as ambiguous.
  **This decoding is inferred from the data, not documented: confirm it by ear on the device** ([SMOKE_TEST.md](SMOKE_TEST.md) §6).
- **Rest: lane 0 holds a "note" of velocity 0 and no other lane holds one.** Every such step has the filler note 60,
  and a played note never has velocity 0 (that is a note-off), so it is the rest marker. A consequence: a real note of velocity 0 alone in a step cannot be told from a rest in this format; the importer
  reads it as a rest.
- **A note step:** the lanes that hold a note, from lane 0 up (the OB-6 fills them in order); most steps have one
  note, chords go up to six.
- **Bytes 896..1023** are a block of fixed patterns that repeat across programs, unrelated to the steps: not the
  sequence, not carried.

Into KNOBI-SIX's format (`synth/seq.h`; never anything `seq_unpack` refuses, checked against the firmware's own
`seq_unpack` on the whole file): a tie step carries a copy of the previous step's notes with the tie flag (a chain of
ties keeps copying); a note outside 12..108 is dropped (as `seq_rec_step` does when recording), a step left with no
note is a rest, a tie at step 1 or after a rest becomes a rest; a lane past an empty lane is ignored (the OB-6 does not
leave gaps). Bytes in a lane without its bit 7, and anything past the length, are ignored.

A few programs of the v1.01 file have oddities (stray bits in lanes past an empty lane, stray velocity bytes without
bit 7, the 0xFF tie marker above): they are ignored as said, and the importer's report names them per program. No note
outside 12..108 is left in a kept lane, so nothing else is dropped. Result: all 500 programs carry a sequence, about
61 KB of flash in all.

The importer clamps (with a warning) any value outside its range; on the v1.01 file there are none. Its report
(`build/factory_bank_report.txt`, local, never committed) lists every byte's range and distribution, the unmapped bytes,
the warnings and the "OB" names, and the sequences (how many, their bytes, steps, rests, ties, lengths, the oddities
per program).
`tests/ob6_import_test.py` checks the packing, the layout, every conversion and the sequence decoding on made-up dumps
(and on the real file, structure and counts only, when `OB6_SYX` points at it).
