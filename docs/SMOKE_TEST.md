# First device smoke test (SMK-37 Pro)

Goal: confirm on real hardware what the Mac and the emulator can't — the firmware boots, the FPU works in the audio
interrupt, the real CPU load, and that saving and recovery behave. Nothing here is a performance; stop at the first
problem and write down what you saw.

Package: `build/device/knobi-six.fwsc` from CI run 44024761787 (commit 42e58f4),
SHA-256 `b030a94a5af41d9e295cf3df4c4093cef8b3879ea431ef1683125f764a5c4db4`. Never install a `-local` package
(it contains your imported OB-6 bank) on a keyboard you lend or sell.

**Run on hardware 2026-11-19 (João, SMK-37 Pro): all 12 passed**, with the later `knobi-six-pro` artifact of CI run
44450085801 (`phase1`, commit e54c5d4, Phases 1-3), SHA-256
`121f6ff150533af6301328342821d779bea04854e5ad1d39d735fa9bfa43458a`. Results in the table below and in
[MEASUREMENTS.md](MEASUREMENTS.md) "First device run". Notes from that run:
- A keyboard already on an smk37-base-family build shows the MIDI port **"X0X FM-1"** (not "SMK-37 Pro-Private"):
  pass `--port "FM-1"` to `uboot_key.py` and `fm1_install.py` (and unplug any real FM-1 first).
- The tools need `mido` / `python-rtmidi` in the Python that runs them (the macOS system `python3` has neither):
  run them with the jl-uboot-tool venv's Python, or install both there.

## 0. Before anything is written

1. Note what runs now: `python3 tools/fm1_install.py --info` (read only). Write down the model and version.
2. **Full flash backup** — [BACKUP.md](BACKUP.md). Read only, twice, `verify_dump.py` must say both dumps are
   identical and show `trimdat` at 0xC1224. Keep the dump in two places (e.g. the Mac and a cloud drive).
   The dump holds the keyboard's key/pad/wheel calibration and Bluetooth pairing, which exist nowhere else.
3. Have the way back ready: M-Vave's stock package for this model (or the Firmware Hub), and your dump.

## 1. Install

`python3 tools/fm1_install.py build/device/knobi-six.fwsc --yes` (identity `SMK-37 Pro_956`). Keep USB and power
connected until it finishes. The first start clears the previous firmware's store (your backup has it).

## 2. Checklist — tick each, note numbers

| # | Check | Expect | Result |
|---|---|---|---|
| 1 | Power on | Boot screen with the KNOBI-SIX wordmark, then Play; no red fault screen | pass |
| 2 | Keys, velocity | INIT sound on every key; soft/hard playing changes level | pass |
| 3 | Pitch / mod wheel | Bend ±2 st; mod wheel adds LFO depth only when an LFO destination is on | pass |
| 4 | Encoders K2–K8, faders | Knobs on each screen move; faders follow the bottom strip; K1 = volume | pass |
| 5 | Pads | Top row toggles (lit red), bottom row pressure → aftertouch (route AT → F FREQ to hear it) | pass |
| 6 | Filter sweep | Cutoff fader sweeps smoothly, no crackle, no crash (this exercises the FPU in the interrupt) | pass, no crackle, no crash |
| 7 | Resonance at max | Rings but never whistles on its own | pass |
| 8 | Globals → CPU line, INIT 6-note chord | Write down CPU %, PEAK %, VOX, SHED | CPU 45 %, PEAK 45 %, VOX 6/6, SHED 0 |
| 9 | Globals → CPU line, a heavy patch (sync + X-Mod + high resonance, 6 notes high up) | Note the numbers; if SHED climbs, voices drop with a short fade, never crackles | CPU 60 %, PEAK 60 %, VOX 6/6, SHED 0; clean |
| 10 | Program change, WRITE | WRITE gives a ~0.2 s gap (fade, silence, fade); power-cycle → program is back | pass: fade gap, program back after power cycle |
| 11 | USB MIDI | Notes from the Mac play; send notes during a WRITE — none stuck afterwards | pass (Ableton loop, WRITE while playing): none stuck |
| 12 | Rescue path | Hold OCTAVE- while switching on → USB rescue screen (then power-cycle) | pass |

