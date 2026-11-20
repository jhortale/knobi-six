# KNOBI-SIX plugin: manual checklist for Ableton Live 12

Nothing here is automated (it needs Live). Build and install first: `plugin/README.md` ("Build", "Install"). Use the
**AU** unless a point says VST3. Tick each point; note what you saw when it is not what is written.

## Load and play

1. **Load.** Live > Settings > Plug-Ins: "Use Audio Units v2" on, Rescan. Drop KNOBI-SIX (Plug-Ins > Audio Units >
   jhortale) on a MIDI track. Look for: it appears, the window opens with the red-and-dark panel, no error.
2. **Play notes.** A held chord, mod wheel, pitch bend, sustain, from a keyboard and from a clip. Look for: six voices,
   sound as the keyboard's init program, no hang on release.
3. **Panel size.** Drag the window's corner: 100 % (1280 x 760) up to 200 %; it cannot go below 100 %. Look for: the
   layout scales as a whole, nothing cut off, the resizer in the corner works.
4. **Session rate.** Settings > Audio: 44.1, 48, 88.2, 96 kHz. Look for: same pitch and sound at each.

## Parameters and automation

5. **Knobs record automation.** Arm the arrangement recording and automation, turn FILTER FREQ with the mouse. Look
   for: an automation lane is written, and the knob's touch (gesture) starts and ends it.
6. **Drawn automation.** Draw automation for FILTER FREQ in the clip or arrangement, play. Look for: the sound follows
   and the panel's knob moves.
7. **MIDI CC and program change, AU then VST3.** With the arrangement recording and automation armed, send CC 74 and a
   program change (a clip, or a controller) into the instance. Look for: with the AU, no automation is written for
   what the core changed itself; with the VST3, whether Live writes automation, or switches a parameter's automation
   off (the known VST3 issue). Report which.
8. **Programs.** The device's program menu lists `000 INIT` ... `499 INIT` (the default build). Pick one, and a MIDI
   program change in a clip. Look for: it loads; the panel's number follows.

## Sets and instances

9. **Save and reopen a set.** Edit a sound and the sequence (EDITED lit), save the set, close Live, reopen. Look for:
   the same sound, the sequence, and the EDITED / DIFFERS FROM BANK lights back.
10. **Two instances.** Two tracks, two different sounds, both playing. Look for: no glitches, no interference.
11. **WRITE across instances.** In instance A press WRITE and store slot 45 (WRITE, choose with the digits, WRITE).
    Look for: "PROGRAM 045 STORED" in A. In instance B, within about a quarter second (the library's reload; B's sound
    never changes, a WRITE loads nothing):
    - B on program 045: the display stays 0 45 and B's own program name (what B plays, not A's); the third LED turns
      to USER SLOT (lit; it read FACTORY, unlit, before); DIFFERS FROM BANK lights when A's stored sound is not the one
      B plays (unlit if they are the same); EDITED as it was. Pick 045 again in B (or send program change 45): B now
      plays A's sound, DIFFERS FROM BANK and EDITED off.
    - B on another program: its line does not change (number, name, LEDs are its own program's). Live's program menu
      for B lists `045` with A's name; choosing 045 in B's browser loads A's sound, with USER SLOT lit.
    Check `~/Music/KNOBI-SIX/library.knobi` exists.

## Sequencer and clock

12. **In time with a clip.** A MIDI clip of 16th notes on a second track (a drum rack) and the sequencer playing the
    same grid (SEQ PLAY on, Live plays from bar 1). Look for: steps land together with the clip, at 44.1 and 48 kHz
    (the plugin delay compensation check: both tracks sound at the same instant).
13. **Latency compensation.** One clip note and one sequencer step on the same beat line. Look for: they line up (no
    flam). The device shows "Delay Compensation" in its context menu (256 samples at 44.1 kHz = 5.8 ms).
14. **Loop.** Loop 1 or 2 bars, play. Look for: steps keep their place across the wrap (the loop start plays its
    step), none doubled or skipped, no hanging note.
15. **Locate.** Jump to a bar line and mid-bar while playing. Look for: it lands on the matching step, notes released.
16. **Stop.** Stop Live with a chord held and the arp on. Look for: the sequencer stops (SEQ PLAY stays lit, WAITING
    FOR LIVE); the arp keeps running at the program's BPM; play again: the sequencer starts on the song's step.
17. **Tempo automation.** Draw a tempo ramp. Look for: the steps follow it next to a drum clip; the BPM display shows
    Live's tempo greyed.
18. **Step entry from Live.** Click SEQ REC, play notes from a MIDI clip or keyboard; try REST, TIE (with a key held
    and not), BACK. Look for: steps appear on the grid as they come; a chord when a key stays held. Also Live's
    Computer MIDI Keyboard (`M`): its notes are recorded the same.
19. **Grid edits while playing.** Click steps, add / remove notes, change the length, REST / TIE while the sequence
    plays. Look for: the change is heard from the next step, never a stuck note.
20. **WRITE keeps playing.** While the sequence plays, WRITE to a slot. Look for: sound and sequence do not stop.

## Load

21. **CPU.** Live's CPU meter with six voices held and the arp or sequencer on (and with 4-8 instances). Look for:
    a low, steady figure; at a small buffer (64-128) no crackle. Report the percentage and the buffer size.

## Report

Write down for each point: ok / not ok, what you saw, the Live version, the rate and buffer, AU or VST3.
