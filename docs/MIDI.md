# KNOBI-SIX MIDI

USB MIDI. The code: `firmware/src/ui/midi_map.c`. Globals (GLOBE screen, see [CONTROLS.md](CONTROLS.md)):
**MIDI CH** ALL (default) or 1..16, **PARAM TX** OFF / CC (default CC), **PARAM RX** OFF / CC (default CC); **MIDI
CLOCK** (Arp + Clock screen) OFF (default) / OUT / IN / THRU / IN NO S/S. NRPN and SysEx come later.

## Received (on MIDI CH; ALL = every channel)

| Message | Does |
| --- | --- |
| note on / off | plays the synth with its velocity (note-off velocity ignored); a note played while CHORD is held is captured; with ARP ON: a key of the arpeggiator's pattern, as a key of the keybed |
| channel pressure | aftertouch; the synth gets the higher of it and the bottom pads' pressure |
| pitch bend | added to the pitch wheel's bend (±BEND RANGE at the ends) |
| CC 1 | mod wheel (the higher of it and the panel's wheel); only adds LFO depth |
| CC 64 | sustain: ≥ 64 = HOLD on (with the HOLD pad: either); the global SUSTAIN = N.C. reads it upside down (< 64 = on) |
| CC 32 | bank select 0..4: the next program change's bank (kept until another CC 32; before any, the current program's bank). 5 and up: the program change is ignored |
| CC 0 | ignored: CC 32 alone picks the bank (as the OB-6 receives it; 5 banks need no second byte) |
| program change | 0..99: that program of the bank, loaded (notes sounding release, no click); ignored in MANUAL and while a WRITE is armed or committing (the edit buffer is what WRITE stores) |
| CC 7 | volume: times PROGRAM VOLUME (the master volume, K1, after both); glides like PROGRAM VOLUME (no zipper); kept across program changes; 127 at power-on |
| CC 120 | all sound off: PANIC (every voice silent at once, HOLD off) |
| CC 123 | all notes off: every note released (the envelopes run their release), the notes HOLD has latched too; HOLD itself (pad or sustain) stays on, latching the notes played after it |
| CC 121 | reset controllers: MIDI wheel, bend, pressure and sustain to rest (not volume, as MIDI's RP-015) |
| CC 4 | foot controller: ignored in phase 1 (the reference lists it as received but not what it does) |
| the CCs below | with PARAM RX = CC: sets the parameter (0..127 over its range; bipolar: 64 = 0). Not sent back |

### MIDI clock in (any channel setting; the global MIDI CLOCK)

The global **MIDI CLOCK** (Arp + Clock screen, pad 3; [CONTROLS.md](CONTROLS.md)) decides. OFF and OUT ignore incoming clock.
IN, THRU and IN NO S/S follow it (`firmware/src/synth/clock.h`):

| Byte | IN / THRU | IN NO S/S |
| --- | --- | --- |
| 0xF8 clock (24 a quarter note) | moves the clock one tick; the tempo is measured over the last beat (24 ticks), so it locks within a beat or two | the same; the first tick after a silence starts from the downbeat |
| 0xFA start | the next 0xF8 is the downbeat: step 0 there | ignored |
| 0xFB continue | runs on from where it stopped | ignored |
| 0xFC stop | stopped: no steps (the arpeggiator waits) | ignored |

No 0xF8 for 0.25 s (more than three ticks at 30 BPM, the slowest tempo) and the clock is not running: per the OB-6,
with the clock set to follow and none present, the arpeggiator does not run. Ticks again: it runs on (unless stopped).
Steps fall on the ticks received (the audio takes the queue at each 5.8 ms block: within one block). THRU sends every
clock byte received straight on. The program's BPM is kept (and TAP still sets it) but not used while following.

### The autosave and MIDI notes

The globals, the octave and the program last loaded are saved once nothing has been played for 3 s (an erase stalls
the sound; [CONTROLS.md](CONTROLS.md)). A MIDI note on or off counts as playing **on any channel**, whatever MIDI CH
says: a sequencer or keyboard sending notes without a 3 s pause (on a channel this unit ignores, too) postpones the
save indefinitely; it happens at the first 3 s of quiet. Nothing is lost meanwhile, short of a power cut.

## Sent

Keys: note on / off on MIDI CH (ALL = channel 1). Arpeggiator: with LOCAL ON, ARP ON and PARAM TX = CC, the keys feed
the arp and the notes it plays go out instead (note on with the key's velocity, note off at the end of its gate); the
keys' own notes do not (a receiver would play the chord under the arp). Every arp note-on sent gets its note-off, even
if PARAM TX is turned off meanwhile. With PARAM TX = OFF the keys go out as played, the arp's notes stay home. With
**LOCAL OFF** the keys never reach the synth or its arp: they go out as played, as ever (a DAW controller), whatever
ARP ON and PARAM TX say, and no arp notes go out for them.
Keys held while one of those three changes: the receiver is kept in step. Keys that start feeding the arp send their
note-offs; keys that stop (ARP OFF: the synth plays them) send their note-ons then, except a key whose note the arp has
on at the receiver right then: the key takes that note over (the arp's own note-off is then not sent) and its note-off
goes out when it is let go. A key's note-off goes out only if its note-on did. With PARAM TX = CC, an edit made on the panel (encoder, fader, pad)
goes out as its CC on the same channel, only when the 7-bit value changes (a 0..255 parameter moving one step may stay
on the same CC value). Edits received over MIDI are not echoed.

MIDI clock, with the global MIDI CLOCK = OUT: when OUT is chosen the clock restarts on step 0 and 0xFA goes out with
that downbeat's 0xF8 right after it (a receiver takes the first 0xF8 after 0xFA as its downbeat: its steps fall with
ours, not a tick late); then 24 0xF8 a quarter note at the clock's BPM (sent from the main loop: within a few ms of
their time); a restart of our clock (the arpeggiator starting with ARP BEAT SYNC off) sends 0xFA and the new downbeat's
0xF8 again, so receivers restart with it; 0xFC when another setting is chosen. THRU: the clock bytes received, as they come. BPM and DIVISION have no CC (NRPN later).

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
