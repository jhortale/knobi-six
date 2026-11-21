# midi-chlorian: the portal that publishes the synths, managed in Sanity

Date: 2026-11-20. Status: approved by João, 2026-11-21. Roadmap step 2 of
`docs/superpowers/specs/2026-11-20-monorepo-design.md`.

## Intent

**midi-chlorian** (the name is João's, 2026-11-20) is one public site where people get João's synths, at
`https://jhortale.github.io/midi-chlorian/` (GitHub Pages from the public repo `jhortale/midi-chlorian`; a custom
domain can come later). Its main job is getting a firmware onto their keyboard: they open the
synth's page, the page finds their keyboard and installs, in the browser (the knobi-flash installer: SMK-37 Pro /
Elite / MKE-P37 today, the FM-1 later). The secondary option is a download to their computer: the firmware file
(`.fwsc`) for offline use or another installer, its GPL source, and, when they exist, the desktop versions (VST3 / AU,
Mac and PC).

What the site lists (synths, releases, which one is current, descriptions, release notes) is managed in **Sanity**,
not in the repo's JSON, so João can change it without a code change. Releases are created by CI, never by hand: a
release tag makes CI build and check the files, then write the release into Sanity as a **draft**; João reviews it in
Sanity's Studio and publishes it; publishing rebuilds and deploys the site.

Nothing is published today (no knobi-flash repo, no GitHub release, `release.yml` never ran): the portal is the first
public site, and replaces the plan of a separate knobi-flash site (`docs/WEB_INSTALLER.md`'s knobi-flash setup is
superseded by `docs/PORTAL.md`).

Note on the name: "midi-chlorian" is a Lucasfilm term (Star Wars). A pun on it for a free hobby site is very unlikely
to matter; avoid using it as a trademark (logo, merchandise, a commercial product name).

## Goals

- A static portal on GitHub Pages: a home page listing the synths, and one page per synth with its current release
  (install on my keyboard; downloads) and its older releases.
- Sanity holds the content; the dataset is **private**, read only by CI with a token.
- CI writes each tagged release into Sanity as a draft (files, sizes, SHA-256, identities, source archive filled in);
  it never publishes.
- Publishing in Sanity triggers a GitHub workflow that rebuilds and deploys the portal with only published content.
- Every deploy re-checks every file it publishes: SHA-256 against Sanity and against the release's `SHA256SUMS`, the
  package's identity and loader mark (`@knobi/installer-core`), release names only, never a `-local` or `cpu1` file.
- The installer's behaviour and safety are unchanged (backup gate, model and foreign-loader refusals, confirm dialog,
  the simulated keyboard only in dev and tests).
- The content model has room for desktop builds and for other people's firmwares, without either existing yet.

## Non-goals

- Manuals, videos, sound demos, a blog: each synth gets a short description and an image at most.
- The desktop plugin itself (roadmap step 3; the `plugin` branch is deferred): the model and the page support its
  downloads, nothing builds them yet.
- FM-1 instrument entries in the catalog: a follow-up once an FM-1 build of a synth exists (the protocol is shared).
- Listing other people's firmwares: the model allows an external entry, the portal does not show any yet.
- Accounts, comments, analytics.

## Anti-goals (a failure even if it works)

- **A file reaches the site that CI did not build from a tag**, or that does not match its SHA-256 / identity.
- **A draft becomes visible** without João publishing it in Sanity.
- **A Sanity token or the dispatch token leaks into the published site** or the repo.
- **The installer is weakened**: any check from `apps/installer` lost on the way into the portal.
- **A `-local` package or the OB-6 bank** reaches Sanity or the site.
- Something is installed on a real keyboard by an agent (CLAUDE.md): development uses the simulated keyboard.

## Constraints

- The monorepo rules (`docs/MONOREPO.md`): upstream's paths never move; packages private, exact pins
  (`saveExact`), one lockfile that installs on Linux and macOS; no dependency cycles; `@knobi/device-sim` never in a
  published build.
- The Web MIDI installer needs its package files from the site's own origin (the reason the files live on the site).
- GitHub releases of the private knobi-six repo are readable only with a token: CI copies files from them at deploy.
- The site is public; the source archive is the whole tag (as already decided for knobi-flash).
- Versions current on `main`: Node 24, pnpm 12.21.0, Turborepo 2.11.13, Vite 8.3.8, React 19.3.1, TypeScript 7.0.2,
  Tailwind 4.3.5; Sanity Studio `sanity` 6.24.0, `@sanity/client` 8.17.0 (latest on 2026-11-20).
