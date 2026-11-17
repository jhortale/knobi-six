# OB-6 synth schema reference (for an original OB-6-inspired firmware)

Source: OB-6 Operation Manual v1.2 (Feb 2021), all 104 PDF pages read.
Page numbers below are the **printed manual page numbers** (PDF page = printed page + 10).
Everything is paraphrased. "Not specified" means the manual does not say. Items marked
**[inference]** are my own reading, not manual statements. This document is a reference
for designing an original instrument; it carries no branding intent.

Conventions: NRPN ranges come from the Appendix C program/global parameter tables
(pp. 83-86) and are the most precise statement of each parameter's resolution.

---

## 1. Voice architecture & signal path  (pp. 1, 16-27, 28, 34, 49-50, 52)

- **Voices:** 6-voice polyphonic, fully analog voice path (VCOs, VCF, VCA). Two units can be
  poly-chained over MIDI for 12 voices (p. 8).
- **Per voice:** 2 VCOs + 1 sub-oscillator + 1 white-noise source (p. 16).
  - VCO 1: continuously variable shape sawtooth <-> pulse (variable width). Hard-syncable to VCO 2.
  - VCO 2: continuously variable shape triangle <-> sawtooth <-> pulse (variable width); has fine
    detune, LOW FREQ (LFO mode) and KEYBOARD (key-tracking) switches.
  - Sub octave: square wave one octave below VCO 1 (p. 20).
  - Noise: white noise (p. 20). Whether noise is per voice or shared: "per voice" per p. 16.
- **Mixer:** independent levels for VCO 1, VCO 2, Sub Octave, Noise (p. 20). Levels are
  deliberately allowed to overdrive later stages (no internal limiting).
- **Filter:** one 2-pole (12 dB/oct) analog state-variable filter per voice; mode knob morphs
  LP -> notch -> HP continuously; separate BP on/off switch; does not self-oscillate (pp. 21-22).
- **VCA:** analog, driven by the Loudness Envelope (p. 26).
- **Envelopes:** two ADSRs per voice: Filter Envelope and Loudness Envelope (pp. 23, 26).
- **LFO:** one LFO section (5 shapes + hidden noise). Whether it is one global LFO or one per
  voice: not specified. **[inference]** key-reset behaviour (reset on first key only) suggests a
  single shared LFO.
- **Distortion:** analog distortion, amount set per program (p. 34). Its position in the chain
  (pre/post filter, per-voice or summed): not specified.
- **Effects:** two digital effects (24-bit / 48 kHz) A then B in series after the analog path;
  global ON/OFF is a true bypass (p. 28). Reverbs available only in slot B.
- **Stereo:** stereo L/R outputs; per-voice panning via PAN SPREAD (alternating L/R voices).
  A global Stereo/Mono setting defeats all panning (pp. 15, 50).
- **Program volume:** per-program output level (PRGM VOL); master volume is a non-programmed
  global knob (p. 49).
- **Voice modes:** Poly (default) and Unison (monophonic, 1 note, stack of N voices, or chord
  memory). Key-assign (note priority) modes apply only in Unison (pp. 52-55).

Signal flow (one voice, then the summed stereo bus):

```
[VCO1 saw<>pulse]--+                                                        pan(spread)  stereo bus
[VCO2 tri<>saw<>pulse]--+-->[MIXER]-->[SVF 12dB LP<>notch<>HP | BP]-->[VCA]--->(L/R)-->[dist?]-->[FX A]-->[FX B]-->L/R out
[SUB sq -1oct VCO1]-+       FENV-^                               LENV-^           (dist position not specified)
[NOISE white]------+
```

---

## 2. Parameter tables per panel section

Columns: Name | Control | Range / values (NRPN resolution) | Function | Notable behaviour.

### 2.1 VCO 1  (pp. 16-18, 84)

| Name | Control | Range | Function | Notes |
|---|---|---|---|---|
| Frequency | knob (stepped) | 0-60 semitones (5 oct) | Base pitch | Adjusts in semitone steps. With front-panel transpose (+/-2 oct) total span quoted as 9 octaves, ~16 Hz to 8 kHz. No fine tune on VCO 1. |
| Shape | knob | 0-255 | Morph saw -> pulse | Continuously variable; in-between shapes are blends. |
| Pulse Width | knob | 0-255 | Width of pulse component | Center = square; narrows toward thin pulse at **both** extremes (full left and full right). |
| Sync | switch | Off/On | Hard-sync VCO 1 (slave) to VCO 2 (master) | VCO 1 resets each VCO 2 cycle. Classic use: sweep VCO 1 pitch via X-Mod while synced. |

