# KNOBI-SIX — Phase 1 (the voice) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A standalone SMK-37 Pro firmware that plays the complete OB-6-schema voice (VCO 1/2, mixer, filter, two envelopes, LFO, X-Mod, aftertouch, misc, unison/portamento/hold), stores programs, speaks USB MIDI, and shows the OB-6-panel-style UI — tested on the Mac and measured in the emulator, never flashed in this phase.

**Architecture:** A private repo `knobi-six` started from `fc79/smk37-base`. The base is a unity build: `firmware/src/app.c` `#include`s `platform.c`; our modules are `.c` files included from `app.c` in the order listed below, so each is also compilable on its own by the host test harness. DSP lives in `firmware/src/synth/`, the program model in `firmware/src/params/`, controls and screens in `firmware/src/ui/`. `app_audio()` renders control-rate updates every 16 frames and audio per frame into the stereo Q15 buffer.

**Tech Stack:** C (JieLi pi32v2 clang 4.0.1, `-Os`), the base's host runner (`tests/run_host.sh`: scripted input → PNG screens + WAV + `expect` checks), C unit tests on the host (`cc`), Python 3 + Pillow, GitHub Actions with JieLi's Linux toolchain and the AC79 SDK, `joakimeriksson/fm1-emu` for cycles.

**Spec:** `docs/superpowers/specs/2026-11-16-standalone-firmware-design.md` (binding) and `docs/ob6-reference.md` (the synth schema; section numbers below refer to it). Read both before any task.

## Global Constraints

