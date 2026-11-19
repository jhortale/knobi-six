# KNOBI-SIX plugin (AU / VST3 for Ableton Live) — design

Date: 2026-11-19 · Status: ready for approval · Builds on the firmware specs (2026-11-16 standalone, 2026-11-17
Phase 2, 2026-11-18 Phase 3), which stay binding for the sound and behaviour.

## Intent

João wants to **make music with KNOBI-SIX in his Ableton Live sets** while the SMK-37 is away (and after): the same
synth — sound, arp, clock, sequencer, programs — as an instrument plugin in Live 12 on his Mac. A browser workbench
(rehearsing the hardware UI) may come later; it is not this project.

## Goals

- An **AU and VST3 instrument** that Live 12 Suite loads on this Mac (Apple Silicon), built with **JUCE** (GPL, like
  KNOBI-SIX).
- **The firmware's own synth code** inside it (synth/, engine.c, params/, the arp, clock, sequencer, distortion): one
  source of truth, so the plugin sounds and behaves like the keyboard. No forked copy of the DSP.
- **A full OB-6-style panel** as the window: every section visible at once (oscillators, mixer, filter, envelopes,
  LFO / X-Mod, aftertouch / misc, arp, clock, sequencer, programs), based on the approved mockups in
  `docs/design/standalone-ui/` (panel-style-v2, sections-v2).
- **Every sound parameter is a Live parameter**: automatable, MIDI-mappable, visible on Push; changes from the panel,
  automation and MIDI CC all agree.
- **A shared 500-program bank** like the keyboard (bank / tens / ones browser, WRITE, RESTORE FACTORY), kept in one
  library file on this Mac and shared by every set and instance; the local OB-6 factory bank (with sequences) fills the
  factory layer, as in the local firmware build. Each Live set also saves the instance's current sound (program +
  sequence + edits) in its state, so a set reopens as it was even if the bank changed.
- **Sequencer and arp in time with Live**: Live's tempo and transport drive the clock (Start / Stop / position), like
  MIDI CLOCK IN; the sequencer records from Live MIDI with SEQ REC on the panel.

## Non-goals / anti-goals

- Not the hardware rehearsal UI (no 240×240 firmware screen view, no SMK-37 control layout) — later, browser.
- No program swap with the keyboard yet (needs a dump format the firmware lacks) — a follow-up once the keyboard is back.
- No Windows, no AAX, no App Store / notarized distribution in this project; a local build for João's Mac.
- The OB-6 factory data never leaves this Mac: the plugin build that includes it is local-only, like `-local` firmware
  packages; nothing with OB-6 data or names is committed or published.
- The plugin must never touch the keyboard (no flashing, no install paths).
- Not a different synth: no extra voices, no new DSP, no "plugin-only" sound features in this project. 6 voices + the
  CPU guard behaviour stay as on the device (the guard may simply never trigger on a Mac).

## Constraints

- Firmware sources are shared, not copied: the plugin compiles the needed `firmware/src` modules directly; any change
  needed to make them build outside the unity/platform build is a small, guarded refactor that keeps the firmware image
  and all firmware tests unchanged.
- The firmware renders fixed blocks (256 frames, 16-frame control periods) at 44.1 kHz, integer/Q15 out. The plugin
  adapts: internal 44.1 kHz rendering with a FIFO for any host block size, resampled to the session rate (44.1/48/88.2/
  96 kHz) with a good-quality resampler, and the resulting latency reported to Live so it stays in time.
- Real-time safe audio thread: no allocation, locks or file I/O in processBlock; library file I/O on a background or
  message thread.
- Licensing: GPL-3.0 project; JUCE under its GPL licence; credits added to LICENSING.md / README.
- Built with CMake (via Homebrew) and Xcode 26.6 on this Mac; ad-hoc signed for local use; installs to
  ~/Library/Audio/Plug-Ins/Components and /VST3; must pass `auval`.

## Approaches considered

- **JUCE (chosen by João)**: one codebase for AU + VST3, reliable in Live, ready-made parameters/state/UI/MIDI; GPL fits.
- CLAP + wrapper: lean, MIT; Live 12 doesn't load CLAP natively, AU wrapper least proven.
- iPlug2: permissive, smaller community.

## Design decided with João

- Plugin first (before any browser workbench), for making music in Live sets.
- JUCE.
- Window = the full OB-6 panel (option 2); no hardware view in the first version.
- Programs = shared 500-program bank like the keyboard (option b); swap with the keyboard later.
- Sequencer on the panel = step entry like the keyboard **plus mouse editing** of the 64-step grid: click a step to
  set or clear notes (up to 6), change each note's velocity, make it a rest or a tie, change the length. Same stored
  format and limits as the firmware (64 steps, 6 notes, notes 12–108, tie rules of seq.h), so every sequence stays
  playable on the SMK-37; mouse edits go through the same edit-buffer path (edited flag, WRITE to the bank).

## Our calls (correct if wrong)

