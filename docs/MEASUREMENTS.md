# Measurements

## Phase-1 CPU gate (Task 11): result BLOCKED, NVOICE left at 6

**Rule** (spec "Constraints", plan Task 11): `NVOICE = 6` if the 6-voice worst case takes less than 50 % of the audio
block in **both** cycle models at 240 MHz. If not, profile the hot spots and optimise once. If it is still over,
`NVOICE = 4`, unless 4 voices are over too. Then the gate is BLOCKED and João decides.

**Result.** After the optimisation pass, the 6-voice worst case takes **172 % (emu) / 248 % (hw)** of the block at
240 MHz. The 4-voice worst case takes **120 % / 164 %**. It does not pass at 360 MHz either: 115 % / 165 % for 6 voices
and 80 % / 109 % for 4 voices (hw model). **Neither voice count meets the gate, so the gate is BLOCKED.** `NVOICE` stays
at 6 (voices.h, unchanged) until João decides what to change (see "What would have to change" below).

### Method

- **Emulator.** [joakimeriksson/fm1-emu](https://github.com/joakimeriksson/fm1-emu) (MIT), pinned at `60645a5`. It is
  patched (`tests/cpu_emu/patch_emu.py`) to record instructions and cycles per program counter in two cycle models:
  - **emu**: fm1-emu's own model (two ALU operations per cycle);
  - **hw**: the 2026-11-15 hardware probe table (ALU 1 cycle; load, store and multiply 2; a taken branch, call or
    return 7.5).
  The two models differ by about 1.4×. Both come from João's spike in `jhortale/smk37-analog` (branch
  `spike/device-link`, `spike/cpu/`), which is copied here with credit.
- **Harness.** `tests/cpu_emu/harness.c` builds params plus `synth/` as `app.c` does: a unity build with the JieLi
  clang, the app's flags (`-target pi32v2 -Os -ffunction-sections -fno-builtin`, `tools/build.py` CFLAGS) and
  `-DNVOICE=6` or `4`. It loads the patch, plays six keys and renders 256-frame blocks (one audio interrupt) through
  `synth_render`.
- **Per-block cost.** Each case runs with 40 blocks and with 8 blocks. The difference divided by 32 is the cost of one
  block, so start-up and the note-ons cancel out.
- **Correctness check.** Each run ends with its result words: the number of voices sounding, `sizeof(synth_t)` and a
  checksum of the output. The checksums before and after the optimisation are identical, so the pass is bit-exact.
- **Where it runs.** CI job `fm1-emu` in `.github/workflows/build.yml`; `bash tests/cpu_emu/run.sh EMU OUTDIR` runs it
  locally (Docker off Linux x86-64).
