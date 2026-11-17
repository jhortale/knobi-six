# KNOBI-SIX MIDI

USB MIDI. The code: `firmware/src/ui/midi_map.c`. Globals (GLOBE screen, see [CONTROLS.md](CONTROLS.md)):
**MIDI CH** ALL (default) or 1..16, **PARAM TX** OFF / CC (default CC), **PARAM RX** OFF / CC (default CC). NRPN and
SysEx come later.

## Received (on MIDI CH; ALL = every channel)

| Message | Does |
| --- | --- |
| note on / off | plays the synth with its velocity (note-off velocity ignored); a note played while CHORD is held is captured |
| channel pressure | aftertouch; the synth gets the higher of it and the bottom pads' pressure |
| pitch bend | added to the pitch wheel's bend (±BEND RANGE at the ends) |
| CC 1 | mod wheel (the higher of it and the panel's wheel); only adds LFO depth |
| CC 64 | sustain: ≥ 64 = HOLD on (with the HOLD pad: either) |
| CC 0, CC 32 | bank select (the next program change's bank) |
| program change | Task 10 loads programs; until then the request is only recorded |
| CC 7 | volume: times PROGRAM VOLUME (the master volume, K1, after both); glides like PROGRAM VOLUME (no zipper); kept across program changes; 127 at power-on |
| CC 120 | all sound off: PANIC (every voice silent at once, HOLD off) |
| CC 123 | all notes off: every note released (the envelopes run their release); HOLD (pad or sustain) stays as it is |
| CC 121 | reset controllers: MIDI wheel, bend, pressure and sustain to rest (not volume, as MIDI's RP-015) |
| CC 4 | foot controller: ignored in phase 1 (the reference lists it as received but not what it does) |
| the CCs below | with PARAM RX = CC: sets the parameter (0..127 over its range; bipolar: 64 = 0). Not sent back |

## Sent

Keys: note on / off on MIDI CH (ALL = channel 1). With PARAM TX = CC, an edit made on the panel (encoder, fader, pad)
goes out as its CC on the same channel, only when the 7-bit value changes (a 0..255 parameter moving one step may stay
on the same CC value). Edits received over MIDI are not echoed.

## The CC map

One CC per continuous parameter (the knobs; switches and selects have none until NRPN). Our own map: General MIDI's
sound controllers where one fits (cutoff 74, resonance 71, attack 73, release 72, decay 75, LFO rate 76 / depth 77,
detune 94, portamento time 5), the rest on CCs MIDI leaves undefined. None of 0, 1, 4, 6, 7, 10, 11, 32, 38, 64, 96..101,
120..127 (unit test `cc_map`).

| CC | Parameter | CC | Parameter |
| --- | --- | --- | --- |
| 5 | PORTA RATE | 27 | FENV ATTACK |
| 14 | VCO1 FREQ | 28 | FENV DECAY |
| 15 | VCO1 SHAPE | 29 | FENV SUSTAIN |
| 16 | VCO1 PW | 30 | FENV RELEASE |
| 17 | VCO2 FREQ | 31 | LENV SUSTAIN |
| 18 | VCO2 FINE | 71 | RESONANCE |
| 19 | VCO2 SHAPE | 72 | LENV RELEASE |
| 20 | VCO2 PW | 73 | LENV ATTACK |
| 21 | VCO1 LEVEL | 74 | FILTER FREQ |
| 22 | VCO2 LEVEL | 75 | LENV DECAY |
| 23 | SUB OCTAVE | 76 | LFO FREQ |
| 24 | NOISE | 77 | LFO INITIAL AMT |
| 25 | FILTER MODE | 94 | DETUNE |
| 26 | FENV AMOUNT | 102 | LENV AMOUNT |
| | | 103 | XMOD FILTER ENV |
| | | 104 | XMOD VCO2 |
| | | 105 | AT AMOUNT |
| | | 106 | PAN SPREAD |
| | | 107 | BEND RANGE |
| | | 108 | PROGRAM VOLUME |
