# First device smoke test (SMK-37 Pro)

Goal: confirm on real hardware what the Mac and the emulator can't — the firmware boots, the FPU works in the audio
interrupt, the real CPU load, and that saving and recovery behave. Nothing here is a performance; stop at the first
problem and write down what you saw.

Package: `build/device/knobi-six.fwsc` from CI run 44024761787 (commit 42e58f4),
SHA-256 `b030a94a5af41d9e295cf3df4c4093cef8b3879ea431ef1683125f764a5c4db4`. Never install a `-local` package
(it contains your imported OB-6 bank) on a keyboard you lend or sell.

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
| 1 | Power on | Boot screen with the KNOBI-SIX wordmark, then Play; no red fault screen | |
| 2 | Keys, velocity | INIT sound on every key; soft/hard playing changes level | |
| 3 | Pitch / mod wheel | Bend ±2 st; mod wheel adds LFO depth only when an LFO destination is on | |
| 4 | Encoders K2–K8, faders | Knobs on each screen move; faders follow the bottom strip; K1 = volume | |
| 5 | Pads | Top row toggles (lit red), bottom row pressure → aftertouch (route AT → F FREQ to hear it) | |
| 6 | Filter sweep | Cutoff fader sweeps smoothly, no crackle, no crash (this exercises the FPU in the interrupt) | |
| 7 | Resonance at max | Rings but never whistles on its own | |
| 8 | Globals → CPU line, INIT 6-note chord | Write down CPU %, PEAK %, VOX, SHED | |
| 9 | Globals → CPU line, a heavy patch (sync + X-Mod + high resonance, 6 notes high up) | Note the numbers; if SHED climbs, voices drop with a short fade, never crackles | |
| 10 | Program change, WRITE | WRITE gives a ~0.2 s gap (fade, silence, fade); power-cycle → program is back | |
| 11 | USB MIDI | Notes from the Mac play; send notes during a WRITE — none stuck afterwards | |
| 12 | Rescue path | Hold OCTAVE- while switching on → USB rescue screen (then power-cycle) | |

## 3. Decide

- Item 8 PEAK under ~50 % and 9 shedding rarely → keep 6 voices.
- Item 9 shedding constantly on musical patches → switch to 4 voices (`NVOICE`, one line).
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
`cpu1-emu`), never the `knobi-six-pro` artifact's `knobi-six.fwsc`. Download it from CI run 44144831394
(https://github.com/jhortale/knobi-six/actions/runs/44144831394, commit 9384171, the end of Phase 2 2A/2B; older
runs' probes are superseded): 652,737 B, SHA-256 `4444f363c3bd3ba71a46a4b9b6ffe40d766420de8e8dc63623f8038f5f7dfcea`
(the `.fwsc` inside the artifact zip, computed from a download). Before installing, check it:
`shasum -a 256 knobi-six-cpu1probe.fwsc` must print exactly that. Identity
`SMK-37 Pro_956`, as the default. (A local build: `./build.sh --probe-cpu1`; `--model elite` / `mke` for those units;
build.py refuses `--release` with it: a probe is never published.) Never install a `-local` probe on a keyboard you
lend or sell.

Install: `python3 tools/fm1_install.py knobi-six-cpu1probe.fwsc --yes`, USB and power connected until it finishes.

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
