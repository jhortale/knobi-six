# Measurements

## Phase 2 status (2A second core, 2B clock / arpeggiator / distortion)

Everything below is emulator and host-test evidence: **nothing has run on a keyboard**. The device numbers come from
the CPU1 probe ([SMOKE_TEST.md](SMOKE_TEST.md) §5), then the effects (2C) are planned against them. Last CI run on
`phase2` (9384171): 44144831394, `firmware`, `fm1-emu` and `cpu1-emu` all green; its artifacts include
`knobi-six-pro` (the default package, one core) and `knobi-six-cpu1probe`.

| what | single core | dual core (`KNOBI_CPU1=1`, DUAL CORE on) | source |
|---|---|---|---|
| render, 6 voices, % of the 5.805 ms block at 240 MHz (hw cycle model) | init chord 65.4, musical C3..C5 80.4, C5..C7 91.6, extreme C3..C5 123.6 | 42.8, 50.3, 57.7, 75.9 (critical path estimate) | Task 3 tables below |
| same at 360 MHz | 43.6, 53.6, 61.1, 82.4 | 28.5, 33.5, 38.4, 50.6 | Task 3 |
| change | | -34.5 to -42.8 % (not -50: CPU0 keeps the tick, bus, DC blocker, limiter) | Task 3 |
| CPU0 in the emulator's own time (the CPU guard's measure) | 32.7 to 67.0 % | 20.4 to 39.6 %, CPU1 job 12 to 31 % | Task 3 |
| distortion at 127 vs 0 | +1.3 % of the block (+17,000 to +19,800 hw cycles a block); at 0 the stage is skipped (+0.1 %, bit for bit the old output) | the same, it stays on CPU0 after the join | Task 7 |
| clock, arpeggiator | host and unit tests; no per-block cost figure was recorded for them | | Tasks 5, 6 |

The default build (one core, DUAL CORE and CPU1 absent) is what a release is. Dual core is a separate
`KNOBI_CPU1=1` build, never a release; DUAL CORE defaults to OFF even there.

Caveats that apply to every dual-core figure: fm1-emu has **no cache and no bus-contention model**, both cores run at
full speed in it, and prior art measured -27 to -41 % on devices, so **expect the device to be slower than these
numbers**. The clock speed itself (240 vs 360 MHz) is not known for this unit; both columns are given. The emulator
does not hold a core through `C1_CON`, has no PC-limit fault for CPU1's ROM boot and no supply-dependent misreads
(`--flaky-core1` models them), and does not turn the XIP flash off during an erase.

CI gate, narrowed on João's decision (commit ae72240): the `cpu1-emu` job used to compare the default build against
the pre-HAL base (e800d7e) byte for byte (`app.bin`, `app.dis`, package). Every later feature changes those, so it now
checks only that the default build has **CPU1 absent** (`CPU1: absent (KNOBI_CPU1=0, one core)` from build.py) and that
the update loader `ota.bin` is **byte for byte** the base's. The loader is the one part that cannot be emulated and
threatens the update path; the app and package are no longer compared.

Pre-existing, not from Phase 2: a small DC offset remains on the output for asymmetric loud material (the report
attributes it to the existing limiter, not the distortion). Test
`dist_max_chord` (Task 7) measured, over 10 s on the loud pulses + sync chord, **-64.7 dBFS at distortion 32 and
-68 dBFS at 0** (with distortion at 127: -105.4 dBFS; init chord -96.6). The same figure at amount 0 and 32 shows the distortion
is not its source; noted so it is not blamed on 2B.

### What only the device can confirm

None of this is answerable on the computer or in the emulator; the probe package and its procedure
([SMOKE_TEST.md](SMOKE_TEST.md) §5, [research/second-core.md](research/second-core.md) "What only the hardware can
answer") exist for the first block. A fail on any of the first four means DUAL CORE stays off on this unit.

1. **CPU1 starts** on this unit at all (`C1 IDLE` vs `C1 FAIL SILENT`).
2. **Shared-RAM misreads** at the stock supply (`MISREAD`, `BIT 00200000` is the known fault; units differ).
3. **CPU1's FPU**: the float workload equal on both cores, no trap (`FPU OK`).
4. **DUAL = SINGLE** checksums equal on the real chip.
5. **The real speed-up**: `LOAD 1C / 2C` on the probe's test patch (the emulator says -35 to -43 %, the device
   likely less; cache and flash contention).
6. **The real clock** (240 vs 360 MHz) and the real single-core CPU % for the NVOICE decision; Phase 1's open item.
7. **The WRITE gap** with CPU1 on: whether the flash gate holds up under a real erase and write (the emulator cannot
   turn the XIP flash off).
8. **MIDI clock jitter on USB**: clock out is sent from the main loop (within a few ms of its time), clock in is
   queued to the 5.8 ms audio block; whether a DAW or hardware slave accepts that, and a tempo lock, need a real host.
9. **Distortion taper and level by ear**: the curve and the "a chord stays about as loud" rule are tested by numbers;
   whether 32 is a pleasant warmth, 127 a usable maximum, and single notes' +8 dB acceptable is João's ear. Imported
   programs now play their stored distortion value too.
10. **Per-program ARP HOLD**: it is inferred from the importer's slot for imported programs; confirm against real
    programs that it means what it does on the OB-6.
11. Arp and clock feel: tap tempo, swing, ARP BEAT SYNC, the arp under an external clock from a real DAW.

## Dual core

### Phase 2 Task 2: the CPU1 HAL in fm1-emu (KNOBI_CPU1=1; no audio jobs yet)

The whole device image (`KNOBI_CPU1=1 ./build.sh`, the Pro, with the test hooks of `platform.c` /
`hal/fm1_cpu1.h` where named) booted in fm1-emu `60645a5` (joakimeriksson, MIT) in its dual mode: cpu0 and cpu1 a
step each; cpu1 released by `0x10008` bit 3 at the word at `0x01C7FFF8`; `--flaky-core1 RATE` flips bit 21 of
cpu1's RAM loads now and then. 1,500 emulated ms each, `bash tests/cpu1_emu/run.sh EMU OUT` (CI job `cpu1-emu`).
The boot takes ~0.3 s, so a run plays ~203 audio halves (the single-core image: 203).

