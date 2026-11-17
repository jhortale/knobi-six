#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The synth's instructions per sample on this computer, where DINGHY_PERF=1 cannot count them (no hardware counter:
# a VM, a container, a CI runner): callgrind counts the instructions of synth_render (the whole audio render bar the
# queue drain) over a host run, divided by the frames rendered while notes sound (FROM_MS..TO_MS of the script).
# Linux with valgrind; the yardstick is the base's (DINGHY_PERF, x86-64): ~1000-1500 per sample fits the ISR.
#   sh tests/cpu_host.sh SCRIPT FROM_MS TO_MS     e.g. sh tests/cpu_host.sh tests/scripts/worst.txt 200 6000
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O2 -Wno-unused-function -DDINGHY_HOST -DBOARD_SMK37=1 -DAPP_VER='"dev"' -Ifirmware/hal -Ifirmware/src -Ifirmware/gen \
    -o build/host/cpu firmware/src/app.c -lm 2>/dev/null
valgrind --tool=callgrind --collect-atstart=no --toggle-collect=synth_render --callgrind-out-file=build/host/cpu.cg \
    build/host/cpu build/host/cpu "$1" >/dev/null 2>&1
ir=$(callgrind_annotate build/host/cpu.cg 2>/dev/null | sed -n 's/^ *\([0-9,]*\) .*PROGRAM TOTALS.*/\1/p' | tr -d ,)
fr=$(( ($3 - $2) * 441 / 10 ))
echo "cpu    $1 ($(uname -m)): synth_render $ir instructions, $((ir / fr)) per sample over $2..$3 ms"