- **Clock**: follow Live's transport when it plays (tempo, start/stop, bar position → step 1 on the bar), internal BPM
  (the program's) when Live is stopped — equivalent to the firmware's MIDI CLOCK IN plus internal fallback; MIDI CLOCK
  OUT not offered in the plugin.
- **MIDI in**: notes, velocity, pitch bend, mod wheel, channel pressure (aftertouch), sustain, the firmware's CC map,
  program change (+ bank select) → the bank. Sequence/arp notes are not sent to MIDI out in the first version.
- **Globals**: the firmware's globals that matter in a plugin (master tune, transpose, velocity / aftertouch curves,
  stereo/mono, pot behaviour not needed) live in the library file; MIDI channel = omni in the plugin.
- **Library file**: `~/Music/KNOBI-SIX/library.knobi` (the firmware's packed program + sequence records, versioned,
  written atomically by temp-file + rename); the factory layer comes from the local factory header compiled in.
- **Parameter IDs** are stable (the firmware's param ids) so automation in saved sets survives updates.
- **Panel** sizes: resizable, 100–200 %, default fits a 1440-wide screen.

## Our calls after the builder check (correct if wrong)

- **What is shared**: synth/, engine.c, params/ (programs, globals data model, prog_seq pairing logic), the sequencer
  model and player, the arp and clock, **and** the behaviour-carrying parts of ui/: the CC map / MIDI handling
  (ui/midi_map.c: CC map, mod wheel + aftertouch merge, sustain / HOLD, bank select + program change) and step entry
  (ui/seq_rec.c). Not shared: controls.c, screens.c, panel_gfx.c, lcd/usb/flash/platform. Making these build outside
  the unity build is a guarded refactor (headers / `static` → module boundaries) that leaves the firmware image's
  behaviour and every firmware test unchanged; the CPU1 path is compiled out (KNOBI_CPU1=0).
- **Threading**: one engine instance per plugin instance, driven only on the audio thread. MIDI and automation are
  applied inside processBlock at their sample offsets (the internal render is split at event times, so notes are
  sample-accurate); panel edits from the message thread go through a lock-free queue into the same path. The
  firmware's producer/consumer queue is kept as the engine's input (processBlock is producer and consumer).
- **Clock from Live**: a new clock entry point that follows the host directly (tempo + PPQ position + playing), so the
  arp / sequencer step is derived from Live's position: starting mid-bar, looping and locating land on the matching
  step; tempo automation is followed. When Live is stopped, the internal clock runs at the program's BPM (P_BPM). P_BPM
  stays a parameter; it is ignored while Live plays (shown greyed).
- **Parameters**: all live ids (Phase 1 params + P_DISTORTION, P_BPM, P_CLK_DIV, P_ARP_*), as JUCE parameters with
  their firmware ids as stable parameter IDs; switches → bool, selects → choice, knobs → integer steps over their
  range (bipolar centred). Not exposed: the stored-only FX ids, P_SEQ_GEN, the chord, the name. Loading a program
  updates every parameter's value for display without writing automation (Live's usual behaviour for preset loads).
  Programs are also exposed as the host program list (500 entries) and follow MIDI program change + bank select.
- **Library file**: header (magic, version) + 500 user slots, each empty (= factory) or a full copy of program +
  sequence (the firmware's packed records), plus the globals. Written atomically (temp + rename); last write wins;
  every instance reloads when the file changes (file watcher), so a WRITE in one instance appears in the others. The
  factory layer stays compiled in, read-only; RESTORE FACTORY + WRITE empties the user slot.
- **Factory bank gating**: a CMake option, off by default. The default plugin build uses no factory data (empty or a
  synthetic test layer); only an explicit local option compiles in firmware/gen/factory_bank.h, and that binary is
  marked local-only (name suffix `-local`, like firmware packages). Nothing committed can produce an OB-6-data binary.
- **Render comparison**: same MIDI in → the plugin's internal 44.1 kHz output (before resampling) is bit-exact with
  the firmware host build (tests/host.c) for the same block partition; with sample-accurate event splitting, the
  reference uses the same split. Resampled output is checked separately (level, aliasing, latency report).
- **Set state**: the instance stores its program number, the full current program + sequence and an "edited" flag;
  on load the stored copy wins over the bank's (the panel shows "edited" / "differs from bank"). The edit-buffer rules
  (WRITE to store into the bank, lost without WRITE except via the set's state) follow the firmware.
- **Sequencer recording**: the OB-6 / firmware step entry (no real-time recording, a non-goal in the firmware spec):
  SEQ REC on the panel, notes from Live MIDI or the computer keyboard, REST / TIE / BACK buttons on the panel.

## Left to the builder

The plugin repo layout (a `plugin/` directory in this repo with its own CMake), how to factor the shared firmware
modules out of the unity build, the resampler choice, the FIFO/latency figure, JUCE version pinning, the panel's exact
drawing (following the mockups), and the tests (host-side unit tests for the adapter, state round-trip, parameter
mapping, clock sync; `auval`; a render comparison against the firmware host build: same MIDI in → same audio at 44.1 kHz,
bit-exact or within a stated tolerance).
