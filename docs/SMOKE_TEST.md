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
