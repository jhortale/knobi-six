# The KNOBI monorepo: one repo for several synths, their platforms and a portal

Date: 2026-11-20. Status: approved by João, 2026-11-20.

## Intent

João's vision: this repository becomes the home of more than one synth (firmware). KNOBI-SIX is the first; others will
follow. Each synth can run on several platforms: the M-VAVE SMK-37 keyboard (and the FM-1, which shares its update
protocol), desktop (VST3 / AU on PC and Mac) and, later, mobile. A **portal** publishes them: a headless CMS manages the
releases, keyboards get an install option (the generic web installer, for the SMK family and the FM-1) and desktop
builds get download links. What can be shared (the UI, the builder, the installer, the engine) lives in packages, not
copied per synth or per platform.

The synths are João's own. Other people's firmwares may be listed on the portal later; nothing here may rule that out,
but none of their code comes into this repo.

This document designs the **first step**: the repository structure, and moving the existing web installer
(`web/`, knobi-flash) into shared packages and an app. The later steps are recorded in [Roadmap](#roadmap) so they are
designed on top of this one.

## Goals

- A monorepo layout with room for several synths, apps and shared packages, adopted without breaking anything that
  works today.
- `git merge upstream/main` (fc79/smk37-base, carrying Felucca / SLOOP / genkigenki / OberVave platform changes) keeps
  working as it does now.
- The TypeScript side becomes a pnpm workspace with Turborepo: shared packages with their own tests, one app (the
  installer) built from them.
- Per-synth metadata (identity, catalog entry) lives with the synth, generic instrument definitions live with the
  catalog package, so a second synth is added by adding a folder, not by editing shared code.
- Same behaviour, same checks: the installer page, its catalog generator, its tests (135 today), the fixtures check
  against `tools/fm1_install.py`, the release deploy to knobi-flash.

## Non-goals (this step)

- The portal and the CMS, the FM-1 instrument entries, desktop download links: [Roadmap](#roadmap) step 2.
- Moving the synth engine (`firmware/src/synth/`, `params/`, `ui/`) or making it a package: step 3, through the
  deferred `plugin` branch.
- Merging the `plugin` branch (deferred by João on 2026-11-20; see [Roadmap](#roadmap)).
- A second synth, mobile, Windows builds, renaming the repository.
- Any change to the firmware, `tools/build.py`, the device build or the host test suite.

## Anti-goals (a failure even if it works)

- **An upstream merge stops being a plain `git merge`.** Upstream's paths do not move.
- **The installer behaves differently** for a user, or a check is lost (SHA-256, identity, loader mark, model and
  foreign-loader refusals, backup gate, release-only packages, never a `-local` or `cpu1` file on the site).
- **Tests are lost or weakened** in the move; the drift checks against `tools/fm1_install.py` stop running.
- **The simulated keyboard reaches the published page.**
- The JavaScript tooling leaks into the firmware side (no `package.json` needs to exist for `./build.sh` or
  `sh tests/run_tests.sh`).

## Constraints

- **Upstream paths stay put:** `firmware/` (all of it: `hal/`, `loader/`, `src/`, `gen/`), `tools/`, `tests/`,
  `build.sh`, `LICENSE`, `LICENSES/`, `.github/workflows/build.yml` and `release.yml` (ours, but edited in place), the
  root `README.md` and `LICENSING.md` (credits are added to, never dropped).
- CLAUDE.md: nothing is installed on a keyboard; the installer is exercised only through its simulated keyboard.
- The toolchain: Node 24 (CI), pnpm 12.21.0, Turborepo 2.11.13, Vite 8.3.8, Vitest 5.1.1, React 19.3.1, TypeScript
  7.0.2 (the versions on `phase1` today, plus pnpm and Turborepo), exact pins.
- The packages are private to this repo (`"private": true`), never published to npm; scope `@knobi/`.
- Licences unchanged: the installer core keeps its GPL-3.0-only headers and credits (a port of `tools/fm1_install.py`);
  shadcn components keep MIT.

## Approaches considered

- **A. Hybrid (picked by João).** The root keeps upstream's layout; a workspace grows beside it (`apps/`, `packages/`,
  `synths/`). pnpm workspaces + Turborepo for TypeScript; C stays under `firmware/`. Upstream merges unchanged; the
  root mixes the two layouts and the engine is not a package yet.
- **B. Full reorganisation** (`platforms/smk37/`, `synths/knobi-six/firmware/`), upstream merged through git's subtree
  strategy with path mapping. Cleanest tree, but every upstream merge gets harder and the engine move collides with the
  `plugin` branch. Rejected: João keeps syncing with upstream.
- **C. Like A with plain npm workspaces.** No new tools, but no caching or task ordering once there are several apps
  and packages. Rejected as a false economy.

## Design

### The tree

```
firmware/ tools/ tests/ build.sh …        upstream's layout, unchanged (the SMK platform + KNOBI-SIX's app)
package.json                              the workspace root: private, scripts only (turbo run …), engines node 24
pnpm-workspace.yaml                       apps/*, packages/*, synths/*
pnpm-lock.yaml
turbo.json                                tasks: build, typecheck, test, lint; outputs dist/**
.npmrc                                    pnpm settings (exact pins)
apps/
  installer/                              knobi-flash: the page (was web/)
packages/
  installer-core/   @knobi/installer-core   the update protocol (was web/src/protocol/ + midi/backend.ts)
  web-midi/         @knobi/web-midi         the Web MIDI backend (was web/src/midi/webmidi.ts)
  device-sim/       @knobi/device-sim       the simulated keyboard + package builder (was web/src/dev/)
  catalog/          @knobi/catalog          schema, validation, queries, generator, generic instruments
  ui/               @knobi/ui               shadcn components, cn(), the KNOBI panel theme
  tsconfig/         @knobi/tsconfig         shared tsconfig bases
synths/
  knobi-six/                                per-synth metadata: synth.json, catalog entry, README
```

`web/` is removed; every file in it moves (git mv, so history follows) or is split as below.

### What goes where

| From `web/` | To | Notes |
| --- | --- | --- |
| `src/protocol/*` (sevenbit, messages, package, identity, updater), `src/midi/backend.ts` | `packages/installer-core/src/` | the device-independent update protocol; exports `Updater`, `DELAY`, `loadPackage`, `MidiBackend`, `Link`, `pairOutput`, `SysexQueue`, `InstallError`, messages |
| `test/messages`, `package` tests, `test/helpers/fixtures.ts`, `test/fixtures/python.json` | `packages/installer-core/test/` | protocol tests against the Python fixtures only (the packages they need are the fixtures' raw bytes); the fixture check: `python3 tests/web_fixtures.py --out packages/installer-core/test/fixtures/python.json` (the script stays in `tests/`, only its documented output path changes) |
| `test/updater.test.ts`, the `makePackage`-vs-Python checks of `test/package.test.ts` | `packages/device-sim/test/` | the updater against the simulated keyboard, and the package builder against Python's: they need the simulator, which depends on installer-core, so they live with it (no cycle; my call after review) |
| `src/midi/webmidi.ts`, `test/webmidi.test.ts` | `packages/web-midi/` | depends on installer-core |
| `src/dev/fakeDevice.ts`, `src/dev/makePackage.ts` | `packages/device-sim/src/` | test and dev only: a devDependency of every package that uses it, never a dependency of an app's production code |
| `src/catalog/*`, `scripts/make-catalog.ts`, `test/catalog`, `test/make-catalog` | `packages/catalog/` | the generator becomes the package's bin, still runnable with plain `node`; it takes `--synth <id>` and reads that synth's naming, models and IDENT from `synths/<id>/synth.json` instead of its hardcoded KNOBI-SIX values (my call after review) |
| `public/firmware/<id>/<version>/` (other firmwares' committed packages, checked by `--public`) | `synths/<id>/releases/<version>/` | for your own synths only; other people's firmwares are the portal's business (step 2), never committed here (my call after review) |
| `catalog/base.json` | split: `packages/catalog/instruments.json` (protocols, families, instruments) + `synths/knobi-six/catalog.json` (the KNOBI-SIX firmware entry) | the generator merges `instruments.json` with every `synths/*/catalog.json`; the drift test (patterns = `fm1_install.py`'s) reads `instruments.json` |
| `src/components/ui/*`, `src/lib/utils.ts`, the theme part of `src/index.css`, `components.json` | `packages/ui/` | shadcn's monorepo layout: `packages/ui/src/components/*`, `src/lib/utils.ts`, `src/styles/globals.css` (Tailwind + the panel theme); `apps/installer` gets its own `components.json` pointing at `@knobi/ui` |
| `src/ui/*` (Page, installer store, app), `src/main.ts`, `src/dev/mock.ts`, `index.html`, `vite.config.ts` (with the `backup.txt` plugin), `site/`, `test/ui`, `test/dist` | `apps/installer/` | the app: imports the packages; `?mock` still loads `@knobi/device-sim` only in dev |
| `README.md`, `.gitignore` | `apps/installer/` + root `.gitignore` | |

Package boundaries (no cycles; Turborepo refuses a cyclic graph, so installer-core never depends on device-sim, not
even for its tests): `installer-core` ← `web-midi`; `installer-core` ← `catalog`; `installer-core` ←
`device-sim`; `ui` stands alone; `apps/installer` uses all of them (`device-sim` only in dev and tests).

Packages are consumed as TypeScript source (each `package.json` `exports` points at `src/*.ts`); Vite and Vitest
compile them, `tsc --noEmit` checks them, nothing is prebuilt. Each package has `typecheck` and `test` scripts; the app
has `dev`, `build`, `typecheck`, `test`.

### synths/knobi-six

```
synths/knobi-six/
  synth.json        id, name, summary, licence, identities per model (SMK-37 Pro_956 …), IDENT, data family,
                    platforms: { "smk37": { "firmware": "firmware/", "build": "./build.sh" } }
  catalog.json      the KNOBI-SIX firmware entry of the catalog (no releases: the generator adds them)
  README.md         what the synth is, where its parts live today (firmware/src/app.c, synth/, …), what moves later
```

`synth.json` drives the catalog generator (`--synth knobi-six`): the package file pattern
(`knobi-six-{version}{suffix}.fwsc`), the suffix for each model (`""` → SMK-37 Pro, `-elite` → SMK-37 Elite, `-mke` →
MKE-P37), IDENT (`956`). A test checks it against `tools/build.py`'s IDENT and package names and `docs/IDENTITY.md`. The
firmware build does not read it.

**Deploys with several synths (this step: one).** The generator writes the synth being released from its release
files, and copies every other synth's `catalog.json` entry unchanged, with any releases committed under its
`synths/<id>/releases/` (hash-checked). A deploy never drops another synth's entry. Publishing several synths' releases
independently is the portal's job (step 2, Sanity); this keeps the static site correct until then. (My call after
review.)

### Build, test, CI, release

- Root scripts: `pnpm turbo run build|typecheck|test`. `pnpm install` at the root installs everything.
- `.github/workflows/web.yml` → runs on changes to `apps/**`, `packages/**`, `synths/**`, the workspace files,
  `tests/web_fixtures.py`, `tools/fm1_install.py`; steps: pnpm setup, fixtures check, `pnpm install --frozen-lockfile`,
  `pnpm turbo run typecheck build test`. The firmware workflow (`build.yml`) is unchanged.
- `release.yml`'s `site` job: same gate and checks; builds `apps/installer`, runs the catalog generator from
  `packages/catalog` with `synths/*/catalog.json`, same output layout on knobi-flash.
- The lockfile must install on Linux (CI) and macOS; checked by running the CI steps in a Linux container before the
  PR (the Vite 8 lockfile lesson).

### Docs

CLAUDE.md (commands, where things live), `docs/WEB_INSTALLER.md` (paths), `LICENSING.md` (the `web/` rows point at the
new paths; nothing dropped), the root README (one paragraph on the layout), a short `docs/MONOREPO.md`: the tree, the
rules (upstream paths stay; packages private; device-sim never in production), how to add a package or a synth.

## Roadmap

Each is its own design and plan, in this order unless João reorders it.

1. **This step: the monorepo structure** (above).
2. **The portal.** A site that lists the synths and their releases, managed by a headless CMS: **Sanity** (João,
   2026-11-20). Sanity hosts the content (its Content Lake) and the editing studio; the studio can live in this repo
   as `apps/studio/` and the portal as `apps/portal/`, both in the workspace. To design: how the static site reads
   Sanity (at build time, so the installer keeps same-origin `catalog.json` and packages, or at run time through
   Sanity's CDN API), Sanity's plan and dataset visibility, and where binaries are stored (Sanity assets or keep them
   with the site). Per release and platform: SMK / FM-1 → the generic web installer (the
   `apps/installer` page, or its packages inside the portal); desktop → download links. Binaries stay CI-built from
   release tags, the CMS describes and links them (provenance, SHA-256). The FM-1's instrument entries join
   `packages/catalog/instruments.json`. Other people's firmwares, when allowed: catalog entries pointing at their files
   and their source, with a review step.
3. **The engine package and the desktop plugin.** The `plugin` branch (JUCE 8 AU / VST3 for Live 12 on macOS,
   ~14 000 lines, unmerged, **deferred by João on 2026-11-20**) extracts the synth, arp, clock and sequencer into a
   portable core (`firmware/src/core/knobi_core.h`) used by the firmware and the plugin. When it is taken up: merge or
   rebase it onto the monorepo, then decide where the core lives (it is not upstream's code, so it may move under
   `synths/knobi-six/` or `packages/`), and the plugin under `synths/knobi-six/plugin/`. Public desktop downloads need
   Developer ID signing and notarization on macOS and a Windows build (the branch is Apple Silicon only, ad-hoc signed).
4. **More platforms and synths:** mobile (e.g. AUv3 on iOS from the same core), a second synth (its `synths/<id>/`, its
   firmware app on the shared SMK platform, which needs `build.sh` to take the app's directory).

## Notes for the builder

- `LICENSING.md` cites `web/synth/analog.c` from the separate smk37-analog repository (the PolyBLEP row): that path is
  not ours and must not be rewritten when the `web/` rows move to the new paths.
- Upstream (`upstream/main`) has no `web/`, and no other branch touches it, so removing `web/` carries no merge risk
  (checked in review).

## Left to the builder

- `tsconfig` bases and project references, Turborepo task details and caching inputs, lint setup (none exists today;
  optional).
- Whether `device-sim`'s `mock.ts` lives in the app (as now) or in the package.
- The exact `exports` maps.
