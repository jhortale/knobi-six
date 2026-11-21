# knobi-flash

The browser installer for KNOBI-SIX (Web MIDI): the development harness of the installer, not a published site (`?mock`
runs it against a simulated keyboard). The public site is midi-chlorian (`apps/portal/`, [docs/PORTAL.md](../../docs/PORTAL.md)),
whose synth pages embed the same installer. Design: `docs/superpowers/specs/2026-11-20-web-installer-design.md`.

```sh
pnpm install                              # at the repository root
pnpm --filter @knobi/installer dev        # http://localhost:5173/  (?mock: a simulated keyboard, never a real one)
pnpm --filter @knobi/installer test       # the page and dist/ tests (after a build)
pnpm turbo run typecheck build test       # everything, from the root
python3 ../../tests/web_fixtures.py --out ../../packages/installer-core/test/fixtures/python.json   # after a change to tools/fm1_install.py
```

The page is React + shadcn/ui (Tailwind v4) from `@knobi/ui` (components and the panel theme); behaviour in
`@knobi/installer-ui` (`packages/installer-ui/`), drawn by its `InstallerPanel` inside `src/ui/Page.tsx`.

`@knobi/installer-core` (`packages/installer-core/`) is a port of `tools/fm1_install.py` (GPL-3.0-only, Leo Kuroshita and M. Geisler); keep the two in
step: `packages/installer-core/test/fixtures/python.json` is what the Python answers, and CI fails when it is out of date.