- Branch protection on `main`: changes through pull requests; GitHub Actions is blocked by the account's billing at
  the time of writing, which blocks every workflow here, the deploy included, until it is fixed.

## Approaches considered

- **A. Static portal rebuilt from Sanity on publish (picked by João).** CI drafts releases into Sanity; a Sanity
  webhook on publish triggers a GitHub workflow that reads the published content, copies the files from the GitHub
  releases, checks them, builds the portal and deploys it to GitHub Pages. Files same-origin and checked before going
  live; private dataset; static, free hosting; independent of Sanity at run time. About two minutes from publish to
  live, and a webhook-to-workflow hop to maintain.
- **B. Portal reads Sanity live; files in Sanity's asset storage.** Instant publishing, but a public dataset,
  firmware binaries on Sanity's bandwidth, checks only in the visitor's browser, an outage takes the portal down.
- **C. Next.js on Vercel with on-demand revalidation.** Near-instant updates and server-side tokens, but a new host
  and runtime to run and pay for, and same-origin packages need extra work.

## Design

### Pieces

```
apps/
  portal/          @knobi/portal          the public site (Vite + React, @knobi/ui): home + one page per synth
  studio/          @knobi/studio          Sanity Studio: the content model, deployed to Sanity's hosting
  installer/       @knobi/installer       the standalone installer: kept as the dev harness (?mock), not published
packages/
  installer-ui/    @knobi/installer-ui    the installer's store + React view (from apps/installer/src/ui), used by both apps
  content/         @knobi/content         the Sanity schema types, the GROQ queries, and the release writer (Node)
  catalog/ …                              unchanged; gains "catalog from published content"
```

