# midi-chlorian, the portal

The site at <https://jhortale.github.io/midi-chlorian/>: a home page, a page per synth (description, the installer,
downloads, releases and their notes). Static, built from this repository's `main` by `.github/workflows/portal.yml` and
published by this repository's GitHub Pages (`actions/deploy-pages`). Design: `docs/superpowers/specs/2026-11-20-portal-design.md`
(and its amendment of 2026-11-21: the content is Markdown with frontmatter, no Sanity).

## What lives where

- `synths/<id>/synth.md`: what the page says about a synth (frontmatter `name`, `summary`, `order`, `image?`, `origin`; the body is
  the description). `synth.json` and `catalog.json` beside it keep the repo-owned data: identity, licence, package names, the
  install warning.
- `synths/<id>/releases/<version>.md`: one file per release (frontmatter `version`, `date`, `tag`, `current?`, `source`,
  `builds`; the body is the release notes). **Published = merged into `main`.** A release PR is the draft.
- `apps/portal/`: the app (React, Vite; absolute base `/midi-chlorian/`). `packages/content/`: the Markdown reader and the
  site builder (`src/cli/site.ts`). `.github/workflows/portal.yml`: the deploy.

Markdown is rendered at build time (`marked`, raw HTML escaped). Every page is served under `<base href="/midi-chlorian/">`,
so in notes and descriptions **do not use `#anchor` links or relative links**: write absolute URLs
(`https://jhortale.github.io/midi-chlorian/...` or any other site).

## One-time setup (João)

1. `jhortale/midi-chlorian` (this repository, public; renamed from `knobi-six` on 2026-11-21, GitHub redirects the old
   name) → Settings → Pages → Source: **GitHub Actions**.
2. In `jhortale/knobi-six` → Settings → Actions → General → Workflow permissions: tick **Allow GitHub Actions to create and
   approve pull requests**. The release workflow's `release-pr` job needs it to open the release PR.
3. Branch protection on `main` stays as it is; release PRs are merged with the admin bypass (below).

## The release flow

1. Tag `knobi-six-v1.0` (`<synth>-v<version>`; nothing after the version: no prereleases) and push the tag.
2. `release.yml` builds, tests and publishes the GitHub release (it refuses to replace assets a release already has), then its
   `release-pr` job verifies the files (uploader `github-actions[bot]`, `SHA256SUMS`, identity, loader mark), writes
   `synths/knobi-six/releases/1.0.md` with every CI-owned field filled and a first draft of the notes, and opens the pull
   request `release/knobi-six-v1.0` into `main`.
3. Edit the notes (the body) in that pull request, and set `current: true` if it is the release to show. Do not edit
   `version`, `date`, `tag`, `source` or `builds`.
4. Merge it **with the admin bypass**. A pull request opened with the workflow's token does not start other workflows, so its
   checks never run and the required ones can never pass; the pull request only adds one Markdown file.
5. The merge touches `synths/**`, so `portal.yml` runs: it builds the app, downloads every release named by a release
   file on `main`, re-checks each file (uploader, hashes, identities), refuses a `-local` or `cpu1` file and any token, adds a page
   per synth and `404.html`, and deploys with `force_orphan`. The site is live when that run is green. A manual run
   (Actions → portal → Run workflow, on `main`) redeploys the same thing.

If `release-pr` fails **after** the assets were uploaded, retry with **Re-run failed jobs**. A full re-run starts the upload again
and stops at the existing-assets guard (a published release never changes).

## Adding a synth

1. `synths/<id>/synth.md` (frontmatter `name`, `summary`, `order`, `origin: own`, optionally `image`: a path inside the folder; the body is the
   description), `synth.json` (identity, package names per model, IDENT: `docs/IDENTITY.md`) and `catalog.json` (its firmware entry,
   `releases: []`), plus the `package.json` of `docs/MONOREPO.md`.
2. Release files come from the tag flow above. A synth without a release is built but shows no downloads.
3. `release.yml` builds only `knobi-six` from this repository; another synth's firmware needs its own build first (roadmap).

## What never changes after merge

A release file's CI-owned frontmatter (`version`, `date`, `tag`, `source`, `builds`) and the files it names. `web.yml` runs
`.github/scripts/check-release-edits.sh` on every pull request and refuses a change to them; only the notes and `current` may
change. To correct a release, publish a new version.

## What a deploy publishes

`portal.yml` publishes to this repository's GitHub Pages: the app, a page per synth and `404.html`,
`content.json` and `catalog.json`, `firmware/<synth>/<version>/` (packages, `SHA256SUMS`, the release's source archive),
synth images, and **the source of the site itself**: `source/midi-chlorian-src-<commit>.tar.gz` (`git archive` of the whole
repository at the deployed commit, which holds no token and no OB-6 factory bank; the build asserts the latter), a
`source/index.html` that links it (every page's footer links `source/`), `LICENSE` (GPL-3.0) and a `README.md` (unofficial,
GPL-3.0-only, where the source is, credits via `LICENSING.md` inside the archive; its text is `apps/portal/site/README.md`).
The site's JavaScript is built from `main`, so this archive is the source that matches it, and it may differ from a
release's `-src.tar.gz`. A release's `SHA256SUMS` covers the packages **and** `<synth>-<version>-src.tar.gz`.

## Never on the site

A `-local` or `cpu1` file, any token or key. The site builder checks the data and `portal.yml` checks the merged site once
more (`-local`/`cpu1` file names, `ghp_`, `github_pat_`, `-----BEGIN`, and the workflow's own token; the source archive is
skipped by the text scan, it being the repository itself); either one stops the deploy. Every file on the site comes from a release asset of `jhortale/midi-chlorian` uploaded by `github-actions[bot]`.

## Working on it

```sh
pnpm install
pnpm --filter @knobi/portal dev                  # add ?mock to the URL: the fixture site and a simulated keyboard
pnpm turbo run build test --filter @knobi/portal...   # what portal.yml runs first
node packages/content/src/cli/site.ts --out /tmp/site-data      # the site's data (needs gh and a token); --release TAG=DIR --assets TAG=FILE work offline
```

Agents never install on a real keyboard (CLAUDE.md): the installer runs only against `@knobi/device-sim` there.
