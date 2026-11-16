# Standalone OB-6-inspired firmware for the SMK-37 Pro — design

Date: 2026-11-16 · Status: ready for approval · Name: **KNOBI-SIX**

## Intent and why

João (Glocal, live duo) wants his M-VAVE SMK-37 Pro to *be* one instrument: a polyphonic synth that follows the
**OB-6's synth schema** — its voice architecture, parameters and modulation — with a UI on the keyboard's own screen
and controls that looks and works like an OB-6-style front panel. It replaces the earlier plan of adding an engine
inside M-VAVE's SMK37 OPEN firmware, which hit hard limits (sealed engine, 4 knobs per slot, flash budget of a few
hundred bytes, needs a patched copy of M-VAVE's library). A standalone firmware owns the whole chip, flash and panel.

It must be obvious at a glance which firmware is on the keyboard.

## Goals

- **Phase 1 — the voice (first playable):** the complete OB-6 voice and modulation schema (VCO 1, VCO 2, MIXER,
  FILTER, both ENVELOPES, LFO, X-MOD, AFTERTOUCH, MISC, PORTAMENTO/UNISON/HOLD/KEY MODE), programs (bank/program,
  write, manual), the globals that apply, USB MIDI, and the full panel UI. Measured CPU and flash on the chip.
- **Phase 2 — clock, arpeggiator, effects:** CLOCK (BPM, tap, divisions, MIDI clock in/out), ARPEGGIATOR, distortion,
  and the two-slot EFFECTS (A → B, reverbs in B) as far as the CPU allows — chorus, delay, phaser first.
- **Phase 3 — sequencer:** the 64-step polyphonic step sequencer stored per program.
- Each phase ends playable, tested on the Mac, and measured on the chip (emulator) before the next starts.

## Non-goals

- Anything from the SMK37 OPEN firmware (tracks, sequencer, sampler, DX7). The Open-firmware work stays as it is
  (branches `analog-engine`, `spike/device-link`).
- Copying OB-6 factory programs, its name, logo, signature, or its manual's text. Own presets, own name.
- Poly-chain, CV/gate jacks, analog calibration routines, NRPN SysEx dumps compatible with a real OB-6, alternative
  tuning tables (later, if wanted).

## Anti-goals (failure even if it "works")

- **Losing the keyboard's calibration or bricking it.** Install only after a full flash backup; unique firmware
  identity; the base's rescue path must keep working.
