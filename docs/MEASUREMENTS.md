# Measurements

## Phase-1 CPU gate after the performance push (Task 11, second pass): NVOICE not decided, the CPU guard on

**The gate** (João, replacing "every destination at full"): the **musical stress patch** at 6 voices must take less
than 50 % of the 256-frame block (5.805 ms) at 240 MHz in **both** cycle models. If it does, `NVOICE = 6`; if not but
4 voices pass, `NVOICE = 4`; otherwise report. The musical stress patch is played as two chords:
- C3 G3 C4 E4 G4 C5 (`tests/scripts/musical.txt`, harness PATCH 3);
- C5 G5 C6 E6 G6 C7 (`musical_high.txt`, PATCH 4).

The extreme patches (every destination at full, below) stay measured, and the CPU guard keeps them from overrunning.

**Result: neither voice count passes at 240 MHz.** The gate is met only by the emu model with 4 voices; the hw
model is over, so the decision goes back to João.

| musical stress | voices | 240 MHz, emu | 240 MHz, hw | 360 MHz, emu | 360 MHz, hw |
|---|---|---|---|---|---|
| C3..C5 | 6 | 55.5 % | 79.9 % | 37.0 % | 53.2 % |
| C5..C7 | 6 | 63.8 % | 91.0 % | 42.6 % | 60.7 % |
| C3..C5 | 4 | 38.2 % | 54.9 % | 25.5 % | 36.6 % |
| C5..C7 | 4 | 44.9 % | 63.9 % | 29.9 % | 42.6 % |

- At 360 MHz, 4 voices pass in both models and 6 voices pass in the emu model only.
- `NVOICE` stays 6. Whatever the clock turns out to be, the CPU guard keeps the audio interrupt from overrunning:
  it sheds voices when a block takes more than 80 % (see "CPU guard" below).

### Patches

- **Musical stress** (`tests/scripts/musical.txt`):
  - mixer at ~75 % (all four at 95);
  - VCO 1 pulse (PW 90) + VCO 2 pulse (PW 160), sync on;
  - FILTER FREQ 90, RESONANCE 201 (~100/127 of the 0..255 range), FENV AMOUNT 76 (~60 %) with FENV SUSTAIN 40;
  - X-Mod VCO 2 at 64 (~50 %) into VCO 1 pitch and the filter frequency;
  - the LFO at ~20 Hz (FREQUENCY 173), amount 128, into VCO 1 pitch and the filter;
  - aftertouch (pressure 100) at 64 into the filter;
  - velocity 100.
- **Extreme**: `worst.txt`, as in the first pass below, on C3..C5 and on notes 120..127.
- **Init chord**: `program_init`, C4..G5.

### Cycles per 256-frame block (CI run 44015858853; identical to the local runs)

The 240 MHz and 360 MHz columns are % of the block (emu / hw).

| patch | voices | insns | emu cycles | hw cycles | 240 MHz | 360 MHz |
|---|---|---|---|---|---|---|
| init chord | 6 | 449,920 | 638,059 | 902,438 | 45.8 / 64.8 | 30.5 / 43.2 |
| musical C3..C5 | 6 | 622,581 | 773,843 | 1,112,808 | 55.5 / 79.9 | 37.0 / 53.2 |
| musical C5..C7 | 6 | 688,674 | 889,222 | 1,268,209 | 63.8 / 91.0 | 42.6 / 60.7 |
| extreme C3..C5 | 6 | 922,580 | 1,203,942 | 1,712,765 | 86.4 / 122.9 | 57.6 / 82.0 |
| extreme 120..127 | 6 | 1,056,051 | 1,376,403 | 1,948,252 | 98.8 / 139.8 | 65.9 / 93.2 |
| init chord | 4 | 307,469 | 438,716 | 620,903 | 31.5 / 44.6 | 21.0 / 29.7 |
| musical C3..C5 | 4 | 424,352 | 532,395 | 765,363 | 38.2 / 54.9 | 25.5 / 36.6 |
| musical C5..C7 | 4 | 477,920 | 625,116 | 890,430 | 44.9 / 63.9 | 29.9 / 42.6 |
| extreme C3..C5 | 4 | 648,252 | 854,035 | 1,212,629 | 61.3 / 87.0 | 40.9 / 58.0 |
| extreme 120..127 | 4 | 713,090 | 931,120 | 1,318,482 | 66.8 / 94.6 | 44.6 / 63.1 |