- Follow the OB-6 schema in `docs/ob6-reference.md`; the 9 rules in the spec's "Source of truth" section are requirements.
- Own names only: "KNOBI-SIX", no Oberheim / OB-6 / Sequential names, logos or factory patches in code, UI or presets.
- Chip rules (base `docs/RULES.md`): no `double`; no `float` unless a module enables the FPU via `-mfprev1` in `tools/build.py` **and** its cycle cost is measured lower; no 64-bit division (`/`, `%` on 64-bit); no ROM-looking constants; no raw registers outside `hal/`. App built with `-Os` (do not change).
- Audio: `app_audio(int32_t *out, uint32_t n)`, n = 256 stereo frames (`L R L R`) at 44.1 kHz, `out` already zero, **add** into it, full scale ±32767. Worst-case render **< 50 % of 5.8 ms** (Task 11 gate). Control-rate work every 16 frames.
- ISR ↔ main loop through `volatile` single-writer variables only; no flash, screen or `platform_poll()` in the ISR.
- Identity: `APP_NAME "KNOBI-SIX"`, own `STORE_MAGIC`, `DATA_FAMILY`, an unused `IDENT 9NN`, own package names.
- Encoders: K1 `ENC_MASTER` stays master volume (platform default). K2..K8 (`ENC_SELECT`, `ENC_ALGORITHM`, `ENC_PRESETS`, `ENC_KNOB1..4`) are the screen's knobs.
- Pads: `smk_pad_events = 1`. Top row pads 0..7 = switches/digits; bottom row pads 8..15 = pressure only (highest `fm1_in.pad_press[8..15]` = aftertouch), never a switch.
- Voices: `NVOICE` compile-time, 6 target / 4 fallback (Task 11 rules).
- Nothing is installed on a keyboard. Pushing to GitHub only to the private repo `jhortale/knobi-six`, after João OKs creating it (Task 0).
- Commit messages end with a blank line and `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **Worst-case patch** (6 voices, max mixer levels, VCO2 → VCO1 X-Mod at max, sync on, max resonance, LFO at 500 Hz on all destinations): no int32 overflow, output soft-limited not wrapped — test `voice_worst_case_bounded` in Task 7.
2. **Knob/fader moves while notes sound** (cutoff, mode, shape, PW, levels): no zipper or click — test `params_smoothed_no_step` in Task 7.
3. **Program change or WRITE while notes are held** (incl. HOLD drones): no stuck notes, no click beyond the deliberate fade — host scripts `prog_change_held.txt`, `write_held.txt` in Task 10.
4. **Bottom-pad pressure while a top pad is toggled**: the top pad toggles exactly once, aftertouch follows only bottom pads — host script `pads_split.txt` in Task 8.
5. **Unison/key-mode edge cases** (last-note priority with retrigger, releasing the newest of several held keys, portamento between legato notes) — tests in Task 6.

---

### Task 0: Repo, identity, docs, CI

**Files:** new repo `~/Dev/audio/knobi-six` (clone of `fc79/smk37-base` with history); Modify `firmware/src/app.c:18-19` (`APP_NAME`, `STORE_MAGIC`), `tools/build.py` (`IDENT`, `DATA_FAMILY`, package name), `.github/workflows/release.yml` (package names), `README.md`, `LICENSING.md` (credit chain kept); Create `CLAUDE.md`, `docs/` copies of the spec, `ob6-reference.md`, `design/standalone-ui/*.html`, this plan; Create `.github/workflows/build.yml` (device build, artifacts).

**Interfaces:** Produces the build commands later tasks use: `tests/run_host.sh SCRIPT`, `make -C tests unit` (Task 1 creates it), `.github/workflows/build.yml` (dispatch + push), artifact `knobi-six-pro.fwsc` and `build/app.map`.

- [ ] **Step 1:** Ask João to OK creating private GitHub repo `jhortale/knobi-six`. Then clone base: `git clone https://github.com/fc79/smk37-base ~/Dev/audio/knobi-six`, `git remote rename origin upstream`, create the private repo, add `origin`, branch `phase1`.
- [ ] **Step 2:** Pick `IDENT`: list idents in `fmhub.json` and on thefirmwarehub.com, choose an unused `9NN`, record the check in `docs/IDENTITY.md`. Set `APP_NAME "KNOBI-SIX"`, `STORE_MAGIC 0x36424E4Bu` ("KNB6"), `DATA_FAMILY` and package name `knobi-six`.
- [ ] **Step 3:** `.github/workflows/build.yml` on `ubuntu-24.04`: install JieLi Linux toolchain and clone the AC79 SDK as `zvenson/fm1-build` does, run `./build.sh` (Pro), `sh tests/run_tests.sh`, upload `build/*.fwsc`, `build/app.map`, `build/app.dis`.
- [ ] **Step 4: Verify** `tests/run_host.sh tests/scripts/diag.txt` passes on the Mac; CI run green with the renamed package; the package's identity string shows `KNOBI-SIX`.
- [ ] **Step 5:** `CLAUDE.md`: commands, "never install on the keyboard without João", where modules live, the spec + reference are binding. Commit `chore: KNOBI-SIX identity on smk37-base`.

### Task 1: Program model (parameter table + record)

**Files:** Create `firmware/src/params/params.h`, `params.c`; Create `tests/unit/test_params.c`, `tests/unit/Makefile` (target `unit` builds every `tests/unit/test_*.c` with `cc -std=c11 -O2 -Wall -Werror -DDINGHY_HOST -Ifirmware/src -Ifirmware/hal` and runs them; `tests/run_tests.sh` calls it).

**Interfaces — Produces:**
```c
enum param_kind { PK_KNOB, PK_SWITCH, PK_SELECT };
typedef struct { uint16_t id; uint8_t section, kind, phase, bipolar; int16_t min, max, def; const char *name; const char *const *labels; } param_t;
enum param_id { P_VCO1_FREQ, P_VCO1_SHAPE, P_VCO1_PW, P_VCO1_SYNC, P_VCO2_FREQ, P_VCO2_FINE, P_VCO2_SHAPE, P_VCO2_PW, P_VCO2_LOWFREQ, P_VCO2_KEYBOARD,
  P_MIX_VCO1, P_MIX_VCO2, P_MIX_SUB, P_MIX_NOISE, P_FLT_FREQ, P_FLT_RESO, P_FLT_MODE, P_FLT_BP, P_FLT_TRACK, P_FLT_ENV_AMT, P_FLT_VEL,
  P_FENV_A, P_FENV_D, P_FENV_S, P_FENV_R, P_LENV_A, P_LENV_D, P_LENV_S, P_LENV_R, P_LENV_AMT, P_LENV_VEL,
  P_LFO_FREQ, P_LFO_SYNC, P_LFO_SHAPE, P_LFO_AMT, P_LFO_D_VCO1, P_LFO_D_VCO2, P_LFO_D_PW, P_LFO_D_AMP, P_LFO_D_FFREQ, P_LFO_D_FMODE,
  P_XM_FENV, P_XM_VCO2, P_XM_D_VCO1, P_XM_D_SHAPE1, P_XM_D_PW1, P_XM_D_FFREQ, P_XM_D_FMODE, P_XM_D_BP,
  P_AT_AMT, P_AT_D_VCO1, P_AT_D_VCO2, P_AT_D_LFOAMT, P_AT_D_AMP, P_AT_D_FFREQ, P_AT_D_FMODE,
  P_DETUNE, P_PAN_SPREAD, P_BEND_RANGE, P_KEY_MODE, P_PRG_VOL, P_PORTA_ON, P_PORTA_MODE, P_PORTA_RATE, P_UNISON_ON, P_UNISON_VOICES,
  /* phase 2/3 ids reserved after this line: distortion, FX A/B, clock, arp, sequencer */ P_COUNT_PHASE1 };
