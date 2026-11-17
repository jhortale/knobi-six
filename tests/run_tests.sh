#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# KNOBI-SIX's host tests (no hardware). Run from the repo root after building (the Pro last: the tests use its build/
# intermediates; the Elite and MKE-P37 packages are checked when they are there):
#   MODEL=elite sh build.sh; MODEL=mke sh build.sh; sh build.sh
#   sh tests/run_tests.sh
# The app on the host (tests/run_host.sh, tests/scripts), the SMK-37 flash map, pads and pitch wheel, the update path
# (ota.c, the loader, tools/fm1_install.py; from the fm1-to-smk37 kit's reference layer, genkigenki/sloop-fm1 smk37)
# and the packages.
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"   # (fm1pkg_make.py: the SDK files of a package)
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
fail=0
run() { echo "== $1"; shift; "$@" || fail=1; }

run "unit tests (tests/unit/test_*.c)" make -s -C tests unit
run "the OB-6 importer on synthetic dumps (tools/ob6_import.py; no OB-6 data)" python3 tests/ob6_import_test.py
run "tools/build.py and a local factory bank: --release refused, other builds -local and a warning" python3 tests/build_factory_test.py

# (knobi-six-local.fwsc: built with the local factory bank, docs/FACTORY_BANK.md)
[ -f build/knobi-six.fwsc ] || [ -f build/knobi-six-local.fwsc ] || { echo "run ./build.sh first"; exit 1; }

run "the app on the host: the default script" sh tests/run_host.sh
run "every kind of control once: keys, a button, an encoder, a pad, a fader, a wheel, an outer key" sh tests/run_host.sh tests/scripts/diag.txt
run "velocity, a pad, an outer key, the pitch wheel" sh tests/run_host.sh tests/scripts/smk37.txt
run "MIDI in on USB, any channel; a key out on channel 1" sh tests/run_host.sh tests/scripts/midi.txt
run "a key held: its note sounds until the key is let go; a bottom pad plays no note (expect)" sh tests/run_host.sh tests/scripts/notes.txt
run "the init program: a six-note chord, the wheels, the release (expect)" sh tests/run_host.sh tests/scripts/chord.txt
run "the worst-case patch, six voices, set through the event queue (expect)" sh tests/run_host.sh tests/scripts/worst.txt
run "the musical stress patch (the CPU gate), C3..C5 (expect)" sh tests/run_host.sh tests/scripts/musical.txt
run "the musical stress patch, C5..C7 (expect)" sh tests/run_host.sh tests/scripts/musical_high.txt
run "Review Focus 4: bottom-pad pressure while a top pad toggles: once; aftertouch only from the bottom row (expect)" sh tests/run_host.sh tests/scripts/pads_split.txt
run "faders through the pot mode: passthru, jump, relative (expect)" sh tests/run_host.sh tests/scripts/faders_potmode.txt
run "MIDI CC in and out, PARAM TX / RX, MIDI CH, wheel, bend, sustain, program change + CC 32 bank (expect)" sh tests/run_host.sh tests/scripts/midi_cc.txt
run "chord memory: hold CHORD and play, the chord plays from a key (expect)" sh tests/run_host.sh tests/scripts/chord_memory.txt
run "the screens: every one drawn and shot, the bindings drawn, only what changed redrawn (expect)" sh tests/run_host.sh tests/scripts/screens.txt
run "the globals: transpose, tune, velocity curve, LOCAL, MONO, sustain polarity, AT curve; saved when quiet (expect)" sh tests/run_host.sh tests/scripts/globals.txt
run "the autosave: not inside a staccato phrase (keys, MIDI) nor while a pad is held; 3 s after (expect)" sh tests/run_host.sh tests/scripts/autosave.txt
run "Review Focus 3: WRITE during a HOLD drone: stored, the drone sounds on (expect)" sh tests/run_host.sh tests/scripts/write_held.txt
run "Review Focus 3: WRITE's WAV: no click, only the deliberate fade (10 ms out, 10 ms in) around the save's silence" \
    python3 tests/wav_check.py build/host/app.wav --steady 600 1450 --check 1450 2050 --fade-max 25 --gap-max 300
run "the factory bank (the synthetic test bank): fallback, WRITE overrides, GLOBE + WRITE restores (expect)" \
    env FACTORY=tests/unit/factory_test_bank.h sh tests/run_host.sh tests/scripts/factory.txt