## 3. Decide

- Item 8 PEAK under ~50 % and 9 shedding rarely → keep 6 voices.
- Item 9 shedding constantly on musical patches → switch to 4 voices (`NVOICE`, one line).
- **2026-11-19: 6 voices kept** (item 8 PEAK 45 %, item 9 PEAK 60 % with no shedding).
- Any red fault screen: photograph it (vector, PC, return address) — the PC is looked up in `build/device/app.dis`.

## 4. Going back

Stock: M-Vave's package for this model via its updater or the Firmware Hub (keep your dump — one report says the
official updater erased an SMK-37's calibration when run from a custom firmware). Or the dump, sector by sector, as in
BACKUP.md "Putting it back".

## 5. CPU1 probe (Phase 2: the second core's first run on this unit)

The probe is the normal firmware plus checks of the second core (CPU1) that only the hardware can answer
([research/second-core.md](research/second-core.md) "What only the hardware can answer": does CPU1 start on this
unit, does it misread shared RAM at the stock supply levels, does its FPU work, what is the real speedup). It plays
like the default firmware, so the checklist in §2 can be run with it too. **DUAL CORE (the audio on two cores) is OFF
at every start of the probe**; you turn it on yourself in step D, once the checks have passed.

Same safety rules as §0: the **full flash backup first** (the Phase 1 dump is fine if nothing was written to the
keyboard since; else a new one), the way back ready, and **João installs it himself** — no agent ever runs
`fm1_install.py`, flashes, or uses the USB rescue.