extern const param_t PARAMS[P_COUNT_PHASE1];
typedef struct { uint8_t version; char name[20]; int16_t v[96]; uint8_t chord[6]; uint16_t crc; } program_t; /* v[] sized for phases 2-3 */
void program_init(program_t *p);                          /* every value = its default, name "INIT" */
int  program_pack(const program_t *p, uint8_t *buf, uint32_t max);   /* bytes written or -1 */
int  program_unpack(program_t *p, const uint8_t *buf, uint32_t n);  /* 0 ok, -1 bad crc/version */
int16_t param_clamp(uint16_t id, int32_t value);
```
Ranges come from `docs/ob6-reference.md` §2 (e.g. VCO FREQ 0..60 semitone steps, VCO2 FINE −50..+50, LFO SHAPE 0..4, FLT TRACK 0..2, BEND 0..12, PORTA MODE 0..3, UNISON VOICES per §6, KEY MODE 0..5); continuous knobs without a stated range use 0..255.

- [ ] **Step 1: Failing tests** `test_params.c`: `ids_dense` (PARAMS[i].id == i for all); `defaults_in_range`; `bipolar_set` exactly {P_VCO2_FINE, P_FLT_ENV_AMT, P_AT_AMT, P_XM_FENV, P_XM_VCO2}; `lfo_dests_are_switches`; `pack_roundtrip` (random values → pack → unpack equal, name kept); `unpack_rejects_corrupt` (flipped byte → −1); `packed_size <= 256`.
- [ ] **Step 2:** `make -C tests unit` → fails (no symbols).
- [ ] **Step 3:** Implement the table (names in the panel's words), CRC-16, packing.
- [ ] **Step 4:** pass. **Step 5:** commit `feat: OB-6-schema program model`.

### Task 2: Oscillators (VCO 1, VCO 2, sub, noise, sync)

**Files:** Create `firmware/src/synth/osc.h`, `osc.c` (start from `analog-engine`'s `web/synth/analog.c` PolyBLEP code: `blep`, `blep_side`, `blep_near`, `saw`); Test `tests/unit/test_osc.c`.

**Interfaces — Produces:**
```c
typedef struct { uint32_t ph1, ph2, inc1, inc2; int16_t shape1, pw1, shape2, pw2; uint8_t sync; uint32_t noise; } osc_t;
void osc_set_pitch(osc_t *o, int32_t note1_q16, int32_t note2_q16);   /* fractional MIDI notes, Q16 */
void osc_set_shape(osc_t *o, int shape1, int pw1, int shape2, int pw2); /* param units from PARAMS */
/* one frame: returns VCO1, VCO2, SUB, NOISE in Q15 via out[4]; vco2_out also returned for X-Mod */
void osc_tick(osc_t *o, int32_t out[4]);
int32_t note_to_inc(int32_t note_q16);     /* phase increment, no 64-bit division */
```
VCO 1 shape 0..max morphs saw → pulse; VCO 2 shape morphs triangle → saw → pulse (centre = saw); PW centre = square, thinning symmetrically toward both ends; sub = square one octave below VCO 1; noise = per-voice xorshift white; sync: VCO 1 phase reset on VCO 2 wrap, with a BLEP at the reset; LOW FREQ and KEYBOARD are applied by the voice (Task 6) through the notes passed in.

- [ ] **Step 1: Failing tests:** `pitch_a4` (440 Hz ±0.5 % both VCOs); `vco1_shape_ends` (shape 0 = saw spectrum: 2nd harmonic −6 dB ±1; max = pulse: even harmonics < −30 dB at PW centre); `vco2_shape_points` (0 = triangle: 3rd harmonic −19 dB ±1.5; centre = saw; max = pulse); `pw_symmetric` (PW centre ± d gives equal harmonic magnitudes); `sub_octave` (fundamental at f/2); `sync_locks_to_vco2` (VCO1 period = VCO2 period when sync on, VCO1 detuned up 7 semitones); `alias_saw_c8` (alias at 18984 Hz ≥ 30 dB under the fundamental, as in ANALOG); `noise_flat` (mean ≈ 0, spectrum within ±3 dB over 4 octave bands).
- [ ] **Step 2:** fail. **Step 3:** implement. **Step 4:** pass. **Step 5:** commit `feat: OB-6 oscillator section`.

### Task 3: Filter

**Files:** Create `firmware/src/synth/filter.h`, `filter.c` (from ANALOG's SVF fast path + exact fallback, `soft`, cutoff table); Test `tests/unit/test_filter.c`.

**Interfaces — Produces:**
```c
typedef struct { int32_t s1, s2; } svf_t;
typedef struct { int32_t g, a1, a2, k, lp, hp, bp; } svf_coef_t;
/* cutoff_q16: filter FREQ param + modulations, in param units Q16; mode 0..255 LP->notch->HP; bp 0..32768 Norm->BP */
void svf_coef(svf_coef_t *c, int32_t cutoff_q16, int reso, int mode, int32_t bp_mix);
int32_t svf_tick(svf_t *f, int32_t in_q15, const svf_coef_t *c);
```
Rules: 12 dB/oct; MODE continuous LP → notch → HP; BP switch selects the band-pass output; `bp_mix` crossfades the mode output with BP (used by the X-Mod Norm↔BP destination); **Q is capped so no self-oscillation**; soft saturation at the input (mixer overdrive) and in the band-pass path.

- [ ] **Step 1: Failing tests:** `lp_hp_notch` (as ANALOG: LP −36 dB a decade up, HP mirror, notch ≤ −20 dB at fc); `bp_switch` (peak at fc ≥ −1 dB, −12 dB per octave both sides ±2); `bp_crossfade` (bp_mix 0 → mode output, 32768 → BP output, monotone between); `no_self_oscillation` (max reso, impulse → energy after 1 s < −80 dB of the first 10 ms); `overdrive_soft` (input 4× full scale → output bounded, THD rises smoothly, no wrap); `cutoff_smooth` (step in cutoff_q16 → no sample jump > 2× steady state).
- [ ] **Step 2–5:** fail, implement, pass, commit `feat: SEM-style filter`.

### Task 4: Envelopes and VCA

**Files:** Create `firmware/src/synth/env.h`, `env.c`; Test `tests/unit/test_env.c`.

**Interfaces — Produces:**
```c
typedef struct { int32_t level; uint8_t stage; } adsr_t;           /* level Q30, 0..2^30 */
typedef struct { int32_t a_inc, d_coef, s_level, r_coef; } adsr_rates_t;
void adsr_rates(adsr_rates_t *r, int a, int d, int s, int rel);   /* param units -> per-sample rates (fm_exp2_fixed style, no tables) */
void adsr_gate(adsr_t *e, int on);
int32_t adsr_tick(adsr_t *e, const adsr_rates_t *r);              /* level Q30 */
/* VCA control = env*amt + lfo_amp + at_amp, clamped 0..ceiling (Q15); rule 3 */
int32_t vca_control(int32_t env_q30, int lenv_amt, int vel_on, int vel, int32_t lfo_q15, int32_t at_q15);
/* filter env contribution in cutoff units Q16; amount bipolar; velocity scales it when vel_on (rule 2) */
int32_t fenv_mod(int32_t env_q30, int fenv_amt, int vel_on, int vel);
```
Stages exponential (RC-like) attack/decay/release; ranges per reference §2.5.

- [ ] **Step 1: Failing tests:** `fenv_bipolar` (amt −max → cutoff offset negative, equal magnitude to +max); `lenv_unipolar` (lenv amt has no negative range; amt 0 → VCA 0 with no LFO/AT); `vca_sum_clamp` (lenv amt 0 + LFO square to AMP → gated non-zero sound; lenv amt max + AT to AMP → no increase over ceiling); `velocity_scales_amounts_only` (vel_on off → output independent of velocity; on → scales amount, not timing); `adsr_times` (attack/decay/release times match the reference ranges ±5 % at min, mid, max); `release_reaches_zero`.
- [ ] **Step 2–5:** fail, implement, pass, commit `feat: filter and loudness envelopes, additive VCA`.

### Task 5: LFO, X-Mod, aftertouch, mod wheel

**Files:** Create `firmware/src/synth/mod.h`, `mod.c`; Test `tests/unit/test_mod.c`.

**Interfaces — Produces:**
```c
typedef struct { uint32_t ph, inc, rnd; int32_t held; uint8_t keyed; } lfo_t;    /* one shared LFO */
void lfo_set(lfo_t *l, int freq_param, int synced, uint32_t clock_inc);      /* 0.022..500 Hz */
void lfo_key(lfo_t *l, int first_key_down);                                   /* reset on first key only */
int32_t lfo_tick(lfo_t *l, int shape, uint32_t frames);                       /* Q15; sine/random bipolar, saw/rev saw/square >= 0; random at max rate = noise */
/* total depth: initial amount + mod wheel + (aftertouch if AT->LFO AMT), Q15; negative AT amount inverts */
int32_t lfo_depth(int initial, int wheel_0_127, int32_t at_q15, int at_to_lfoamt);
typedef struct { int32_t vco1_pitch, vco1_shape, vco1_pw, flt_freq, flt_mode, bp_mix, vco2_pitch, pw_all, amp; } mods_t;  /* offsets in param units Q16 */
/* sum every enabled destination for one voice from LFO, X-Mod (fenv, vco2 sample) and aftertouch; rules 4, 6, 7 */
void mods_sum(mods_t *m, const int16_t *v /* program values */, int32_t lfo_q15, int32_t depth_q15,
              int32_t fenv_q30, int32_t vco2_q15, int32_t at_q15);