### 2.2 VCO 2  (pp. 16-18, 84)

| Name | Control | Range | Function | Notes |
|---|---|---|---|---|
| Frequency | knob (stepped) | 0-60 semitones | Base pitch | Same as VCO 1. |
| Detune (fine) | knob, center detent position | 0-254, +/- quarter tone (+/-50 cents), centre = 0 | Fine tune VCO 2 | 12 o'clock = centred. |
| Shape | knob | 0-254 | Morph triangle -> saw -> pulse | Continuously variable. The exact ordering of the middle point (saw) is shown by panel icons tri (left) / pulse (right); saw in the middle is **[inference]** from the manual's list order. |
| Pulse Width | knob | 0-255 | As VCO 1 | Square at centre, narrow at both ends. |
| Low Freq | switch | Off/On | Turns VCO 2 into a sub-audio LFO for X-Mod | FREQUENCY, DETUNE, SHAPE and PW still act on it. The resulting low-frequency range: not specified. |
| Keyboard | switch | Off/On | Key tracking of VCO 2 | Off = ignores keyboard/MIDI notes, sits at base frequency; still responds to modulation (drone / fixed LFO). |

### 2.3 MIXER  (p. 20, 84)

| Name | Control | Range | Function |
|---|---|---|---|
| VCO 1 | knob | 0-127 | Level of VCO 1 |
| VCO 2 | knob | 0-127 | Level of VCO 2 |
| Sub Octave | knob | 0-127 | Level of square sub, 1 oct below VCO 1 |
| Noise | knob | 0-127 | White-noise level |

Note: mixer levels can intentionally overdrive the signal path; high levels + high resonance +
full Loudness Env Amount can clip the outputs (pp. 20, 22).

### 2.4 FILTER  (pp. 21-22, 84)

| Name | Control | Range | Function | Notes |
|---|---|---|---|---|
| Frequency | knob | 0-164 | Cutoff | Unusual 165-step resolution in NRPN. CC74 (brightness) and the filter expression pedal add to cutoff. |
| Resonance | knob | 0-255 | Emphasis at cutoff | Does **not** self-oscillate (by design). High settings can cause output clipping. |
| Mode (NOTCH; LP...HP) | knob | 0-127 | Continuous morph LP -> notch (centre) -> HP | Blends modes in between. |
| BP | switch | Off/On | Selects band-pass | BP only via this switch; a continuous normal<->BP crossfade is reachable only via X-Mod "Norm-BP" destination. |
| Track | 3-state switch (two LEDs HALF / FULL) | Off / Half / Full (0-2) | Keyboard -> cutoff | Higher notes open the filter. Exact scaling of Half/Full (e.g. 50%/100% of 1 V/oct): not specified. |

KNOBI-SIX's calls (the controller's ruling after the listening round, `firmware/src/synth/filter.h`): Frequency
0-164 spans 16 Hz .. 18.8 kHz exponentially (0.746 semitone a step); Track HALF / FULL = 0.5 / 1 octave of cutoff
per octave; Envelope Amount +-127 sweeps the whole 0-164 range.

### 2.5 ENVELOPES

#### Filter Envelope (pp. 23-25, 84)

| Name | Control | Range | Notes |
|---|---|---|---|
| Attack | knob | 0-127 | Time from cutoff knob level up to the peak set by AMOUNT. |
| Decay | knob | 0-127 | Time to fall to sustain level. |
| Sustain | knob | 0-127 | Held level while key down. |
| Release | knob | 0-127 | Time to close after key-up. |
| Amount | knob, **bipolar** | 0-254 (centre = 0) | Env -> cutoff depth; negative inverts the envelope. Cutoff setting bounds the audible effect (e.g. LP fully open + positive amount = no change). |
| Velocity | switch | Off/On | Key velocity scales the env amount effect on cutoff; sign follows Amount (positive: harder = brighter; negative: harder = darker). |