**Package**: `knobi-six-cpu1probe.fwsc`, the CI artifact **`knobi-six-cpu1probe`** of the `build` workflow (job
`cpu1-emu`), never the `knobi-six-pro` artifact's `knobi-six.fwsc`. Download it from CI run 44159088422
(https://github.com/jhortale/knobi-six/actions/runs/44159088422, commit 6ba6f6b, Phase 2 after the final review's
fixes; older runs' probes are superseded): 652,737 B, SHA-256
`ea8e6bb4079c71fd8478d16cb71b189738550b5578fd2316ed1417c82a814eef`
(the `.fwsc` inside the artifact zip, computed from a download). Before installing, check it:
`shasum -a 256 knobi-six-cpu1probe.fwsc` must print exactly that. Identity
`SMK-37 Pro_956`, as the default. (A local build: `./build.sh --probe-cpu1`; `--model elite` / `mke` for those units;
build.py refuses `--release` with it: a probe is never published.) Never install a `-local` probe on a keyboard you
lend or sell.

Install: `python3 tools/fm1_install.py knobi-six-cpu1probe.fwsc --yes`, USB and power connected until it finishes.

**Run on hardware 2026-11-20 (João, SMK-37 Pro): passed, A to E**, with the `knobi-six-cpu1probe` artifact of CI run
44450085801 (SHA-256 `997dab6dbdb6b97eb5c5a09968863bf2c1ffdcae9c2634d6841b3c717ffa4a7f`; the newer run supersedes
the one named above). ERR 0 and MISREAD 0 throughout (~42 min), FPU and DUAL=SINGLE OK, `DUAL ON 16%` for ~11 min
with the same sound; LOAD 1C 58 % / 2C 38-39 %. Numbers in [MEASUREMENTS.md](MEASUREMENTS.md) "CPU1 probe on the
device".

### What the Globals screen shows (GLOBE)

Four probe lines above the usual three (`CPU ...`, `WHEELS ...`, `FADERS ...`); red when something is wrong:

| line | reads | means |
|---|---|---|
| `C1 PING n  ERR n  MISREAD n` | PING +1 a second; ERR and MISREAD 0 | PING: round trips through CPU1 that came back right (one a second). ERR: failures (a ping wrong or late, a probe job not back, CPU1 retired for any reason). MISREAD: words read wrong in the 16 KB shared-RAM test (CPU0 writes, CPU1 reads back; CPU1 writes, CPU0 reads back while CPU1 works; 4 KB every other audio block) plus jobs CPU1's mailbox check refused |
| `FPU OK  DUAL=SINGLE OK` | `--` for the first ~2 s, then `OK` | FPU: a fixed float workload run on CPU1 and on CPU0 at the same time, every second; `BAD` = the results differ, `TRAP` = CPU1 faulted in it. DUAL=SINGLE: a test patch (the musical stress patch, 6 notes) rendered inside the firmware, never to the speaker, on one core and then on two, every ~1.5 s: the checksums must be equal. After a misread, `BIT xxxxxxxx` = the bits that came back wrong (the known fault is `00200000`) |
| `LOAD 1C x%  2C y%  JOB z%` | three numbers | the same test patch's render time, in % of an audio block: on one core (1C), on CPU0 with two cores (2C, its wait for CPU1 included), CPU1's own share (JOB). **The real speedup on this unit is 1C / 2C** |
| `PROBE n%  C1 IDLE  DUAL OFF` | `C1 IDLE` | PROBE n%: the probe's own time in the audio interrupt, the last second's peak, % of a block (it is also counted in the CPU line and the CPU guard, so a crackle from it sheds voices like any load). CPU1's state: `IDLE` (running, waiting for work), `FAIL` + why: `PATTERN` (a RAM misread in the test), `FPU` (FPU BAD), `DUAL` (DUAL=SINGLE BAD), `MISREAD` (its mailbox), `LATE`, `BARRED`, `FAULT`, `SILENT` (it never started). DUAL: the audio's DUAL CORE global, `OFF`, `ON n%` (CPU1's share of the real sound) or `FAIL` |

Any failure retires CPU1 for the rest of the power-on (held; one core; the keyboard plays on, at most one glitched
block) — a misread in the RAM test, FPU BAD and DUAL=SINGLE BAD included, so DUAL CORE can never put a core known to
read or compute wrong on the sound (it then shows `DUAL FAIL` and plays on one core). The probe never restarts it; a
power cycle does. With the probe, the CPU line includes the probe's own time (a few % even when idle).

### How to run it (about 35 min)

| step | what | read / write down |
|---|---|---|
| A | Power on, GLOBE within ~5 s | line 4 `C1 IDLE` (else `C1 FAIL SILENT`: CPU1 does not start on this unit — photograph, then step B anyway: everything else runs on one core) |
| B | **10 min idle** on the Globals screen, nothing played | PING ≈ 600, ERR 0, MISREAD 0, FPU OK, DUAL=SINGLE OK; write down LOAD 1C / 2C / JOB |
| C | **10 min playing**, DUAL CORE still OFF: the §2 checklist (items 1–12; the CPU line for items 8 and 9 is the 5th line); chords, the heavy patch, a WRITE, program changes. The probe runs on whatever screen is shown | back on Globals: PING ≈ minutes × 60, ERR 0, MISREAD 0 |
| D | Only if B and C passed: **DUAL CORE ON** (Globals pad 7; the header shows `On`, line 4 `DUAL ON n%`), **10 min playing**: the INIT 6-note chord, the heavy patch, a WRITE, program changes, USB MIDI notes | the sound the same as with it off, no crackle; line 4 stays `DUAL ON n%`; the CPU line (items 8 and 9) with DUAL CORE on vs off on the same patch |
| E | Power off and on | line 4 back to `DUAL OFF` (the probe always starts with it off); PING counting again |

**Pass**: at the end of B, C and D: ERR 0, MISREAD 0, FPU OK, DUAL=SINGLE OK, `C1 IDLE`, `DUAL ON n%` in D, no
glitch heard, no red fault screen, and PING about one a second since power-on. **Fail**: any ERR or MISREAD, FPU
`BAD` / `TRAP`, DUAL=SINGLE `BAD`, `C1 FAIL`, `DUAL FAIL`, or a red fault screen. On a fail: note the minute, photograph
the screen, power-cycle and run B again; failing twice means this unit's CPU1 is not usable as it is (the supply
question in research/second-core.md), and DUAL CORE stays off.

**Photograph** (close enough to read the small lines): the Globals screen at the end of A, B, C and D, at any
failure, and any red fault screen (the PC is looked up in the probe's `app.dis`, in the same artifact). Write down
LOAD 1C / 2C / JOB and the CPU line with DUAL CORE off and on.

### Going back

The default firmware: `knobi-six.fwsc` (the `knobi-six-pro` artifact) with `fm1_install.py`, as in §1; the programs
and globals stay (the probe saves the same records, plus DUAL CORE in a blob of its own that the default build never
reads). Stock: M-Vave's package or your dump, as in §4.

## 6. Sequencer (Phase 3: the step sequencer and its storage)

Written before any keyboard run (the run is noted below); the emulator and the host tests cover the logic and the flows
([MEASUREMENTS.md](MEASUREMENTS.md) "Phase 3"), not the flash and not the ear. Same safety rules as §0 (**the backup
first**, the way back ready, and **João installs it himself**: no agent runs `fm1_install.py`, flashes, or uses the
USB rescue). **Package**: the `knobi-six-pro` artifact of the last green `phase3` CI run (the run and its SHA-256 are
named in MEASUREMENTS.md "Phase 3" once the branch is final); a `-local` package (your imported OB-6 bank) only on your
own keyboard. Run §2 first with it (items 1-12 are unchanged; item 10 now also stores a sequence, below).

**Run on hardware 2026-11-20 (João, SMK-37 Pro): S1-S14 passed**, with the regular `knobi-six-pro`
package of CI run 44450085801 (the one §2 passed with); S14 with a local build of the same commit plus João's imported bank (`knobi-six-local.fwsc`, his keyboard only). Numbers in [MEASUREMENTS.md](MEASUREMENTS.md) "Sequencer on
the device".

Words, as in [CONTROLS.md](CONTROLS.md) "Sequencer": SEQ opens the Sequencer screen; SEQ REC arms recording (lit);
`<` / `>` = DEC / INC on that screen while recording.

| # | Check | Expect | Result |
|---|---|---|---|
| S1 | SEQ: the Sequencer screen; SEQ again: back | STOP, STEP --, LEN 0, XP 0 (a program with no sequence); `SEQ n KB / m KB` shows the store | pass |
| S2 | **Record**: SEQ REC, then play C, E, G one at a time; `>`; two keys held and a third added, all let go; `<` | each key sounds as it is played; the grid gets a red dot a step, a dash for the rest, a stack of dots for the chord; `<` erases the last step | pass (`<` erased the chord) |
| S3 | Record a tie: hold a note, press `>` twice, let go | the step plus two blue tie bars; SEQ REC again ends it (lit off) | pass |
| S4 | **Play**: SEQ PLAY at 120 BPM, 1/16 | steps in time with BPM and DIVISION (change both while it plays), a short note each (gate half the step), the rest silent, the tie one long note with no retrigger, then it loops; STEP follows | pass |
| S5 | **Transpose**: while it plays hold SEQ REC and press a key a fifth above C4 (G4); then C4 | from the next note it plays a fifth up; that key makes no sound and records nothing; C4 = back to normal. With UNISON on a key alone does it | pass |
| S6 | SEQ REC **tapped** while it plays | it stops, recording arms (the screen says REC); a second tap ends it. The sequence is kept if no key was pressed | pass |
| S7 | Arp and sequencer | SEQ PLAY with ARP on turns the arp off; ARP ON stops the sequence | pass |
| S8 | **WRITE with a sequence** (record or load one, then WRITE, ENTER; once with it playing, once stopped) | the same fade, a gap and fade-in as item 10 but longer: **time it** (stopwatch or recording); STORED; the sequence plays on afterwards; no stuck note | pass; the gap felt short (not timed) |
| S9 | **Power-cycle** | the program is back with its sequence (LEN, SEQ PLAY plays the same notes, ties and rests). A program change loads that program's own sequence and stops the playing one; a sequence edited and not written is gone after a program change | pass |
| S10 | MIDI notes in while recording (a DAW or keyboard on USB) | recorded like the keys; with LOCAL OFF only the MIDI notes are | pass |
| S11 | MIDI out: PARAM TX = CC, LOCAL ON, SEQ PLAY | every note-on the DAW sees has its note-off, ties send nothing; stop mid-chord: no hanging note | pass (Ableton) |
| S12 | **External clock**: MIDI CLOCK = IN from a DAW, SEQ PLAY (ARMED), the DAW starts / stops / continues | silent until it runs; Start: step 1 on the downbeat; Stop pauses and releases the notes; Continue goes on from the held step. CC 123 from the DAW: notes released, it plays on; CC 120 stops it | pass (Ableton) |
| S13 | A heavy patch (the item 9 one) playing a 6-note chord each step, 250 BPM, 1/16 | the CPU line against the same patch with six held notes: PEAK about the same, SHED no higher | sequenced CPU 70 % / PEAK 70 %, held by hand 65 % / 75 %, SHED 0: no extra cost |
| S14 | **Factory sequences** (a local build with your imported bank, your own keyboard): play a dozen programs' sequences, ones with chords, with long held notes, with gaps | **ties hold the note** (no retrigger, ends with the last tied step) and **rests are silent**; anything that sounds wrong: write down the program number | pass (2026-11-20, local build with João's bank): the importer's 3 ambiguous programs (043, 096, 414) and about ten others; ties held, rests silent |

**Not part of the checklist**: `SEQ FULL: NOTHING STORED` needs the store to be full (about 76 KB: 76 full
64-step six-note sequences, the largest there are; a sequence of 16 single-note steps takes 64 B, so every one of the
500 programs could have one), which normal use does not reach; it is tested on
the host only (the host test fills the store with 76 full-size sequences, then checks that the next WRITE is refused
whole, with nothing changed). It can appear while the meter still shows a little room (a full-size sequence needs one sector with about 850 B free; short ones still fit).

### What only the device can confirm

None of this is answerable on the computer or in the emulator.

1. **The store on real flash**: sequences written, read back and surviving power-cycles; weak cells and wear (the log,
   its compaction and its read-backs are tested against a flash model); a power cut during WRITE leaves the old
   program with its old sequence or the new pair, never a mix (tested on the host by cutting at every flash operation;
   the real flash and supply are another matter).
2. **The WRITE gap with a sequence**: the host's model of the flash says ~0.26 s (the sequence store opens a sector),
   ~0.31 s when the store has to compact first, a worst case of ~0.4 s; the emulator times no erase. The real erase
   times decide whether that is a pleasant gap (S8; a compacting WRITE shows only after many writes).
3. **MIDI clock jitter with the sequencer**: clock in is queued to the 5.8 ms audio block, clock out is sent from the
   main loop; whether a DAW or a hardware slave stays tight with a sequence on it needs a real host (S12).
4. **Rest and tie decoding by ear**: how the OB-6 file marks a rest and a tie was inferred from the data and tested
   for consistency only (S14).
5. **The CPU with a sequence on the chip**: the emulator says the sequencer costs well under 1 % of the block beside
   the voices ([MEASUREMENTS.md](MEASUREMENTS.md) "Phase 3"), with no cache or contention model (S13).
6. **Feel**: step entry timing (a chord is recorded when every key is up), the velocity stored through the VEL CURVE,
   the pads' and buttons' response, whether the grid and the two hint lines read at arm's length.