- **A generic synth with OB-6 labels.** The schema rules in the reference (below) are requirements, not flavour.
- **Thin/digital sound:** aliasing, clicks, zippered knobs, a filter that whistles (the OB-6's does not self-oscillate).
- **A UI that looks like any other firmware** (or like the stock one).
- Crackles: the audio interrupt overrunning its budget under a full chord with modulation.

## Source of truth for the synth: `docs/ob6-reference.md`

Extracted from the OB-6 Operation Manual v1.2 (own words, page references). **Every section of the firmware follows
it.** It lists 83 per-program parameters (41 continuous, 42 switches/selectors), the modulation matrix, effects,
arp/clock/sequencer, programs and globals. Rules a builder could easily get wrong — all binding:

1. **Filter:** 2-pole 12 dB/oct state-variable; MODE morphs continuously LP → notch → HP; **BP is a separate switch**;
   a smooth Norm↔BP crossfade exists only as an X-Mod destination; **no self-oscillation at any RESONANCE**; key
   track OFF / HALF / FULL; gain staging is not limited — high mixer levels and resonance overdrive softly.
2. **Envelopes:** two ADSRs per voice. **Filter env amount is bipolar** (negative inverts); **loudness env amount is
   unipolar**. The two VELOCITY switches scale the respective env amounts; velocity does nothing else.
3. **VCA control = envelope + LFO (AMP dest) + aftertouch (AMP dest), clamped at a ceiling** — a sum, not a product
   (LFO square to AMP with loudness amount 0 still gates sound; aftertouch to AMP does nothing when env amount is full).
4. **LFO:** one shared LFO (key reset on first key), 0.022–500 Hz, shapes sine, saw, reverse saw, square, random
   (+ hidden noise at max random rate); **sine and random bipolar, saw/rev-saw/square positive only**; one depth
   (INITIAL AMOUNT + mod wheel + aftertouch when routed) feeds every enabled destination (VCO1, VCO2, PW (4-state),
   AMP, F FREQ, F MODE); **the mod wheel has no other routing**; a negative aftertouch amount inverts the LFO.
5. **Oscillators:** VCO 1 saw ↔ pulse, no fine tune, **hard-sync slave** of VCO 2. VCO 2 triangle ↔ saw ↔ pulse,
   fine ±50 cents, **LOW FREQ** (becomes a sub-audio source), **KEYBOARD** (key tracking off). Frequency knobs step
   in semitones (61 steps). Pulse width is square at centre, thinning toward both ends. Sub = square 1 octave below
   VCO 1. Noise = white, per voice. Oscillators stable; drift comes only from **DETUNE** (random per-voice slop).
6. **X-Mod:** two signed sources — **filter envelope** and **VCO 2** (audio rate) — into the same set of enabled
   destinations: VCO1 freq, VCO1 shape, VCO1 PW, filter freq, filter mode, Norm↔BP.
7. **Aftertouch:** signed amount into VCO1, VCO2, LFO AMT, AMP, F FREQ, F MODE.
8. **Effects:** two digital slots in series A → B; reverbs only in B; one ON/OFF is a true bypass; delay ≤ 1 s
   (synced times halved until they fit). Distortion is a separate per-program amount (position not specified).
9. **Arp and sequencer** cannot run together. HOLD latches. Key modes (low/high/last, ± retrigger) apply only in
   unison. Bend range 0–12 is per program.

Where the manual is silent or contradicts itself (flagged in the reference), the builder decides and records it.

## Constraints

- **Hardware (SMK-37 Pro):** 37 velocity keys (no key aftertouch), 16 RGB pads with pressure (0..127), 8 endless
  encoders, 4 faders, pitch + mod wheels, ~18 buttons with LEDs, 240×240 16-bit colour LCD, USB MIDI, USB audio;
  the audio callback renders stereo frames (the analog output may be mono — verify on the base).
- **Chip / platform** (from `fc79/smk37-base` docs RULES.md, API.md): JieLi AC791N; audio interrupt
  `app_audio()` gets **256 frames at 44.1 kHz every 5.8 ms**, interrupted by USB and key scan — **target < 50 %
  (2.9 ms)** under a worst-case patch; no 64-bit division; **single-precision FPU available** if enabled
  (`-mfprev1`), `double` never; the build refuses ROM-looking constants and raw register access outside `hal/`.
- **Storage:** settings + 8 blobs (1 sector each) + 8 slots (4 sectors each); a flash erase stalls audio → save only
  when silent. The first start clears another firmware's store (backup has it).
- **Identity:** own `APP_NAME`, `STORE_MAGIC`, `DATA_FAMILY`, an unused `IDENT 9NN` (checked on the Firmware Hub),
  own package names — never mistaken for another firmware by installers.
- **Measured reference:** the ANALOG engine (6 voices, 2 osc + sub, SVF, one shared ADSR) costs 150k–257k cycles per
  128 frames on this chip in emulation (22–37 % at 240 MHz). The full OB-6 voice adds a second envelope, VCO 2
  triangle/sync, noise, LFO and **audio-rate X-Mod** — **6 voices are the target, 4 the accepted fallback**, decided
  by measurement in phase 1.
- Dev machine: Mac (Apple Silicon); device builds in Docker (linux/amd64) locally or GitHub Actions; JieLi Linux
  toolchain + AC79 SDK (as in `zvenson/fm1-build`). Private GitHub repo.

## Approaches considered

**Base:** (a) **`fc79/smk37-base` (chosen)** — minimal standalone SMK-37 firmware (Dinghy/Felucca/SLOOP platform,
GPL-3.0) already installed and checked on an SMK-37 Pro: update path, USB rescue, USB MIDI/audio, keys/pads/encoders/
faders/wheels/LEDs, screen, flash store, host tests that render screens to PNG and sound to WAV. (b) Bare JieLi AC79
SDK — every driver and the install/rescue path from scratch; rejected. (c) Keep extending SMK37 OPEN — rejected by
João (sealed engine, 4 knobs, no room).

**Scope (João picked 1):** (1) **phases, voice first**; (2) all 83 parameters at once; (3) ~40-parameter essentials.

## Design

### Repository

New private repo  started from `fc79/smk37-base` with its history, GPL-3.0, credits kept
(LICENSING.md). Identity changed first (names, magic, IDENT). Our code lives in `firmware/src/`:

- `synth/` — the voice and modulation DSP (`osc`, `filter`, `env`, `lfo`, `xmod`, `voice`, `voices` allocation/
  unison/portamento), started from the tested pieces of the ANALOG engine (`analog-engine` branch: PolyBLEP
  oscillators, ZDF SVF fast path, voice handling, drift) and extended to the schema above. Fixed-point stays
  where it is proven; the FPU may be used where it is simpler and measured cheaper (builder's call, per module).
- `params/` — the program model: the 83 parameters as one table (id, section, name, range, bipolar, default,
  phase), program record (versioned, room for the phase-3 sequence), globals.
- `ui/` — screens, panel graphics, control mapping.
- `app.c` — glue: controls → params, MIDI, audio callback, save/load.
- Host tests and the ANALOG-style WAV renderer under `tests/` (the base's host runner already makes PNG + WAV).

### Voice and modulation (phase 1)

Per voice: VCO1 (saw↔pulse, sync slave), VCO2 (tri↔saw↔pulse, fine, LOW FREQ, KEYBOARD), sub, noise → mixer
(unlimited gain, soft overdrive) → SVF (LP↔notch↔HP morph, BP switch, Norm↔BP crossfade for X-Mod, capped Q, no
self-oscillation, key track off/half/full) → VCA (env + LFO + AT, clamped). Filter ADSR (bipolar amount, velocity
switch), loudness ADSR (unipolar amount, velocity switch), exponential (RC-like) stages. One shared LFO. X-Mod with
VCO2 at audio rate. Aftertouch from **pad pressure** (keys have none) and MIDI channel pressure. Detune = per-voice
random slop. Unison (voices / chord memory) with key modes, portamento modes, bend range per program, program
volume, pan spread (applied to the stereo frames; no effect if the output is mono).

### Controls and UI

Visual language approved on the mockups in `docs/design/standalone-ui/` (`panel-style-v2.html`,
`sections-v2.html`): charcoal panel, **white bold section titles over blue rules**, black knobs with a white dot and
small white labels, **red 7-segment displays** for program/BPM/effect values, red LEDs and lit buttons, **wood
cheeks** on both screen edges, own outlined wordmark. Same header mark on every screen.

Screens, in panel order: **Boot/identity** · **Play** (bank/program on red 7-segment, patch name, voices LEDs,
FILTER knobs, performance switches) · **VCO 1 + VCO 2** · **MIXER + FILTER** · **ENVELOPES** (filter row,
loudness row) · **LFO + X-MOD** · **AFTERTOUCH + MISC** · **EFFECTS** (phase 2) · **CLOCK + ARP + PROGRAM**
(arp/clock phase 2) · **GLOBALS**.

Mapping principles (exact button assignment is the builder's, written into the doc when decided):
- The 7 encoders other than MASTER map 1:1 to the knobs drawn on the current screen, in reading order.
- The 4 faders follow the screen (bottom blue strip says what they do); on Play they are FREQUENCY, RESONANCE,
  MODE, ENV AMOUNT. Faders are absolute → the OB-6 **pot mode** global (relative / passthru / jump) applies to them.
- **Pads, decided by João:** the **top row (8 pads)** is the panel's switches: destinations and switches of the
  current screen (lit red when on), section stepping, and on Play the performance switches (X-MOD, UNISON, ARP, HOLD)
  and program digits. The **bottom row (8 pads) is pressure only on every screen**: pressing one never switches
  anything; its pressure (the highest held) is the aftertouch source. Gesture: right hand on the keys, left hand
  leaning on a bottom pad. Bottom pads may glow with the pressure amount.
- Program addressing follows the OB-6 idea (bank / tens / ones, WRITE, MANUAL), sized to the flash (below).
- Draw only what changed (a full screen is 113 KB over SPI).

### Programs and globals

Program record: the 83 parameter values + 20-char name (+ sequence in phase 3), versioned. Count set by flash: at
least 100 user programs, more if the record allows (8 slots × 16 KB available). Own factory presets (not the OB-6's),
written after phase 1 by ear. Globals kept from the OB-6 list where they apply: transpose, master tune, MIDI channel,
MIDI clock mode (phase 2), param transmit/receive (CC), local control, pot mode, velocity curve, aftertouch curve,
stereo/mono, sustain polarity. Saving happens only while silent.

### MIDI

USB MIDI in/out: notes, velocity, channel pressure, pitch bend, mod wheel, sustain, all-notes/sound off, program
change + bank select, and a CC per continuous parameter (own CC map, documented). NRPN and SysEx later.

### Testing and measurement

- Host unit tests per schema rule (e.g. env amount polarity, VCA sum-clamp, LFO polarity per shape, wheel only
  adds LFO depth, sync, LOW FREQ, KEYBOARD, key track, BP switch, no self-oscillation at max RESO, X-Mod
  destinations, unison key modes), plus the anti-aliasing / click / zipper / overflow tests carried over from ANALOG.
- WAV renders of fixed scripts for listening (João signs off by ear; OB-6 reference recordings optional).
- Screens rendered to PNG on the host for every screen, compared against the mockups.
- CPU: cycles per 256-frame block in the fm1-emu emulator for the worst-case patch (6 voices, X-Mod audio rate,
  all LFO destinations) → decides 6 vs 4 voices; flash/RAM from the build.
- No hardware until phase 1 passes all of that **and** João has made a full flash backup and says go.

## Phases and exit criteria

1. **Voice:** all phase-1 sections playable from keys/pads/encoders/faders and MIDI; programs save/load; all
   screens; tests green; worst-case CPU < 50 % of the audio budget at the chosen voice count; listening round.
2. **Clock, arp, effects:** as goals; CPU still < 50 % with effects on.
3. **Sequencer:** 64 steps × 6 notes, rests/ties, per program, arp/seq exclusive.

## Decided by João

- Standalone firmware, not inside SMK37 OPEN.
- Follow the OB-6 synth schema (manual v1.2) closely; UI in the OB-6 panel's visual language (approved mockups).
- Phased: voice first, then clock/arp/effects, then sequencer.
- Must be instantly recognisable as this firmware.
- Pads: top row = switches/digits, bottom row = aftertouch pressure on every screen.

## Our calls (correct if wrong)

- Base `fc79/smk37-base`; private repo `knobi-six`.
- 6 voices target, 4 fallback, decided by measurement.
- Aftertouch source = bottom-row pad pressure (highest held) + MIDI channel pressure.
- Pan spread applied to stereo frames only.
- Own presets and own CC map; no OB-6 factory patches.
- Distortion (unspecified position): on the summed bus before effects, in phase 2.
- **Mod wheel** only adds LFO depth (rule 4 wins over the mockup caption "from pad pressure & mod wheel", which is
  wrong and will be corrected).
- **Encoders:** MASTER stays the master volume at all times (the OB-6 has a dedicated VOLUME knob; the base already
  wires it). The other 7 encoders are the knobs of the current screen, so screens carry at most 7 encoder knobs plus
  the 4 faders; switching rows/sections (e.g. FILTER ↔ LOUDNESS envelope) is a button, not an encoder.
- **Mockups are a style guide, not a pixel target:** the same visual language on 240×240 with the base's fonts
  (plus one small font if labels need it); host-rendered PNGs of every screen are reviewed by João, not pixel-diffed.
- **WRITE saves immediately.** If the base's sliced save (`store_sliced`) proves click-free on the chip it is used;
  otherwise WRITE fades the audio out, saves, and fades back in (a short, deliberate dropout). Other saves (globals,
  last program) still wait for silence. Verifying this is part of phase 1.
- **Toolchain and CPU gate:** the JieLi Linux toolchain and the AC79 SDK are fetched in CI exactly as in
  `zvenson/fm1-build` (already working in this project's private repo); the CPU gate is `joakimeriksson/fm1-emu`
  (MIT) with both cycle models, as already run for ANALOG. Emulator figures gate phases 1–3; the first on-device
  check (after João's backup) confirms them with the platform's `fm1_ticks()` meter.

## Left to the builder

Exact button/pad assignments (within the principles), fixed-point vs FPU per DSP module, LFO/envelope curves within
the reference, program count from the record size, how the reference's flagged contradictions are resolved (record
each), test framework details.