How the extreme cases moved (6 voices, hw cycles, against the first pass):
- C3..C5: 2.40 M → 1.71 M;
- high chord: 3.45 M → 1.95 M.

How the musical patch moved (6 voices, hw cycles, C5..C7), step by step on the same harness:

| step | hw cycles |
|---|---|
| float SVF, control-rate envelopes, X-Mod / 2, two passes (first build) | 1.44 M |
| struct copies removed (they were byte-loop `memcpy` calls) | 1.30 M |
| F-FREQ-only coefficient path, branch-free clamps in `mods_fm1` / `svf_gk` | 1.31 M |
| branch-free VCA clamp (final) | 1.27 M |

The specialised loops and the division by 3 as a multiply came in between; the sync change affects only VCO 2 above
4 kHz, so the extreme high chord, not this patch.

Two attempts were measured slower and reverted:
- a branch-free soft clip and drive (more instructions than the branches they replaced);
- forcing the slow integer SVF inline (in the first pass).

What it still costs (musical C5..C7, 6 voices, hw cycles; harness ablations, removing one feature at a time):
- base pipeline: 0.95 M. That is two pulse oscillators, sub and noise, the mixer, the float SVF, the VCA and the
  control rate;
- X-Mod into VCO 1 pitch: 0.15 M;
- X-Mod into the filter: 0.08 M;
- pulse instead of saw: 0.12 M;
- sync: 0.06 M.

Hot spots (hw cycles per block):
- `voice_render`: 0.87 M. About 50/50 between the oscillator pass and the filter pass; loads, stores and spills are
  ~30 % of all cycles, taken branches and calls ~25 %.
- `osc_vco1_sync`: 0.09 M. `osc_blep`: 0.06 M.
- Control rate (`voice_control`, `mods_sum`, `osc_exp2`, `svf_coef_fast`): ~0.11 M.
- `synth_render`, the bus and limiter: 0.04 M.
- The harness's own buffer clearing and checksum (`main`): 0.012 M.

### What changed (applied from the first pass's list, plus the guard)

1. **X-Mod every 2 samples.** VCO 1 pitch, shape and PW, and the filter coefficients, are computed on even frames of
   the control period. The frame between gets the targets themselves, and the even frame gets the mean of the
   previous and new targets: a linear interpolation, one frame (23 µs) late. With only F FREQ modulated, only g, k,
   a1, a2 and the BP gain are recomputed.
   - Test: `xmod_filter_audio_rate` checks the interpolated targets frame by frame (error 1.2e-7).
2. **Float SVF** on the FPU. The app is built with `-mcpu=r3 -mfprev1 -ffp-contract=off` (tools/build.py `APP_FPU`,
   the app only); every float operation is a native instruction, with no library call. The drive polynomial, soft
   clips, cutoff and RESONANCE maps, and damping floor are unchanged, and 1 / (1 + g(g + k)) is one FPU division.
   - It is measured cheaper: the integer path took the 64-bit fallback on 74 % of the overdriven samples.
   - Tests: every filter test passes (responses, no self-oscillation, overdrive curve, decay to zero, cutoff
     smoothness). The bit-exact fast/slow test became `extreme_states`: bounded and finite for states up to the old
     int32 range.
3. **Sync above 4 kHz VCO 2:** one BLEP per restart (`osc_vco1_sync_fast`). VCO 1's own wrap and pulse edges inside
   the window, and the sub under sync, go uncorrected up there. Worst alias (`test_osc sync_alias_high`), ours vs
   naive sync:

   | VCO 2 | VCO 1 +7 st | VCO 1 +19.3 st |
   |---|---|---|
   | 4.3 kHz | −32.9 vs −10.8 dB | −11.5 vs −6.1 dB |
   | 6.3 kHz | −18.7 vs −10.7 dB | −9.4 vs −6.3 dB |
   | 8.4 kHz | −9.5 vs −4.6 dB | −7.0 vs −6.9 dB |

   Below 4 kHz nothing changed: `sync_alias` and `bspline_exact` pass as before. `bspline_exact` skips its 7 synced
   cases above 4 kHz.
