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
python3 $H/analyze.py "$OUT" | tee "$OUT/summary.md"
