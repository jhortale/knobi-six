# The KNOBI monorepo

One repository for several synths (KNOBI-SIX first), their platforms (the SMK-37 keyboard and FM-1 today; desktop and
mobile later) and the tools that publish them. Design and roadmap:
`docs/superpowers/specs/2026-11-20-monorepo-design.md`.

## The tree

```
firmware/ tools/ tests/ build.sh …   upstream's layout (fc79/smk37-base, with Felucca, SLOOP, genkigenki, OberVave):
                                     the SMK platform and KNOBI-SIX's firmware app. Never moved.
package.json pnpm-workspace.yaml     the TypeScript workspace root (pnpm 12, Turborepo); scripts only
pnpm-lock.yaml turbo.json
apps/
  installer/      @knobi/installer        knobi-flash, the web installer page (published by release.yml)
packages/
  installer-core/ @knobi/installer-core   the Felucca-family update protocol (a port of tools/fm1_install.py)
  web-midi/       @knobi/web-midi         the browser's Web MIDI as an installer-core backend
  device-sim/     @knobi/device-sim       a simulated keyboard and a package builder: tests and ?mock only
  catalog/        @knobi/catalog          catalog.json: types, checks, queries ("."), synths and the generator ("./node");
                                          instruments.json: the protocols, keyboard families and instruments
  ui/             @knobi/ui               shadcn/ui components and the KNOBI panel theme (Tailwind v4)
  tsconfig/       @knobi/tsconfig         shared TypeScript settings
synths/
  knobi-six/      @knobi/synth-knobi-six  KNOBI-SIX's synth.json (identity, package names) and catalog entry
```

Dependencies only point one way: `installer-core` ← `web-midi`, `device-sim`, `catalog`; `ui` stands alone; apps use
them all (`device-sim` only as a devDependency).

## Commands

```sh
pnpm install                              # once, at the root (Node 24; pnpm from package.json's packageManager)
pnpm turbo run typecheck build test       # everything (CI: .github/workflows/web.yml)
pnpm --filter @knobi/installer dev        # the installer, ?mock for the simulated keyboard
pnpm --filter @knobi/catalog test         # one package
./build.sh; sh tests/run_tests.sh         # the firmware, as before (no Node needed)
```

## Rules

- **Upstream's paths never move** (`firmware/`, `tools/`, `tests/`, `build.sh`): `git merge upstream/main` keeps
  working. New structure grows beside them.
- **Packages are private** (`"private": true`, scope `@knobi/`), consumed as TypeScript source (no prebuild), versions
  pinned exactly (`saveExact: true` in `pnpm-workspace.yaml`), one lockfile (`pnpm-lock.yaml`). The lockfile must install on Linux (CI) and macOS: after changing
  dependencies, run CI's steps in a Linux container (`docker run node:24`).
- **No cycles** (Turborepo refuses them): tests that need the simulated keyboard live in `device-sim` or in the app.
- **`@knobi/device-sim` never reaches a published build** (`apps/installer/test/dist.test.ts` checks).
- **Tailwind**: `@knobi/ui`'s `globals.css` declares `@source "../components"`, so every app that imports it gets the
  kit's classes (also checked in `dist.test.ts`).
- Licences: GPL-3.0-only, with the credits in `LICENSING.md`; shadcn's files are MIT.

## Adding a package

`packages/<name>/` with a `package.json` (`"name": "@knobi/<name>"`, `"private": true`, `"type": "module"`,
`exports` pointing at `src/*.ts`, scripts `typecheck` and `test`), a `tsconfig.json` extending `@knobi/tsconfig/base.json`
(or `react.json`), tests under `test/`. Depend on it with `"@knobi/<name>": "workspace:*"`.

## Adding a synth

`synths/<id>/` with:

- `synth.json`: `id`, `name`, `ident` (its identity number, free in the Firmware Hub catalog: `docs/IDENTITY.md`),
  `packageName` (`<id>-{version}{suffix}.fwsc`), `models` (file-name suffix → keyboard model), `platforms`;
- `catalog.json`: its firmware entry (`id` = the folder's name, `releases: []`);
- `package.json`: `@knobi/synth-<id>`, exporting both JSON files;
- a README.

Its firmware app on the shared SMK platform is roadmap step 4 (it needs `build.sh` to build another app than
`firmware/src/app.c`). The desktop plugin and the shared engine are step 3 (the deferred `plugin` branch); the portal
(Sanity) is step 2.