- `@knobi/installer-ui`: `Installer` (the store) and `InstallerPanel` (today's `Page` minus the page chrome), taking
  the catalog (or a single release) as input; `apps/installer` and `apps/portal` both render it.
- `@knobi/content`: the document types (TypeScript), the Studio schema definitions (imported by `apps/studio`), the
  GROQ queries the portal build uses, and `writeReleaseDraft()` used by CI. Node-only parts behind `./node`, as in
  `@knobi/catalog`.

### Content model (Sanity)

- **synth**: `id` (slug, equals `synths/<id>/`), `name`, `summary`, `description` (short rich text), `image`
  (optional), `license`, `origin` (`own` | `external`; only `own` is shown for now), `order`.
- **release**: `synth` (reference), `version`, `date`, `tag` (the git tag), `notes` (rich text, edited by João;
  CI fills a first draft from the GitHub release notes), `current` (boolean: the one the synth page installs; one per
  synth), `source` { `file`, `sha256`, `size` }, and `builds[]`:
  - `kind`: `keyboard` | `desktop`;
  - keyboard: `instrument` (an id from `packages/catalog/instruments.json`), `identity`, `file`, `sha256`, `size`;
  - desktop: `format` (`vst3` | `au`), `os` (`mac` | `windows`), `arch`, `file`, `sha256`, `size`.
- Read-only in the Studio for everything CI writes (files, hashes, sizes, identities, tag, version): João edits
  `notes`, `current`, the synth's text and image, and publishes. Document ids are deterministic
  (`release.<synth>.<version>`), so CI re-running on the same tag updates the same draft.
- Instruments, families, port patterns and loader rules stay in `packages/catalog/instruments.json` (code-coupled,
  drift-tested against `tools/fm1_install.py`); Sanity references them by id.

### Flows

1. **Tag → draft.** `release.yml` on a `v*` tag (no `-`): builds, tests, makes the packages, `SHA256SUMS` and the
   source archive, attaches them to the GitHub release (as today), then a job runs `writeReleaseDraft()` with
   `SANITY_WRITE_TOKEN`: it checks every file again (names from `synth.json`, identity, loader mark, SHA-256), and
   creates or replaces the draft release in Sanity. It never publishes. The `site` job is removed.
2. **Review → publish.** In the Studio, João reads the draft, edits notes, sets `current`, publishes.
3. **Publish → deploy.** A Sanity webhook (on publish of `release` or `synth`) calls GitHub's `repository_dispatch`
   (event `sanity-publish`) with a fine-grained token limited to this repository. `portal.yml` runs on that event (and
   on manual dispatch):
   - queries the **published** perspective with `SANITY_READ_TOKEN`;
   - for each published release, downloads its files from the GitHub release `tag` (`gh release download`, the
     workflow's own token), checks each against Sanity's SHA-256 and the release's `SHA256SUMS`, and keyboard
     packages through `loadPackage` (identity, loader mark);
   - builds `catalog.json` from the published content (`@knobi/catalog`: same schema 1, so the installer is
     unchanged), builds `apps/portal`, copies the files to `firmware/<synth>/<version>/`;
   - refuses any `-local` or `cpu1` file, and any token-looking string in `dist/`;
   - deploys to GitHub Pages (`jhortale/midi-chlorian`, branch `gh-pages`, with a deploy key as for knobi-flash).
4. **Deploy target.** `jhortale/midi-chlorian` (public, Pages from `gh-pages`); it holds only built files.
5. **Which code goes live (my call, as João did not pick).** The site is always built from `main` (protected: pull
   requests and checks). `portal.yml` runs on the Sanity publish event, on manual dispatch, and on pushes to `main`
   that touch `apps/portal/`, `packages/installer-*`, `packages/ui/`, `packages/catalog/`, `packages/content/`; no
   GitHub approval gate (the review is the pull request and, for content, Sanity). Each deploy checks out `main`.

### The portal

```
/                     the synths: name, summary, image, current version, a link to its page
/<synth>/             KNOBI-SIX — summary, description
                        Current: 1.0 (date) · release notes
                        [Install on my keyboard]   (the installer panel, for this release's keyboard builds)
                        Download: SMK-37 Pro .fwsc · Elite · MKE-P37 · source (GPL) · (later) VST3/AU Mac · PC
                        Older releases ▸ (each: notes, downloads, install)
```

- Each synth page has a stable URL that works on GitHub Pages without server rewrites (prerendered pages or one
  `index.html` per route: builder's call).
- The KNOBI panel look (`@knobi/ui`), dark, phone width without horizontal scroll.
- Downloads are plain links to the same-origin files, with the size and SHA-256 shown; the backup warning appears
  before the install button, as today.

### Rules settled after review (my calls)

- **A published release never changes.** `writeReleaseDraft()` refuses when a *published* release document with that
  id exists, and the release job refuses to replace existing assets of a GitHub release. A fix is a new version. (A
  re-run before publishing replaces the draft, but only CI-owned fields; João's `notes` and `current` are kept.)
- **Provenance without attestations.** GitHub's build attestations (`actions/attest-build-provenance`, used in
  `release.yml` today) require GitHub Enterprise Cloud on a private repository, so they cannot be relied on, and that
  step should be made non-blocking (or dropped) when the release job is reworked. Instead every deploy and every draft
  write checks that each file comes from a GitHub release of this repository whose assets were uploaded by
  `github-actions[bot]`, and that it matches the release's `SHA256SUMS` and Sanity's SHA-256.
- **The repo stays the source for safety-critical and code-coupled synth data.** `synths/<id>/` keeps `id`,
  `license`, the identities and package names (`synth.json`), and the install warning shown before installing
  (`catalog.json`'s `notes`, e.g. KNOBI-SIX's "Replaces the stored firmware and its saved data; calibration is not
  written"). Sanity owns the display text: name, summary, description, image, order. The deploy refuses a Sanity synth
  with no `synths/<id>/`, and `catalog.json` for the installer takes `notes` and `license` from the repo. The `?mock`
  harness keeps using `synths/<id>/catalog.json`.
- **Tags name their synth:** `<synth>-v<version>`, e.g. `knobi-six-v1.0` (no release was ever made, so nothing to
  migrate; the old `v0.1*` tags stay as they are). `release.yml` triggers on `*-v*` tags without a `-` after the
  version (no prereleases), and reads the synth id from the tag.

### Secrets and setup (João)

- A Sanity project (plan, region and dataset name his) with a **private** dataset; tokens: `SANITY_WRITE_TOKEN`
  (Editor, for CI drafts) and `SANITY_READ_TOKEN` (Viewer, for the deploy), as GitHub Actions secrets.
- The Studio deployed with `sanity deploy` (Sanity's hosting) or run locally.
- A webhook in Sanity → `https://api.github.com/repos/jhortale/knobi-six/dispatches` with a fine-grained GitHub
  token (this repository only, Contents: read and write), stored only in Sanity's webhook settings.
- The public repo `jhortale/midi-chlorian` (Pages from `gh-pages`) and a deploy key, its private half as the
  secret `MIDI_CHLORIAN_DEPLOY_KEY` in knobi-six.
- `docs/PORTAL.md`: the one-time setup and the release flow, replacing the knobi-flash parts of
  `docs/WEB_INSTALLER.md`.

### Testing

- Content: schema types and the queries tested against fixtures (no network); `writeReleaseDraft()` tested with a
  fake Sanity client (the document it writes, idempotence, refusals: wrong hash, `-local`, wrong identity, missing
  source).
- Deploy build: a local dry run from a fixture of published content plus a fake release folder (as was done for
  knobi-flash): the site layout, the catalog, the refusals.
- Portal: React tests in jsdom (home lists synths; synth page shows current release, downloads, older releases; the
  installer panel works against the simulated keyboard); a `dist` test (no simulator, no tokens, Tailwind classes of
  `@knobi/ui` present); a browser check of the built site.
- Studio: builds (`sanity build`) in CI; no tests against a live Sanity project.

## Left to the builder

- Prerendering vs. one `index.html` per route; the router.
- How `InstallerPanel` is cut out of today's `Page` (props, layout), as long as `apps/installer` keeps working.
- The Studio's desk structure and field validation details.
- Which job attaches the source archive and notes to the GitHub release (today's `release.yml` does it).

## Amendment, 2026-11-21: Markdown with frontmatter instead of Sanity (João)

João chose to keep the content in the repo, as Markdown files with YAML frontmatter, instead of Sanity: simpler (no
outside service, tokens, webhook or Studio), versioned with the code, free. This amendment **replaces** every
Sanity-specific part above (the Sanity dataset, tokens, webhook, `repository_dispatch`, `apps/studio`, the Studio
schema); everything else stands: the static portal on GitHub Pages at `jhortale/midi-chlorian`, files only from CI
release assets re-checked on every deploy, published releases never change, repo-owned safety data, the
`<synth>-v<version>` tags, the installer unchanged.

### Content files

- `synths/<id>/synth.md`: frontmatter `name`, `summary`, `order` (number), `image` (optional, a path inside
  `synths/<id>/`), `origin` (`own`; `external` reserved, not shown); the body (Markdown) is the description.
  `synth.json` and `catalog.json` keep their roles (identity, package names, licence, install warning).
- `synths/<id>/releases/<version>.md`: frontmatter `version`, `date`, `tag`, `current` (boolean, optional), `source`
  {`file`, `sha256`, `size`}, `builds` (the same keyboard/desktop build entries as the Sanity model above); the body
  is the release notes.
- Parsed with `yaml` 2.9.1; Markdown rendered to HTML **at build time** with `marked` 18.2.1, raw HTML in the
  Markdown escaped (not passed through). A synth or release whose file does not validate stops the build with an
  error naming the file.

### Flows (replacing "Tag → draft", "Review → publish", "Publish → deploy")

1. **Tag → release PR.** On `<synth>-v<version>`, `release.yml` builds, tests, uploads the files to the GitHub
   release (refusing to replace existing assets), verifies them (uploader `github-actions[bot]`, `SHA256SUMS`,
   identity, loader mark, release names only), writes `synths/<synth>/releases/<version>.md` with every CI-owned
   field filled and a first draft of the notes, and opens a pull request `release/<tag>` into `main` with the
   workflow's token. It refuses when that file already exists on `main` (a published release never changes).
2. **Review → publish.** João edits the notes (and `current`) in the PR and merges it. Because a PR opened by the
   workflow's own token does not trigger other workflows, its required checks never run: **release PRs are merged
   with the admin bypass** (they only add one Markdown file). A GitHub App token can replace this later.
3. **Merge → deploy.** `portal.yml` runs on pushes to `main` that touch `synths/**`, the portal or its packages, and
   on manual dispatch; it builds the site from `main` and deploys it.
4. **Edits after publishing.** A check in `web.yml` refuses any change to the CI-owned frontmatter of an existing
   release file (only `notes`, i.e. the body, and `current` may change).

### What goes away

`apps/studio`, the Sanity dependencies and schema, `SANITY_WRITE_TOKEN`, `SANITY_READ_TOKEN`, the webhook and its
GitHub token, the `sanity-publish` event. Review Focus items about Sanity become the same checks on the Markdown files.