```

- [ ] **Step 1: Failing tests:** `lfo_range` (min ≈ 0.022 Hz, max ≈ 500 Hz ±2 %); `lfo_polarity` (sine/random cross zero; saw, rev saw, square never < 0); `lfo_random_max_is_noise` (max rate random: flat spectrum ±3 dB); `lfo_key_reset_first_only`; `wheel_only_adds_depth` (wheel changes nothing when every LFO dest is off; adds depth when one is on); `at_negative_inverts_lfo`; `xmod_sources_share_dests` (enabling VCO1 dest routes both fenv and VCO2 scaled by their own signed amounts); `xmod_bp_dest` (Norm↔BP crossfade moves 0→1 with X-Mod fenv); `pw_dest_4state` (off / VCO1 / VCO2 / both per reference).
- [ ] **Step 2–5:** fail, implement, pass, commit `feat: LFO, X-Mod, aftertouch routing`.

### Task 6: Voices, unison, key modes, portamento, detune, pan

**Files:** Create `firmware/src/synth/voice.h`, `voice.c` (one voice: osc + filter + 2 env + mods), `voices.h`, `voices.c` (allocation, unison, key modes, portamento, hold, bend, detune slop, pan); Test `tests/unit/test_voices.c`.

**Interfaces — Consumes:** Tasks 1–5. **Produces:**
```c
#ifndef NVOICE
#define NVOICE 6
#endif
typedef struct synth synth_t;                       /* whole synth state, no pointers */
void synth_init(synth_t *s);
void synth_program(synth_t *s, const program_t *p); /* load all values, snap smoothing */
void synth_param(synth_t *s, uint16_t id, int16_t v); /* live edit, smoothed */
void synth_note(synth_t *s, int note, int vel);     /* vel 0 = off */
void synth_hold(synth_t *s, int on);
void synth_bend(synth_t *s, int bend14);  void synth_wheel(synth_t *s, int v); void synth_pressure(synth_t *s, int v);
void synth_panic(synth_t *s);
void synth_render(synth_t *s, int32_t *lr, uint32_t frames);   /* adds into L R pairs, Q15 */
int  synth_active(const synth_t *s);
```
Behaviour per reference §6: poly allocation (released first, then oldest); unison = N stacked voices or chord memory, key modes low/high/last ± retrigger only in unison; portamento 4 modes; bend range per program; detune = random per-voice slop; pan spread alternating L/R (odd unison count centres one); HOLD latches; program volume.

- [ ] **Step 1: Failing tests:** `poly_six` (7th note steals the oldest released, else oldest); `unison_stack` (N voices on one note, detune spread); `key_mode_last_retrig` and `key_mode_last_legato` (releasing newest returns to previous held note; retrigger restarts envelopes only in retrig modes); `key_mode_low_high`; `porta_modes` (glide time per mode; legato-only mode glides only between overlapping notes); `bend_range_program` (range 12 → +12 semitones); `pan_spread_alternates`; `hold_latches_and_releases`; `panic_silences`. Review Focus 1: `voice_worst_case_bounded` lives in Task 7.
- [ ] **Step 2–5:** fail, implement, pass, commit `feat: voice manager`.

### Task 7: Audio integration, smoothing, level and the first CPU numbers

**Files:** Modify `firmware/src/app.c` (replace the base's sine: `#include` the synth modules, `app_audio` → `synth_render`, notes from keys/MIDI → `synth_note`); Test `tests/unit/test_render.c`; Create `tests/scripts/chord.txt`, `worst.txt`.

