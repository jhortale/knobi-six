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
./build.sh                                  # -> build/knobi-six.fwsc (--model elite|mke for the others)
sh tests/run_tests.sh                       # the host test suite, after ./build.sh
tests/run_host.sh tests/scripts/diag.txt    # the app on the computer: screens as PNG, sound as WAV (build/host/)
make -C tests unit                          # unit tests (from Task 1 on)
```

CI (`.github/workflows/build.yml`) does the device build and the tests on every push to `phase1` and uploads the
package, `app.map` and `app.dis`.

## Credits

KNOBI-SIX starts from [smk37-base](https://github.com/fc79/smk37-base) by fc79, which is the SMK-37 layer of
[Dinghy](https://github.com/zvenson/dinghy) (zvenson) on [Felucca](https://github.com/hugelton/Felucca)'s and
[SLOOP](https://github.com/isod89/sloop-fm1)'s platform, with genkigenki's SMK-37 port. What came from where, and the
third-party parts: [LICENSING.md](LICENSING.md); what the base changed from Dinghy: [docs/CHANGES.md](docs/CHANGES.md).
The platform's API and the chip's rules: [docs/API.md](docs/API.md), [docs/RULES.md](docs/RULES.md). The original
README of the base is in git history (`upstream/main`).

GPL-3.0-only; "M-VAVE", "SMK-37", "MKE-P37" and "OB-6" belong to their owners, and KNOBI-SIX is not made or endorsed
by them.
