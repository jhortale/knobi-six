# KNOBI-SIX MIDI

USB MIDI. The code: `firmware/src/ui/midi_map.c`. Globals (GLOBE screen, see [CONTROLS.md](CONTROLS.md)):
**MIDI CH** ALL (default) or 1..16, **PARAM TX** OFF / CC (default CC), **PARAM RX** OFF / CC (default CC); **MIDI
CLOCK** (Arp + Clock screen) OFF (default) / OUT / IN / THRU / IN NO S/S. NRPN and SysEx come later.

## Received (on MIDI CH; ALL = every channel)

| Message | Does |
| --- | --- |
| note on / off | plays the synth with its velocity (note-off velocity ignored); a note played while CHORD is held is captured; with ARP ON: a key of the arpeggiator's pattern, as a key of the keybed; while a sequence is being recorded (SEQ REC): recorded as a key is, by note number (keys and MIDI notes make one chord: [CONTROLS.md](CONTROLS.md) "Sequencer"). A MIDI note never transposes a playing sequence (only the keys do) |
| channel pressure | aftertouch; the synth gets the higher of it and the bottom pads' pressure |
| pitch bend | added to the pitch wheel's bend (±BEND RANGE at the ends) |
| CC 1 | mod wheel (the higher of it and the panel's wheel); only adds LFO depth |
| CC 64 | sustain: ≥ 64 = HOLD on (with the HOLD pad: either); the global SUSTAIN = N.C. reads it upside down (< 64 = on) |
| CC 32 | bank select 0..4: the next program change's bank (kept until another CC 32; before any, the current program's bank). 5 and up: the program change is ignored |
| CC 0 | ignored: CC 32 alone picks the bank (as the OB-6 receives it; 5 banks need no second byte) |
| program change | 0..99: that program of the bank, loaded (notes sounding release, no click) with its sequence (a playing sequence stops, its transpose goes back to none; a recording ends); ignored in MANUAL and while a WRITE is armed or committing (the edit buffer is what WRITE stores) |
| CC 7 | volume: times PROGRAM VOLUME (the master volume, K1, after both); glides like PROGRAM VOLUME (no zipper); kept across program changes; 127 at power-on |
| CC 120 | all sound off: PANIC (every voice silent at once, HOLD off; a playing arpeggiator or sequence stops) |
| CC 123 | all notes off: every note released (the envelopes run their release), the notes HOLD has latched too; HOLD itself (pad or sustain) stays on, latching the notes played after it. A playing sequence has its notes released and **plays on** from its next step (only CC 120 stops it) |
| CC 121 | reset controllers: MIDI wheel, bend, pressure and sustain to rest (not volume, as MIDI's RP-015) |
| CC 4 | foot controller: ignored in phase 1 (the reference lists it as received but not what it does) |
| the CCs below | with PARAM RX = CC: sets the parameter (0..127 over its range; bipolar: 64 = 0). Not sent back |

### MIDI clock in (any channel setting; the global MIDI CLOCK)

The global **MIDI CLOCK** (Arp + Clock screen, pad 3; [CONTROLS.md](CONTROLS.md)) decides. OFF and OUT ignore incoming clock.
IN, THRU and IN NO S/S follow it (`firmware/src/synth/clock.h`):

| Byte | IN / THRU | IN NO S/S |
| --- | --- | --- |
| 0xF8 clock (24 a quarter note) | moves the clock one tick; the tempo is measured over the last beat (24 ticks), so it locks within a beat or two | the same; the first tick after a silence starts from the downbeat |
| 0xFA start | the next 0xF8 is the downbeat: step 0 there (a sequence playing or armed: from its step 1) | ignored |
| 0xFB continue | runs on from where it stopped (a sequence: on from the step it stopped at) | ignored |
| 0xFC stop | stopped: no steps (the arpeggiator waits; the sequencer pauses, its step kept) | ignored |

Under IN and THRU the clock **waits for a start**: ticks alone give the tempo but no steps until a 0xFA (the next 0xF8
is the downbeat) or a 0xFB arrives, so the arpeggiator stays silent until the sender's transport starts (the screen
says MIDI CLOCK STOPPED). IN NO S/S has no such wait: it runs from the first tick after a silence.

No 0xF8 for 0.25 s (more than three ticks at 30 BPM, the slowest tempo) and the clock is not running: per the OB-6,
with the clock set to follow and none present, the arpeggiator does not run. Ticks again: it runs on (unless stopped).
Steps fall on the ticks received (the audio takes the queue at each 5.8 ms block: within one block). THRU sends every
clock byte received straight on. The program's BPM is kept (and TAP still sets it) but not used while following.

### The sequencer and MIDI (Phase 3)

There is no CC or NRPN for SEQ PLAY, SEQ REC or the sequence itself (no SysEx dump yet), and no footswitch message
beyond CC 64, which is the sustain (HOLD): it latches the keys played live, never the sequence's notes. What MIDI does
reach, all described above:
- **Notes in** are recorded while a sequence is being recorded (SEQ REC), on the MIDI CH heard, with the keys: one chord
  between them, velocity as received (0 = a note-off); under LOCAL OFF too (the keys then are not recorded, so a DAW
  that echoes them back records each note once). They never transpose a playing sequence: only the keys do (SEQ REC
  held, or UNISON on).
