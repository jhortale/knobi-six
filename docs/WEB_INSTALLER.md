# knobi-flash, the web installer

The browser installer for KNOBI-SIX (Web MIDI), source in `apps/installer/` and the `@knobi/*` packages it is built from (`docs/MONOREPO.md`), published inside the midi-chlorian portal (<https://jhortale.github.io/midi-chlorian/>, [docs/PORTAL.md](PORTAL.md)). Design: `docs/superpowers/specs/2026-11-20-web-installer-design.md`.

## Working on it

```sh
pnpm install                              # at the repository root
pnpm --filter @knobi/installer dev        # http://localhost:5173/?mock : the page on a simulated keyboard (knobiMock in the console)
pnpm turbo run typecheck build test       # every package and the app; the app's tests run after its build (dist.test.ts)
```

Where things are:

- `apps/installer/`: the page (React; the installer itself is `@knobi/installer-ui`: behaviour in `installer.ts`, drawn by `InstallerPanel`; `src/ui/Page.tsx` is the page chrome), the `?mock` page,
  the site files (`site/`), the page and `dist/` tests.
- `packages/ui/` (`@knobi/ui`): the shadcn/ui components (add one with `npx shadcn add NAME` in `packages/ui/`) and the
  KNOBI panel theme on Tailwind CSS v4 (`src/styles/globals.css`).
- `packages/installer-core/` (`@knobi/installer-core`): the update protocol. `packages/web-midi/`: Web MIDI.
  `packages/device-sim/`: the simulated keyboard (dev and tests only). `packages/catalog/`: the catalog, its checks and
  the generator. `synths/knobi-six/`: KNOBI-SIX's `synth.json` and catalog entry.

`packages/installer-core/src/` is a port of `tools/fm1_install.py`. After any change to the Python, regenerate what it
answers and port the change: `python3 tests/web_fixtures.py --out packages/installer-core/test/fixtures/python.json`.
CI (`.github/workflows/web.yml`) fails when the fixtures are out of date, and `packages/catalog/test/catalog.test.ts`
fails when the catalog's port, model-family or loader patterns differ from `PORT_RE`, `SMK_FAMILY`, `OWN_LOADER_RE`.

Agents never run it against a real keyboard (CLAUDE.md).

## Deploy and releases

The installer is part of the midi-chlorian portal now: the portal's synth pages embed it, and the site is built and
deployed from `main` by `.github/workflows/portal.yml` (one-time setup, the release flow, what is published:
[docs/PORTAL.md](PORTAL.md)). The standalone knobi-flash site and its release-tag deploy are gone. The page in
`apps/installer/` stays as the installer's own development page (`?mock`).

## Adding an instrument or a firmware

Everything the page offers comes from `packages/catalog/instruments.json` (protocols, families, instruments) and each
`synths/<id>/catalog.json` (a firmware), schema in `packages/catalog/src/schema.ts`.

- **An instrument of the same update protocol**: add it to `instruments.json`'s `instruments` (and its model to the family, or a new family
  with its `match` and `ports` patterns); its packages then appear for devices of that model.
- **Another synth of yours**: a folder `synths/<id>/` with `synth.json` (identity, package names per model, IDENT),
  `catalog.json` (its firmware entry) and a `package.json` (`@knobi/synth-<id>`); see `docs/MONOREPO.md`. Its releases, and how it reaches
  the site, are the portal's: `docs/PORTAL.md` ("Adding a synth"). Other people's firmwares are never committed here.
- **Another update protocol** needs code: a new `protocols` entry and an updater for it.