- [ ] **Step 1: Failing tests:** `voice_worst_case_bounded` (Review Focus 1 patch: 6 notes 10 s → no sample outside ±32767 before the platform clip, no wrap); `params_smoothed_no_step` (Review Focus 2: cutoff, mode, shape, PW, mix levels jumped mid-note → max sample step ≤ 2× steady state); `silence_costs_nothing` (no voices → render leaves `out` zero).
- [ ] **Step 2:** fail. **Step 3:** wire it in; keys and MIDI notes play; K1 volume untouched.
- [ ] **Step 4:** pass; `tests/run_host.sh tests/scripts/chord.txt` makes a WAV; `DINGHY_PERF=1 tests/run_host.sh tests/scripts/worst.txt` prints instructions per sample — record it (yardstick in base: 1000–1500 fits).
- [ ] **Step 5:** commit `feat: KNOBI-SIX voice plays`.

### Task 8: Controls and MIDI mapping

**Files:** Create `firmware/src/ui/controls.h`, `controls.c` (screen → encoder/fader/pad bindings, pot modes, aftertouch from bottom pads), `firmware/src/ui/midi_map.c` (CC map in and out); Modify `app.c` main loop; Create `tests/scripts/pads_split.txt`, `faders_potmode.txt`, `midi_cc.txt`; host probes for `expect` (`param ID`, `screen`, `at`).

