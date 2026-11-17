# The AC791N's second core for KNOBI-SIX's DSP

Research note, 2026-11-17. Nothing here was run on a keyboard. **[F]** marks a fact found in a source (cited);
**[I]** marks inference or estimate. Sources read at: keremimo/melodee `165d177`, hugelton/felucca-bsp `fbc254d`,
joakimeriksson/fm1-emu `2ed9865` (KNOBI-SIX pins `60645a5`), JieLi AC79 SDK tag `AC79NN_SDK_V1.2.1_2023-12-13`
(gitee, commit `d179b44`).

## Verdict

**Feasible with platform work.** The second core has been started bare-metal (no RTOS) on the FM-1 by several
firmwares and ships on by default in Melodee and in ChoralRoot, including ChoralRoot's SMK-37 builds. All of them use the
same pattern: CPU0's audio interrupt hands CPU1 part of the **current** block, renders its own share, then waits
for CPU1 before it returns. There is **no extra block of latency**. The known problems are: some units misread
shared SRAM unless the core supply is raised; the PC guards must be open while CPU1 boots through ROM; every reset path must hold CPU1 first; the
flash may only be switched off while CPU1 sits in its RAM idle loop.

**Expected gain [I]:** about **1.4–1.7×** the DSP budget for the voices, not 2×. On the 91 % stress case (240 MHz
model) that means about 55–66 % of a block. Device results on the FM-1 cut the block by 27–41 %, not 50 %
(below). The difference is mainly the shared I-cache and XIP flash.

**Effort [I]:** about 1.5–3 weeks. That is 4–6 days of platform port (mostly copying felucca-bsp's `chip/fm1_cpu1.*`
and its flash gate), 2–4 days to split the synth and test it on the host and in fm1-emu, and the rest for device
validation, which João has to run. The platform files are "do not edit unless the plan says so" (CLAUDE.md), so this
needs a plan entry first.

## 1. Prior art

### Melodee (keremimo/melodee): "8 voices dual-core, 5 single-core"

- **[F] Start**: `firmware/hal/fm1_multicore.h:78-106` (`fm1_multicore_start`). The steps follow the SDK's
  `EnableOtherCpu` (system.a `port.c.o`) and cpu.a `startup.S.o`, as the header says (lines 3-6):
  1. Hold CPU1 (`fm1_core1_stop`).
  2. Clear CPU1's interrupt-config bank `0x1EEF300..0x1EEF37C` (CPU0's is `0x1EEF100`).
  3. Write the entry address to `0x01C7FFF8`.
  4. Set `SYS_DIV` (`0x10008`) bit 3 while CPU1 starts.
  5. Set `C1_CON` (`0x1EEE004`) bit 3 and clear bit 1 to release CPU1.
  6. Wait up to 10 ms for a `ready` word, then restore `SYS_DIV`.

  The entry stub is `firmware/crt0.S:21-33`: `cli; icfg = 0; usp/sp/ssp = CPU1 stacks; reti = fm1_core1_main; rti`.
- **[F] Hold**: `hal/fm1_core1_stop.h:21-45`. It sets `C1_CON` bit 1, then clears bit 3. The bus-invalid debug traps are masked
  around the hold because "holding CPU1 can abort an outstanding bus access and raise a delayed bus-invalid flag on
  CPU0". It is called on every reset/update path (`hal/fm1_sys.h:122-173`) and first thing in `fm1_cstart`
  (`src/main.c:446`).
- **[F] IPC**: `src/audio_worker.h`. There is one outstanding job (function pointer + context). Each core writes its
  own words, and the two cores' words sit on separate 32-byte lines. Every flag access is followed by a `csync`.
  CPU1 runs a job only when `request == done+1` and a check word (`fn ^ ctx ^ request*0x9E3779B1`) matches.
  `audio_worker_join` (lines 99-121) times out after 10 ms. On a timeout or fault, CPU1 is retired: it is held and
  CPU0 renders alone. No spinlocks and no inter-processor interrupt are used. CPU1 runs with **no interrupts**
  and polls with a short nop pause (`fm1_multicore.h:26-33`).
- **[F] Placement**: CPU1's poll loop is in RAM (`.dsp_text`, `fm1_multicore.h:63-76`; `app.ld:35`). Jobs may run
  from XIP. "Every audio block joins its jobs, so main-loop flash writes cannot overlap an XIP worker callback.
  Update and reset paths stop CPU1" (`BUILDING.md:543-546`).
- **[F] Use**: each paired voice is submitted from inside the render, and the join happens before the mix
  (`src/eng_prophet_test.c:543-565`). The FX bus is a separate job (`src/fx.c:606-613`).