Hidden response-curve feature: routing X-Mod FILTER ENV to FILTR FREQ (slightly negative,
~11 o'clock) and interacting with the filter-env Amount changes the effective "snap"/curve of
the filter envelope (p. 25). **[inference]** this is simply a second, signed env->cutoff path
summing with the main one, but the manual frames it as a response-curve change.

Envelope time ranges in seconds, curve shape (exponential/linear), and retrigger behaviour in
poly mode: not specified. Figures show the conventional exponential-looking ADSR.

#### Loudness Envelope (pp. 26-27, 84)

| Name | Control | Range | Notes |
|---|---|---|---|
| Attack / Decay / Sustain / Release | knobs | 0-127 each | Standard ADSR to the VCA. |
| Amount | knob, **unipolar** | 0-127 | Env -> VCA depth; normally set fully clockwise. Reducing it is the recommended fix for clipping. |
| Velocity | switch | Off/On | Velocity modulates the Loudness Env **amount** (harder = louder). |

VCA behaviour implied by the manual: with Loudness Env Amount at 0, LFO square -> AMP with
Initial Amt at max still produces sound ("gated VCA" trick, p. 27); aftertouch -> Loudness with
positive amount has no effect when Env Amount is already full (p. 63). So VCA control =
**sum** of (env x amount) + LFO + aftertouch, clamped at a ceiling. **[inference]**

### 2.6 LFO  (pp. 35-37, 84)

| Name | Control | Range | Notes |
|---|---|---|---|
| Frequency | knob | 0-254; ~0.022 Hz to 500 Hz | Reaches audio range. When LFO Sync is on, follows BPM (clock divisions mapping: not specified). |
| Shape | selector (LED list) | Sine, Sawtooth, Reverse Saw, Square, Random (0-4) | Hidden 6th shape: Random + Frequency at maximum = white noise. |
| LFO Sync | switch | Off/On | Locks to arp/sequencer/MIDI clock. |
| Initial Amt | knob | 0-255 | Always-on depth. At 0, modulation only via mod wheel. |
| Dest: VCO 1 | switch | Off/On | Pitch of VCO 1. |
| Dest: VCO 2 | switch | Off/On | Pitch of VCO 2. |
| Dest: Pulse Width | 4-state switch (LEDs 1, 2) | none / PW1 / PW2 / both (0-3) | |
| Dest: Amp | switch | Off/On | VCA (tremolo). |
| Dest: Filtr Freq | switch | Off/On | Cutoff. |
| Dest: Filtr Mode | switch | Off/On | LP<->notch<->HP morph position. |

Polarity: Sine and Random are bipolar; Saw, Reverse Saw and Square are unipolar positive
(square gives natural trills, sine gives centred vibrato) (p. 35).
Random = sample-and-hold, one new value per cycle.
Key reset: by default the LFO phase resets on a key press, but not if other notes are already held
(p. 36). Whether this reset also applies when LFO Sync is off: ambiguous (sentence sits under
LFO Sync).
All enabled destinations share one depth (Initial Amt + mod wheel); per-destination depth:
none.

### 2.7 X-MOD  (pp. 37-39, 85)

| Name | Control | Range | Notes |
|---|---|---|---|
| Filter Env (source amount) | knob, bipolar | 0-254 (centre 0) | Filter envelope as mod source. |
| VCO 2 (source amount) | knob, bipolar | 0-254 (centre 0) | VCO 2 output as audio-rate (or LOW FREQ) source; character depends on VCO 2 shape. |
| Dest: VCO 1 | switch | Off/On | VCO 1 frequency (FM). |
| Dest: Shape 1 | switch | Off/On | VCO 1 waveshape. |
| Dest: PW 1 | switch | Off/On | VCO 1 pulse width. |
| Dest: Filtr Freq | switch | Off/On | Cutoff (audio-rate filter FM). |
| Dest: Filtr Mode | switch | Off/On | LP/notch/HP morph. |
| Dest: Norm <-> BP | switch | Off/On | Crossfade normal filter -> band-pass. |

Both sources drive the **same** set of enabled destinations, each with its own signed depth.

### 2.8 AFTERTOUCH  (pp. 61-63, 84-85)

| Name | Control | Range | Notes |
|---|---|---|---|
| Amount | knob, bipolar | 0-254 (centre 0) | Single depth for all enabled destinations. |
| Dest: VCO 1 | switch | Off/On | Pitch up (positive) / down (negative). |
| Dest: VCO 2 | switch | Off/On | Same for VCO 2. |
| Dest: LFO Amt | switch | Off/On | Adds LFO depth; negative amount increases depth **with inverted LFO waveform**. |
| Dest: Loudness | switch | Off/On | Amplitude; no effect upward if Loudness Env Amount is already full. |
| Dest: Filtr Freq | switch | Off/On | Cutoff. |
| Dest: Filtr Mode | switch | Off/On | Moves LP->notch->HP. |

Aftertouch is channel (mono) pressure: applies to all held notes (p. 61). Response curve is a
global setting (4 curves, default linear).

### 2.9 MISC PARAMETERS / OUTPUT  (pp. 19, 49-52, 60, 84)

| Name | Control | Range | Notes |
|---|---|---|---|
| Detune (voice slop) | knob | 0-127 | Randomised per-voice oscillator detuning, subtle to very out of tune; in Unison, detunes stacked voices by a fixed amount. |
| Pan Spread | knob | 0-127 | 0 = all centred; increasing spreads voices, alternating L/R. |
| Program Volume | knob | 0-127 | Per-program level. |
| Master Volume | knob | not programmable | Also sets headphone level. Not subject to pot modes. |
| Pitch-bend range (WHL RANGE) | hold button + inc/dec | 0-12 semitones | Stored per program (NRPN 31). Up/down symmetric; separate up/down ranges: not offered. |
| Distortion | hold EFFECT + MIX knob | 0-127 | Analog distortion amount per program. |

### 2.10 EFFECTS  (pp. 28-34, 85)  -- see section 4.

| Name | Control | Range |
|---|---|---|
| On/Off | switch | Off/On (global for A and B; true bypass) |
| Effect | switch | A / B (which slot the knobs edit) |
| Type | knob + 3-char display | per slot (see sec. 4) |
| Mix | knob | 0-127 dry->wet |
| Param 1 | knob | 0-255 |
| Param 2 | knob | 0-127 |
| Clk Sync | switch | Off/On per slot (delays) |

### 2.11 CLOCK  (pp. 40, 43, 48-49, 85)

| Name | Control | Range | Notes |
|---|---|---|---|
| Tap Tempo | button | 30-250 BPM | Tap at least 4 times; LED flashes at tempo. |
| BPM | knob + display | 30-250 | Stored per program (NRPN 167). Ignored when following external MIDI clock. Also drives synced LFO and synced delays. |
| Value | knob (LED list) | 10 divisions (0-9): Half, Qtr, 8th, 8th D, 8th S(wing), 8th T, 16th, 16th S(wing), 16th T, 32nd | Step length relative to BPM; works for internal and external clock. |

### 2.12 ARPEGGIATOR  (pp. 40-44, 85)

| Name | Control | Range |
|---|---|---|
| On/Off | switch | Off/On (stored per program) |
| Octaves | selector | 1, 2, 3 |
| Mode | selector | Up, Down, Up+Down, Random, Assign (played order) |

### 2.13 SEQUENCER  (pp. 44-48, 85-86)

| Name | Control | Range |
|---|---|---|
| Record | switch | Off/On (not stored) |
| Play | switch | Off/On (not stored) |
| (uses BPM / Value / Tap Tempo from CLOCK) | | |

### 2.14 PORTAMENTO / UNISON / HOLD / KEY MODE  (pp. 51-55, 84-85)

| Name | Control | Range | Notes |
|---|---|---|---|
| Portamento | switch | Off/On | No effect if Rate = 0. |
| Rate | knob | 0-127 | Glide time/rate. |
| Portamento mode | hold PORTAMENTO + inc/dec | Fixed Rate (default), Fixed Rate Auto (legato only), Fixed Time, Fixed Time Auto (legato only) | |
| Unison | switch | Off/On | |
| Unison voices / chord | hold UNISON + inc/dec | 7 values (NRPN 0-6): 1-6 voices stacked, plus "CHd" chord memory once a chord is stored. Manual text says 2-6; NRPN range implies 1 is also selectable (ambiguous). |
| Key Mode | hold KEY MODE + inc/dec | Low, Low-retrig, High, High-retrig, Last, Last-retrig (0-5) | Unison only. |
| Hold | switch | Off/On | Latch; with arp: relatch. Saved per program: not specified (no NRPN listed). |

### 2.15 PROGRAM / BANK / WRITE / MANUAL / TRANSPOSE  (pp. 2-7, 50, 56-59)

| Name | Control | Function |
|---|---|---|
| Bank (Decrement) | button | Hold + 0-9 = hundreds digit; also global "decrement". |
| Tens (Increment) | button | Hold + 0-9 = tens digit; also "increment". |
| Program 0-9 | 10 buttons | Ones digit; double as global parameter selectors. |
| Write | button | Arms save (blinks); ones-digit press commits; press again to cancel. |
| Globals | button (2 LEDs) | 1st press top row, 2nd press bottom row; during Write = Compare. Pressed 3 times = store current program as power-on default. |
| Manual | button | Live-panel mode (sound = current knob positions); program change disabled while on. Manual+Write = load basic init preset; Manual+0 = calibrate; Manual+7/8/9 = wheel calibration. |
| Transpose Down/Up | 2 buttons, 5 LEDs | -2..+2 octaves; global, not saved; transposes MIDI out too. |

### 2.16 GLOBALS -- see section 7.

---

## 3. Modulation matrix  (pp. 24, 27, 35-39, 60-63, 79-81)

Fixed-routing matrix (no free assignment). "Depth" column = which control sets the amount.

| Source | Polarity of source | Depth control (sign) | Destinations |
|---|---|---|---|
| LFO (sine, random) | bipolar | Initial Amt (0..+) + Mod wheel + AT(LFO Amt, signed) | VCO1 pitch, VCO2 pitch, PW1 / PW2 / both, Amp (VCA), Filter cutoff, Filter mode |
| LFO (saw, rev saw, square) | unipolar + | same | same |
| LFO noise (Random @ max freq) | bipolar **[inference]** | same | same |
| Mod wheel | unipolar | adds to LFO depth | whatever LFO destinations are enabled (no direct routing) |
| Filter envelope | unipolar ADSR | Filter Env Amount (bipolar) | Filter cutoff |
| Filter envelope (via X-Mod) | unipolar | X-Mod Filter Env (bipolar) | VCO1 pitch, Shape1, PW1, cutoff, filter mode, Norm<->BP |
| VCO 2 audio (or LOW FREQ) | bipolar, shape-dependent | X-Mod VCO 2 (bipolar) | same X-Mod destination set |
| Loudness envelope | unipolar ADSR | Loudness Env Amount (unipolar) | VCA |
| Velocity | unipolar | switch only (fixed depth, not specified) | Filter-env amount (sign follows amount); Loudness-env amount |
| Aftertouch (channel) | unipolar | AT Amount (bipolar) | VCO1 pitch, VCO2 pitch, LFO amount, Loudness, cutoff, filter mode |
| Key tracking | - | Off / Half / Full | Filter cutoff (VCO 2 tracking on/off via KEYBOARD switch) |
| Pitch wheel | bipolar | 0-12 semitones | All oscillators (both VCOs; VCO 2 with KEYBOARD off: not specified) |
| Expression pedal 1 | unipolar | - | Volume |
| Expression pedal 2 / CC74 | unipolar | - | Added to filter cutoff |
| CC64 / sustain pedal | switch | - | Holds envelopes at sustain |
| Detune (slop) | random per voice | Detune knob | Oscillator pitch |

Notable: there is no LFO -> LFO-rate, no envelope -> VCO2 pitch except via aftertouch/LFO,
and no velocity -> anything besides the two envelope amounts.

---

## 4. Effects  (pp. 28-34, 85)

Structure: two slots in series, **A -> B**. Each slot has Type, Mix (0-127), Param 1 (0-255),
Param 2 (0-127) and Clk Sync. A single ON/OFF switch (true bypass) enables/disables both.
All effect settings saved per program. Reverbs only in B (B is last in chain). Either slot
can be Off. Hold EFFECT ~1 s and turn MIX to set the separate analog Distortion amount.

| Type | Slot | Description (own words) | Param 1 | Param 2 |
|---|---|---|---|---|
| Off | A, B | - | - | - |
| bbd | A, B | BBD-style analog delay emulation (darker, lossy repeats) | delay time | feedback |
| ddl | A, B | Clean digital delay | delay time | feedback |
| cho | A, B | Vintage chorus | rate | depth |
| FL1 | A, B | Flanger, high resonance (through-zero) | rate | depth |
| FL2 | A, B | Flanger, no feedback, tape-style through-zero | rate | depth |
| PH1 | A, B | 6-stage phaser, high resonance | rate | depth |
| PH2 | A, B | 6-stage phaser, lower resonance | rate | depth |
| PH3 | A, B | Emulation of the designer's original 6-stage phaser | rate | depth |
| rin | A, B | Ring modulator (emulation of original unit) | mod frequency (or ratio when tracking) | low-note pitch tracking Off/On |
| HAL | B | Hall reverb (largest) | time | early reflections |
| rOO | B | Room reverb | time | early reflections |
| PLA | B | Plate reverb | time | early reflections |
| SPr | B | Spring reverb (guitar-amp style) | decay | tone |

Ring modulator: tracking Off = fixed modulator frequency; tracking On = Param 1 sets a ratio
to the **lowest held note**, so the ratio stays constant across the keyboard (p. 33).

Delay clock sync (Clk Sync on), Param 1 becomes a division:

| Value | Delay length |
|---|---|
| 1 | 4 beats |
| 2d | 3 beats |
| 2 | 2 beats |
| 4d | 1.5 beats |
| 4 | 1 beat |
| 4t | 1 1/3 beats (as printed; **ambiguous**, a quarter triplet would be 2/3 beat) |
| 8d | 3/4 beat |
| 8 | 1/2 beat |
| 8t | 1/2 beat (as printed; **ambiguous**, an eighth triplet would be 1/3 beat) |
| 16d | 3/8 beat |
| 16 | 1/4 beat |

Maximum delay time 1 s; synced values longer than 1 s are halved repeatedly until <= 1 s.
NRPN table lists FX1 Type 0-5 and FX2 Type 0-9, which contradicts the 10 / 14 types listed
above -- **inconsistent in manual**.

---

## 5. Arpeggiator, clock, sequencer  (pp. 40-49, 85-86)

**Clock**
- Internal BPM 30-250 (per program), tap tempo, or external MIDI clock (MIDI or USB port, global).
- Value (division) table: Half = BPM/2, Qtr = BPM, 8th / 8th D / 8th Swing = BPM x2,
  8th T = x3, 16th / 16th Swing (full swing) = x4, 16th T = x6, 32nd = x8. Swing amount: not
  specified beyond "swing" / "full swing".
- With external clock set to In / Slave Thru and no clock present, arp and sequencer do not run.
- Rear "Footswitch-Sequence" jack: footswitch start/stop, or audio input to step (Trigger),
  gate envelopes (Gate), or both (Trigger+Gate). Audio <= 5 Vpp.

**Arpeggiator**
- Modes: Up, Down, Up+Down, Random, Assign (order played). Octaves 1/2/3 (adds octaves above).
- With HOLD on: latch + **relatch** (a new chord replaces the old one), but notes added while at
  least one key is still held are appended to the pattern.
- Arp and sequencer are mutually exclusive (turning one on disables the other).
- Arp Beat Sync (global, bottom row button 9, unlabelled): quantizes arp start to the current
  division.
- Arp/seq notes can be sent as MIDI notes (when Param Xmit is CC or NRPN).
- Arp gate length, velocity of arp notes, up+down endpoint repetition: not specified.

**Sequencer**
- Single polyphonic step sequence per program, up to 64 steps, up to 6 notes per step, with
  rests and ties. Stored with the program (per-step notes 12-108 and per-note velocity 0-127 in
  the NRPN map, 6 note+velocity lanes x 64 steps).
- Recording (step entry): press RECORD, play notes/chords; each key-press (chord: keep at least one
  key held to add notes) records a step. TENS/INC = rest; holding note(s) and pressing TENS/INC
  repeatedly = tie (extend). BANK/DEC = step back and erase. Transpose buttons usable while
  recording. Real-time recording: not offered.
- Playback: PLAY toggles; loops at current BPM/Value; external start/stop via footswitch or MIDI.
- Transpose: while playing, hold RECORD and press a key; middle C = no transposition. On OS
  1.5.5+, with Unison on, no RECORD hold is needed.
- Voices played live compete with the sequence for the 6 voices.
- Gate length per step, swing beyond Value, sequence length setting separate from recorded
  length: not specified.

---

## 6. Unison / voice / key modes, portamento, pan, detune, volume, wheel range  (pp. 19, 49-55, 60-61)

- **Unison:** mono, one note; stacks 2-6 voices (poly-chain: 2,3,4,5 or 12). Up to 12
  oscillators on one note. Key Mode (Low/High/Last, each with or without retrigger) applies only
  in Unison. "Retrigger" variants retrigger envelopes on each keystroke; plain variants are
  therefore legato **[inference]**.
- **Chord memory:** in Unison, holding up to 6 notes and pressing UNISON stores the chord; single
  keys then play the transposed chord. Low-note priority maps the played key to the lowest chord
  note; high-note priority to the highest. Saved with the program. Cleared by: Unison off, hold
  one note, press UNISON, re-save.
- **Unison detune & spread:** DETUNE detunes stacked voices by a fixed amount. Stereo spread of
  the stack: p. 19 says the PAN SPREAD setting controls width (odd stack = one voice centred);
  p. 52 says the DETUNE knob also spreads them -- **contradictory**; implement either or both.
- **Detune (poly):** random per-voice detuning emulating vintage instability; oscillators are
  otherwise "extremely stable". Distribution and rate of drift: not specified.
- **Portamento:** 4 modes (Fixed Rate default = time grows with interval; Fixed Time = constant
  time; "A" variants only when legato). Rate 0-127. Behaviour in Poly mode (which voice glides
  from where): not specified.
- **Pan Spread:** 0-127, voices alternately left/right with increasing offset.
- **Program Volume:** 0-127 per program; MIDI CC7 volume combines with master and program volume.
- **Pitch wheel range:** 0-12 semitones per program. **Mod wheel:** adds LFO depth (see sec. 3).
- **Hold:** sustain-like latch; with arp, latch + relatch.
- **Voice allocation in Poly mode** (round-robin, reuse same note, steal policy): not specified.

---

## 7. Programs & globals  (pp. 2-7, 11-15, 56-59, 64-66, 77-89)

**Programs**
- 1000 programs = 10 banks x 100. Banks 0-4 user (writable); banks 5-9 factory (read-only, edits
  allowed but cannot be saved there). Factory ships 000-499 identical to 500-999.
- Selection: BANK+digit (hundreds), TENS+digit (tens), digit (ones). Dec+Inc / Inc+Dec chords step
  by one program with wrap across tens.
- Write: arm, optionally choose bank/tens, ones digit commits; re-press Write to cancel.
- Compare: while Write is armed, GLOBALS enters compare (play the stored version or any target
  location); cannot write while comparing.
- Saved-value indicator: display dot lights when a knob passes its stored value.
- Manual (live panel) mode; basic init preset = Manual + Write.
- Program data = 1024 bytes (1171 bytes packed MS-bit SysEx); name = 20 chars (ASCII 32-125).
- Program change 0-99 within the current bank; CC32 bank select 0-9 (received 0-4 user, 5-9
  factory).

**Pot modes (global):** Relative (value scales between stored value and travel end), Passthru
(no change until knob crosses stored value), Jump (absolute). Pots have ~300 degrees travel;
panel mixes endless encoders and pots.

**Global settings** (top row = GLOBALS once; bottom row = twice; BANK/TENS step values)

| Button | Top row | Range | Bottom row | Range |
|---|---|---|---|---|
| 0 | Transpose (master coarse) | -12..+12 semitones | Local Control | Off/On |
| 1 | Master Tune | -50..+50 cents | Seq Jack | Normal / Trigger / Gate / Trigger+Gate |
| 2 | MIDI Channel | All, 1-16 | Pot Mode | Relative / Passthru / Jump |
| 3 | MIDI Clock | Off / Out / In / Slave Thru / In-no-start-stop | Sustain +/- | N.O. / N.C. / N.O.+seq N.C. / N.C.+seq N.O. |
| 4 | Clock Port | MIDI / USB | Alt Tuning | Normal, 1-16 |
| 5 | Param Xmit | Off / CC / NRPN | Vel Response | curves 0-7 |
| 6 | Param Rcv | Off / CC / NRPN | AT Response | curves 0-3 (default linear) |
| 7 | MIDI Control | Off / On | Stereo/Mono | Stereo / Mono |
| 8 | MIDI SysEx port | MIDI / USB | Pgm Dump | Program / Tens / Bank / User banks / All |
| 9 | MIDI Out port | MIDI / USB (NRPN: Off/MIDI/USB/MIDI+USB) | (unlabelled) Arp Beat Sync | Off / On |

Other global-level items: front-panel Transpose (+/-2 oct, not saved), Master Volume, default
startup program, Seq Transpose (NRPN 1089, listed among globals). Global reset = GLOBALS + WRITE.
Global dump = 25 parameters (transmit only).

Alternative tunings: 16 built-in user-replaceable scales + non-erasable 12-TET (Harmonic series,
Carlos Harmonic 12, Meantone, 24-TET, 19-TET, 31-TET, Pythagorean C, several JI lattices,
gamelan scales, Yamaha Just Major/Minor, Partch 43-tone, Arabic 12-tone) (pp. 69-72).
Replaceable via SysEx.

Calibration: auto oscillator + filter calibration (Manual + 0); instrument "learns" its
temperature range over repeated calibrations; wheel calibration (Manual + 7/8/9) (pp. 65-66).

MIDI received: notes (note-off velocity ignored), poly pressure (listed), channel pressure,
pitch bend, CC1, CC4, CC7, CC32, CC64, CC74, CC121, CC123, program change, full CC map
(p. 81), NRPN with data inc/dec and RPN reset (pp. 82-83).

---

## 8. Parameter count  (derived from the program NRPN table, pp. 84-85)

Per-program sound/performance parameters (excluding name and sequence data): **83**.

- **Continuous (knob-type, >= 128 steps or time/level): 41**
  VCO1 Freq*, VCO1 Shape, VCO1 PW, VCO2 Freq*, VCO2 Fine, VCO2 Shape, VCO2 PW,
  VCO1/VCO2/Sub/Noise levels (4), Portamento Rate, Detune, Filter Freq, Resonance, Filter Mode,
  Program Volume, Pan Spread, Distortion, Loudness Env Amt + ADSR (5), Filter Env Amt + ADSR (5),
  LFO Freq, LFO Initial Amt, Aftertouch Amt, FX A Mix/P1/P2 (3), FX B Mix/P1/P2 (3),
  X-Mod Filter Env Amt, X-Mod VCO2 Amt, BPM.
  (*VCO Freq is a knob but stepped in 61 semitones.)
  Of these, 5 are bipolar (signed, centre = 0): VCO2 Fine, Filter Env Amt, Aftertouch Amt,
  X-Mod Filter Env, X-Mod VCO2. (VCO shapes/PW are centre-meaningful but not signed.)
- **Switched / selector: 42**
  VCO1 Sync, VCO2 Low Freq, VCO2 Keyboard, Porta On, Porta Mode (4), Bend Range (0-12),
  Filter Key Track (3), Filter Vel, Norm/BP, Loudness Vel, LFO Shape (5), LFO Sync,
  LFO dests x6 (VCO1, VCO2, PW 4-state, Amp, Filt Freq, Filt Mode), AT dests x6,
  FX A Type, FX A Sync, FX B Type, FX B Sync, FX On, X-Mod dests x6, Unison On,
  Unison voices/chord (7), Key Mode (6), Arp On, Arp Mode (5), Arp Octaves (3), Clock Division (10).
- **Additional per-program data:** name (20 chars); sequence (64 steps x 6 note + 6 velocity);
  unison chord memory (storage location not listed in NRPN table).
- **Not per-program (global):** all section-7 globals (~25), front-panel Transpose, Master Volume,
  Hold (not in NRPN table, so **probably** not stored -- not specified), Seq Play/Rec state,
  Manual mode.

---

## 9. Things that matter for faithful sound

### Stated or directly implied by the manual
1. Filter is a **2-pole, 12 dB/oct state-variable** filter in the SEM tradition; mode is a
   continuous LP->notch->HP morph plus a separate BP; **it does not self-oscillate** even at
   max resonance (pp. 21-22).
2. Resonance and mixer levels can **overdrive / clip** the path; the design intentionally does not
   limit gain staging (pp. 20, 22, 27). Saturation at high levels is part of the voice.
3. Oscillator shapes are **continuously morphing**, not switched; pulse width is symmetric about
   square (pp. 16-17).
4. Oscillators are very stable by themselves; vintage drift comes only from the **Detune (slop)**
   parameter, random per voice (pp. 16, 19).
5. Oscillators and filters are analog and temperature-sensitive, requiring calibration; tuning
   precision is "calibrated", not perfect (p. 65).
6. Filter-envelope amount is **bipolar**; loudness envelope amount is unipolar and typically maxed;
   velocity scales envelope amounts rather than acting directly (pp. 24, 27).
7. VCA is additive with LFO/aftertouch and saturates at a ceiling (gated-VCA example; AT note)
   (pp. 27, 63).
8. X-Mod (VCO2 and filter-env as signed sources into VCO1 pitch/shape/PW and filter
   freq/mode/BP) is the core of the character for FM/sync sounds (pp. 37-39).
9. Hard sync: VCO 1 slaved to VCO 2 (p. 17).
10. LFO reaches 500 Hz (audio-rate modulation) and has a hidden noise mode (pp. 35-36).
11. Effects are clean 24-bit/48 kHz digital after a fully analog path; true bypass when off (p. 28).
12. Pan spread alternates voices L/R; unison with odd count centres one voice (pp. 19, 50).

### Own inference (not in the manual)
- SEM-style SVF character: LP and HP share one resonant core, notch = LP+HP sum; morph is likely a
  crossfade of the SVF outputs. A BP-switch crossfade (Norm<->BP) suggests the mode path is
  separate from the BP tap.
- The non-self-oscillating resonance implies a soft resonance limit / damping floor; a digital SVF
  should cap Q below oscillation and add gentle saturation in the integrators/feedback.
- Envelope stage curves are probably exponential (RC-style) as drawn in the ADSR figures.
- Detune likely applies a static random offset per voice per note (or slowly wandering), plus
  in unison a deterministic spread; the manual does not distinguish.
- A 0-164 cutoff range suggests the cutoff knob maps to a semitone-ish scale (~13.7 octaves at
  1 step = 1 semitone) **[speculative]**.
- Filter key track Full is probably 1:1 with pitch (1 V/oct), Half = 0.5.