- **Budget.** One block is 256 frames at 44.1 kHz = 5.805 ms:
  - 1,393,197 cycles at 240 MHz (the clock fm1-emu models), so the 50 % gate is **696,599 cycles**;
  - 2,089,796 cycles at 360 MHz (the clock seen under the bootloader; the SMK-37's own clock is not confirmed).
- **Patches.**
  - **init chord**: `program_init`, C4 E4 G4 C5 E5 G5 at velocity 100 (`tests/scripts/chord.txt`).
  - **worst**: `tests/scripts/worst.txt` = `test_render` WORST, at velocity 127 with the mod wheel and aftertouch at
    127. It has:
    - every mixer level at maximum;
    - VCO 1 pulse PW 0 and VCO 2 pulse PW 255, with sync on;
    - FILTER FREQ 100, maximum RESONANCE, FENV AMT 127;
    - X-Mod VCO 2 and the filter envelope at maximum on every destination, which runs the per-sample filter
      coefficient path;
    - the LFO square at 500 Hz, full amount, on every destination;
    - aftertouch on every destination.
    It is played on C3 G3 C4 E4 G4 C5, and on a **high chord** of notes 120 121 123 124 126 127 (Task 2: there the
    out-of-line PolyBLEP / sync path of pulse + pulse + sync runs on most samples).

### Cycles per 256-frame block

The 240 MHz and 360 MHz columns are % of the block (emu / hw); "gate" means under 50 % in both models at 240 MHz.

Before (HEAD `099167b`):

| patch | voices | insns | emu cycles | hw cycles | 240 MHz | 360 MHz | gate |
|---|---|---|---|---|---|---|---|
| init chord | 6 | 559,087 | 960,689 | 1,300,869 | 69.0 / 93.4 | 46.0 / 62.2 | no |
| worst C3..C5 | 6 | 1,521,252 | 2,296,755 | 3,172,978 | 164.9 / 227.7 | 109.9 / 151.8 | no |
| worst high chord | 6 | 2,210,111 | 3,449,769 | 4,769,422 | 247.6 / 342.3 | 165.1 / 228.2 | no |
| init chord | 4 | 380,416 | 654,052 | 886,867 | 46.9 / 63.7 | 31.3 / 42.4 | no |
| worst C3..C5 | 4 | 1,053,609 | 1,598,265 | 2,208,470 | 114.7 / 158.5 | 76.5 / 105.7 | no |
| worst high chord | 4 | 1,457,953 | 2,273,870 | 3,143,953 | 163.2 / 225.7 | 108.8 / 150.4 | no |

After the optimisation pass (CI run 44010363233, the same numbers as the local run):

| patch | voices | insns | emu cycles | hw cycles | 240 MHz | 360 MHz | gate |
|---|---|---|---|---|---|---|---|
| init chord | 6 | 462,908 | 665,188 | 930,203 | 47.7 / 66.8 | 31.8 / 44.5 | no |
| worst C3..C5 | 6 | 1,277,700 | 1,695,643 | 2,396,581 | 121.7 / 172.0 | 81.1 / 114.7 | no |
| worst high chord | 6 | 1,802,993 | 2,460,607 | 3,448,630 | 176.6 / 247.5 | 117.7 / 165.0 | **no** |
| init chord | 4 | 316,096 | 456,750 | 639,352 | 32.8 / 45.9 | 21.9 / 30.6 | yes |
| worst C3..C5 | 4 | 884,162 | 1,182,457 | 1,668,887 | 84.9 / 119.8 | 56.6 / 79.9 | no |
| worst high chord | 4 | 1,193,183 | 1,629,445 | 2,283,876 | 117.0 / 163.9 | 78.0 / 109.3 | **no** |

What the pass gained:
- hw cycles fell by 24-28 % on the worst cases and 28 % on the init chord.
- Per voice and sample, the 6-voice worst case still costs 1,560 hw cycles (C3..C5) and 2,245 (high chord).
- For the gate, the whole voice would have to cost **453** hw cycles per sample with 6 voices, or **680** with 4.

Host yardstick (`tests/cpu_host.sh`, x86-64 instructions per sample) moved the same way:
- init chord: 2379 → 2110;
- worst: 5675 → 4900.

### Hot spots after the pass (hw cycles per block)

6-voice worst case, C3..C5 (2.40 M in all):
- `voice_render`: 1.41 M. This is the inlined per-sample loop: oscillators, the X-Mod paths (`mods_fm1`, VCO 1
  shape, `svf_coef_fast`), the mixer, two ADSRs, the SVF fast path, the VCA and pan.
- `svf_slow_x`: 0.40 M. With the input overdriven and RESONANCE at maximum, the SVF states leave the fast path's
  ±2^29 gate on **74 % of samples** (counted on the host; the output overflow check never fires). Those samples take
  the exact 64-bit path.
- `osc_blep`: 0.19 M. `osc_sub_blep`: 0.09 M. `osc_vco1_sync`: 0.09 M.
- Control rate:
  - `mods_sum`: 0.05 M;
  - `voice_control`: 0.04 M;
  - `synth_render`, the bus and limiter: 0.04 M.

6-voice worst case, high chord (3.45 M in all):
- `voice_render`: 1.35 M.
- `osc_vco1_sync`: **1.17 M**. This is the band-limited hard sync near every VCO 2 wrap, which at 8-12 kHz is most
  samples.
- `svf_slow_x`: 0.39 M.
- `osc_blep`: 0.29 M.

Init chord, 6 voices (0.93 M in all):
- `voice_render`: 0.70 M. A saw through an open filter costs about 450 hw cycles per voice and sample.
- Control rate: `svf_coef`, `osc_exp2`, `voice_control`, `mods_sum`, together about 0.12 M.

Before the pass, at `-Os` nothing per sample was inlined. Each voice and sample paid calls and returns to these
functions, at 7.5 hw cycles each plus register saves:
- `osc_tick`, `svf_tick`, `adsr_tick` ×2, `vca_control`, `vca_apply`;
- in the worst case, also `mods_fm1`, `osc_set_shape`, `svf_coef_fast`, `osc_side`, `osc_step` and `svf_slow`.

### The optimisation pass (commit `60b2d1d`, bit-identical)

- **Per-sample inlining.**
  - `osc_tick`, VCO 1's part of `osc_set_shape`, `svf_tick`, `svf_coef_fast`, `adsr_tick`, `vca_control`, `vca_apply`
    and `mods_fm1` have static always-inline cores (`*_hot`), which `voice_render` calls.
  - The external functions remain as wrappers for the tests and the control-rate callers.
  - `osc_side`, `osc_step` and `osc_ev` are inlined into the step paths.
  - The clamps are forced inline.
- **SVF slow path.** `svf_slow` gets the input already driven, so it is not driven twice. Forcing it inline was tried
  and was slower (more register pressure on the common path), so it stays out of line.
- **X-Mod shape.** The per-sample X-Mod shape update now recomputes VCO 1's amounts only (`osc_set_shape1_hot`).
- **Fold-free note clamps** (Task 7 review). `osc_log2_inc` and `mods_fm1` clamp `note + 0x3C0000` instead of loading
  the never-written `osc_note_lo_neg` / `mods_note_lo_neg`. That removes the loads per sample, makes `mods_fm1` 32-bit
  only and puts no ROM-window constant in the code: the device build's check passes, and the harness disassembly
  was checked too.
- **Filter gains on a new note** (Task 7 review). `synth_voice_on` sets `g0 = coef` after its `voice_control`, so a new
  note no longer ramps its filter's lp / hp / bp gains from the voice's previous note.
- **Tests.** All unit and host tests pass, and the emulator checksums are unchanged.

### What would have to change (for João: design decisions, not done here)

Neither voice count is close to the gate. Even 4 voices on the worst case need about 2.4× (C3..C5) and 3.3× (high
chord) less. Fixed-point tuning of the same algorithms will not close that gap. These options change the sound or the
spec, so they are listed for a decision and not applied:

1. **X-Mod at a reduced rate.** Today `mods_fm1`, VCO 1 shape / PW and `svf_coef_fast` run every sample. Running them
   every 2 or 4 samples, with interpolation, would cut the largest part of `voice_render` in the worst case. The spec
   says "audio-rate X-Mod".
2. **A cheaper large-state SVF path.** 74 % of the worst case's samples take the 64-bit exact path. A 32-bit path
   proven for |state| < 2^30, or a float SVF (`-mfprev1`: float add and multiply cost 2 hw cycles), would need new
   bounds and tests, and the output would no longer be bit-identical to today's.
3. **Hard sync at high pitch.** Above about 4 kHz VCO 2, the band-limited sync could use a simpler correction (one
   BLEP per restart). This is the 1.17 M on the high chord, and it changes the sound there.
4. **Envelopes and VCA at control rate**, with per-sample linear ramps (docs/RULES.md suggests it). They cost about
   0.25 M per 6-voice block before inlining. This changes the output.
5. **Structure.** Render each voice's 16-frame period with its state held in locals, not loaded from the struct every
   sample.
6. **Clock.** If the SMK-37 really runs at 360 MHz, every figure above shrinks by 1/3. Even then, 4 voices on the worst
   case are 80-109 %.
7. **The worst case itself.** Every destination at full is a stress patch. A gate on a "loud but musical" patch would
   be a spec change.

### Not modelled

- fm1-emu has no cache model. The harness runs from SRAM (all hits), while on the device the synth runs from the
  cached XIP flash, so real figures can only be higher.
- The interrupt's own overhead (the queue drain, the platform's mixing, USB and key-scan preemption) is not included.
- The cycle models are estimates from hardware probes. The instruction counts are exact.

## Flash, RAM, pool (device build, Pro)

| build | image (of 619,520) | RAM .data+.bss (of 98,304) | pool (of 344,064) |
|---|---|---|---|
| `099167b` (before) | 98,384 B | 23,684 B | 47,092 B |
| `f4b216b` (after the pass) | 100,800 B (+2,416: the inlined per-sample path) | 23,684 B | 47,092 B |

`sizeof(synth_t)`: 2,184 B with 6 voices, 1,672 B with 4.
