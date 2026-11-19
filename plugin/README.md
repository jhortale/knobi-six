# KNOBI-SIX plugin (AU / VST3)

KNOBI-SIX as an instrument plugin for Ableton Live 12 on a Mac (Apple Silicon): the firmware's own synth, arp, clock and
sequencer (`firmware/src/core/knobi_core.h`) inside a [JUCE](https://juce.com) 8 plugin. Design:
`docs/superpowers/specs/2026-11-19-plugin-design.md`; plan: `docs/superpowers/plans/2026-11-19-plugin.md`.

Status (plan Task 3): the skeleton. It plays from MIDI (notes, velocity, bend, mod wheel, pressure, sustain, the
firmware's CC map, bank select + program change) with the init program; no parameters, set state, program bank, host
clock or panel yet (Tasks 4–8). The window is a placeholder.

## Build

```sh
brew install cmake ninja                       # once (Xcode 26.6 / its command-line tools for the compiler)
cmake -S plugin -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/plugin
ctest --test-dir build/plugin --output-on-failure
```

The first configure downloads JUCE 8.0.15 (pinned by commit and SHA-256 in `plugin/CMakeLists.txt`, ~23 MB). The
bundles land in `build/plugin/KnobiSix_artefacts/Release/AU/KNOBI-SIX.component` and `.../VST3/KNOBI-SIX.vst3`, ad-hoc
signed (`codesign -dv` says `Signature=adhoc`). A build installs nothing.

### With the OB-6 factory bank (local only)

`-DKNOBI_FACTORY_LOCAL=ON` compiles your local `firmware/gen/factory_bank.h` (made by `tools/ob6_import.py`, never
committed; `docs/FACTORY_BANK.md`) into the plugin. That build is named **KNOBI-SIX-local** everywhere (bundle files,
plugin name, AU code `aumu Kn6l Jhrt`, bundle id `com.jhortale.knobisix.local`), so it never stands in for the
default build and both can be installed side by side. Never share it; CI never builds it. Use a separate build
directory:

```sh
cmake -S plugin -B build/plugin-local -G Ninja -DCMAKE_BUILD_TYPE=Release -DKNOBI_FACTORY_LOCAL=ON
cmake --build build/plugin-local
```

(Until plan Task 6 the factory programs are reachable only by MIDI program change.)

## Identity

| | default | `KNOBI_FACTORY_LOCAL=ON` |
| --- | --- | --- |
| name | KNOBI-SIX | KNOBI-SIX-local |
| AU type / subtype / manufacturer | `aumu` `Kn6x` `Jhrt` | `aumu` `Kn6l` `Jhrt` |
| bundle id | `com.jhortale.knobisix` | `com.jhortale.knobisix.local` |
| VST3 category | Instrument / Synth | Instrument / Synth |

The codes follow Apple's rules (manufacturer: at least one capital; subtype: exactly one capital) and stay fixed:
Live finds a plugin in a saved set by them.

## Install (for Live 12)

Copy the bundles into your user plug-in folders and let Live rescan:

```sh
ditto build/plugin/KnobiSix_artefacts/Release/AU/KNOBI-SIX.component ~/Library/Audio/Plug-Ins/Components/KNOBI-SIX.component
ditto build/plugin/KnobiSix_artefacts/Release/VST3/KNOBI-SIX.vst3 ~/Library/Audio/Plug-Ins/VST3/KNOBI-SIX.vst3
killall -9 AudioComponentRegistrar 2>/dev/null; auval -v aumu Kn6x Jhrt | tail -3   # the AU registered and valid
```

To update, copy again (quit Live first, or rescan). To remove both:

```sh
rm -rf ~/Library/Audio/Plug-Ins/Components/KNOBI-SIX.component ~/Library/Audio/Plug-Ins/VST3/KNOBI-SIX.vst3
killall -9 AudioComponentRegistrar
```

(The local build's bundles are `KNOBI-SIX-local.component` / `.vst3`, removed the same way.)

In Live 12: Settings → Plug-Ins → turn on "Use Audio Units v2" and "Use VST3 Plug-in System Folders" (or "Custom
Folder" pointing at `~/Library/Audio/Plug-Ins/VST3`), then **Rescan** (hold Option while clicking it for a full
rescan). KNOBI-SIX appears in the browser under Plug-Ins → Audio Units → jhortale and VST3 → jhortale.

## Checking it in Live (by hand, not automated)

1. Drop KNOBI-SIX (AU or VST3) on a MIDI track; its window opens (a placeholder until the panel).
2. Play notes from a keyboard or a clip: a held chord, the mod wheel, pitch bend, sustain. It should sound like the
   keyboard's init program, six voices.
3. Change the session rate (Settings → Audio: 44.1, 48, 88.2, 96 kHz): it keeps the pitch and sounds the same.
4. Put a clip with short notes on the grid next to an audio click (or a drum rack) at 48 kHz: they stay in time
   (Live compensates the plugin's latency: 256 samples at 44.1 kHz, 332 at 48 kHz, 610 at 88.2, 664 at 96 — 5.8 to
   6.9 ms; Live shows it in the device's context menu → "Delay Compensation").
5. Two instances on two tracks play at once without affecting each other.

## auval

`auval -v aumu Kn6x Jhrt` validates the installed AU (it needs the copy in `~/Library/Audio/Plug-Ins/Components`).
The auval runs in development (and CI's, on its runner) install the **default** (non-factory) AU there, never the
local one; on this Mac it is installed since Task 3 (remove it as above).
`cmake -DKNOBI_TEST_AUVAL=ON` adds it to `ctest` (CI's `plugin-macos` job installs the AU on the runner and runs it).

## How it works

- `src/CoreRunner.*`: one core instance (`kc_t`) per plugin instance. The core renders at 44.1 kHz in the firmware's
  256-frame blocks; MIDI is applied at its sample (`kc_render_exact` splits the block there), and the output is the
  core's stream delayed by a fixed latency, so it does not depend on the host's block size and sits exactly on the
  host's timeline once Live compensates the reported latency.
- `src/Resampler.*`: 44.1 kHz → the session rate, a 96-tap Kaiser-windowed sinc at exact rational times (flat to
  ~19 kHz, images and aliasing below −100 dB).
- `src/CoreAccess.h`: the threading seam. The audio thread owns the instance in `processBlock`; other threads ask for a
  call, served by the audio thread at the start of its next block (or run directly when the host is not processing).
- `src/Processor.*`: the JUCE `AudioProcessor` (stereo out, MIDI in, no MIDI out).
- `tests/`: CTest — `render_44k_bitexact`, `render_48k`, `midi_sample_accurate`, `rt_no_alloc` (an allocation and
  lock counter around `processBlock`), `seam`, `silent_block`, `prepare_keeps_pending`, `events_ordered`,
  `bundle_exports` (the bundles export none of the firmware's symbols).
- What `rt_no_alloc` covers: `processBlock` as the tests call it — the processor, CoreRunner, the core, CoreAccess's
  audio side, the silent path and its replay (`silent_block`) — counting the malloc family (incl. typed and zone
  allocators, so `operator new`) and pthread mutexes / rwlocks / `os_unfair_lock`. Not covered: what JUCE's AU / VST3
  wrapper does around `processBlock` in a real host (its own locks), condition variables, semaphores, dispatch calls.
- A collision (rare: the host resumes while another thread runs a call it took after a real idle of 3 block periods):
  that block fades out to silence over 64 samples, its MIDI is replayed at the start of the next block (in order;
  more than 256 messages: all notes off), the missed samples are rendered then (output dropped) so the timeline stays
  the host's, and the next block fades in over 64 samples. A note shorter than the missed block collapses to its start.

## Licence

GPL-3.0-only (the repository's `LICENSE`). JUCE 8 is used under its AGPLv3 option (`LICENSING.md`).