run "Review Focus 3: program change under a held chord and a HOLD drone, MANUAL: nothing sticks (expect)" sh tests/run_host.sh tests/scripts/prog_change_held.txt
run "Review Focus 3: the program change under the chord: no click" python3 tests/wav_check.py build/host/app.wav --steady 700 990 --check 990 1300
run "Review Focus 3: the program change under the HOLD drone: no click" python3 tests/wav_check.py build/host/app.wav --steady 2250 2490 --check 2490 2800
run "a program change (MIDI or the panel) while WRITE is armed or committing: ignored, the edit is stored (expect)" sh tests/run_host.sh tests/scripts/write_prog_change.txt
run "the clock (Task 5): tap tempo, the division, LFO SYNC, MIDI clock in: start, stop, continue, gone (expect)" sh tests/run_host.sh tests/scripts/clock.txt
run "the CPU guard on the fake clock: a spike sheds a voice, the limit comes back only where it fits; the diagnostics (expect)" sh tests/run_host.sh tests/scripts/cpu_guard.txt

$CC -DBOARD_SMK37=1 -Ifirmware/hal -o "$OUT/smk_flash_test" tests/smk_flash_test.c
run "SMK-37 flash map: settings, blobs, owner tag, slots in the store; never the head, BTIF/USRTRIM, USR, key_mac" "$OUT/smk_flash_test"
$CC -DBOARD_SMK37=1 -Wno-int-to-pointer-cast -Ifirmware/hal -o "$OUT/smk_pad_test" tests/smk_pad_test.c
run "SMK-37 pads: one hit per strike (a rebound, a noisy release), every strike of a fast roll" "$OUT/smk_pad_test"
$CC -o "$OUT/smk_wheel_test" tests/smk_wheel_test.c
run "SMK-37 pitch wheel: the bend is back to 0 at rest, however slowly the wheel was moved" "$OUT/smk_wheel_test"

$CC -o "$OUT/ldr_screen_test" tests/ldr_screen_test.c
run "update loader's screen (from Skiff): UPDATE MODE, WRITING and its bar, UPDATE FAILED" "$OUT/ldr_screen_test" "$OUT"

# the OTA staging and the loader windows: an older, shorter app -> this build
head -c 60000 build/app.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py --board smk37 build/app.bin build/loader/ota.bin "$OUT/smk_new.fwsc" >/dev/null
python3 tools/fm1pkg_make.py --board smk37 "$OUT/old_app.bin" build/loader/ota.bin "$OUT/smk_old.fwsc" >/dev/null
run "SMK-37 package layout (fm1pkg_make --board smk37): PRCT 0x9D000, stock region table" \
    python3 tests/smk_pkg_test.py "$OUT/smk_new.fwsc" "SMK-37 Pro_900"
$CC -DBOARD_SMK37=1 -o "$OUT/ota_test_smk" tests/ota_test.c
run "M-UPGRADE entry, SMK-37 Pro (staging 0xBB000, parm ota-SMK-37 Pro_016)" "$OUT/ota_test_smk" "$OUT/smk_new.fwsc"
for m in 1 2; do
    $CC -DBOARD_SMK37=1 -DT_MODEL=$m -o "$OUT/ota_test_smk_m$m" tests/ota_test.c
    run "M-UPGRADE entry, SMK-37 model $m (1 Elite, 2 MKE-P37): identity and parm under its name" "$OUT/ota_test_smk_m$m" "$OUT/smk_new.fwsc"
done
$CC -DBOARD_SMK37=1 -o "$OUT/ldr_test_smk" tests/ldr_test.c
run "update loader, SMK-37: app area 0x4000-0x9CFFF, protected ranges untouched" "$OUT/ldr_test_smk" "$OUT/smk_old.fwsc" "$OUT/smk_new.fwsc"
for m in ":SMK-37 Pro_956" "-elite:SMK-37 Elite_956" "-mke:MKE-P37_956"; do
    for f in build/knobi-six*${m%%:*}.fwsc; do
        [ -f "$f" ] || continue
        case "$f" in *-elite*|*-mke*) [ -n "${m%%:*}" ] || continue ;; esac
        run "package $f: layout, identity ${m#*:}, the loader marker" python3 tests/smk_pkg_test.py "$f" "${m#*:}"
    done
done

run "installer (tools/fm1_install.py): install and the update-mode resume check the model" python3 tests/install_test.py

[ "$fail" = 0 ] && echo "ALL TESTS PASSED" || echo "SOME TESTS FAILED"
exit "$fail"
