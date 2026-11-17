# KNOBI-SIX controls

What each control of the SMK-37 Pro does. The code: `firmware/src/ui/controls.c` (the tables `BINDINGS`,
`BINDINGS_ROW2`), MIDI: [MIDI.md](MIDI.md). Principles from the spec (Controls and UI, Our calls): K1 stays the master
volume; K2..K8 are the knobs of the screen, in reading order; the faders follow the screen through the pot mode; the
top row of pads is switches, the bottom row is pressure only.

## Buttons

| Button (printed) | id | Does |
| --- | --- | --- |
| `<` / `>` | `BTN_LEFT` / `BTN_RIGHT` | the previous / next section, in panel order: Play, VCO 1 + 2, Mixer + Filter, Envelopes, LFO + X-Mod, Aftertouch + Misc, Program, Globals (stops at the ends) |
| PATCH | `BTN_HOME` | Play |
| PARA | `BTN_EDIT` | the last edit section visited (VCO 1 + 2 at first) |
| GLOBE | `BTN_GLO` | Globals; again: back to where you were |
| SCALE | `BTN_SEL` | the other row of a two-row screen: Envelopes FILTER / LOUDNESS, LFO + X-Mod pads LFO / X-MOD, Aftertouch + Misc pads AFTERTOUCH / MISC |
| CHORD | `BTN_ENV` | chord memory: hold it, play up to 6 notes (keys or MIDI), let go: those notes are the program's chord and UNISON goes on with UNISON VOICES = CHD. Tapped with no note: nothing changes |
| OCTAVE- / + | `BTN_OCT_DOWN` / `UP` | the keys' octave, -2..+2 |
| BT | `BTN_SAVE` | WRITE: arms a write (shows Program, the destination = the current program); again: cancels (see [Programs](#programs)) |
| KNOB BANK, FADER BANK, ARP, NOTE REPEAT, FX, SEQ, SEQ PLAY, SEQ REC | | nothing yet (arp, effects, sequencer: phases 2 and 3) |

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
| Play | FILTER FREQ, RESONANCE, MODE, ENV AMOUNT | FREQ, RESO, MODE, ENV AMOUNT | SYNC (VCO1 sync), UNISON, ARP (phase 2), HOLD |
| VCO 1 + 2 | VCO1 FREQ, SHAPE, PW, VCO2 FREQ, FINE, SHAPE, PW | VCO1 SHAPE, VCO1 PW, VCO2 SHAPE, VCO2 PW | ◀, ▶, SYNC, LOW FREQ, KEYBOARD |
| Mixer + Filter | VCO1, VCO2, SUB OCT, NOISE, FREQ, RESO, MODE | FREQ, RESO, MODE, ENV AMOUNT | ◀, ▶, BP, KEY TRACK (off / half / full) |
| Envelopes, FILTER row | ATTACK, DECAY, SUSTAIN, RELEASE, AMOUNT | A, D, S, R | ◀, ▶, VELOCITY > FILTER, VELOCITY > AMP, ROW |
| Envelopes, LOUDNESS row | ATTACK, DECAY, SUSTAIN, RELEASE, AMOUNT | A, D, S, R | the same |
| LFO + X-Mod, LFO row | LFO FREQ, LFO SYNC, SHAPE, AMOUNT, X-MOD FILTER ENV, X-MOD VCO2 | LFO FREQ, LFO AMT, XM FILTER ENV, XM VCO2 | ◀, ▶, LFO > VCO1, VCO2, PW (none / PW1 / PW2 / both), AMP, F FREQ, F MODE |
| LFO + X-Mod, X-MOD row | the same | the same | ◀, ▶, X-MOD > VCO1, SHAPE1, PW1, F FREQ, F MODE, NORM-BP |
| Aftertouch + Misc, AFTERTOUCH row | AT AMOUNT, DETUNE, PAN SPREAD, BEND RANGE, KEY MODE, PROGRAM VOLUME, PORTA RATE | AT AMOUNT, DETUNE, PORTA RATE, PRG VOLUME | ◀, ▶, AT > VCO1, VCO2, LFO AMT, LOUDNESS, F FREQ, F MODE |
| Aftertouch + Misc, MISC row | the same | the same | ◀, ▶, UNISON, UNISON VOICES (1..6, CHD), PORTAMENTO, PORTA MODE |
| Program | BANK (0..4), TENS (0..9), ONES (0..9) | — | ◀, ▶, WRITE, ENTER, MANUAL |
| Globals | MIDI CH (ALL, 1..16), POT MODE, PARAM TX (OFF / CC), PARAM RX (OFF / CC), TRANSPOSE (-12..+12), TUNE (-50..+50 cents), VEL CURVE (0..7) | — | ◀, ▶, LOCAL (ON / OFF), STEREO / MONO, SUSTAIN (N.O. / N.C.), AT CURVE (0..3) |

Every phase-1 parameter is on some screen (unit test `bindings_valid`). The globals are kept in flash (below).

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
its 20-character name and its chord memory, with a CRC; one that was never stored (or is broken) loads as the init
program.

- **Select:** on Program, K2 / K3 / K4 turn the bank, tens and ones digits; the program loads as a digit moves (a digit
  stops at its ends, it does not carry). MIDI: program change 0..99 in the current bank, CC 32 picks the bank
  ([MIDI.md](MIDI.md)). A change while notes sound releases them (their release runs; the values glide, no click): the release tails
  already sound with the new program (its filter, levels, shapes glide to it in ~6 ms; its envelope rates at once).
- **WRITE** (pad 2, or BT on any screen): arms (the pad blinks; the digits now pick the destination, nothing loads);
  **ENTER** (pad 3) stores the sound as it is (the program in play with its edits) there, and that program is then the
  one in play. WRITE again cancels. Storing rewrites the program's 16 KB slot, whose erase stops the chip for some
  200 ms: the sound fades out (10 ms), goes silent while the flash works, and fades back in (10 ms). A deliberate
  dropout, never a click (`tests/scripts/write_held.txt`, `tests/wav_check.py`). The status line under the pads says
  WRITE TO b-tt?, STORED or WRITE FAILED. A power loss during a store loses nothing already stored: the 7 slots of 74
  programs live in 8 flash slots, a store goes to the spare one and only then replaces the old copy.
- **MANUAL** (pad 4): the live panel. The sound is what the panel says: the four faders' Play parameters (FILTER FREQ,
  RESONANCE, MODE, ENV AMOUNT) jump to where the faders stand; the program number shows as dashes and MIDI program
  change is ignored. MANUAL again leaves it (the sound stays); turning a digit loads that program. WRITE works from
  MANUAL too.
- **At power-on** the program last loaded or stored comes back (kept with the globals).

## Globals and what is kept

The globals (GLOBE), the keys' octave and the last program are kept in the base's blobs 0 and 1, in turn (a save
goes to the other one, with a sequence number: one cut by a power loss leaves the previous record), saved 2 s after
the last change once nothing sounds (an erase stalls the sound) and loaded at power-on; nothing saved, broken records
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

The OB-6 globals not here: MIDI clock and clock port (phase 2), the sequencer jack, alternative tunings, the SysEx and
MIDI-out ports and program dumps (later, with SysEx), arp beat sync (phase 2).
