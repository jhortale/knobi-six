#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/build.py and the local factory bank (docs/FACTORY_BANK.md): with firmware/gen/factory_bank.h there, a
--release build refuses to run (before anything is built), any other build names its package knobi-six-local*.fwsc and
warns that it holds the user's own imported bank; without it, the names are the usual ones. On a stand-in header in a
temporary directory (the real one, if any, is never touched), with the toolchain steps stubbed out.

    python3 tests/build_factory_test.py
"""
import contextlib
import io
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build  # noqa: E402

fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL", what)


def run_main(args, header, out):
    """build.main with args, the factory header at `header`, the outputs in `out`, every toolchain step stubbed:
    (exit message or None, stderr, the package files written)"""
    saved = (build.FACTORY_H, build.OUT, build.build_loader, build.build_app, build.check, build.mmio_check,
             build.fm1pkg_make.sdk_file, build.fm1pkg_make.flash_image, build.fm1pkg_make.ufw, sys.argv,
             dict(build.BOARD_DEFS), dict(build.BOARD_PREFIX))
    built = []
    build.FACTORY_H, build.OUT = header, out
    build.build_loader = lambda: built.append("loader") or b"L"
    build.build_app = lambda: built.append("app") or (b"A", "", "", "")
    build.check = lambda *a: ([], [])
    build.mmio_check = lambda: []
    build.fm1pkg_make.sdk_file = lambda rel: b""
    build.fm1pkg_make.flash_image = lambda img, key: img
    build.fm1pkg_make.ufw = lambda flash, ota, product: b"PKG"
    sys.argv = ["build.py", *args]
    err, msg = io.StringIO(), None
    try:
        with contextlib.redirect_stderr(err), contextlib.redirect_stdout(io.StringIO()):
            build.main()
    except SystemExit as e:
        msg = str(e.code)
    finally:
        (build.FACTORY_H, build.OUT, build.build_loader, build.build_app, build.check, build.mmio_check,
         build.fm1pkg_make.sdk_file, build.fm1pkg_make.flash_image, build.fm1pkg_make.ufw, sys.argv, bd, bp) = saved
        build.BOARD_DEFS.clear(), build.BOARD_DEFS.update(bd)
        build.BOARD_PREFIX.clear(), build.BOARD_PREFIX.update(bp)
    return msg, err.getvalue(), sorted(p.name for p in out.glob("*.fwsc")), built


with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    header = td / "gen" / "factory_bank.h"
    header.parent.mkdir()

    # factory_guard on its own
    check(build.factory_guard(None, header) == "", "no header: no mark")
    check(build.factory_guard("1.0", header) == "", "no header: a release is fine")
    header.write_text("/* a stand-in: not Sequential's data */\n")
    err = io.StringIO()
    with contextlib.redirect_stderr(err):
        check(build.factory_guard(None, header) == "-local", "header, not a release: -local")
    check("must not" in err.getvalue() or "never share" in err.getvalue(), "header, not a release: the warning")
    try:
        build.factory_guard("1.0", header)
        check(False, "header, release: refused")
    except SystemExit as e:
        check("--release refused" in str(e.code) and "Sequential" in str(e.code), "header, release: the message")

    # the whole main(): a release with the header stops before anything is built, no package
    out = td / "out1"
    out.mkdir()
    msg, err, pkgs, built = run_main(["--release", "1.0"], header, out)
    check(msg is not None and "--release refused" in msg, f"main --release with the header: refused ({msg})")
    check(pkgs == [] and built == [], f"main --release with the header: nothing built ({pkgs}, {built})")
    print(f"release with the header: refused, nothing built: {msg[:60] if msg else msg}...")

    # a dev build with the header: knobi-six-local.fwsc (and -local-elite), the warning
    out = td / "out2"
    out.mkdir()
    msg, err, pkgs, built = run_main([], header, out)
    check(msg is None and pkgs == ["knobi-six-local.fwsc"], f"main, header: knobi-six-local.fwsc ({msg}, {pkgs})")
    check("never share" in err and "do not share" in err, "main, header: the warnings")
    msg, err, pkgs, built = run_main(["--model", "elite"], header, out)
    check(msg is None and "knobi-six-local-elite.fwsc" in pkgs, f"main --model elite, header: -local-elite ({pkgs})")
    print(f"dev build with the header: {pkgs}, warned: {'never share' in err}")

    # without the header: the usual names, a release too
    header.unlink()
    out = td / "out3"
    out.mkdir()
    msg, err, pkgs, built = run_main([], header, out)
    check(msg is None and pkgs == ["knobi-six.fwsc"] and "warning: " + str(header) not in err,
          f"main, no header: knobi-six.fwsc ({msg}, {pkgs})")
    msg, err, pkgs, built = run_main(["--release", "1.0"], header, out)
    check(msg is None and "knobi-six-1.0.fwsc" in pkgs, f"main --release, no header: knobi-six-1.0.fwsc ({msg}, {pkgs})")
    print(f"without the header: {pkgs}")

# a release refuses test-only defines in DINGHY_DEFS (before anything is built); a harmless one and a dev build pass
import os  # noqa: E402

for d in ("-DKNOBI_CPU1_TEST=1", "-DKNOBI_SEQ_TEST", "-DKNOBI_PROBE_TEST=1", "-DDINGHY_HOST", "-DKNOBI_CPU1_PROBE=1",
          "-DKNOBI_NO_FACTORY", "-DKNOBI_CPU1=1", "-DFELUCCA_OTA_DRYRUN=1"):
    try:
        build.defs_guard("1.0", "-DFELUCCA_UAC=0 " + d)
        check(False, f"release with {d}: refused")
    except SystemExit as e:
        check("--release refused" in str(e.code) and d in str(e.code), f"release with {d}: the message")
    build.defs_guard(None, d)                                   # (not a release: allowed)
build.defs_guard("1.0", "-DFELUCCA_UAC=0")                      # (a harmless define: allowed)
build.defs_guard("1.0", "")
with tempfile.TemporaryDirectory() as td2:
    out = Path(td2)
    os.environ["DINGHY_DEFS"] = "-DKNOBI_SEQ_TEST=1"
    try:
        msg, err, pkgs, built = run_main(["--release", "1.0"], Path(td2) / "none.h", out)
    finally:
        del os.environ["DINGHY_DEFS"]
    check(msg is not None and "test-only defines" in msg and built == [], f"main --release, DINGHY_DEFS: refused ({msg})")
print("release with test defines: refused")

print("build_factory_test:", f"{fails} FAILED" if fails else "all passed")
sys.exit(1 if fails else 0)
