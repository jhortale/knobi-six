# KNOBI-SIX Phase 2 — design addendum

Date: 2026-11-17 · Status: ready for approval · Extends `2026-11-16-standalone-firmware-design.md` (still binding).

## Why this addendum

Phase 1 left the voices at 55–91 % of the audio budget (emulator, 240 MHz) for 6 voices, so Phase 2's effects have no
room on one core. João chose to research the AC791N's second core first; `docs/research/second-core.md` found it
feasible with platform work (other FM-1 / SMK-37 firmwares run it bare-metal) for an estimated 1.4–1.7× voice budget.
João then chose to shape Phase 2 **dual-core first**.

## Goals

- **2A — Second core, safely, off by default.** A seam that renders any voice subset into a bus (bit-exact with one
  core); the CPU1 HAL ported from felucca-bsp (MPL-2.0, credits kept) behind `KNOBI_CPU1` (build flag) plus a global
  "DUAL CORE" option (default OFF); fork/join of the voice loop inside the audio interrupt (no added latency); fallback
  to one core on any CPU1 failure; measured per core in fm1-emu; a **probe build** for the first device run that
  starts CPU1, pings it and checks shared-RAM reads, with no audio jobs.
- **2B — Clock, arpeggiator, distortion** (cheap, independent of 2A): per the OB-6 reference §5 and §4 (distortion
  amount per program).
- **2C — Effects**: planned separately **after** the dual-core numbers from the device; not in this plan.

## Non-goals / anti-goals

- No pipelined render (+5.8 ms latency) — fork/join only.
- Dual core never on by default until João's probe run passes on his unit; a CPU1 failure must never brick, never
  count twice against the boot guard, never enter the rescue path by itself.
- No supply-voltage changes without values read from the stock SMK-37 firmware (unknown today) — the probe reports
  misreads instead.
- No effects in this plan; no sequencer (Phase 3).

## Design (summary — details in the research doc §3, binding where it says [F])

- **Core 0**: audio interrupt + main loop as today; runs `synth_tick` (shared context, smoothers, LFO), posts "voices
  set B into bus1", renders set A into bus0, joins, sums bus1 into bus0, then DC blocker + limiter. 16 hand-offs per
  block. Balance by `voice.busy`, CPU0 takes the smaller share. Join timeout below one block; on timeout CPU1 is retired
  and the rest of the session runs single-core (one glitched block at most).
- **Core 1**: no interrupts, idle loop in RAM (`.c1_text` inside `.ram_text`), one job at a time, mailbox with a check
  word; held on every reset/loader/update path; faults handled before CPU0's.
- **Flash**: every erase/write waits for CPU1 idle in RAM (felucca-bsp's flash-access gate).
- **CPU guard** keeps measuring CPU0's whole interrupt, join wait included; the Globals line adds "C1 ON/OFF/FAIL".
- **Clock**: internal BPM 30–250 per program, tap tempo, divisions incl. dotted/triplet/swing, MIDI clock in (USB) /
  out, arp beat sync (global), LFO sync from the clock.
- **Arpeggiator**: Up, Down, Up+Down, Random, Assign; octaves 1–3; HOLD latch + relatch (notes added while a key is
  still held are appended); arp notes go to MIDI out when PARAM TX is on; arp and (future) sequencer exclusive.
- **Distortion**: per-program amount, odd-symmetric soft drive on the summed bus before the (future) effects;
  measured cost.

## Decided by João

- Second core first; Phase 2 shaped dual-core first; effects after the dual-core budget is known.

## Our calls (correct if wrong)

- Dual core behind a build flag *and* a global option, default off, until the device probe passes.
- Clock/arp/distortion built in parallel with 2A (no shared files except params/controls).
- Arp gate length 50 % of the step, arp velocity = the played key's, Up+Down without repeating the end notes (the
  manual doesn't say).
- Distortion position: summed bus, before effects.

## Left to the builder

Mailbox layout, exact balance heuristic, join-timeout value within the stated bounds, swing amounts, tap-tempo
averaging.
