# KNOBI-SIX

Standalone OB-6-inspired polysynth firmware for the M-VAVE SMK-37 Pro. A clone of fc79/smk37-base (remote `upstream`);
`origin` is the private repo jhortale/knobi-six. Working branch: `phase1`.

## Binding documents

The spec `docs/superpowers/specs/2026-11-16-standalone-firmware-design.md` and the OB-6 reference
`docs/ob6-reference.md` are binding; the plan `docs/superpowers/plans/2026-11-16-standalone-phase1.md` says the order.
The UI mockups are in `docs/design/standalone-ui/`. If code and spec disagree, stop and ask. Identity (name, store
magic, data family, IDENT 956): `docs/IDENTITY.md`.

## Never install on the keyboard without João

Do not run `tools/fm1_install.py`, do not flash, do not use the USB rescue or `tools/backup/`. Nothing is installed on
any keyboard. Builds, host tests and CI are fine.

## Commands

```sh
./build.sh                                  # device build (Pro) -> build/knobi-six.fwsc, app.bin, app.map, app.dis
MODEL=elite sh build.sh; MODEL=mke sh build.sh; sh build.sh    # all three, the Pro last (as run_tests.sh expects)
sh tests/run_tests.sh                       # host suite (needs a build first)
tests/run_host.sh SCRIPT                    # the app on the computer, e.g. tests/scripts/diag.txt -> build/host/
make -C tests unit                          # unit tests, tests/unit/test_*.c (created in Task 1)
```

The toolchain is Linux x86-64 only: on the Mac `build.sh` runs it in Docker; needs `JIELI_TOOLCHAIN` (default
`~/.jieli/toolchain`, `tools/get_toolchain.sh`), `AC79_SDK` (default `~/fw-AC79_AIoT_SDK`) and Pillow. CI:
`.github/workflows/build.yml` (push to `phase1`, manual dispatch); `release.yml` publishes on `v*` tags.

## Where things live

- `firmware/src/app.c`: the app (the file to change; includes `platform.c`). New synth modules go beside it in
  `firmware/src/`, included by `app.c` (unity build).
- `firmware/src/platform.c`, `firmware/hal/`, `firmware/loader/`: the platform (updates, USB, MIDI, audio, input,
  screen, flash). Do not edit unless the plan says so. API: `docs/API.md`; chip rules: `docs/RULES.md` (no FPU, no
  libgcc, no 64-bit division, integers only; the audio interrupt is short, no flash, no screen).
- `tools/build.py`: IDENT, DATA_FAMILY, package names. `tests/`: host tests and scripts.
- Credits (`LICENSING.md`, README) stay as they are; add to them, never drop.

## Commits

Small, conventional (`feat(...)`, `fix`, `docs`, `chore`, `test`). Commit messages end with a blank line and
`Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
