#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Run firmware/src/app.c on this computer (tests/host.c), as an SMK-37: build/host/app.wav and the screens as PNG.
#   tests/run_host.sh [script]   your own script (tests/host.c says what a line can do)
#   FACTORY=1     with the local factory bank (firmware/gen/factory_bank.h, docs/FACTORY_BANK.md); by default the
#                 host runs without one (the tests expect the init program wherever nothing is stored)
#   FACTORY=FILE  with the factory bank header FILE (e.g. tests/unit/factory_test_bank.h)
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
N=app
case "${FACTORY:-0}" in
    0) FDEF=-DKNOBI_NO_FACTORY ;;
    1) FDEF= ;;
    *) FDEF="-DKNOBI_FACTORY_H=\"$(pwd)/$FACTORY\"" ;;
esac
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DBOARD_SMK37=1 -DAPP_VER="\"${APP_VER:-dev}\"" $FDEF -Ifirmware/hal -Ifirmware/src -Ifirmware/gen -o build/host/$N firmware/src/app.c -lm
build/host/$N build/host/$N ${1:+"$1"}
python3 - "$N" <<'PY'
import sys
from pathlib import Path
from PIL import Image
for p in sorted(Path("build/host").glob(sys.argv[1] + "-*.ppm")):
    Image.open(p).resize((480, 480), Image.NEAREST).save(p.with_suffix(".png"))
    p.unlink()
    print("screen ->", p.with_suffix(".png"))
PY