- **[F] Failures found on the device** (`BUILDING.md:458-582`):
  - The first install boot-looped with `DBG_MSG=0x400` (`c1_pc_limit_err`). The application PC-limit guards were
    armed before CPU1 had run its ROM bootstrap. The fix is to arm them only after CPU1 reports in (`src/main.c:261-271`).
  - On some units CPU1 misreads SRAM (`0x00200000` where RAM holds 0; melodee#17). CPU0 also took corrupted return
    addresses with high bits flipped.
  - The fix was to apply the stock firmware's supply setup before starting CPU1 (`hal/fm1_power.h`: DCDC on,
    VDC14 7 = 1.60 V, SYSVDD 15 = 1.38 V, verified by read-back). The stock FM-1 firmware is single-core
    (`CPU_CORE_NUM 1`), so the stock firmware does not prove that dual-core is stable.
- **[F] Cache RAM** (`hal/fm1_cache.h`) is separate: 28 KB of data-cache ways mapped as RAM. It is unrelated to
  CPU1, but it is the same platform-hardening work.
- First dual-core commit: `f12b310807` "perf: add experimental dual-core audio" (2026-10-08).

### The others

- **hugelton/felucca-bsp** `chip/fm1_cpu1.h`, `chip/fm1_cpu1.S`, `chip/fm1_cpu1_stop.h`, `chip/fm1_flash_access.h`
  (MPL-2.0, by agreement with Melodee's author: felucca-bsp#5). This is the most reusable form and the **closest
  ancestor of smk37-base's HAL**.
  - [F] API: `fm1_cpu1_start/run/wait/busy/check/stop`, an 8 ms job timeout (line 69), a misread check word,
    a `.noinit` "bar" that keeps CPU1 off until power-off after a failure, and a stack watermark.
  - [F] `fm1_cpu1_start` (line 214) opens the PC limits, unlocks the protected RAM top for `0x01C7FFF8`, starts
    CPU1 with a 20 ms handshake, then restores both.
  - [F] `fm1_cpu1_fault_c` (line 350) tells CPU1 faults that are reported on CPU0 apart from CPU0's own faults.
  - [F] The flash gate makes the RAM flash driver refuse to run unless CPU1 has acknowledged that it is in its
    RAM idle loop.
  - [F] Its `irq_save` clears ICFG bits 8 and 9. smk37-base's `hal/fm1_flash.h:259` does only `cli`.
- **charlesvestal/fm1-x0x** `docs/plans/2026-10-04-dual-core-and-clock.md`:
  - [F] Measured on one FM-1 with the drums on CPU1: "all five 83 -> 53 %, worst case 115 -> 81 %", then 68 % after
    rebalancing so that CPU1 was not the critical path (lines 22-30).
  - [F] "There is one cache controller for the dual-core complex ... the cache way split is 'CPU' against
    'peripheral' ... not core 0 against core 1" (lines 64-71).
  - [F] Measured clock: 360 MHz on the FM-1 (lines 101-110).
  - [F] "`C0_TL_CKCNT` does not count (DBG_CON 0)".
  - [F] Issue #10 (misreads, measured by Jangada) and #21: CPU0 crashes "every few minutes" at SYSVDD/VDC14 14/4
    and is stable with CPU1 held.
- **Quixotic7/ChoralRootFM1** `docs/DUALCORE.md` (ported from fm1-x0x):
  - [F] A fork/join of one CTL block at a time (lines 120-135).
  - [F] On the device, "the split alone took MORPH PAD's worst half from 3263 to 2389 us, and the VA in RAM to
    2103" (about lines 284-290): −27 %, then −36 % with the voice code in RAM.
  - [F] VDC14 raised to 5 (1.50 V). Measured rail table: 14/4 is marginal, 15/5 clean for 15 min (lines 527-536).
  - [F] It ships `-1core` twin packages.
  - [F] Its **SMK-37 port** (`SMK37.md`, genkigenki's port) runs Dual Core On by default. "0.39-smk37 was tested on
    SMK-37s by their owners" (`SMK37.md:8`). [I] So CPU1 has very likely run on SMK-37 units, but there is no
    published SMK-specific rail or misread data.
- **joakimeriksson/fm1-dock** `firmware/src/cpu1.c` (from andrerezemini/sloop-lab):
  - [F] Screens CPU1 for misreads over the first 128 blocks.
  - [F] Notes that misreads correlated with the BLE radio until the stock supply setup was applied.
- **leonardoMirabal/frodhes** `firmware/hal/fm1_core1.h`: [F] raises the rails to the stock levels (VDC14 7, SYSVDD 15),
  then starts CPU1 and sends a ping job.
- **zednaked/jangada**, **hugelton/Felucca** (`tests/dualcore_test.c`; PR #247 "Second core: the two cores render the
  parts of each block (bit for bit one core's sound)"), **fc79/drumtonic** (`firmware/hal/fm1_cpu1.{h,S}`),
  isod89/sloop-fm1, isod89/clipper-fm1, illuminus5/skiff, bonszaimusic/caravel: [F] all carry the same `fm1_cpu1`
  HAL (`gh search code 0x01C7FFF8`).
- **genkigenki/erik37** (SMK-37 grand piano) `base/bsp/chip/fm1_cpu1.h`: [F] "EXPERIMENTAL: compiles, has not run on a
  device".
- **zvenson/dinghy, jartificer/fm1-gm, fc79/smk37-base**: [F] no second-core code.

## 2. The JieLi SDK (AC79 V1.2.1)

- [F] `include_lib/driver/cpu/wl82/asm/cpu.h:43`: `#define CPU_CORE_NUM 2`. `current_cpu_id()` reads `cnum`
  (lines 79-84).
- [F] Spinlock: `testset b[r]` in a loop, with `csync` before and after (`cpu.h:189-190`). There is also a disabled
  `lockset`/`lockclr` hardware-lock variant (lines 165-186).
- [F] The task switch is a soft interrupt (`CPU_TASK_SW`: `ILAT_SET |= BIT(7-a)`, `cpu.h:56-61`). The SDK's
  FreeRTOS SMP layer (system.a `port.c`, `EnableOtherCpu`, `cpu1_run_flag`; cpu.a `startup.S`, `cpu1_main`,
  `.cpu1_ustack/.cpu1_sstack`) is binary only. Melodee and felucca-bsp re-derived its register sequence. [I] Nothing
  RTOS-specific is needed: the bare-metal base can do it, as the projects above show.
- [F] Registers (`asm/csfr.h:106-195`): `corex2` at `0x1EEE000`.
  - `C0_CON`, `C1_CON` (+4), `CACHE_CON`, `DCACHE_WAY`, `ICACHE_WAY`.
  - Per-core counters `C0/C1_IF_UACNT`, `_RD_UACNT`, `_WR_UACNT`, `_TL_CKCNT` (+0x200..+0x23C). [I] These are
    probably cache-miss and clock counters. fm1-x0x found `TL_CKCNT` idle with `DBG_CON` 0.
  - `C1_WR_LIMIT_EN`. q32DSP per-core banks at `0x1EEF000 + 0x200*n` (ICFG, EMU, ETM).
- [F] Cache (`asm/cache.h`, `cpu.h:45-53`): `NO_CACHE_ADDR` only remaps addresses at `0x4000000` and above (SDRAM).
  `flush_dcache`/`flushinv_dcache` are for DMA. The linker script reserves CPU1 stacks (`cpu/wl82/sdk_ld_sfc.c:189-194`).
  [F+I] Internal SRAM (`0x01C00000..`) is shared directly. The XIP flash (`0x02000000..`) is fetched through **one
  cache shared by both cores**. Prior art needs no cache maintenance for SRAM, only `csync` for ordering.
- [F] Board-level facts from prior art, not from the SDK headers:
  - The entry word is `0x01C7FFF8`.
  - CPU1 boots through mask ROM, so the PC limits must be open at start.
  - CPU1 has no interrupt to wake it, so prior art polls.
  - CPU1's EMU stack-limit bank is at `0x1EEF2D0..`.

## 3. Fit with smk37-base / KNOBI-SIX

What exists today [F]:
- One core.
- `fm1_alnk0_irq` (`firmware/src/platform.c:382`) renders 256 frames into the free I2S half. It runs at priority 3;
  TIMER5 at priority 4 nests inside it.
- `fm1_cstart` (`platform.c:460`) enables every guard, PC limits included, at line 480, before anything else.
- The flash driver is in `.ram_text` (RAMTEXT `0x01C00000`, 24 KB, `firmware/app.ld`). It switches XIP off with
  `cli` (`hal/fm1_flash.h:259`), and a sliced save turns interrupts back on between slices of up to 400 µs
  (`fm1_flash.h:270-283`).
- The voices render per 16-frame control period into a shared int32 bus (`src/synth/voices.c:507-523`;
  `VOICE_CTL 16`, `NVOICE 6`).

What would change [I, following felucca-bsp]:

| Where | Change |
| --- | --- |
| `hal/` | add `fm1_cpu1.h`, `fm1_cpu1.S`, `fm1_cpu1_stop.h`, `fm1_flash_access.h` (felucca-bsp, MPL-2.0, keep its credits in LICENSING.md); add a supply setup (`fm1_power.h`) **with SMK-37 values, still unknown** |
| `hal/fm1_sys.h` | hold CPU1 in `fm1_reboot`, `fm1_enter_uboot`, `fm1_core_reset`/`fm1_enter_update` (lines 109-149) and at the top of `fm1_cstart` |
| `hal/fm1_irq.h` | `fm1_fault_c`: let `fm1_cpu1_fault_c` take CPU1's faults first; a CPU0 crash holds CPU1 and sets the bar |
| `hal/fm1_flash.h` | `irq_save` clears ICFG bits 8 and 9; flash entry points refuse to run unless CPU1 is idle in RAM |
| `app.ld` | `.c1_text` inside `.ram_text`, so one copy loop and one PC window cover it; CPU1 stacks (~3 KB) in RAM or POOL |
| `tools/build.py` | check that `.c1_text` is in the image (fm1-x0x's first build ran into erased flash) and audit CPU1's stack depth |
| `platform.c` `fm1_cstart` | start CPU1 after RAM init and before `fm1_audio_init`/`fm1_timer5_start` (prior art: a CPU1 started later never reaches its entry), with the PC limits opened around the start; never start it in `recovery_main` or after a boot-guard failure; read the `.noinit` bar |
| `crt0.S`, the ISR wrappers | no change |
| `synth/voices.c` | split the voice loop (below) |

Minimal design [I]:
- **Core 0**: the audio interrupt and the main loop, as today. **Core 1**: no interrupts, an idle loop in RAM, one job
  at a time.
- Inside `synth_render`, per control period: CPU0 runs `synth_tick`. That writes the shared `ctx`, the smoothers and
  the LFO, so it stays on CPU0, before the hand-off. CPU0 posts "voices k..5 into `bus1`", renders voices 0..k-1 into
  `bus0`, joins, then adds `bus1` into `bus0` and runs the DC blocker and limiter. That is 16 handshakes per block.
  - [I] At under ~1 µs each (CPU1's wake latency is a ~64-nop pause), that is ≤ ~0.3 % of a block.
  - The bus is int32 and integer addition is associative, so the output is **bit-identical** to one core's. Host
    tests can prove it, as ChoralRoot's `tests/cr_c1_thread_test.c` does.
  - A later step: precompute the 16 `ctx` periods and hand off once per block.
- Balance: give CPU0 the smaller share, since TIMER5 (key scan, USB) nests in its interrupt. ChoralRoot weights
  CPU0's share ×1.07. Balance by `voice_t.busy`, not by index.
- Phase 2 effects: run them as a second job after the voice join. Examples: reverb on CPU1 while CPU0 runs the
  delay and chorus. Or run the voices 3+3, then FX split by effect.
  - A pipelined design is possible but should be avoided: CPU1 renders the next block into a double buffer, and the
    output gets **+1 block = +5.8 ms** of latency.
  - Fork/join inside the block costs no latency. Its only cost is that CPU0 idles if CPU1 is late.
- The CPU guard (`synth_guard`) keeps measuring CPU0's whole interrupt, the join wait included. That is the number
  that misses the deadline.

Risks [F where cited, otherwise I]:
- **Misreads / supply** [F, the main risk]. Without a raised core supply, some units corrupt SRAM reads on **both** cores
  (fm1-x0x#21, melodee#17). Detection only catches what touches the mailbox. A wrong sample or a wrong return
  address on CPU0 is not caught.
  - The SMK-37's stock supply setup and safe rail levels have not been read out by anyone, as far as found.
  - Mitigations from prior art: a `-1core` twin package or a global "dual core" option, the `.noinit` bar, a misread
    screen at boot, and the fallback to one core.
- **Flash** [F]. An erase or write switches XIP off. CPU1 must be in its RAM loop, so the gate is needed.
  - Sliced saves are fine because every interrupt joins before it returns. A slice runs with interrupts off and
    CPU1 idle.
  - [I] An erase stalls CPU0 for ~50 ms either way; CPU1 is idle then, so nothing is gained or lost.
- **Watchdog**: only CPU0 feeds it. A hung CPU1 is caught by the join timeout (8–10 ms), costs one glitched block,
  and CPU1 is then retired. Make the timeout shorter than a block plus margin, but longer than the worst job.
- **Update/rescue path**: hold CPU1 before every reset, loader entry and `fm1_enter_update`. Never start it in
  `recovery_main`. A CPU1 start failure must not count twice against the boot guard, or the unit drops into the USB
  rescue (Melodee's first build did exactly that). Use the bar or a fall-back-to-one-core start.
- **Shared I-cache / XIP contention** [F, measured indirectly]. This is why the gain is −27…−41 %, not −50 %.
  Hot voice code in RAM helps (ChoralRoot −27 → −36 %). KNOBI-SIX's RAMTEXT is 24 KB with the flash driver in it;
  Melodee grew RAMTEXT to `0x7F00` and put audio code in POOL (`melodee/firmware/app.ld:20-50`).
- **FPU**: the app uses floats (`-mfprev1`, the SVF and the DC blocker). [I] Each core has its own FPU registers, and
  Melodee's Prophet kernel runs floats on CPU1, but this has not been checked on KNOBI-SIX.
- Licence: felucca-bsp's files are MPL-2.0. That is compatible with GPL-3.0 (file-level copyleft). Keep the headers.

## 4. Measurement

- [F] fm1-emu emulates CPU1 since `11967fc` (2026-10-11), and that is in KNOBI-SIX's pinned `60645a5`.
  - `crates/fm1-soc/src/machine.rs:1-24, 680-905` (`run_cpu1`, `run_dual`): CPU0 runs in slices of 1024
    instructions, and CPU1 then runs for as many cycles as the slice took.
  - `--flaky-core1` injects misreads. `FM1_STRICT` charges CPU1 its full instruction costs.
  - There is **no cache model and no bus-contention model** (KNOBI-SIX's own MEASUREMENTS.md notes the missing cache
    model).
- [I] So fm1-emu can:
  - verify the start sequence, the handshake, the fallbacks and bit-exactness on the real image;
  - give each core's cycles per block with the existing `patch_emu.py` per-PC counters, split by `cnum`;
  - show critical-path balance (CPU0's wait time).

  It cannot give the real speedup.
- [I] Estimate from the existing harness numbers (`docs/MEASUREMENTS.md:94-108`): per-voice work (`voice_render`,
  `osc_*`, the per-voice control) is ~1.1 M of ~1.27 M hw cycles per block at 6 voices, so ~85–90 % parallelises.
  - Ideal 3+3: ~0.55 + 0.1 serial + handshakes ≈ **~52 % of today's block**.
  - Applying prior art's device ratio (achieved ÷ ideal: fm1-x0x 0.53–0.68 of before, ChoralRoot 0.64–0.73): 91 %
    becomes about 55–66 % at 240 MHz, or 37–44 % if the SMK runs at 360 MHz.
- On the device: time the interrupt with `fm1_ticks()` as today, plus CPU0's join wait and CPU1's job time
  (`TIMER4` on CPU1). Optionally enable `DBG_CON` and read `C0/C1_TL_CKCNT` and `*_IF_UACNT`, which are probably
  I-cache miss counts, to see contention directly. An A/B test is the same image with the dual-core option off.

## 5. Smallest safe first step

1. **Host only, no platform change**: refactor `synth_render` so that the voice loop is "render the voice set S into
   this bus". Run it as two calls inline, then on two host threads. Prove it bit-exact against the current output
   with a unit test plus ThreadSanitizer, and add the fm1-emu cycle split. This is useful even without CPU1: it is
   the seam.
2. **Port felucca-bsp's `fm1_cpu1` HAL behind `KNOBI_CPU1=0`** (default off) and run it in fm1-emu's dual mode,
   including with `--flaky-core1` and `nostart`/`hang` cases.
3. **A probe build for João** (with CPU1 on): start CPU1 into the RAM idle loop, run a ping job and a misread screen,
   and show the status on the Globals screen, with **no audio jobs**. Hold CPU1 on every reset path. Only after that
   runs clean on his unit, hand it the voices.

## What only the hardware can answer

- Whether CPU1 starts on this SMK-37 Pro with smk37-base's boot state (and on the Elite / MKE-P37).
- The SMK-37's stock supply setup (SYSVDD / VDC14 / DCDC), whether CPU1 misreads at the SPL's levels, and which level
  fixes it. Reading this needs a look at the stock SMK firmware's `power_init`, as was done for the FM-1's V15.
- The real clock (240 or 360 MHz, MEASUREMENTS.md: not confirmed).
- The real speedup with the shared I-cache/XIP, and how much moving `voice_render` to RAM gives.
- Interaction with USB audio, TIMER5 nesting, sliced saves (suspend/resume) and the screen DMA under a dual load.
- Long-run stability (hours) and power or temperature with CPU1 busy-polling.