4. **Envelopes and VCA at control rate.** `adsr_tick16` runs 16 RC steps as one (1 − c^16 is precomputed in
   `adsr_rates`), with a per-sample linear ramp clamped to 0..full.
   - Timing: `adsr_times16` puts all 128 knob positions within 5 % (worst 2.3 %). The per-sample model's
     `adsr_times` is unchanged.
   - The 2 ms minimum gate (2.20 ms) and the click/zipper tests (`params_smoothed_no_step`, `amp_mod_fades`) pass.
   - A note-on between ticks starts its attack at the next tick (≤ 0.36 ms). On the device, events are applied at
     block starts, which are tick starts.
5. **Structure.** `voice_render` now runs two passes over the period:
   1. oscillators, VCO 1 X-Mod and the mixer;
   2. filter, VCA and pan.

   It has no struct copies (a struct assignment is a call to `libc.c`'s byte-loop `memcpy`). Each pass is
   specialised by constant arguments (which X-Mod paths, even or odd frame), so the loops do not test the patch's
   switches per frame.
6. **The CPU guard** (`synth_guard`, called from `eng_render`). `fm1_ticks()` times `synth_render`; on the host, a
   fake clock does.
   - When a block takes more than 80 % of its time, the voice limit drops by one. The voices over the limit are shed
     (the oldest released first, then the oldest held), each fading out in 5 ms like a steal. It does not shed again
     while they fade.
   - After ~1 s (172 blocks) under 55 %, one voice comes back.
   - New notes and unison stacks respect the limit.
   - Test: `test_render cpu_guard_sheds`. At 120 % for six voices it sheds to four, with no output step beyond the
     steady sound's, and recovers to six.

### What it sounds like now

These are the differences against the first pass, by ear-relevant effect; not yet listened to (Task 12):
- **X-Mod** is sampled at 22 kHz with interpolation, so its very top sidebands differ slightly.
- **The filter's output** is not bit-identical (float). The responses are within the existing test tolerances.
- **Envelopes** step every 0.36 ms, with linear segments between steps.
- **Hard sync with VCO 2 above 4 kHz** aliases more: the figures are in item 3 above.
- **When the guard sheds**, voices drop out with a 5 ms fade, as a voice steal does.

Host yardstick (`tests/cpu_host.sh`, x86-64 instructions per sample):

| patch | first pass | now |
|---|---|---|
| init chord | 2110 | 1805 |
| musical | — | 2749 |
| worst | 4900 | 4092 |

### Flash / RAM (device build, Pro, CI)

| | first pass | now |
|---|---|---|
| image (of 619,520) | 100,800 B | 116,448 B (+15,648: the specialised passes and the FPU path) |
| RAM .data+.bss | 23,684 B | 24,100 B |
| pool | 47,092 B | 47,092 B |
| `sizeof(synth_t)`, 6 voices | 2,184 B | 2,600 B |
| `sizeof(synth_t)`, 4 voices | 1,672 B | 1,968 B |

### Concerns

- **No cache model.** fm1-emu does not model a cache: the harness runs from SRAM, while on the device the synth runs
  from cached XIP flash.
- **The interrupt's own costs are not included:** the queue drain, the fade, USB and the key scan.
- **The CPU guard relies on `fm1_ticks()`** (TIMER4, 24 per µs) being readable in the audio interrupt.
- **The app is now built with `-mcpu=r3 -mfprev1`.** The device build and every host test pass in CI, but nothing
  has run on a keyboard.

The first pass's record follows.

## Phase-1 CPU gate (Task 11, first pass): result BLOCKED, NVOICE left at 6

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
