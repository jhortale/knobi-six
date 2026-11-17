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
| BT | `BTN_SAVE` | (Task 10: WRITE) |
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
| Program | — (Task 10: bank / program, WRITE) | — | ◀, ▶ |
| Globals | MIDI CH (ALL, 1..16), POT MODE, PARAM TX (OFF / CC), PARAM RX (OFF / CC) | — | ◀, ▶ |

Every phase-1 parameter is on some screen (unit test `bindings_valid`). The globals here are kept in RAM until Task 10
stores them with the others.

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
