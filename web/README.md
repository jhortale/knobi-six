# knobi-flash

The browser installer for KNOBI-SIX (Web MIDI), published at <https://jhortale.github.io/knobi-flash/> from the
public repo `jhortale/knobi-flash`. Design: `docs/superpowers/specs/2026-11-20-web-installer-design.md`.

```sh
npm ci
npm run dev          # http://localhost:5173/  (?mock: a simulated keyboard, never a real one)
npm test             # Vitest: protocol, catalog, updater, page, against a simulated keyboard
npm run build        # dist/
python3 ../tests/web_fixtures.py --out test/fixtures/python.json   # after a change to tools/fm1_install.py
```

The page is React + shadcn/ui (Tailwind v4), themed in `src/index.css`; behaviour in `src/ui/installer.ts`.

`src/protocol/` is a port of `tools/fm1_install.py` (GPL-3.0-only, Leo Kuroshita and M. Geisler); keep the two in
step: `test/fixtures/python.json` is what the Python answers, and CI fails when it is out of date.