| case | image | expected | seen |
|---|---|---|---|
| plain | no test hook (as it would be installed) | start, boot ping, CPU1 idle in its RAM loop | ready 1, request = complete = 1, jobs 1, rejected 0; no halt, no fault; 203 halves |
| start | hook 3: a ping every 17 blocks (~100 ms) inside the audio interrupt; a post and a join from the main loop every 100 ms; at ~600 ms an erase, a page write, a sliced erase and a sliced write of one store sector, each read back | every ping back; the main loop's post refused, its join 0; the flash work through the gate with CPU1 on | IDLE, 12 pings back (12 jobs), rejected 0; main-loop post -1 (11 refused), join 0; flash work at 668 ms, all 6 steps ok, 4 pings before it and 8 after; flash_ok 1; boot guard failed 0, pending 1; 203 halves |
| nostart | hook 1: CPU1's entry never reports in | one core after 20 ms, boot guard unaffected | FAIL (SILENT), no job; boot guard failed 0, pending 1, no chip reset; 200 halves (the boot 20 ms later) |
| hang | hook 2: a job that never returns, joined with a 4 ms deadline from the post | retired within one block (5,804 us) | join -1 after 4,003 us; FAIL (LATE), barred until power-off; boot guard failed 0, pending 1; 202 halves |
| flaky | hook 3 + `--flaky-core1 1e-7 --seed 7 --cpu1-survives` | a misread fails the check, CPU1 retired, audio goes on | 14 misreads injected; FAIL (MISREAD) at 441 ms after 1 good ping, rejected 14, barred; join afterwards 0; 203 halves |
| reset | hook 5: a warm reset (`fm1_reboot`) inside the start window, after CPU1 answered and before the boot ping | the boot guard counts it once; the second boot runs one core (start mark -> bar; bootguard.failed) | 1 chip reset; second boot: OFF, barred 1, no job; boot guard failed 1, pending 1; 194 halves |

`--cpu1-survives` in the flaky case: a misread pointer can send cpu1 to any address, and without the option the
emulator stops the whole machine there; on the chip that is a fault CPU0 handles (`fm1_cpu1_fault_c`), a path the
emulator does not have. The hang and flaky cases' "rejected"/spinning after the retirement are the emulator's cpu1
still running (it does not model the hold); CPU0's side is what the cases check.

