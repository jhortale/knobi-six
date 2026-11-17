# KNOBI-SIX

A standalone, OB-6-inspired polyphonic synth firmware for the **M-VAVE SMK-37 Pro** (also built for the Elite and the
MKE-P37): six voices, two oscillators, a state-variable filter, the 12 knobs of the front panel, and patch memory, all
running on the keyboard itself. The design is in
[docs/superpowers/specs/2026-11-16-standalone-firmware-design.md](docs/superpowers/specs/2026-11-16-standalone-firmware-design.md),
the OB-6 reference it follows in [docs/ob6-reference.md](docs/ob6-reference.md), the work plan in
[docs/superpowers/plans/2026-11-16-standalone-phase1.md](docs/superpowers/plans/2026-11-16-standalone-phase1.md).

> **Status: in development, not installable yet.** Phase 1 is being built; nothing has been tried on a keyboard. Do
> not install it. Its identity (name, data, `SMK-37 Pro_956`): [docs/IDENTITY.md](docs/IDENTITY.md).

## Build and test

Linux on x86-64, or macOS with Docker; Python 3 with Pillow; JieLi's toolchain and three files of the AC79 SDK.

```sh
tools/get_toolchain.sh                      # JieLi's clang for the chip, to ~/.jieli/toolchain
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
./build.sh                                  # -> build/knobi-six.fwsc (--model elite|mke; -local with a factory bank)
sh tests/run_tests.sh                       # the host test suite, after ./build.sh
tests/run_host.sh tests/scripts/diag.txt    # the app on the computer: screens as PNG, sound as WAV (build/host/)
make -C tests unit                          # unit tests (from Task 1 on)
```

CI (`.github/workflows/build.yml`) does the device build and the tests on every push to `phase1` and uploads the
package, `app.map` and `app.dis`.

## Importing the OB-6 factory programs

KNOBI-SIX ships with **empty banks**: every program starts as the init program. If you own the OB-6 factory programs
file, you can build your own firmware with them as a read-only factory bank (program N, never written, plays factory
program N; GLOBE + BT twice restores it; [docs/FACTORY_BANK.md](docs/FACTORY_BANK.md)).

The programs belong to **Sequential / Dave Smith Instruments**. They are **not included** in this repository or in any
KNOBI-SIX build we publish, and the header you generate holds their data: **never commit it or redistribute it**, nor
a firmware built with it (`firmware/gen/factory_bank.h` is in `.gitignore`).

1. Open Sequential's own page "OB-6 Factory Sounds", <https://sequential.com/ob-6-factory-sounds/>, and download
   the OB-6 factory programs (.syx) offered on that page.
2. Unzip it: it holds the program file (`OB6_Programs_v1.01.syx` in version 1.01, 500 programs) and a ReadMe.
3. Generate the local header:
   ```sh
   python3 tools/ob6_import.py ~/Downloads/OB-6-Factory-Programs/OB6_Programs_v1.01.syx
   ```
   It writes `firmware/gen/factory_bank.h` and a report of every conversion and warning in
   `build/factory_bank_report.txt`.
4. Build as usual (`./build.sh`) and install as usual. The bank adds about 100 KB to the image. With the header
   there, the package is `build/knobi-six-local.fwsc` (`-local-elite`, `-local-mke`) and the build warns that it holds
   your imported bank: install it on your own keyboard, **never share it**. A `--release` build refuses to run while
   the header is there.

Program names are kept as they are, including the ones that contain "OB". The sequencer data of the
programs is not imported (a program has no room for it); effects, arpeggiator and clock settings are kept for later
phases but do nothing yet. Delete `firmware/gen/factory_bank.h` to build without the bank again.

## Credits

KNOBI-SIX starts from [smk37-base](https://github.com/fc79/smk37-base) by fc79, which is the SMK-37 layer of
[Dinghy](https://github.com/zvenson/dinghy) (zvenson) on [Felucca](https://github.com/hugelton/Felucca)'s and
[SLOOP](https://github.com/isod89/sloop-fm1)'s platform, with genkigenki's SMK-37 port. What came from where, and the
third-party parts: [LICENSING.md](LICENSING.md); what the base changed from Dinghy: [docs/CHANGES.md](docs/CHANGES.md).
The platform's API and the chip's rules: [docs/API.md](docs/API.md), [docs/RULES.md](docs/RULES.md). The original
README of the base is in git history (`upstream/main`).

GPL-3.0-only; "M-VAVE", "SMK-37", "MKE-P37" and "OB-6" belong to their owners, and KNOBI-SIX is not made or endorsed
by them.
