# knobi-flash, the web installer

The browser installer for KNOBI-SIX (Web MIDI), source in `apps/installer/` and the `@knobi/*` packages it is built from (`docs/MONOREPO.md`), published at <https://jhortale.github.io/knobi-flash/>
from the public repo `jhortale/knobi-flash`. Design: `docs/superpowers/specs/2026-11-20-web-installer-design.md`.

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

## One-time setup (João)

1. Create the **public** repo `jhortale/knobi-flash` (empty). Settings → Pages: deploy from branch `gh-pages`, `/`.
2. Make a deploy key: `ssh-keygen -t ed25519 -N "" -C knobi-flash-deploy -f knobi-flash-deploy`.
   In `jhortale/knobi-flash` → Settings → Deploy keys: add `knobi-flash-deploy.pub`, **allow write access**.
   In `jhortale/knobi-six` → Settings → Secrets and variables → Actions: secret `KNOBI_FLASH_DEPLOY_KEY` = the
   private key `knobi-flash-deploy`. Then delete both files.
3. In `jhortale/knobi-six` → Settings → Environments: create `knobi-flash`, required reviewer: yourself. Every deploy
   waits for your approval there.

## The first release on the site

1. Install a release package on your own keyboard from the page on localhost first: put the release's `.fwsc` files
   and `SHA256SUMS` in a folder, then
   `node packages/catalog/src/make-catalog.ts --synth knobi-six --version X.Y --dir FOLDER --out apps/installer/public/catalog.json --files apps/installer/public/firmware/knobi-six/X.Y`
   and `pnpm --filter @knobi/installer dev` (without `?mock`), in Chrome; delete `apps/installer/public/catalog.json`
   and `apps/installer/public/firmware/` afterwards (`apps/installer/.gitignore` keeps them out of git; never commit
   them).
2. Tag `vX.Y` (no `-`: prereleases never deploy). `release.yml` builds, tests and releases; its `site` job then waits
   for your approval, builds the page and `catalog.json` from the release, refuses any `-local` or `cpu1` file, and
   pushes to `knobi-flash`. Each deploy replaces the site with that release.
3. Redeploy an existing release (the site was lost, or a synth's `catalog.json` descriptions changed at that tag): Actions →
   release → Run workflow, `deploy_tag` = `vX.Y`. It builds the page **from the tag**, so the page always matches the
   published source archive; a fix to the page itself needs a new release tag. The run also builds the firmware of the
   branch it is started from, which must succeed.

## What a deploy publishes

`index.html` and `assets/` (the page, with source maps), `backup.txt` (`docs/BACKUP.md`, which the page's backup
checkbox links to: the source repo is private), `catalog.json`, `firmware/knobi-six/X.Y/` with the three
`.fwsc` packages, `SHA256SUMS` and `knobi-six-X.Y-src.tar.gz` (`git archive` of the tag: the GPL source, with the
installer's own under `apps/` and `packages/`), `README.md`, `LICENSE`.

## Adding an instrument or a firmware

Everything the page offers comes from `packages/catalog/instruments.json` (protocols, families, instruments) and each
`synths/<id>/catalog.json` (a firmware), schema in `packages/catalog/src/schema.ts`.

- **An instrument of the same update protocol**: add it to `instruments.json`'s `instruments` (and its model to the family, or a new family
  with its `match` and `ports` patterns); its packages then appear for devices of that model.
- **Another synth of yours**: a folder `synths/<id>/` with `synth.json` (identity, package names per model, IDENT),
  `catalog.json` (its firmware entry) and a `package.json` (`@knobi/synth-<id>`); see `docs/MONOREPO.md`. A release
  built by CI is added by the generator (`--synth <id>`); a release committed under `synths/<id>/releases/<version>/`
  (its `catalog.json` lists it with `sha256`) is checked and kept on every deploy. Other people's firmwares are never
  committed here: that is the portal's job (roadmap step 2).
- **Another update protocol** needs code: a new `protocols` entry and an updater for it.
