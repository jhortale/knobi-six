#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# KNOBI-SIX second core in the emulator (Phase 2 Task 2): build the app with KNOBI_CPU1=1 (and the test hooks of
# hal/fm1_cpu1.h and platform.c, -DKNOBI_CPU1_TEST), boot each image in joakimeriksson/fm1-emu (pinned; dual core:
# cpu0 and cpu1 a step each, cpu1 released by 0x10008 bit 3 at the word 0x01C7FFF8) and check what the boot left in
# RAM (check.py). The cases:
#   plain    KNOBI_CPU1=1 as João would install it: CPU1 started, the boot ping, idle in its RAM loop, no fault
#   start    test hook 3: + a ping round trip every ~100 ms inside the audio interrupt: all come back; a post from
#            the main loop is refused, a join with nothing outstanding says 0; the boot's flash reads passed the gate
#            (flash_ok); at 600 ms an erase, a write, a sliced erase and a sliced write through the gate with CPU1 on,
#            each read back, the pings going on between the slices
#   nostart  test hook 1: CPU1 never reports in -> one core after 20 ms, the boot guard unchanged, the audio runs
#   hang     test hook 2: a job that never returns -> the join times out (4 ms, under one 5.8 ms block), retired
#   flaky    test hook 3 with --flaky-core1 (cpu1 misreads RAM now and then): the check word -> retired, audio runs.
#            With --cpu1-survives: a misread pointer can send cpu1 anywhere, and without it the emulator stops the
#            whole machine there (the chip would raise a fault that CPU0 handles; the emulator has no such path)
#   reset    test hook 5: a warm reset inside the start window (after CPU1 answered, before the boot ping): the
#            second boot counts one failed boot (as any boot crash), CPU1 stays off (barred by the start mark, and
#            bootguard.failed), the audio runs; one chip reset only
#   dual     test hook 6 (Task 3, tests/cpu1_emu/dual_hook.c): four patches played by the audio interrupt from a fixed
#            script, DUAL CORE off then on, in one image and one run: every output checksum the same, CPU1 jobs only
#            with it on, CPU0's render time (the joins' waits included) and CPU1's job load per patch
#   dualfail test hook 7: as dual, CPU1's 1000th voice job never returns: the join fails, CPU0 renders set B in that
#            period, CPU1 retired (LATE), one core from there; every checksum still the same
#   dualmid  test hook 9: as dualfail, but the job stops after one voice of set B (late part way): that voice is
#            rendered again from one period on, once; the patch's checksum differs, its largest step and peak stay
#            within the one-core run's, the next patches equal again
#   write    test hook 8: DUAL CORE on, a chord held, WRITE + ENTER (the program slot's erases and writes), then the
#            globals' autosave (the extended record, read back), a chord again: CPU1 stays IDLE (the flash gate never
#            had to retire it), the jobs go on, no fault, the audio runs
#   bash tests/cpu1_emu/run.sh FM1_EMU OUTDIR     (as ./build.sh: JIELI_TOOLCHAIN, AC79_SDK, Pillow; Docker off Linux)
# It leaves build/ holding the last CPU1 image: run ./build.sh again for the default one.
set -euo pipefail
cd "$(dirname "$0")/../.."
EMU=$(cd "$(dirname "$1")" && pwd -P)/$(basename "$1"); OUT=$2; mkdir -p "$OUT/work"
OUT=$(cd "$OUT" && pwd -P)
MS=${CPU1_EMU_MS:-1500}
RATE=${CPU1_EMU_FLAKY:-1e-7}
DMS=${CPU1_EMU_DUAL_MS:-2400}                    # dual, dualfail: the boot + 8 segments of 40 blocks (1.86 s)
WMS=${CPU1_EMU_WRITE_MS:-6500}                   # write: up to the autosave (~5 s) and a chord after it

build() {   # name, test hook (empty: none)
    rm -f build/knobi-six*-cpu1*.fwsc
    if ! KNOBI_CPU1=1 DINGHY_DEFS="${2:+-DKNOBI_CPU1_TEST=$2}" ./build.sh > "$OUT/$1.build.log" 2>&1; then
        cat "$OUT/$1.build.log"; exit 1
    fi
    cp build/knobi-six*-cpu1*.fwsc "$OUT/$1.fwsc"
    cp build/app.map "$OUT/$1.map"
    grep -E '^  (ok|FAIL)' "$OUT/$1.build.log" > "$OUT/$1.checks"
}
sym() { grep -E " $2\$" "$OUT/$1.map" | cut -c1-8; }
emu() {     # case, image, dump symbol, words, extra emu options...
    local c=$1 img=$2 s=$3 n=$4; shift 4
    (cd "$OUT" && "$EMU" --fwsc "$img.fwsc" --ms "${RUN_MS:-$MS}" --dump "$(sym "$img" "$s")" "$n" "$@") > "$OUT/$c.log" 2>&1 || true
    echo "$c done"
}

build plain ""
build test3 3
build nostart 1
build hang 2
build reset 5
build dual 6
build dualfail 7
build write 8
build dualmid 9
emu plain plain fm1_cpu1_mb 13
emu start test3 cpu1_test 20
emu nostart nostart cpu1_test 20
emu hang hang cpu1_test 20
emu flaky test3 cpu1_test 20 --flaky-core1 "$RATE" --seed 7 --cpu1-survives
emu reset reset cpu1_test 20
RUN_MS=$DMS emu dual dual dual_test 66 --keys 800:GLOBE --snap 2000 &
RUN_MS=$DMS emu dualfail dualfail dual_test 66 &
RUN_MS=$DMS emu dualmid dualmid dual_test 66 &
RUN_MS=$WMS emu write write dual_test 66 &
wait
python3 tests/cpu1_emu/check.py "$OUT" "$MS" "$RATE" "$DMS" "$WMS" | tee "$OUT/summary.md"