Default build (KNOBI_CPU1 unset): `app.bin` sha1 `44541dc3b3f1f62f0021c4c359ae2f3ee1e59ca0`, the loader and the
package byte for byte the same as before the HAL (e800d7e; local build with the local bank; CI's knobi-six.fwsc and
app.dis of run 44079859772 equal run 44075144656's, and the CPU-gate summary is unchanged). The update
loader holds CPU1 at its start only in a KNOBI_CPU1 build (`ota.bin` 8,621 B there); the CI job `cpu1-emu` builds
e800d7e beside HEAD and compares the loader `ota.bin` byte for byte (until Task 5 it also compared `app.bin`, `app.dis` and the package; dropped since
every later feature changes them, João's call); build.py: `CPU1: absent (KNOBI_CPU1=0, one core)`. The
CPU1 build: image +2,760 B (221,892 B), RAM +3,216 B (CPU1's stacks 3,072 B and the mailbox), `.c1_text` 148 B in
the RAM code, with no flash address in it; CPU1's stack 0 B for the loop and the ping (budget 2,560 B).

What the emulator does not show (only a device can): it does not hold a core through `C1_CON` bits 1/3 (after a
retirement its cpu1 goes on spinning; the firmware's side, single core, is what is checked), has no PC-limit fault
for cpu1's ROM boot, no cache or bus contention, and no supply-dependent misreads (`--flaky-core1` is a model of
them). Whether CPU1 starts on the SMK-37 at all, misreads at its stock rails, and how the flash gate behaves under a
real save with CPU1 on stay for the device and the probe build (Task 4; the emulator's side: Task 3's `write` case below).

### Phase 2 Task 3: the voices on two cores (KNOBI_CPU1=1 + DUAL CORE on; emulator only)

What runs (`engine.c` `eng_split`, voices.c's `SYNTH_SPLIT` in a KNOBI_CPU1 build; [API.md](API.md) "Second core"):
each 16-frame control period of the audio interrupt, CPU0 runs the tick, splits the busy voices (`synth_balance`:
CPU0 the first n / 2 by index, CPU1 the rest), posts set B (`cpu1_job_voices`, CPU1's own bus), renders set A, joins
(1.5 ms from the post at most; and within the block's deadline and its shared join wait of half a block: the final
review's fix, case `slow` below), adds CPU1's bus, then the DC blocker and the limiter. Under two voices busy, DUAL CORE off
(the default) or CPU1 off: one core. Cache lines: 32 B (the AC79 SDK's `asm/cache.h` `CACHE_LINE_COUNT`, Melodee's and
felucca-bsp's mailbox lines): `voice_t` is 320 B = 10 lines and `synth_t.voice[]` starts on a line in a CPU1 build
(`SYNTH_VOICE_ALIGN`, static asserts), CPU1's bus and its time counter sit on lines of their own.

**fm1-emu has no cache and no bus-contention model**: both cores fetch and load at full speed in it. On the chip they
share one I-cache and the XIP flash, and the voice code (`voice_render` alone is 17.8 KB) runs from the flash on both.
Prior art measured −27…−41 % on devices ([research/second-core.md](research/second-core.md) §1, §4), not the ideal
~−50 %; **expect the device to be slower than the numbers below**.

**Cycles per core** (`tests/cpu_emu/`, harness `DUAL=1`: the same synth and split, the emulator's second core started
bare-metal through a minimal two-line mailbox; `patch_emu.py` records core 1's pcs with bit 31 set and charges only
core 0's costs to the clock; local run = CI's `fm1-emu` job). 6 voices, cycles per 256-frame block, emu / hw model.
CPU0's work leaves out its wait (the emulator steps the cores an instruction each, so the wait's length is in
instructions, not in either cycle model); CPU1's job leaves out its idle poll. The block's critical path is estimated
as CPU0's serial work + max(set A, set B) (the same side the larger in every period, as with 3 + 3 voices):

| patch | one core | CPU0 (A + serial) | CPU0 set A | CPU1 job (set B) | critical path (est.) | change |
|---|---|---|---|---|---|---|
| init chord | 644,307 / 911,408 | 410,906 / 580,095 | 232,846 / 331,599 | 244,191 / 347,508 | 422,251 / 596,004 | −34.5 / −34.6 % |
| musical C3..C5 | 779,194 / 1,120,333 | 471,291 / 675,151 | 299,936 / 435,572 | 318,694 / 461,377 | 490,048 / 700,956 | −37.1 / −37.4 % |
| musical C5..C7 | 895,157 / 1,276,663 | 511,666 / 729,465 | 340,181 / 489,647 | 394,281 / 563,393 | 565,767 / 803,211 | −36.8 / −37.1 % |
| extreme C3..C5 | 1,210,238 / 1,721,818 | 654,247 / 932,772 | 474,950 / 679,926 | 566,782 / 805,241 | 746,078 / 1,058,087 | −38.4 / −38.5 % |
| extreme 120..127 | 1,382,949 / 1,957,717 | 781,958 / 1,106,545 | 602,402 / 853,744 | 611,782 / 867,367 | 791,337 / 1,120,169 | −42.8 / −42.8 % |

% of the block (5.805 ms), emu / hw model:

| patch | one core, 240 MHz | two cores, 240 MHz | one core, 360 MHz | two cores, 360 MHz |
|---|---|---|---|---|
| init chord | 46.2 / 65.4 | 30.3 / 42.8 | 30.8 / 43.6 | 20.2 / 28.5 |
| musical C3..C5 | 55.9 / 80.4 | 35.2 / 50.3 | 37.3 / 53.6 | 23.4 / 33.5 |
| musical C5..C7 | 64.3 / 91.6 | 40.6 / 57.7 | 42.8 / 61.1 | 27.1 / 38.4 |
| extreme C3..C5 | 86.9 / 123.6 | 53.6 / 75.9 | 57.9 / 82.4 | 35.7 / 50.6 |
| extreme 120..127 | 99.3 / 140.5 | 56.8 / 80.4 | 66.2 / 93.7 | 37.9 / 53.6 |

The split is ~−35…−43 %, not −50 %: CPU0 keeps the tick (the smoothers, the LFO, every voice's control and envelopes),
the bus, the DC blocker and the limiter (~170–180 k emu cycles a block), and set B (the higher notes, by index) costs
more than set A. Every dual run's checksum equals its one-core run's.

**The whole image** (`tests/cpu1_emu/`, the KNOBI_CPU1=1 package booted in fm1-emu `60645a5`, cases with
`tests/cpu1_emu/dual_hook.c`; CI job `cpu1-emu`, all 9 cases ok locally):

| case | what | seen |
|---|---|---|
| dual | the audio interrupt plays a fixed script, block by block: the init chord, the musical patch on C3..C5 and C5..C7, the extreme patch on C3..C5, each from `synth_init`, 32 blocks held + 8 released; DUAL CORE off, then on, same image, same run | every checksum the same (24ff6590, 2c757139, debaa049, edcde981); 580 CPU1 jobs per patch with it on, none off; IDLE, nothing refused; the Globals line shows `C1 ON 20%` |
| dualfail | as dual, CPU1's 1000th voice job never returns (in the second patch with DUAL CORE on) | the join fails after 1.5 ms, CPU0 renders set B in that period: every checksum still the same; FAIL (LATE) at 1,669 ms, jobs 580, 419, 0, 0 (one core after); no halt, 358 audio halves |
| dualmid | as dualfail, but the 1000th job renders one voice of set B and then hangs (late part way) | CPU0 renders set B again, that voice from one period on: FAIL (LATE) at 1,669 ms; only that patch's checksum differs (99f307e5); its largest sample-to-sample step 42,349 = the one-core pass's (the waveform's own edges are larger than the jump), peak 28,494 vs 28,253, under the limiter's ceiling; the next patches equal again |
| write | DUAL CORE on at 400 ms, the chord held, WRITE + ENTER at 0.9 / 1.0 s, notes off at 1.8 s, the globals' autosave, a chord again | WRITE stored (rc 1) at 1,060 ms; autosave at 5,721 ms, DUAL CORE read back 1 (its blob); jobs 1,104 before the WRITE, 1,536 after it, 7,498 at the end; CPU1 IDLE throughout (the flash gate never had to retire it: every erase and write found it idle in its RAM loop), rejected 0; no fault, 1,065 audio halves in 6.5 s |

What the write case cannot see: fm1-emu does not turn the XIP flash off during an erase or a write, so a CPU1 fetch
from the flash at that moment would not fault here. The evidence that none happens is the gate: every flash entry
point runs only with `fm1_flash_access_ok` (CPU1 held, or idle in its RAM loop, acknowledged), `fl_enter` refuses
otherwise, and the case shows the gate never had to retire CPU1 (it stayed IDLE) while the saves succeeded.

CPU0's render in the emulator's own time (no cost model; what the CPU guard measures, the joins' waits included),
per block, % of the block, DUAL CORE off / on; and CPU1's job load (`C1 ON n%`):

| patch, 6 voices | off: CPU0 | on: CPU0 | change | on: CPU1 job |
|---|---|---|---|---|
| init chord | 32.7 % | 20.4 % | −37.7 % | 12 % |
| musical C3..C5 | 45.1 % | 26.7 % | −40.8 % | 19 % |
| musical C5..C7 | 49.9 % | 29.7 % | −40.6 % | 22 % |
| extreme C3..C5 | 67.0 % | 39.6 % | −40.8 % | 31 % |

A mutation check: dropping one sample of CPU1's bus from the sum makes every dual checksum differ (7eb24497, …):
the case fails as it should.

Default build: `app.bin` (sha1 `44541dc3…`), `app.dis`, the loader and the package byte for byte as before this task
(everything new is under `KNOBI_CPU1`: the global, the split, the alignment, the Globals line); the CI job still
compares them with e800d7e's default build. The CPU1 build: image 222,756 B (+864 B), RAM 27,876 B (+256 B: CPU1's
bus and counter on their lines); CPU1's stack: the deepest job `cpu1_job_voices` 612 B of 2,560 B. The globals:
the version-1 record in blobs 0 / 1 is byte for byte the same in both builds; a KNOBI_CPU1 build keeps DUAL CORE in
blob 2 ("DUAL", the value, a CRC16), which a default build never touches, so swapping the builds keeps the globals both
ways; blob 2 missing or broken: OFF (`test_store_cpu1` `globals_dual_blob`).

### Phase 2 Task 4: the CPU1 probe in fm1-emu (`--probe-cpu1`; the package for the first device run)

The probe image (`./build.sh --probe-cpu1`, `knobi-six-cpu1probe.fwsc`, `src/cpu1_probe.c`; the device procedure:
[SMOKE_TEST.md](SMOKE_TEST.md) "CPU1 probe") booted in fm1-emu `60645a5`, 4,000 emulated ms, GLOBE at 800 ms
(`tests/cpu1_emu/run.sh` cases `probe`, `probeflaky`; CI job `cpu1-emu`, which also uploads the package as the artifact
`knobi-six-cpu1probe` and the screen as `probe_screen.png`). Image 226,312 B, RAM .data+.bss 47,332 B (+19,456 B on
the CPU1 build: the 16 KB pattern buffer and the test patch's synth).

| case | expected | seen |
|---|---|---|
| probe | the counters go up, no error, no misread, FPU and DUAL=SINGLE OK, CPU1 IDLE | PING 4, ERR 0, MISREAD 0 over 77 rounds of 16 KB (4 KB steps every other block, CPU0 reading the last step's chunk while CPU1 works); the probe's own time PROBE 8 % of a block at its peak (emulator time; counted in the CPU line and the guard); FPU 4 ok (both cores `d755fe69`), no trap; DUAL=SINGLE 2 passes equal (`17408792`, 128 CPU1 jobs a pass); LOAD 1C 45 % 2C 26 % JOB 19 % (the emulator's time, no cost model: as Task 3's -41 %); IDLE; 634 audio halves |
| probemis | test hook `KNOBI_PROBE_TEST=1`: CPU1 reads `0x00200000` where 0 is in one word of round 40 | MISREAD 1 (CPU1's), BIT 00200000, ERR 1, CPU1 retired at once: FAIL PATTERN (HAL why MISREAD), rounds 40; the audio ran; lines 1, 2 and 4 red |
| probeflaky | `--flaky-core1 1e-7 --seed 7 --cpu1-survives`: the errors show | 40 misreads injected; ERR 1, MISREAD 40 (the mailbox check refused them; the misread came before the first probe ping), FAIL (MISREAD); the audio ran (634 halves) |

The screen at the end of `probe`: `C1 PING 4  ERR 0  MISREAD 0`, `FPU OK  DUAL=SINGLE OK`, `LOAD 1C 45%  2C 26%  JOB 19%`,
`PROBE 8%  C1 IDLE  DUAL OFF`, then the CPU, wheels and faders lines. A pattern misread, FPU BAD or DUAL=SINGLE BAD
retires CPU1 (FAIL PATTERN / FPU / DUAL), so DUAL CORE never renders the sound on a core that read or computed wrong. The default build stays byte for byte the same
(app.bin, app.dis, ota.bin, the package; also the plain `KNOBI_CPU1=1` app.bin). What the emulator cannot say (no
cache or bus model, no supply-dependent misreads, no FPU trap path) is what the device run is for.

## Phase 2 Task 7: the distortion (fm1-emu, local run = CI's `fm1-emu` job)

The distortion (`synth/voices.h` "Distortion": an odd cubic soft clip on the summed bus, after both cores' voices and
before the DC blocker and the limiter) costs per frame two float multiplies to the curve's input, a compare, and for a
sample under the knee three multiplies and a subtract (no division; the coefficients, with a six-step Newton square
root, are worked out at a control tick only when the smoothed amount moves). It runs on CPU0, after the join. Harness
`DIST=127` (run.sh's `x` runs) against the same patch at 0 (the `p` runs), 6 voices, one core, cycles per 256-frame
block, emu / hw model:

| patch | amount 0 | amount 127 | delta | delta per frame, hw | 240 MHz hw, 0 -> 127 |
|---|---|---|---|---|---|
| init chord | 644,969 / 912,388 | 658,345 / 932,228 | +13,375 / +19,840 | +77.5 | 65.5 -> 66.9 % |
| musical C3..C5 | 779,857 / 1,121,313 | 792,168 / 1,139,112 | +12,311 / +17,800 | +69.5 | 80.5 -> 81.8 % |
| musical C5..C7 | 895,819 / 1,277,643 | 907,600 / 1,294,668 | +11,781 / +17,025 | +66.5 | 91.7 -> 92.9 % |

At amount 0 the stage is skipped (one float compare a 16-frame period) and the output is the one before this task bit
for bit: the harness checksums equal the previous commit's (init chord `55761876`, musical `c7a1eda5`, `aaafdace`,
same local run of `ae72240`), at +662 / +980 cycles a block (the extra smoother and the compare: 644,307 / 911,408
before on the init chord, +0.1 %). At full drive it is ~+1.3 % of the block at 240 MHz (hw model), the same in a
dual-core build (it stays on CPU0: add it to CPU0's serial part in the Task 3 table). Under the CPU guard's 80 %
nothing changes for the gate; the musical patch was already over 50 % on one core.

## After the listening round: the bus DC blocker and limiter (CI run 44021256878)

The listening round's fixes (FILTER FREQ re-anchored to 16 Hz .. 18.8 kHz, the bus DC blocker at 5 Hz and the new
soft limiter, the autosave after 3 s of quiet) add per-sample work only on the bus: per frame, two float
multiply-adds, four int/float conversions and the limiter's knee test (its division only above -4.4 dBFS). Against
CI run 44018881050 (the commit before), 6 voices, cycles per 256-frame block:

| patch | emu before | emu after | hw before | hw after | 240 MHz hw |
|---|---|---|---|---|---|
| musical C3..C5 | 776,089 | 778,541 (+2,452) | 1,116,104 | 1,119,500 (+3,396) | 80.1 -> 80.4 % |
| musical C5..C7 | 891,468 | 894,504 (+3,036) | 1,271,505 | 1,275,830 (+4,325) | 91.3 -> 91.6 % |
| init chord | 640,305 | 643,654 (+3,349) | 905,734 | 910,575 (+4,841) | 65.0 -> 65.4 % |

About 13-19 hw cycles a frame (+0.3 % of the block); the musical patches also sound different now (FILTER FREQ 90 is
~770 Hz on the new map, 262 Hz before), which the filter's float path does not charge for. NVOICE stays 6; the CPU
guard is unchanged.

## Phase-1 CPU gate after the performance push (Task 11, second pass): NVOICE = 6 with the CPU guard (João, 2026-11-17)

**The gate** (João, replacing "every destination at full"): the **musical stress patch** at 6 voices must take less
than 50 % of the 256-frame block (5.805 ms) at 240 MHz in **both** cycle models. If it does, `NVOICE = 6`; if not but
4 voices pass, `NVOICE = 4`; otherwise report. The musical stress patch is played as two chords:
- C3 G3 C4 E4 G4 C5 (`tests/scripts/musical.txt`, harness PATCH 3);
- C5 G5 C6 E6 G6 C7 (`musical_high.txt`, PATCH 4).

The extreme patches (every destination at full, below) stay measured, and the CPU guard keeps them from overrunning.

**Result: neither voice count passes at 240 MHz.** The gate is met only by the emu model with 4 voices; the hw
model is over, so the decision went back to João: **NVOICE = 6 with the CPU guard** ("Decision", at the end).

| musical stress | voices | 240 MHz, emu | 240 MHz, hw | 360 MHz, emu | 360 MHz, hw |
|---|---|---|---|---|---|
| C3..C5 | 6 | 55.5 % | 79.9 % | 37.0 % | 53.2 % |
| C5..C7 | 6 | 63.8 % | 91.0 % | 42.6 % | 60.7 % |
| C3..C5 | 4 | 38.2 % | 54.9 % | 25.5 % | 36.6 % |
| C5..C7 | 4 | 44.9 % | 63.9 % | 29.9 % | 42.6 % |

- At 360 MHz, 4 voices pass in both models and 6 voices pass in the emu model only.
- `NVOICE` stays 6. Whatever the clock turns out to be, the CPU guard keeps the audio interrupt from overrunning:
  it sheds voices when a block takes more than 80 % (see "CPU guard" below).

### Patches

- **Musical stress** (`tests/scripts/musical.txt`):
  - mixer at ~75 % (all four at 95);
  - VCO 1 pulse (PW 90) + VCO 2 pulse (PW 160), sync on;
  - FILTER FREQ 90, RESONANCE 201 (~100/127 of the 0..255 range), FENV AMOUNT 76 (~60 %) with FENV SUSTAIN 40;
  - X-Mod VCO 2 at 64 (~50 %) into VCO 1 pitch and the filter frequency;
  - the LFO at ~20 Hz (FREQUENCY 173), amount 128, into VCO 1 pitch and the filter;
  - aftertouch (pressure 100) at 64 into the filter;
  - velocity 100.
- **Extreme**: `worst.txt`, as in the first pass below, on C3..C5 and on notes 120..127.
- **Init chord**: `program_init`, C4..G5.

### Cycles per 256-frame block (CI run 44015858853; identical to the local runs)

The 240 MHz and 360 MHz columns are % of the block (emu / hw).

| patch | voices | insns | emu cycles | hw cycles | 240 MHz | 360 MHz |
|---|---|---|---|---|---|---|
| init chord | 6 | 449,920 | 638,059 | 902,438 | 45.8 / 64.8 | 30.5 / 43.2 |
| musical C3..C5 | 6 | 622,581 | 773,843 | 1,112,808 | 55.5 / 79.9 | 37.0 / 53.2 |
| musical C5..C7 | 6 | 688,674 | 889,222 | 1,268,209 | 63.8 / 91.0 | 42.6 / 60.7 |
| extreme C3..C5 | 6 | 922,580 | 1,203,942 | 1,712,765 | 86.4 / 122.9 | 57.6 / 82.0 |
| extreme 120..127 | 6 | 1,056,051 | 1,376,403 | 1,948,252 | 98.8 / 139.8 | 65.9 / 93.2 |
| init chord | 4 | 307,469 | 438,716 | 620,903 | 31.5 / 44.6 | 21.0 / 29.7 |
| musical C3..C5 | 4 | 424,352 | 532,395 | 765,363 | 38.2 / 54.9 | 25.5 / 36.6 |
| musical C5..C7 | 4 | 477,920 | 625,116 | 890,430 | 44.9 / 63.9 | 29.9 / 42.6 |
| extreme C3..C5 | 4 | 648,252 | 854,035 | 1,212,629 | 61.3 / 87.0 | 40.9 / 58.0 |
| extreme 120..127 | 4 | 713,090 | 931,120 | 1,318,482 | 66.8 / 94.6 | 44.6 / 63.1 |

How the extreme cases moved (6 voices, hw cycles, against the first pass):
- C3..C5: 2.40 M → 1.71 M;
- high chord: 3.45 M → 1.95 M.

How the musical patch moved (6 voices, hw cycles, C5..C7), step by step on the same harness:

| step | hw cycles |
|---|---|
| float SVF, control-rate envelopes, X-Mod / 2, two passes (first build) | 1.44 M |
| struct copies removed (they were byte-loop `memcpy` calls) | 1.30 M |
| F-FREQ-only coefficient path, branch-free clamps in `mods_fm1` / `svf_gk` | 1.31 M |
| branch-free VCA clamp (final) | 1.27 M |

The specialised loops and the division by 3 as a multiply came in between; the sync change affects only VCO 2 above
4 kHz, so the extreme high chord, not this patch.

Two attempts were measured slower and reverted:
- a branch-free soft clip and drive (more instructions than the branches they replaced);
- forcing the slow integer SVF inline (in the first pass).

What it still costs (musical C5..C7, 6 voices, hw cycles; harness ablations, removing one feature at a time):
- base pipeline: 0.95 M. That is two pulse oscillators, sub and noise, the mixer, the float SVF, the VCA and the
  control rate;
- X-Mod into VCO 1 pitch: 0.15 M;
- X-Mod into the filter: 0.08 M;
- pulse instead of saw: 0.12 M;
- sync: 0.06 M.

Hot spots (hw cycles per block):
- `voice_render`: 0.87 M. About 50/50 between the oscillator pass and the filter pass; loads, stores and spills are
  ~30 % of all cycles, taken branches and calls ~25 %.
- `osc_vco1_sync`: 0.09 M. `osc_blep`: 0.06 M.
- Control rate (`voice_control`, `mods_sum`, `osc_exp2`, `svf_coef_fast`): ~0.11 M.
- `synth_render`, the bus and limiter: 0.04 M.
- The harness's own buffer clearing and checksum (`main`): 0.012 M.

### What changed (applied from the first pass's list, plus the guard)

1. **X-Mod every 2 samples.** VCO 1 pitch, shape and PW, and the filter coefficients, are computed on even frames of
   the control period. The frame between gets the targets themselves, and the even frame gets the mean of the
   previous and new targets: a linear interpolation, one frame (23 µs) late. With only F FREQ modulated, only g, k,
   a1, a2 and the BP gain are recomputed.
   - Test: `xmod_filter_audio_rate` checks the interpolated targets frame by frame (error 1.2e-7).
2. **Float SVF** on the FPU. The app is built with `-mcpu=r3 -mfprev1 -ffp-contract=off` (tools/build.py `APP_FPU`,
   the app only); every float operation is a native instruction, with no library call. The drive polynomial, soft
   clips, cutoff and RESONANCE maps, and damping floor are unchanged, and 1 / (1 + g(g + k)) is one FPU division.
   - It is measured cheaper: the integer path took the 64-bit fallback on 74 % of the overdriven samples.
   - Tests: every filter test passes (responses, no self-oscillation, overdrive curve, decay to zero, cutoff
     smoothness). The bit-exact fast/slow test became `extreme_states`: bounded and finite for states up to the old
     int32 range.
3. **Sync above 4 kHz VCO 2:** one BLEP per restart (`osc_vco1_sync_fast`). VCO 1's own wrap and pulse edges inside
   the window, and the sub under sync, go uncorrected up there. Worst alias (`test_osc sync_alias_high`), ours vs
   naive sync:

   | VCO 2 | VCO 1 +7 st | VCO 1 +19.3 st |
   |---|---|---|
   | 4.3 kHz | −32.9 vs −10.8 dB | −11.5 vs −6.1 dB |
   | 6.3 kHz | −18.7 vs −10.7 dB | −9.4 vs −6.3 dB |
   | 8.4 kHz | −9.5 vs −4.6 dB | −7.0 vs −6.9 dB |

   Below 4 kHz nothing changed: `sync_alias` and `bspline_exact` pass as before. `bspline_exact` skips its 7 synced
   cases above 4 kHz.
4. **Envelopes and VCA at control rate.** `adsr_tick16` runs 16 RC steps as one (1 − c^16 is precomputed in
   `adsr_rates`), with a per-sample linear ramp clamped to 0..full.
   - Timing: `adsr_times16` puts all 128 knob positions within 5 % (worst 2.3 %). The per-sample model's
     `adsr_times` is unchanged.
   - The 2 ms minimum gate (2.20 ms) and the click/zipper tests (`params_smoothed_no_step`, `amp_mod_fades`) pass.
   - A note-on between ticks starts its attack at the next tick (≤ 0.36 ms). On the device, events are applied at
     block starts, which are tick starts.
5. **Structure.** `voice_render` now runs two passes over the period:
   1. oscillators, VCO 1 X-Mod and the mixer;
   2. filter, VCA and pan.

   It has no struct copies (a struct assignment is a call to `libc.c`'s byte-loop `memcpy`). Each pass is
   specialised by constant arguments (which X-Mod paths, even or odd frame), so the loops do not test the patch's
   switches per frame.
6. **The CPU guard** (`synth_guard`, called from `eng_render`). `fm1_ticks()` times `synth_render`; on the host, a
   fake clock does. The measured time includes any interrupt that preempted the render (USB, key scan), which the
   block did not have either.
   - When a block takes more than 80 % of its time, the cost per voice is taken as used / voices sounding. In one
     step, the voice limit drops to the voices that fit under 80 % at that cost: at least one fewer, at least one.
   - The voices over the limit are shed (the oldest released first, then the oldest held). Each holds its VCA gain
     and fades linearly to 0 in 6 control ticks (~2.2 ms), then is freed. The guard does not shed again while they
     fade.
   - The limit comes back one voice at a time, after ~1 s (172 blocks in a row) where one more voice fits: the load
     projected at limit + 1 voices, at the measured cost per voice sounding (used x (limit + 1) / voices sounding),
     under 70 % (with nothing sounding: the load under 55 %). (It was "under 55 %" for 172 blocks; the init chord
     measures ~65 % in the hw model, so after a shed its five voices, ~54 %, sat on that mark and a patch only a
     little heavier never got its voice back while playing.) A patch whose next voice does not fit stays where it is:
     no oscillation.
   - Every shed is counted; engine.c publishes the load (smoothed), its peak (decaying, ~1 s), the limit and the
     sheds, and the Globals screen's diagnostics line shows them: `CPU 65% PEAK 80%  VOX 5/6  SHED 1` (5 % steps;
     white once anything was shed).
   - New notes and unison stacks respect the limit.
   - A long block with no voice sounding is someone else's load (other interrupts): the guard does nothing.
   - Accepted behaviour (controller): on the extreme patches, where even one or two voices can exceed 80 %, the
     limit can oscillate between 1 and 2. Each re-shed is a 2.2 ms fade, and recovery waits 172 calm blocks.
   - Test: `test_render cpu_guard_sheds`. Six voices at 140 % shed to three in one step; the fake clock reads 70 %
     for the next block.
     - The real next block also pays for the three fading voices over ~96 of its 256 frames: ≈ 96 %, so under 100 %
       within one block and under 80 % from the second.
     - The output has no step beyond the steady sound's.
     - The limit recovers to six at one voice per 172 calm blocks.
   - Test: `test_render cpu_guard_recovers` and `tests/scripts/cpu_guard.txt` (the app, the probes `vlimit`, `sheds`,
     `load`, `peak`; the host's `T cpu TICKS` sets the fake clock). A spike to 90 % sheds six voices to five; at 68 %
     for six (five at ~57 %) the limit is back at six ~1 s later and stays with six sounding; at 85 % for six it stays
     at five. One shed each, no oscillation.
7. **No denormals** (fix round 1). `svf_flush` sets filter states under 1e-20 to 0 at every control tick, far above
   the float denormal range; the FPU's speed on denormals is not known.
   - Test: `test_voices filter_states_flush`. A held voice with the mixer at 0 reaches exactly 0, and no denormal
     is seen.

### What it sounds like now

These are the differences against the first pass, by ear-relevant effect; not yet listened to (Task 12):
- **X-Mod** is sampled at 22 kHz with interpolation, so its very top sidebands differ slightly.
- **The filter's output** is not bit-identical (float). The responses are within the existing test tolerances.
- **Envelopes** step every 0.36 ms, with linear segments between steps.
- **Hard sync with VCO 2 above 4 kHz** aliases more: the figures are in item 3 above.
- **When the guard sheds**, voices drop out with a 5 ms fade, as a voice steal does.

Host yardstick (`tests/cpu_host.sh`, x86-64 instructions per sample):

| patch | first pass | now |
|---|---|---|
| init chord | 2110 | 1805 |
| musical | — | 2749 |
| worst | 4900 | 4092 |

### Flash / RAM (device build, Pro, CI)

| | first pass | now |
|---|---|---|
| image (of 619,520) | 100,800 B | 116,448 B (+15,648: the specialised passes and the FPU path) |
| RAM .data+.bss | 23,684 B | 24,100 B |
| pool | 47,092 B | 47,092 B |
| `sizeof(synth_t)`, 6 voices | 2,184 B | 2,600 B |
| `sizeof(synth_t)`, 4 voices | 1,672 B | 1,968 B |

### Concerns

- **No cache model.** fm1-emu does not model a cache: the harness runs from SRAM, while on the device the synth runs
  from cached XIP flash.
- **The interrupt's own costs are not included:** the queue drain, the fade, USB and the key scan.
- **The CPU guard relies on `fm1_ticks()`** (TIMER4, 24 per µs) being readable in the audio interrupt.
- **The app is now built with `-mcpu=r3 -mfprev1`.** The device build and every host test pass in CI, but nothing
  has run on a keyboard.
  - docs/RULES.md says this flag set alone turns the FPU on, the audio interrupt included, as in ciapibox and
    Drumtonic's FM-1 app builds.
  - Nothing enables it at boot, and nothing is expected to.
  - The floats are passed in the integer registers, so the interrupt's register save covers them.
  - **A device smoke test is required before installing:** boot, play the musical patch, watch the CPU guard's
    limit (Globals: `CPU .. PEAK .. VOX n/6 SHED n`) and the red-screen fault handler.

The first pass's record follows.

## Phase-1 CPU gate (Task 11, first pass): result BLOCKED, NVOICE left at 6

**Rule** (spec "Constraints", plan Task 11): `NVOICE = 6` if the 6-voice worst case takes less than 50 % of the audio
block in **both** cycle models at 240 MHz. If not, profile the hot spots and optimise once. If it is still over,
`NVOICE = 4`, unless 4 voices are over too. Then the gate is BLOCKED and João decides.

**Result.** After the optimisation pass, the 6-voice worst case takes **172 % (emu) / 248 % (hw)** of the block at
240 MHz. The 4-voice worst case takes **120 % / 164 %**. It does not pass at 360 MHz either: 115 % / 165 % for 6 voices
and 80 % / 109 % for 4 voices (hw model). **Neither voice count meets the gate, so the gate is BLOCKED.** `NVOICE` stays
at 6 (voices.h, unchanged) until João decides what to change (see "What would have to change" below).

### Method

- **Emulator.** [joakimeriksson/fm1-emu](https://github.com/joakimeriksson/fm1-emu) (MIT), pinned at `60645a5`. It is
  patched (`tests/cpu_emu/patch_emu.py`) to record instructions and cycles per program counter in two cycle models:
  - **emu**: fm1-emu's own model (two ALU operations per cycle);
  - **hw**: the 2026-11-15 hardware probe table (ALU 1 cycle; load, store and multiply 2; a taken branch, call or
    return 7.5).
  The two models differ by about 1.4×. Both come from João's spike in `jhortale/smk37-analog` (branch
  `spike/device-link`, `spike/cpu/`), which is copied here with credit.
- **Harness.** `tests/cpu_emu/harness.c` builds params plus `synth/` as `app.c` does: a unity build with the JieLi
  clang, the app's flags (`-target pi32v2 -Os -ffunction-sections -fno-builtin`, `tools/build.py` CFLAGS) and
  `-DNVOICE=6` or `4`. It loads the patch, plays six keys and renders 256-frame blocks (one audio interrupt) through
  `synth_render`.
- **Per-block cost.** Each case runs with 40 blocks and with 8 blocks. The difference divided by 32 is the cost of one
  block, so start-up and the note-ons cancel out.
- **Correctness check.** Each run ends with its result words: the number of voices sounding, `sizeof(synth_t)` and a
  checksum of the output. The checksums before and after the optimisation are identical, so the pass is bit-exact.
- **Where it runs.** CI job `fm1-emu` in `.github/workflows/build.yml`; `bash tests/cpu_emu/run.sh EMU OUTDIR` runs it
  locally (Docker off Linux x86-64).
- **Budget.** One block is 256 frames at 44.1 kHz = 5.805 ms:
  - 1,393,197 cycles at 240 MHz (the clock fm1-emu models), so the 50 % gate is **696,599 cycles**;
  - 2,089,796 cycles at 360 MHz (the clock seen under the bootloader; the SMK-37's own clock is not confirmed).
- **Patches.**
  - **init chord**: `program_init`, C4 E4 G4 C5 E5 G5 at velocity 100 (`tests/scripts/chord.txt`).
  - **worst**: `tests/scripts/worst.txt` = `test_render` WORST, at velocity 127 with the mod wheel and aftertouch at
    127. It has:
    - every mixer level at maximum;
    - VCO 1 pulse PW 0 and VCO 2 pulse PW 255, with sync on;
    - FILTER FREQ 100, maximum RESONANCE, FENV AMT 127;
    - X-Mod VCO 2 and the filter envelope at maximum on every destination, which runs the per-sample filter
      coefficient path;
    - the LFO square at 500 Hz, full amount, on every destination;
    - aftertouch on every destination.
    It is played on C3 G3 C4 E4 G4 C5, and on a **high chord** of notes 120 121 123 124 126 127 (Task 2: there the
    out-of-line PolyBLEP / sync path of pulse + pulse + sync runs on most samples).

### Cycles per 256-frame block

The 240 MHz and 360 MHz columns are % of the block (emu / hw); "gate" means under 50 % in both models at 240 MHz.

Before (HEAD `099167b`):

| patch | voices | insns | emu cycles | hw cycles | 240 MHz | 360 MHz | gate |
|---|---|---|---|---|---|---|---|
| init chord | 6 | 559,087 | 960,689 | 1,300,869 | 69.0 / 93.4 | 46.0 / 62.2 | no |
| worst C3..C5 | 6 | 1,521,252 | 2,296,755 | 3,172,978 | 164.9 / 227.7 | 109.9 / 151.8 | no |
| worst high chord | 6 | 2,210,111 | 3,449,769 | 4,769,422 | 247.6 / 342.3 | 165.1 / 228.2 | no |
| init chord | 4 | 380,416 | 654,052 | 886,867 | 46.9 / 63.7 | 31.3 / 42.4 | no |
| worst C3..C5 | 4 | 1,053,609 | 1,598,265 | 2,208,470 | 114.7 / 158.5 | 76.5 / 105.7 | no |
| worst high chord | 4 | 1,457,953 | 2,273,870 | 3,143,953 | 163.2 / 225.7 | 108.8 / 150.4 | no |

After the optimisation pass (CI run 44010363233, the same numbers as the local run):

| patch | voices | insns | emu cycles | hw cycles | 240 MHz | 360 MHz | gate |
|---|---|---|---|---|---|---|---|
| init chord | 6 | 462,908 | 665,188 | 930,203 | 47.7 / 66.8 | 31.8 / 44.5 | no |
| worst C3..C5 | 6 | 1,277,700 | 1,695,643 | 2,396,581 | 121.7 / 172.0 | 81.1 / 114.7 | no |
| worst high chord | 6 | 1,802,993 | 2,460,607 | 3,448,630 | 176.6 / 247.5 | 117.7 / 165.0 | **no** |
| init chord | 4 | 316,096 | 456,750 | 639,352 | 32.8 / 45.9 | 21.9 / 30.6 | yes |
| worst C3..C5 | 4 | 884,162 | 1,182,457 | 1,668,887 | 84.9 / 119.8 | 56.6 / 79.9 | no |
| worst high chord | 4 | 1,193,183 | 1,629,445 | 2,283,876 | 117.0 / 163.9 | 78.0 / 109.3 | **no** |

What the pass gained:
- hw cycles fell by 24-28 % on the worst cases and 28 % on the init chord.
- Per voice and sample, the 6-voice worst case still costs 1,560 hw cycles (C3..C5) and 2,245 (high chord).
- For the gate, the whole voice would have to cost **453** hw cycles per sample with 6 voices, or **680** with 4.

Host yardstick (`tests/cpu_host.sh`, x86-64 instructions per sample) moved the same way:
- init chord: 2379 → 2110;
- worst: 5675 → 4900.

### Hot spots after the pass (hw cycles per block)

6-voice worst case, C3..C5 (2.40 M in all):
- `voice_render`: 1.41 M. This is the inlined per-sample loop: oscillators, the X-Mod paths (`mods_fm1`, VCO 1
  shape, `svf_coef_fast`), the mixer, two ADSRs, the SVF fast path, the VCA and pan.
- `svf_slow_x`: 0.40 M. With the input overdriven and RESONANCE at maximum, the SVF states leave the fast path's
  ±2^29 gate on **74 % of samples** (counted on the host; the output overflow check never fires). Those samples take
  the exact 64-bit path.
- `osc_blep`: 0.19 M. `osc_sub_blep`: 0.09 M. `osc_vco1_sync`: 0.09 M.
- Control rate:
  - `mods_sum`: 0.05 M;
  - `voice_control`: 0.04 M;
  - `synth_render`, the bus and limiter: 0.04 M.

6-voice worst case, high chord (3.45 M in all):
- `voice_render`: 1.35 M.
- `osc_vco1_sync`: **1.17 M**. This is the band-limited hard sync near every VCO 2 wrap, which at 8-12 kHz is most
  samples.
- `svf_slow_x`: 0.39 M.
- `osc_blep`: 0.29 M.

Init chord, 6 voices (0.93 M in all):
- `voice_render`: 0.70 M. A saw through an open filter costs about 450 hw cycles per voice and sample.
- Control rate: `svf_coef`, `osc_exp2`, `voice_control`, `mods_sum`, together about 0.12 M.

Before the pass, at `-Os` nothing per sample was inlined. Each voice and sample paid calls and returns to these
functions, at 7.5 hw cycles each plus register saves:
- `osc_tick`, `svf_tick`, `adsr_tick` ×2, `vca_control`, `vca_apply`;
- in the worst case, also `mods_fm1`, `osc_set_shape`, `svf_coef_fast`, `osc_side`, `osc_step` and `svf_slow`.

### The optimisation pass (commit `60b2d1d`, bit-identical)

- **Per-sample inlining.**
  - `osc_tick`, VCO 1's part of `osc_set_shape`, `svf_tick`, `svf_coef_fast`, `adsr_tick`, `vca_control`, `vca_apply`
    and `mods_fm1` have static always-inline cores (`*_hot`), which `voice_render` calls.
  - The external functions remain as wrappers for the tests and the control-rate callers.
  - `osc_side`, `osc_step` and `osc_ev` are inlined into the step paths.
  - The clamps are forced inline.
- **SVF slow path.** `svf_slow` gets the input already driven, so it is not driven twice. Forcing it inline was tried
  and was slower (more register pressure on the common path), so it stays out of line.
- **X-Mod shape.** The per-sample X-Mod shape update now recomputes VCO 1's amounts only (`osc_set_shape1_hot`).
- **Fold-free note clamps** (Task 7 review). `osc_log2_inc` and `mods_fm1` clamp `note + 0x3C0000` instead of loading
  the never-written `osc_note_lo_neg` / `mods_note_lo_neg`. That removes the loads per sample, makes `mods_fm1` 32-bit
  only and puts no ROM-window constant in the code: the device build's check passes, and the harness disassembly
  was checked too.
- **Filter gains on a new note** (Task 7 review). `synth_voice_on` sets `g0 = coef` after its `voice_control`, so a new
  note no longer ramps its filter's lp / hp / bp gains from the voice's previous note.
- **Tests.** All unit and host tests pass, and the emulator checksums are unchanged.

### What would have to change (for João: design decisions, not done here)

Neither voice count is close to the gate. Even 4 voices on the worst case need about 2.4× (C3..C5) and 3.3× (high
chord) less. Fixed-point tuning of the same algorithms will not close that gap. These options change the sound or the
spec, so they are listed for a decision and not applied:

1. **X-Mod at a reduced rate.** Today `mods_fm1`, VCO 1 shape / PW and `svf_coef_fast` run every sample. Running them
   every 2 or 4 samples, with interpolation, would cut the largest part of `voice_render` in the worst case. The spec
   says "audio-rate X-Mod".
2. **A cheaper large-state SVF path.** 74 % of the worst case's samples take the 64-bit exact path. A 32-bit path
   proven for |state| < 2^30, or a float SVF (`-mfprev1`: float add and multiply cost 2 hw cycles), would need new
   bounds and tests, and the output would no longer be bit-identical to today's.
3. **Hard sync at high pitch.** Above about 4 kHz VCO 2, the band-limited sync could use a simpler correction (one
   BLEP per restart). This is the 1.17 M on the high chord, and it changes the sound there.
4. **Envelopes and VCA at control rate**, with per-sample linear ramps (docs/RULES.md suggests it). They cost about
   0.25 M per 6-voice block before inlining. This changes the output.
5. **Structure.** Render each voice's 16-frame period with its state held in locals, not loaded from the struct every
   sample.
6. **Clock.** If the SMK-37 really runs at 360 MHz, every figure above shrinks by 1/3. Even then, 4 voices on the worst
   case are 80-109 %.
7. **The worst case itself.** Every destination at full is a stress patch. A gate on a "loud but musical" patch would
   be a spec change.

### Not modelled

- fm1-emu has no cache model. The harness runs from SRAM (all hits), while on the device the synth runs from the
  cached XIP flash, so real figures can only be higher.
- The interrupt's own overhead (the queue drain, the platform's mixing, USB and key-scan preemption) is not included.
- The cycle models are estimates from hardware probes. The instruction counts are exact.

## Flash, RAM, pool (device build, Pro)

| build | image (of 619,520) | RAM .data+.bss (of 98,304) | pool (of 344,064) |
|---|---|---|---|
| `099167b` (before) | 98,384 B | 23,684 B | 47,092 B |
| `f4b216b` (after the pass) | 100,800 B (+2,416: the inlined per-sample path) | 23,684 B | 47,092 B |

`sizeof(synth_t)`: 2,184 B with 6 voices, 1,672 B with 4.

## Decision (João, 2026-11-17)

NVOICE = 6 with the CPU guard. The musical stress patch measures 55–80 % (C3–C5) and 64–91 % (C5–C7) of the block at
240 MHz (emu / hw); the guard sheds voices in one step above 80 % so no patch can overrun the audio interrupt. The first
on-device run (after a full flash backup) must read the real load with `fm1_ticks()`; if the guard sheds often on
musical patches at the real clock, NVOICE = 4 is a one-line change.
