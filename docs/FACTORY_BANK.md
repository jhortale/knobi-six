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
- Size: 500 records x 201 B = 100,500 B of flash (record version 2, `program_pack`'s format; no compression needed:
  the image stays far below its 619,520 B limit).

## The record

The bank's records are `program_pack` records (`params.c`), version 2: version, name[20], 86 values as little-endian
int16 (the 66 phase-1 parameters, then the 20 reserved phase-2/3 ids of `params.h`: P_DISTORTION .. P_ARP_HOLD),
chord[6], CRC-16/CCITT-FALSE. Record version 2 is also what WRITE now stores (version-1 records still load: the
reserved values take their defaults). A firmware older than this one reads version-2 records as empty (init).

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
| 128..895 sequencer: 6 lanes of 64 notes and 64 velocities | not carried | no room in a program record |

The importer clamps (with a warning) any value outside its range; on the v1.01 file there are none. Its report
(`build/factory_bank_report.txt`) lists every byte's range and distribution, the unmapped bytes, the warnings and the
"OB" names. `tests/ob6_import_test.py` checks the packing, the layout and every conversion on made-up dumps (and on the
real file when `OB6_SYX` points at it).
