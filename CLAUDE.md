# KNOBI-SIX

Standalone OB-6-inspired polysynth firmware for the M-VAVE SMK-37 Pro. A clone of fc79/smk37-base (remote `upstream`);
`origin` is the private repo jhortale/knobi-six. Trunk: `main` (the default branch; work lands there through pull
requests). `phase1` is where Phases 1-3 were built, kept as it was when `main` started from it (2026-11-20).

## Binding documents

The spec `docs/superpowers/specs/2026-11-16-standalone-firmware-design.md` and the OB-6 reference
`docs/ob6-reference.md` are binding; the plan `docs/superpowers/plans/2026-11-16-standalone-phase1.md` says the order.
The UI mockups are in `docs/design/standalone-ui/`. If code and spec disagree, stop and ask. Identity (name, store
magic, data family, IDENT 956): `docs/IDENTITY.md`.

## Never install on the keyboard without João

Do not run `tools/fm1_install.py`, do not flash, do not use the USB rescue or `tools/backup/`. Nothing is installed on
any keyboard. Builds, host tests and CI are fine. The web installer (`apps/installer/`, `packages/`) too: only its simulated keyboard (`?mock`,
the tests), never a real one.

## Commands

```sh
./build.sh                                  # device build (Pro) -> build/knobi-six.fwsc, app.bin, app.map, app.dis
MODEL=elite sh build.sh; MODEL=mke sh build.sh; sh build.sh    # all three, the Pro last (as run_tests.sh expects)
sh tests/run_tests.sh                       # host suite (needs a build first)
tests/run_host.sh SCRIPT                    # the app on the computer, e.g. tests/scripts/diag.txt -> build/host/
make -C tests unit                          # unit tests, tests/unit/test_*.c (created in Task 1)
pnpm install && pnpm turbo run typecheck build test   # the TypeScript workspace: the installer, the midi-chlorian portal and their packages (docs/MONOREPO.md, docs/PORTAL.md)
```

The toolchain is Linux x86-64 only: on the Mac `build.sh` runs it in Docker; needs `JIELI_TOOLCHAIN` (default
`~/.jieli/toolchain`, `tools/get_toolchain.sh`), `AC79_SDK` (default `~/fw-AC79_AIoT_SDK`) and Pillow. CI:
`.github/workflows/build.yml` (push to `main`, pull requests into `main`, manual dispatch), `web.yml` (the TypeScript
workspace, on every push and pull request that touches it); `release.yml` builds and publishes on `<synth>-v<version>` tags (e.g. `knobi-six-v1.0`; old `v*` tags do nothing) and opens the
release PR; `portal.yml` deploys midi-chlorian on push to `main` (docs/PORTAL.md).

## Where things live

- `firmware/src/app.c`: the app (the file to change; includes `platform.c`). New synth modules go beside it in
  `firmware/src/`, included by `app.c` (unity build).
- `firmware/src/platform.c`, `firmware/hal/`, `firmware/loader/`: the platform (updates, USB, MIDI, audio, input,
  screen, flash). Do not edit unless the plan says so. API: `docs/API.md`; chip rules: `docs/RULES.md` (no FPU, no
  libgcc, no 64-bit division, integers only; the audio interrupt is short, no flash, no screen).
- `tools/build.py`: IDENT, DATA_FAMILY, package names. `tests/`: host tests and scripts.
- The monorepo (`docs/MONOREPO.md`): `apps/` (the installer, the midi-chlorian portal: `docs/PORTAL.md`), `packages/` (`@knobi/*`: installer-core, web-midi,
  device-sim, catalog, content, ui, field, tsconfig), `synths/<id>/` (per-synth metadata and the portal's Markdown content). Upstream's paths (`firmware/`, `tools/`,
  `tests/`, `build.sh`) never move: `git merge upstream/main` must keep working.
- Credits (`LICENSING.md`, README) stay as they are; add to them, never drop.

## Commits

Small, conventional (`feat(...)`, `fix`, `docs`, `chore`, `test`). Commit messages end with a blank line and
`Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
