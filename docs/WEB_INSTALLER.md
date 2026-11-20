# knobi-flash, the web installer

The browser installer for KNOBI-SIX (Web MIDI), source in `web/`, published at <https://jhortale.github.io/knobi-flash/>
from the public repo `jhortale/knobi-flash`. Design: `docs/superpowers/specs/2026-11-20-web-installer-design.md`.

## Working on it

```sh
cd web
npm ci
npm run dev          # http://localhost:5173/?mock : the page on a simulated keyboard (knobiMock in the console)
npm test             # protocol, catalog, generator, updater, page; all against the simulated keyboard
npm run build        # dist/ (run npm test after it: test/dist.test.ts checks dist/ has no simulator)
```

The page is React with shadcn/ui components (`web/src/components/ui/`, added with `npx shadcn add NAME`) on Tailwind
CSS v4, themed in `web/src/index.css`; its behaviour is in `web/src/ui/installer.ts`, drawn by `web/src/ui/Page.tsx`.

`web/src/protocol/` is a port of `tools/fm1_install.py`. After any change to the Python, regenerate what it answers
and port the change: `python3 tests/web_fixtures.py --out web/test/fixtures/python.json`. CI (`.github/workflows/web.yml`)
fails when the fixtures are out of date, and `web/test/catalog.test.ts` fails when the catalog's port, model-family
or loader patterns differ from `PORT_RE`, `SMK_FAMILY`, `OWN_LOADER_RE`.

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
   `cd web && node scripts/make-catalog.ts --version X.Y --dir FOLDER --out public/catalog.json --files public/firmware/knobi-six/X.Y`
   and `npm run dev` (without `?mock`), in Chrome; delete `public/catalog.json` and `public/firmware/` afterwards
   (`web/.gitignore` keeps them out of git; never commit them).
2. Tag `vX.Y` (no `-`: prereleases never deploy). `release.yml` builds, tests and releases; its `site` job then waits
   for your approval, builds the page and `catalog.json` from the release, refuses any `-local` or `cpu1` file, and
   pushes to `knobi-flash`. Each deploy replaces the site with that release.
3. Redeploy an existing release (the site was lost, or `base.json`'s descriptions changed at that tag): Actions →
   release → Run workflow, `deploy_tag` = `vX.Y`. It builds the page **from the tag**, so the page always matches the
   published source archive; a fix to the page itself needs a new release tag. The run also builds the firmware of the
   branch it is started from, which must succeed.

## What a deploy publishes

`index.html` and `assets/` (the page, with source maps), `backup.txt` (`docs/BACKUP.md`, which the page's backup
checkbox links to: the source repo is private), `catalog.json`, `firmware/knobi-six/X.Y/` with the three
`.fwsc` packages, `SHA256SUMS` and `knobi-six-X.Y-src.tar.gz` (`git archive` of the tag: the GPL source, with the
installer's own under `web/`), `README.md`, `LICENSE`.

## Adding an instrument or a firmware

Everything the page offers comes from `web/catalog/base.json` (schema: `web/src/catalog/schema.ts`).

- **An instrument of the same update protocol**: add it to `instruments` (and its model to the family, or a new family
  with its `match` and `ports` patterns); its packages then appear for devices of that model.
- **Another firmware**: add a complete entry to `firmwares` with its `releases[].packages` (`file` under
  `firmware/<id>/<version>/`, `identity`, `sha256`, `size`, `instrument`) and commit the package files under
  `web/public/firmware/<id>/<version>/`. The generator checks their hashes and keeps them on every KNOBI-SIX deploy.
  Only firmwares whose source may be published next to them (GPL: link or add the source archive).
- **Another update protocol** needs code: a new `protocols` entry and an updater for it.
