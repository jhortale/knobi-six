# KNOBI-SIX controls

What each control of the SMK-37 Pro does. The code: `firmware/src/ui/controls.c` (the tables `BINDINGS`,
`BINDINGS_ROW2`), MIDI: [MIDI.md](MIDI.md). Principles from the spec (Controls and UI, Our calls): K1 stays the master
volume; K2..K8 are the knobs of the screen, in reading order; the faders follow the screen through the pot mode; the
top row of pads is switches, the bottom row is pressure only.

## Buttons

| Button (printed) | id | Does |
| --- | --- | --- |
| `<` / `>` | `BTN_LEFT` / `BTN_RIGHT` | the previous / next section, in panel order: Play, VCO 1 + 2, Mixer + Filter, Envelopes, LFO + X-Mod, Aftertouch + Misc, Arp + Clock, Sequencer, Program, Globals (stops at the ends). **While recording a sequence, on the Sequencer screen: `<` = DEC (back: the last step erased), `>` = INC (a rest; with notes held, a tie)**, see [Sequencer](#sequencer); on any other screen they move between screens as ever |
| PATCH | `BTN_HOME` | Play |
| PARA | `BTN_EDIT` | the last edit section visited (VCO 1 + 2 at first) |
| GLOBE | `BTN_GLO` | Globals; again: back to where you were |
| SCALE | `BTN_SEL` | the other row of a two-row screen: Envelopes FILTER / LOUDNESS, LFO + X-Mod pads LFO / X-MOD, Aftertouch + Misc pads AFTERTOUCH / MISC |
| CHORD | `BTN_ENV` | chord memory: hold it, play up to 6 notes (keys or MIDI), let go: those notes are the program's chord and UNISON goes on with UNISON VOICES = CHD. Tapped with no note: nothing changes |
| OCTAVE- / + | `BTN_OCT_DOWN` / `UP` | the keys' octave, -2..+2 |
| BT | `BTN_SAVE` | WRITE: arms a write (shows Program, the destination = the current program); again: cancels (see [Programs](#programs)) |
| GLOBE held + BT, twice within 2 s | | RESTORE FACTORY: the first press asks (the header shows `SurE`, Program says RESTORE FACTORY? EDITS ARE LOST), the second within 2 s puts the current program's factory version into the edit buffer (header `FAC`; nothing is stored until WRITE). Not confirmed in time: forgotten. Globals, opened by GLOBE's press, closes again. No factory bank in the build: the init program. Not while a WRITE is armed (see [Programs](#programs)) |
| FX held + any encoder K2..K8 | `BTN_FX` | DISTORTION (0..127, the program's), on every screen: the OB-6's "hold EFFECT and turn MIX" (reference 2.9, 4). The effects (and a MIX knob) are not there yet, so every knob does it while FX is held; the header shows the value. FX alone: nothing yet. **This narrows when the effects land**: it will be FX held + the MIX knob only, and FX's other uses will belong to the effects |
| SEQ | `BTN_SEQ` | the Sequencer screen; again: back to where you were |
| SEQ PLAY | `BTN_PLAY` | the sequence plays / stops (lit while it plays). While recording: recording ends and the sequence plays |
| SEQ REC | `BTN_REC` | recording: arms (the Sequencer screen; the edit buffer's sequence is replaced, from step 1, at the first entry); again: ends (lit while recording). While the sequence plays: **held + a key** transposes it; **tapped** (let go with no key pressed, the sequence still playing) stops it and starts recording |
| KNOB BANK, FADER BANK, ARP, NOTE REPEAT | | nothing yet (effects: phase 2C; the arpeggiator is on the pads: Play pad 2, Arp + Clock) |

At power-on the boot screen shows for 1 s (or until a button), then Play.

## Encoders

K1 (MASTER) is the volume, always. K2..K8 are the screen's knobs in reading order (table below). A detent steps a knob
by (range + 127) / 128: 1 for ranges up to 128 values, 2 for 0..164 and 0..255. **Acceleration:** a detent less than
40 ms after the same encoder's last one counts 4 times. A switch on an encoder (LFO SYNC) goes on with a clockwise
detent, off with an anticlockwise one; a select (LFO SHAPE, KEY MODE, globals) steps one choice a detent. Nothing wraps.

## Faders and the pot mode

The four faders are absolute (0..127 over the parameter's range; a bipolar parameter's 0 at the middle). The global
**POT MODE** (Globals, K3) decides what a move does when the fader and the stored value disagree:

- **RELATIVE** (default): the move scales between the stored value and the end of the travel: the fader's top is the
  range's top, its bottom the range's bottom, back where the move started is the stored value again.
- **PASSTHRU**: nothing changes until the fader crosses (or lands on) the stored value; then it follows.
- **JUMP**: the fader's value at once.

A fader starts afresh (re-anchors, must cross again) when the screen or row changes, or when its parameter was changed
by something else (an encoder, MIDI, a program).

## Pads

- **Top row (pads 0..7):** the screen's switches, lit red when on (dim when off). A press acts once, however long or
  hard the pad is held: a switch toggles, a select (KEY TRACK, LFO > PW, UNISON VOICES, PORTA MODE) steps to the next
  choice and wraps. On the edit screens pads 0 and 1 are ◀ / ▶ (the previous / next section, as `<` / `>`).
- **Bottom row (pads 8..15):** pressure only, on every screen; pressing one never switches anything. The highest
  pressure held is the aftertouch (the synth gets the higher of it and MIDI channel pressure). They glow red with
  their pressure.

## Screens

Encoders K2..K8, faders 1..4, top pads 0..7. `—`: nothing.

| Screen | K2..K8 | Faders 1..4 | Top pads 0..7 |
| --- | --- | --- | --- |
| Play | FILTER FREQ, RESONANCE, MODE, ENV AMOUNT, DISTORTION (K6, the PERFORMANCE row's DIST knob) | FREQ, RESO, MODE, ENV AMOUNT | SYNC (VCO1 sync), UNISON, ARP (ARP ON), HOLD |
| VCO 1 + 2 | VCO1 FREQ, SHAPE, PW, VCO2 FREQ, FINE, SHAPE, PW | VCO1 SHAPE, VCO1 PW, VCO2 SHAPE, VCO2 PW | ◀, ▶, SYNC, LOW FREQ, KEYBOARD |
| Mixer + Filter | VCO1, VCO2, SUB OCT, NOISE, FREQ, RESO, MODE | FREQ, RESO, MODE, ENV AMOUNT | ◀, ▶, BP, KEY TRACK (off / half / full) |
| Envelopes, FILTER row | ATTACK, DECAY, SUSTAIN, RELEASE, AMOUNT | A, D, S, R | ◀, ▶, VELOCITY > FILTER, VELOCITY > AMP, ROW |
| Envelopes, LOUDNESS row | ATTACK, DECAY, SUSTAIN, RELEASE, AMOUNT | A, D, S, R | the same |
| LFO + X-Mod, LFO row | LFO FREQ, LFO SYNC, SHAPE, AMOUNT, X-MOD FILTER ENV, X-MOD VCO2 | LFO FREQ, LFO AMT, XM FILTER ENV, XM VCO2 | ◀, ▶, LFO > VCO1, VCO2, PW (none / PW1 / PW2 / both), AMP, F FREQ, F MODE |
| LFO + X-Mod, X-MOD row | the same | the same | ◀, ▶, X-MOD > VCO1, SHAPE1, PW1, F FREQ, F MODE, NORM-BP |
| Aftertouch + Misc, AFTERTOUCH row | AT AMOUNT, DETUNE, PAN SPREAD, BEND RANGE, KEY MODE, PROGRAM VOLUME, PORTA RATE | AT AMOUNT, DETUNE, PORTA RATE, PRG VOLUME | ◀, ▶, AT > VCO1, VCO2, LFO AMT, LOUDNESS, F FREQ, F MODE |
| Aftertouch + Misc, MISC row | the same | the same | ◀, ▶, UNISON, UNISON VOICES (1..6, CHD), PORTAMENTO, PORTA MODE |
| Arp + Clock | BPM (30..250, a BPM a detent), DIVISION (HALF, QTR, 8TH, 8TH D, 8TH S, 8TH T, 16TH, 16TH S, 16TH T, 32ND), ARP MODE (UP, DOWN, UP+DN, RANDOM, ASSIGN), ARP OCTAVES (1..3) | — | ◀, ▶, TAP, MIDI CLOCK (global: OFF / OUT / IN / THRU / IN NO S/S), ARP BEAT SYNC (global), ARP (ARP ON), HOLD (ARP HOLD) |
| Sequencer | — | — | ◀, ▶, SEQ PLAY, SEQ REC (as the buttons; the pad SEQ REC acts at once, also while playing) |
| Program | BANK (0..4), TENS (0..9), ONES (0..9) | — | ◀, ▶, WRITE, ENTER, MANUAL |
| Globals | MIDI CH (ALL, 1..16), POT MODE, PARAM TX (OFF / CC), PARAM RX (OFF / CC), TRANSPOSE (-12..+12), TUNE (-50..+50 cents), VEL CURVE (0..7) | — | ◀, ▶, LOCAL (ON / OFF), STEREO / MONO, SUSTAIN (N.O. / N.C.), AT CURVE (0..3) |

Every phase-1 parameter, BPM and DIVISION, the four arpeggiator values, DISTORTION, and every global is on some screen (unit test `bindings_valid`). The globals
are kept in flash (below).

### Arp + Clock

The **Arp + Clock** screen (Phase 2; screen id 7; the Sequencer is 8, Program 9, Globals 10 since Phase 3) is the mockup's "Arp + Clock + Program" page without Program (that
has a screen of its own): CLOCK above, ARPEGGIATOR below.

The clock (`firmware/src/synth/clock.h`) sets the tempo the arpeggiator and LFO SYNC follow. **BPM** (K2) and **DIVISION** (K3) are the program's (stored with it: a program from before keeps 120 and QTR).
The divisions are the OB-6's ten: HALF, QTR, 8TH, 8TH D (a dotted 8th then a 16th, two steps a beat as the OB-6 lists
it), 8TH S and 16TH S (swing: long-short in 2 : 1, a triplet shuffle), 8TH T, 16TH, 16TH T, 32ND. **TAP** (pad 2): tap
at least 4 times; from the 4th tap BPM is the average of the last intervals (up to 4), 30..250; a pause over 2 s starts
a new count (the header counts the taps: `tAP1`..`tAP3`). TAP's pad flashes on each beat while the clock runs. Pad 3
cycles the global **MIDI CLOCK**, pad 4 the global **ARP BEAT SYNC** (both kept with the globals, below). Following a
MIDI clock, the display shows the tempo received (`---` until there is one) and BPM MIDI under it, and the line under
the pads says FOLLOWING MIDI CLOCK, MIDI CLOCK STOPPED (in red: ticks arrive but a 0xFC stopped it, or no 0xFA yet
under IN) or NO MIDI CLOCK: THE ARP WAITS (in red: none arriving); the line under it reminds that BPM and TAP set the
program's own tempo, not the one followed.

The **arpeggiator** (`firmware/src/synth/arp.h`; OB-6 reference 2.12 and 5): **ARP** (pad 5, also Play's pad 2) turns
it on, **MODE** (K4) is UP, DOWN, UP+DN, RANDOM or ASSIGN (the order played), **OCT** (K5) 1..3 octaves (the notes,
then the same an octave up, two octaves up), **HOLD** (pad 6) the program's ARP HOLD. All four are the program's
(stored; a program from before Phase 2 loads with the arp off). With the arp on, the keys (and MIDI notes in) go to the
arpeggiator, never straight to the voices; on each step of the clock (BPM and DIVISION) it plays the next note of its
pattern, for half the step, at the velocity its key was played with. UP+DN turns at the ends without playing them
twice (C E G: C E G E C E G E ...); RANDOM picks a note of UP's pattern each step. The arp's notes play the voices like
keys: UNISON stacks each one, portamento glides between them, chord memory plays its chord on each.
- **HOLD** (Play's HOLD pad, the sustain pedal, or the program's ARP HOLD): the keys let go stay in the pattern; a key
  pressed when none is held starts a new pattern (**relatch**: the new chord replaces the old); a key pressed while one
  is still held is **added** to it. HOLD off: the keys not held leave it.
- **Start**: the first key of a new pattern (or a relatch) starts it from its first note: ARP BEAT SYNC OFF, at once
  (the clock restarts); ON, on the clock's next step. Under MIDI CLOCK OUT always on the next step, as ON: the clock
  never restarts there, so a DAW or drum machine following it never jumps back to its bar start on a new chord.
- **No clock, no arp**: following a MIDI clock that is absent or stopped, the arp is silent and its note released; it
  plays on when the clock runs again.
- ARP off: the arp's note stops and the keys still held sound as played. ALL NOTES OFF (CC 123), a program change
  and PANIC (CC 120) stop it and forget its keys.
- MIDI out: with PARAM TX = CC the arp's notes go out (and the keys' own notes do not). LOCAL OFF: the keys go out as
  played and do not reach the arp ([MIDI.md](MIDI.md)).

**Sequencer playback** (Phase 3 Task 4; OB-6 reference 5; the player: `firmware/src/synth/seq.h` "The player"; the
buttons and the recording: [Sequencer](#sequencer) below):
- **SEQ PLAY** plays the edit buffer's sequence (the program's, as loaded or recorded) from step 1, one step per clock
  step (BPM and DIVISION), looping its recorded length. Each step's notes play for half the step at their stored
  velocities; a rest plays nothing; a tie keeps the notes before it on (no retrigger) to half of the last tied step.
  SEQ PLAY again stops it (its notes released) and the next start is from step 1.
- **Start**: as the arp's. ARP BEAT SYNC OFF, at once (the clock restarts); ON, on the clock's next step; under MIDI
  CLOCK OUT always on the next step (no restart, no 0xFA to the slaves). Following a MIDI clock (IN, THRU), SEQ PLAY
  arms it: it plays only while that clock runs: 0xFA from step 1, 0xFB on from the step it stopped at, 0xFC pauses
  (notes released, the step kept); no clock, silent. IN NO S/S: the ticks alone.
- **Transpose**: while it plays, hold SEQ REC and press a key: the sequence plays that many semitones from middle C
  (C4, 60 = none: the key's note after the octave buttons) from its next note; with UNISON on (and LOCAL ON, and a
  sequence that is not empty), a key alone transposes. That key does nothing else: it does not sound, is not sent as MIDI, is not recorded, and neither is
  its release; the header shows the transpose. MIDI notes in never transpose. A transposed note outside MIDI 0..127 is
  dropped. Not stored; a program change puts it back to none.
- **Arp and sequencer are exclusive**: SEQ PLAY turns ARP ON off; ARP ON stops the sequence (its notes released).
- A program change (and RESTORE FACTORY) stops it and loads that program's sequence; PANIC (CC 120) stops it; ALL NOTES
  OFF (CC 123) releases its notes and it plays on. Keys played live compete with it for the six voices.
- HOLD (the pad, the sustain message) latches the keys played live only, never the sequence's notes: they end at
  their gate as ever, and with UNISON on the line follows the steps.
- MIDI out: its notes go out as the arp's do (PARAM TX = CC and LOCAL ON; every note-on gets its note-off, on its
  channel; [MIDI.md](MIDI.md)).
- **Footswitch start / stop: not available on the SMK-37.** The OB-6's footswitch-sequence jack has no counterpart: the
  platform has no footswitch input distinct from the sustain message (MIDI CC 64; the SMK-37 has no pedal jack: the
  input board's mux C "pedal" channel, `hal/smk_input.h` `fm1_in.pedal`, is a raw ADC value with nothing documented
  behind it). External start / stop is MIDI's (0xFA / 0xFB / 0xFC above).

### Sequencer

The **Sequencer** screen (Phase 3 Task 5; screen id 8, between Arp + Clock and Program; SEQ opens it, SEQ again goes
back): the state (STOP, PLAY, ARMED: playing, waiting for a MIDI clock; REC), three displays (STEP: playing, the step
played last; recording, the step the next entry makes; LEN: the steps; XP: the transpose), the 64-step grid (16 a row;
a step's notes as red dots from the bottom, one a note up to six; a rest a dash; a tie a blue bar joined to the step
before; past the length an empty frame; the marked step framed white), the pads SEQ PLAY and SEQ REC, the sequence
store's use (`SEQ 12 KB / 76 KB`: KB of written sequences, of the KB the store holds) and two lines saying what the
buttons do now. Play shows `SEQ` (playing), `ARM` (armed) or `REC` (recording) beside VOICES.

**Recording** (step entry; OB-6 reference 5; `firmware/src/ui/seq_rec.h`). One sequence per program, in the edit
buffer: recording only edits it, **WRITE** stores it with the program (a program change, RESTORE FACTORY or a power cut
without WRITE loses it, as any edit).
- **SEQ REC** arms: the Sequencer screen shows, ARP ON goes off (the arp and the sequencer are exclusive; turning ARP ON
  while recording ends the recording), and the sequence is **replaced** from step 1 (no overdub) at the first entry (a
  step, `>`; **a `<` as the very first entry does nothing**: there is nothing yet to erase, and the old sequence stays):
  until then the screen shows it empty, and a recording ended with no entry leaves the sequence as it was (not
  edited: a later WRITE keeps the stored one). SEQ REC again, SEQ
  PLAY (which then plays it), WRITE, a program change or RESTORE FACTORY end it. Recording also ends by itself at **64
  steps** (the 64th entry makes it the last).
- **A step**: press a key (or a MIDI note in) and let it go: one step of that note, at its velocity. **A chord**: keep
  at least one key held while adding notes; the step is recorded when every note is up, each note at its own velocity,
  up to 6 (more are not kept; a note twice is kept once). Keys and MIDI notes in make one chord together.
- The note stored is the key's note **after the octave buttons' shift** (OCTAVE +2 and the C4 key: C6 is stored); the
  global TRANSPOSE (a master tune) is not in it. The velocity stored is the one the synth gets: after the global VEL
  CURVE (a key's velocity 0 is stored as 1; MIDI notes in are stored as received). UNISON and chord memory are not recorded: they apply at playback, as
  when playing live. Notes outside 12..108 are not recorded (a step of only such notes is no step).
- **`>` = INC**: with nothing held, a **rest**. With notes held: they are recorded now and a **tie** follows (the step
  held on, extended); each further `>` while they stay held adds one more tie; their release then adds nothing. A new
  key pressed after that, while some are still held, starts the next step.
- **`<` = DEC**: back one step, erasing it (a chord being entered is not touched). Erasing the step of a chord that is
  still held after `>` tied it gives that chord back as being entered: its release records it again, `>` records and
  ties it again.
- `<` / `>` are DEC / INC on the Sequencer screen only: on any other screen (PATCH, PARA, GLOBE, SEQ take you there)
  they change screens as ever, and recording goes on; the pads ◀ / ▶ always change screens.
- The keys **sound** while recording, and go out as MIDI, as when playing (the OB-6 sounds them). Under **LOCAL OFF**
  the keys are not recorded (they go to MIDI only, as ever): a DAW echoing them back records them once, as MIDI in.
- SEQ REC while the sequence plays: **tapped** (let go with no key pressed while it was held, the sequence still
  playing: SEQ PLAY pressed meanwhile cancels it) it stops the sequence (its notes released) and arms recording; **held + a key** it transposes instead (above), and its release then does
  nothing. The Sequencer screen's SEQ REC pad acts at once (stop, record), never a transpose.

**Key routing** (the keybed; MIDI notes in are only ever played, captured by CHORD and recorded):

| State | A key pressed |
| --- | --- |
| idle | plays (LOCAL ON), goes out as MIDI (ARP ON with PARAM TX: the arp's notes instead) |
| recording | plays, goes out as MIDI, and is recorded (LOCAL ON) |
| playing | plays live beside the sequence (the voices compete), goes out; not recorded |
| playing, SEQ REC held | transposes (the key minus C4); nothing else, its release neither |
| playing, UNISON on (LOCAL ON), the sequence not empty | transposes, no hold needed; nothing else (an empty sequence: the key plays) |

**LFO SYNC** (LFO + X-Mod) locks the LFO to the clock's tempo: LFO FREQ then picks a note value, 16 steps from one
cycle in 4 bars (0) through a bar, a half, a quarter (FREQ 96..111, the default 100 among them), an 8th, 16th, up to
16 cycles a beat (254). With no tempo known (a MIDI clock followed but not there yet) it runs free.

### Distortion

**DISTORTION** (Phase 2 Task 7; OB-6 reference 2.9: an amount per program, 0..127) drives the six voices' sum, before
the output's DC blocker and limiter (and before the effects, when they come): 0 is off (the sound exactly as without
it), the first quarter of the knob adds a little warmth, from about 32 it audibly saturates, 127 is a hard, squared-off
drive (`firmware/src/synth/voices.h`). A chord stays at about its level (its peaks are held down while it gets denser);
a single note gets louder, up to ~+8 dB. It is the program's (stored; a program from before Phase 2 loads with 0).
Turn it with **K6 on Play** (DIST) or, anywhere, **hold FX and turn any of K2..K8**. No MIDI CC yet (as the clock's and
the arpeggiator's values).

Play's pad 0 is **SYNC** (VCO1 sync): the Play mockup's X-MOD performance slot, as ruled for Task 8 (the schema has no
X-Mod on/off; sync is the performance switch the OB-6 player reaches for there). It is the same switch as VCO 1 + 2's
pad 2.

## What the screens show

`firmware/src/ui/screens.c` draws each screen from these same tables: a knob on screen is the encoder bound to it
(K2..K8 in reading order), a lit red button is a top pad's switch (on), an LED list is a select stepped by its
encoder or pad, and the bottom blue strip names what the four faders do. On the two-row screens the row the encoders
or pads reach now is marked: Envelopes lights FILTER or LOUDNESS; LFO + X-Mod and Aftertouch + Misc light PADS next to
the section the top pads switch (the other section's pad labels are greyed). The header shows the KNOBI-SIX mark, an
LED per section (the current one lit), the octave and, on a small red display, the last value edited. Globals also
shows the wheels' and faders' raw readings (for checking a unit's travel). The host probe `scrcheck`
(`tests/scripts/screens.txt`) proves the drawing matches the bindings and that every label fits.

## Programs

500 programs, OB-6 style: **bank** 0..4, then **tens** and **ones** 0..9 (program 2-35 = bank 2, 35). The number is
set by the flash: the base's 8 slots of 16 KB hold 74 records each at 221 B a record (the largest a program may become
in later phases), 592 in all, so 5 whole banks of 100 (`firmware/src/params/store_prog.h`). A program is its 66 values,
its 20-character name and its chord memory, with a CRC (since record version 2 also 20 reserved phase-2/3 values:
distortion, effects, clock, arpeggiator; the distortion's, the clock's and the arpeggiator's are in use, the effects' stored for later;
since version 3 the generation of its sequence in the sequence store: WRITE stores the program and the edit buffer's
sequence together, and a full sequence store refuses the whole WRITE, `SEQ FULL: NOTHING STORED`). One that was never stored loads as its **factory
program** when the build has a factory bank, else as the init program. One whose stored record is broken (or of a
version this firmware does not know) loads as the init program and says so: `Err` in the header, STORED PROGRAM
BROKEN: INIT LOADED on Program. One whose record names a sequence the store cannot read loads with an empty sequence
and says STORED SEQUENCE MISSING: EMPTY LOADED (the name is kept: a WRITE that does not touch the sequence releases
nothing, so it may read again after a restart). A board without the sequence store (the FM-1) writes programs only.

The **factory bank** is optional and read only, like the OB-6's banks 5..9: it exists only in a build made with a
locally generated `firmware/gen/factory_bank.h` ([FACTORY_BANK.md](FACTORY_BANK.md); the default build and CI have
none: every program starts as the init program). With one, on the first boot (empty store) program N is factory
program N, and so is every program never written; a written program replaces it in play, but WRITE never changes the
factory bank: **GLOBE held + BT, twice within 2 s** (restore factory: the first press only asks, since the edits
would be lost) brings factory program N back into the edit buffer, to keep with WRITE or to leave.

- **Select:** on Program, K2 / K3 / K4 turn the bank, tens and ones digits; the program loads as a digit moves (a digit
  stops at its ends, it does not carry). MIDI: program change 0..99 in the current bank, CC 32 picks the bank
  ([MIDI.md](MIDI.md)). A change while notes sound releases them (their release runs; the values glide, no click): the release tails
  already sound with the new program (its filter, levels, shapes glide to it in ~6 ms; its envelope rates at once).
- **WRITE** (pad 2, or BT on any screen): arms (the pad blinks; the digits now pick the destination, nothing loads);
  **ENTER** (pad 3) stores the sound as it is (the program in play with its edits) there, and that program is then the
  one in play. WRITE again cancels. While WRITE is armed or storing, a MIDI program change is ignored: the edit
  stays what WRITE stores (`tests/scripts/write_prog_change.txt`). Storing rewrites the program's 16 KB slot, whose erase stops the chip for some
  200 ms: the sound fades out (10 ms), goes silent while the flash works, and fades back in (10 ms). A deliberate
  dropout, never a click (`tests/scripts/write_held.txt`, `tests/wav_check.py`). **With a sequence (Phase 3)** the
  same WRITE also puts the sequence into the sequence store, in the same silent gap: a sector or two more of erases,
  so the gap is longer: ~0.26 s in the host's model of the flash, ~0.31 s when the store has to compact first, the
  worst case ~0.4 s (`write_held`, `write_compact`); the real flash's timing is for the device to say
  ([SMOKE_TEST.md](SMOKE_TEST.md) §6). The status line under the pads says
  WRITE TO b-tt?, STORED, WRITE FAILED: NOTHING CHANGED or, when the sequence store has no room for the sequence,
  SEQ FULL: NOTHING STORED (nothing at all was written, the old program and sequence still hold; the line under it
  says how to make room: clear sequences, or RESTORE FACTORY on programs and WRITE them). It can come a little before the Sequencer screen's store meter reads full: a full-size sequence needs one flash sector with about 850 B free, and once every sector holds close to 80 % live data none has; a short sequence still fits then). A power loss during a store loses nothing already stored: the 7 slots of 74
  programs live in 8 flash slots, a store goes to the spare one and only then replaces the old copy.
- **MANUAL** (pad 4): the live panel. The sound is what the panel says: the four faders' Play parameters (FILTER FREQ,
  RESONANCE, MODE, ENV AMOUNT) jump to where the faders stand; the program number shows as dashes and MIDI program
  change is ignored. MANUAL again leaves it (the sound stays); turning a digit loads that program. WRITE works from
  MANUAL too.
- **At power-on** the program last loaded or stored comes back (kept with the globals).

## Globals and what is kept

The globals (GLOBE), the keys' octave and the last program are kept in the base's blobs 0 and 1, in turn (a save
goes to the other one, with a sequence number: one cut by a power loss leaves the previous record), saved 2 s after
the last change once nothing has played for 3 s (no voice, key, pad or MIDI note; an erase stalls the sound) and loaded at power-on; nothing saved, broken records
or another version give the defaults (`firmware/src/params/globals.c`). A change of program alone is saved at most once
every 30 s (flash wear). What each does:

| Global | Range (default) | Does |
| --- | --- | --- |
| MIDI CH | ALL, 1..16 (ALL) | the channel heard; sent on (ALL = 1) |
| POT MODE | RELATIVE / PASSTHRU / JUMP (RELATIVE) | the faders (above) |
| PARAM TX / RX | OFF / CC (CC) | panel edits sent as CCs / CCs received ([MIDI.md](MIDI.md)) |
| TRANSPOSE | -12..+12 semitones (0) | the master coarse tune: both VCOs (not the MIDI notes sent) |
| TUNE | -50..+50 cents (0) | the master fine tune |
| VEL CURVE | 0..7 (0) | the keybed's velocity, for the synth and MIDI out: 0 linear, 1..3 softer (more at a light touch), 4..6 harder, 7 fixed at 127 |
| LOCAL | ON / OFF (ON) | OFF: the keybed and the pads' pressure go to MIDI only; MIDI in still plays the synth |
| STEREO | STEREO / MONO (STEREO) | MONO: both outputs carry the sum |
| SUSTAIN | N.O. / N.C. (N.O.) | the sustain message's polarity: N.C. reads MIDI CC 64 upside down (the SMK-37 has no pedal jack) |
| AT CURVE | 0..3 (0) | the bottom pads' pressure: 0 linear, 1 soft, 2 softer, 3 hard (MIDI channel pressure as received) |
| MIDI CLOCK | OFF / OUT / IN / THRU / IN NO S/S (OFF) | Arp + Clock pad 3. OFF: the internal clock (BPM); OUT: the same, sent as MIDI clock (0xFA with the downbeat's 0xF8 when chosen and on every restart, then 24 0xF8 a beat, 0xFC when left); IN: follows a MIDI clock (start / continue / stop obeyed; none arriving, or stopped: the clock and the arpeggiator wait); THRU: IN, and what arrives is sent on; IN NO S/S: follows the clock bytes alone (start / stop ignored). USB only (the SMK-37 has no MIDI jack: no CLOCK PORT). [MIDI.md](MIDI.md) |
| ARP BEAT SYNC | OFF / ON (OFF) | Arp + Clock pad 4: a new arp pattern starts on the clock's next step (ON) or at once, restarting the clock (OFF; under MIDI CLOCK OUT the next step, as ON) |
| DUAL CORE | OFF / ON (OFF) | only in a `KNOBI_CPU1=1` build (never a release; [API.md](API.md) "Second core"): Globals pad 7; ON renders half the voices on the second core (the same sound, bit for bit). Saved in a blob of its own (a default build keeps the other globals as they are) |

Under the pads, the Globals screen's **diagnostics**: `CPU 65% PEAK 80%  VOX 6/6  SHED 0` (the audio block's load,
smoothed, and its peak, decaying over ~1 s, in 5 % steps; the CPU guard's voice limit of 6; how many times it has
shed voices since power-on, the line white once it has: [MEASUREMENTS.md](MEASUREMENTS.md), "CPU guard"), then the
raw wheels and faders (checking a unit's travel). A `KNOBI_CPU1=1` build adds the second core after the wheels:
`C1 OFF` (DUAL CORE off), `C1 ON 40%` (on; the second core's own job time, % of the block; the CPU line stays the
first core's whole interrupt, the waits for the second included), `C1 FAIL` (on, but the second core is not running:
it failed, or it never started; one core until power-off). The CPU1 probe (`--probe-cpu1`, a test package only) puts
four lines of its own above these, `C1 PING n  ERR n  MISREAD n`, `FPU OK  DUAL=SINGLE OK`, `LOAD 1C x%  2C y%  JOB z%`
and `PROBE 8%  C1 IDLE  DUAL OFF` (the probe's own time, the second core's state and the DUAL CORE global, which moves there from the wheels'
line), and drops the version line: [SMOKE_TEST.md](SMOKE_TEST.md) "CPU1 probe".

The OB-6 globals not here: the clock port (USB only), the sequencer jack (no footswitch input on the SMK-37), alternative tunings, the SysEx and MIDI-out
ports and program dumps (later, with SysEx). MIDI CLOCK and ARP BEAT SYNC are on the Arp + Clock screen (pads 3 and 4); they
are globals all the same. The record grew with them (version 2): a version-1 record (Phase 1's) still loads, the two at
OFF; a firmware from before them reads the new record as none (its defaults).

Going back to Phase 1 and up again (a known limit, final review; nothing is installed on a keyboard today): the globals
live in two blobs, written in turn, and the newer record (a higher sequence number) wins at boot. Phase 1 reads a
version-2 record as missing, so after a downgrade it starts from its defaults, or from an older Phase-1 record still in
the other blob. If it then saves, its record may carry a lower sequence number than a version-2 record left in the
other blob (Phase 1 counted from its defaults); after upgrading again Phase 2 picks that stale version-2 record, the
globals as they were before the downgrade, over Phase 1's newer one. Nothing breaks, but the globals changed under
Phase 1 are lost: set them again after such a round trip.