**Interfaces — Produces:**
```c
enum screen_id { SCR_BOOT, SCR_PLAY, SCR_VCO, SCR_MIX_FILTER, SCR_ENV, SCR_LFO_XMOD, SCR_AT_MISC, SCR_PROGRAM, SCR_GLOBALS, SCR_COUNT };
typedef struct { uint16_t enc[7]; uint16_t fader[4]; uint16_t pad_top[8]; } binding_t;  /* param ids or 0xFFFF; pad_top may be actions */
extern const binding_t BINDINGS[SCR_COUNT];
void controls_poll(synth_t *s, program_t *edit, int *screen);   /* main loop: encoders K2..K8, faders, pads, buttons */
int  controls_aftertouch(void);                                /* highest bottom-row pressure 0..127 */
```
Bindings follow the approved screens (`docs/design/standalone-ui/sections-v2.html`) with at most 7 encoder knobs; button map documented in `docs/CONTROLS.md` (builder chooses within the spec's principles, e.g. `<`/`>` step sections, PATCH = Play, PARA = Edit, GLOBE = Globals, SCALE switches envelope rows). Fader pot mode relative / passthru / jump (global). CC map: one CC per continuous parameter, documented in `docs/MIDI.md`; param transmit/receive globals.

- [ ] **Step 1: Failing host scripts:** `pads_split.txt` (Review Focus 4: hold bottom pad 10 at pressure 100, tap top pad 2 → `expect 1 param P_LFO_D_VCO1` once, `expect 100 at`; top pad pressure never reaches `at`); `faders_potmode.txt` (passthru: no change until the fader crosses the stored value; jump: immediate); `midi_cc.txt` (CC for FILTER FREQ sets the param and is echoed when transmit on).
- [ ] **Step 2–5:** fail, implement, pass, commit `feat: panel and MIDI control`.

### Task 9: Screens (OB-6-panel visual language)

**Files:** Create `firmware/src/ui/panel_gfx.c` (blue rule + title, knob with white dot at an angle, small label, red 7-segment digits, red LED, lit button, wood cheeks, outlined wordmark), `firmware/src/ui/screens.c` (one draw function per `screen_id`, redraw only what changed); optional small font in `firmware/gen/` if labels need it; Create `tests/scripts/screens.txt` (visits every screen, `shot` each).

**Interfaces:** `void screen_draw(int screen, const program_t *p, const synth_t *s, int full);`

- [ ] **Step 1:** `screens.txt` with `shot` on every screen and `expect` on `screen` ids → fails (screens missing).
- [ ] **Step 2:** implement the primitives and every screen per the mockups as a style guide on 240×240 (Boot, Play, VCO 1+2, Mixer+Filter, Envelopes, LFO+X-Mod, Aftertouch+Misc, Program, Globals).
- [ ] **Step 3:** run → PNGs in `build/host/`; check every label fits, nothing overlaps, every on-screen knob is bound in `BINDINGS`.
- [ ] **Step 4:** send the PNGs to João for review (he signs off the look); fix what he asks.
- [ ] **Step 5:** commit `feat: panel-style screens`.

### Task 10: Programs and globals

**Files:** Create `firmware/src/params/store_prog.c` (programs in slots: count = slot bytes / packed size, ≥ 100; bank/tens/ones addressing; WRITE; MANUAL; last program), `firmware/src/params/globals.c`; Create `tests/unit/test_store.c`, `tests/scripts/prog_change_held.txt`, `write_held.txt`.

**Interfaces — Produces:** `int prog_load(uint32_t n, program_t *p); int prog_save(uint32_t n, const program_t *p); uint32_t prog_count(void); void globals_load(globals_t *g); void globals_save(const globals_t *g);` WRITE uses the base's `store_sliced` if Step 3's test shows no audible break, else fade-out → save → fade-in (≤ 50 ms total). Globals per spec (transpose, master tune, MIDI channel, param xmit/rcv, local control, pot mode, velocity curve, AT curve, stereo/mono, sustain polarity).

- [ ] **Step 1: Failing tests:** `store_roundtrip_all_slots`; `count_at_least_100`; `bad_crc_loads_init`; scripts `prog_change_held.txt` (Review Focus 3: hold a chord, change program → no stuck voice, envelope continues or releases cleanly) and `write_held.txt` (WRITE during a HOLD drone → WAV shows at most the deliberate fade, program stored).
- [ ] **Step 2–5:** fail, implement, pass, commit `feat: programs and globals`.

### Task 11: Device build, CPU and memory gate

**Files:** Modify `.github/workflows/build.yml` (add an `fm1-emu` job, pinned commit, measuring cycles per 256-frame block for `tests/scripts/worst.txt`'s patch at 6 and 4 voices, both cycle models); Create `docs/MEASUREMENTS.md`.

- [ ] **Step 1:** CI: device build + flash/RAM/pool from the build summary + emulator cycles.
- [ ] **Step 2:** Rule: if the 6-voice worst case is < 50 % of 5.8 ms at the model's clock in **both** cycle models, `NVOICE = 6`; else profile the top hot spots and optimise once; if still over, `NVOICE = 4`. Record numbers and the decision in `docs/MEASUREMENTS.md`.
- [ ] **Step 3:** commit `perf: phase-1 CPU gate (NVOICE = N)`.

### Task 12: Starter presets and listening round

**Files:** Create `firmware/src/params/factory.c` (8–16 own starter programs: pad, brass, bass, lead, sync lead, X-Mod bell, PWM strings, noise sweep), `tests/scripts/listen_*.txt`.

- [ ] **Step 1:** Render each preset with a chord and a line script → WAVs in `build/listen/`.
- [ ] **Step 2:** Send them to João; log his notes in `docs/LISTENING.md`; adjust presets (not DSP rules) as he asks.
- [ ] **Step 3:** commit `feat: starter programs`. Phase 1 is done when João signs off sound and screens; installing on the keyboard is a separate decision after his flash backup.
