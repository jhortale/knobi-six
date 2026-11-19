#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# KNOBI-SIX CPU gate (Task 11): build tests/cpu_emu/harness.c with the JieLi pi32v2 clang at the app's flags
# (tools/build.py CFLAGS: -Os, unity build) for each patch x voice count x block count, run it in the patched fm1-emu
# (patch_emu.py: per-pc instructions and two cycle models) and summarise (analyze.py); then the same patches at 6 voices
# with the dual-core split (DUAL=1, the cycles per core). After the spike in
# jhortale/smk37-analog (spike/device-link, spike/cpu/run.sh, João Hortale).
#   bash tests/cpu_emu/run.sh FM1_EMU OUTDIR      (JIELI_TOOLCHAIN as for build.sh; Docker off Linux x86-64)
set -euo pipefail
cd "$(dirname "$0")/../.."
EMU=$1; OUT=$2; mkdir -p "$OUT"
TC="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
H=tests/cpu_emu
if [ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ]; then
    tc() { local t=$1; shift; "$TC/$t" "$@"; }
else   # the toolchain is Linux x86-64 only (as build.sh): run it in Docker
    tc() { local t=$1; shift; docker run --rm --platform linux/amd64 -v "$PWD:/work" -v "$(cd "$TC" && pwd -P):/opt/jieli:ro" \
        -w /work debian:bookworm-slim "/opt/jieli/$t" "$@"; }
fi
# tools/build.py: CFLAGS + APP_FPU + BOARD_DEFS, the app's include paths
FLAGS=(-target pi32v2 -Os -ffunction-sections -fno-builtin -Wall -Wno-unused-function -mcpu=r3 -mfprev1 -ffp-contract=off
       -DBOARD_SMK37=1
       -Ifirmware/hal -Ifirmware/src -Ifirmware/gen)
tc pi32v2/bin/clang -target pi32v2 -c $H/crt0.S -o "$OUT/crt0.o"
for nv in 6 4; do
  for p in 0 3 4 1 2; do
    n="p$p-v$nv"
    for k in 8 40; do
      tc pi32v2/bin/clang "${FLAGS[@]}" -DNVOICE=$nv -DPATCH=$p -DBLOCKS=$k -c $H/harness.c -o "$OUT/$n-$k.o"
      tc pi32v2/bin/ld -T $H/link.ld --gc-sections -o "$OUT/$n-$k.elf" "$OUT/crt0.o" "$OUT/$n-$k.o"
      python3 $H/elf2bin.py "$OUT/$n-$k.elf" "$OUT/$n-$k.bin" > /dev/null
      FM1_SPIKE_COST="$OUT/$n-$k.cost" "$EMU" --bin "$OUT/$n-$k.bin" --steps 1000000000 --dump 0x01C70000 4 \
        > "$OUT/$n-$k.log" 2>&1 || true
    done
    tc pi32v2/bin/nm --print-size "$OUT/$n-40.elf" > "$OUT/$n.nm"
    echo "$n done"
  done
done
# Phase 2 Task 3: the dual-core split (harness DUAL=1), 6 voices, every patch; analyze.py splits the cycles by core
for p in 0 3 4 1 2; do
  n="d$p-v6"
  for k in 8 40; do
    tc pi32v2/bin/clang "${FLAGS[@]}" -DNVOICE=6 -DPATCH=$p -DBLOCKS=$k -DDUAL=1 -c $H/harness.c -o "$OUT/$n-$k.o"
    tc pi32v2/bin/ld -T $H/link.ld --gc-sections -o "$OUT/$n-$k.elf" "$OUT/crt0.o" "$OUT/$n-$k.o"
    python3 $H/elf2bin.py "$OUT/$n-$k.elf" "$OUT/$n-$k.bin" > /dev/null
    FM1_SPIKE_COST="$OUT/$n-$k.cost" "$EMU" --bin "$OUT/$n-$k.bin" --steps 1000000000 --dump 0x01C70000 5 \
      > "$OUT/$n-$k.log" 2>&1 || true
  done
  tc pi32v2/bin/nm --print-size "$OUT/$n-40.elf" > "$OUT/$n.nm"
  echo "$n done"
done
# Phase 2 Task 7: the distortion at full (DIST=127), 6 voices, one core: the init chord and the musical patches
for p in 0 3 4; do
  n="x$p-v6"
  for k in 8 40; do
    tc pi32v2/bin/clang "${FLAGS[@]}" -DNVOICE=6 -DPATCH=$p -DBLOCKS=$k -DDIST=127 -c $H/harness.c -o "$OUT/$n-$k.o"
    tc pi32v2/bin/ld -T $H/link.ld --gc-sections -o "$OUT/$n-$k.elf" "$OUT/crt0.o" "$OUT/$n-$k.o"
    python3 $H/elf2bin.py "$OUT/$n-$k.elf" "$OUT/$n-$k.bin" > /dev/null
    FM1_SPIKE_COST="$OUT/$n-$k.cost" "$EMU" --bin "$OUT/$n-$k.bin" --steps 1000000000 --dump 0x01C70000 4 \
      > "$OUT/$n-$k.log" 2>&1 || true
  done
  tc pi32v2/bin/nm --print-size "$OUT/$n-40.elf" > "$OUT/$n.nm"
  echo "$n done"
done
# Phase 3 Task 7: the sequencer's player through the engine (harness ENG=1, SEQ 0 / 1 / 2 / 3: the six keys held / a
# chord a step with a long release (six voices busy all the time) / a chord and 63 ties / a chord a step with the
# patch's release; 250 BPM, 1/16), 6 voices. q runs: one core, over 91 and 8 blocks (83 = 8.03 steps) and, on the
# musical C3..C5 patch, over 11 and 10 blocks (block 10 holds a step); r runs: the dual-core split (DUAL=1)
seqrun() {   # name, patch, seq, dual, blocks...
    local n=$1 p=$2 sq=$3 du=$4; shift 4
    for k in "$@"; do
        tc pi32v2/bin/clang "${FLAGS[@]}" -DNVOICE=6 -DPATCH=$p -DBLOCKS=$k -DENG=1 -DSEQ=$sq -DDUAL=$du -c $H/harness.c -o "$OUT/$n-$k.o"
        tc pi32v2/bin/ld -T $H/link.ld --gc-sections -o "$OUT/$n-$k.elf" "$OUT/crt0.o" "$OUT/$n-$k.o"
        python3 $H/elf2bin.py "$OUT/$n-$k.elf" "$OUT/$n-$k.bin" > /dev/null
        FM1_SPIKE_COST="$OUT/$n-$k.cost" "$EMU" --bin "$OUT/$n-$k.bin" --steps 1000000000 --dump 0x01C70000 $((4 + du)) \
          > "$OUT/$n-$k.log" 2>&1 || true
    done
    tc pi32v2/bin/nm --print-size "$OUT/$n-$1.elf" > "$OUT/$n.nm"
    echo "$n done"
}
for p in 3 4; do
  for sq in 0 1 2 3; do seqrun "q$p$sq-v6" $p $sq 0 91 8; done
done
for sq in 0 1; do seqrun "q3$sq-v6" 3 $sq 0 11 10; done
for sq in 0 1 2 3; do seqrun "r3$sq-v6" 3 $sq 1 91 8; done
python3 $H/analyze.py "$OUT" | tee "$OUT/summary.md"