- **Clock in** (IN, THRU): SEQ PLAY arms the sequence and it plays only while the clock runs: 0xFA from
  step 1, 0xFB on from the step it stopped at, 0xFC pauses it (notes released, the step kept), the clock going away
  silences it (its notes released, its step kept); a Start while it plays restarts it at step 1. IN NO S/S ignores
  start, continue and stop: the sequence follows the ticks alone. With **OUT** SEQ PLAY starts the sequence on the next step of the division and
  sends no 0xFA (the slaves are not restarted).
- **Notes out**: a playing sequence's notes go out as the arpeggiator's do (Sent, below).
- A DAW that sends CC 123 and 0xFC at its stop leaves a sequence that follows its clock armed (CC 123 only releases
  notes); one that must be stopped from MIDI needs CC 120.

### The autosave and MIDI notes

The globals, the octave and the program last loaded are saved once nothing has been played for 3 s (an erase stalls
the sound; [CONTROLS.md](CONTROLS.md)). A MIDI note on or off counts as playing **on any channel**, whatever MIDI CH
says: a sequencer or keyboard sending notes without a 3 s pause (on a channel this unit ignores, too) postpones the
save indefinitely; it happens at the first 3 s of quiet. Nothing is lost meanwhile, short of a power cut.

With **MIDI CLOCK = OUT** running, the autosave still happens at the first 3 s of quiet (the clock running is not
playing). Its flash erase holds the interrupts off for about 50 ms (docs/RULES.md), so the 0xF8 stream pauses that
long: ~2.4 ticks at 120 BPM. The clock itself runs in the audio interrupt, so it stops for the same time and goes on
from where it was (no 0xFA, no burst of ticks): a slave stays in step with us, but both fall ~50 ms behind the wall
clock, and the slave's tempo estimate sees one long tick gap (a hiccup; a DAW recording against our clock would show
it). No code change for this; to avoid it, let the save happen before a take (3 s of quiet after changing a global,
the octave or the program).

## Sent

Keys: note on / off on MIDI CH (ALL = channel 1). Arpeggiator: with LOCAL ON, ARP ON and PARAM TX = CC, the keys feed
the arp and the notes it plays go out instead (note on with the key's velocity, note off at the end of its gate); the
keys' own notes do not (a receiver would play the chord under the arp). Every arp note-on sent gets its note-off, even
if PARAM TX or LOCAL is turned off meanwhile, and on the channel its note-on went out on, even if MIDI CH changed
meanwhile. With PARAM TX = OFF the keys go out as played, the arp's notes stay home. With **LOCAL OFF** the keys never
reach the synth or its arp: they go out as played, as ever (a DAW controller), whatever ARP ON and PARAM TX say. The arp
then plays what MIDI in feeds it, here only: its notes are not sent under LOCAL OFF, so a DAW that echoes our notes
back (its MIDI thru, as a DAW controller needs) never feeds the arp its own notes (with a latch on, a loop that would
sustain itself).
Sequencer (Phase 3): the notes a playing sequence plays go out under the arp's rules, through the same path: with
PARAM TX = CC and LOCAL ON, note on with the step's velocity, note off at the end of its gate (a tie sends nothing: the
note stays on), each note-off on its note-on's channel and sent whatever PARAM TX, LOCAL or MIDI CH became meanwhile;
stopping it, a program change, ARP ON, PANIC, ALL NOTES OFF, a MIDI stop or the clock going away send the offs of what
is on. PANIC (CC 120) stops the sequence; ALL NOTES OFF (CC 123) only releases its notes and it plays on at its next
step (a DAW sending CC 123 at its stop does not disarm a sequence that follows its clock). Should the main loop stall
long enough for the shared out ring to overrun, every note still on at the receiver gets its note-off at once. The keys played with it go out as played (they are not the sequence's).
Recording a sequence (Phase 3 Task 5): the keys go out as played (they sound too; the arp is off while recording). A
key that transposes a playing sequence (SEQ REC held, or UNISON on with LOCAL ON) sends nothing, nor does its release.
Under LOCAL OFF the keys are not recorded (they go out; a DAW echoing them back records them once, through MIDI in).
Keys held while one of those three changes: the receiver is kept in step. Keys that start feeding the arp send their
note-offs; keys that stop (ARP OFF: the synth plays them) send their note-ons then, except a key whose note the arp has
on at the receiver right then: the key takes that note over (the arp's own note-off is then not sent) and its note-off
goes out when it is let go. A key's note-off goes out only if its note-on did. With PARAM TX = CC, an edit made on the panel (encoder, fader, pad)
goes out as its CC on the same channel, only when the 7-bit value changes (a 0..255 parameter moving one step may stay
on the same CC value). Edits received over MIDI are not echoed.

MIDI clock, with the global MIDI CLOCK = OUT: when OUT is chosen the clock restarts on step 0 and 0xFA goes out with
that downbeat's 0xF8 right after it (a receiver takes the first 0xF8 after 0xFA as its downbeat: its steps fall with
ours, not a tick late); then 24 0xF8 a quarter note at the clock's BPM (sent from the main loop: within a few ms of
their time); 0xFC when another setting is chosen. A new arp pattern never restarts the clock under OUT (whatever ARP
BEAT SYNC says, it starts on the next step of the division), so no 0xFA goes out mid-song: a slaved DAW or drum
machine never jumps back to its bar start on a chord. THRU: the clock bytes received, as they come. BPM and DIVISION have no CC (NRPN later).

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
