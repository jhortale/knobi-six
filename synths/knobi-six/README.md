# KNOBI-SIX

OB-6-inspired six-voice polysynth firmware for the M-VAVE SMK-37 family (Pro, Elite, MKE-P37).

This folder holds KNOBI-SIX's metadata in the monorepo (`docs/MONOREPO.md`):

- `synth.json`: identity (`SMK-37 Pro_956`, IDENT `956`, `docs/IDENTITY.md`), the release package names per model, and
  where each platform's source and build are. The catalog generator (`packages/catalog`) reads it; the firmware build
  does not.
- `catalog.json`: KNOBI-SIX's entry in the installer's catalog (name, summary, licence, notes). Releases are added by
  the generator from a release's files.
- `releases/<version>/` (none yet): committed release packages, only when a release is published without a tag build.

Where its parts live today, at upstream's paths (they do not move: `git merge upstream/main` keeps working):

- the firmware app: `firmware/src/app.c`, `firmware/src/synth/`, `params/`, `ui/`; built by `./build.sh`;
- the desktop plugin (JUCE AU / VST3): the `plugin` branch, deferred (roadmap step 3 in
  `docs/superpowers/specs/2026-11-20-monorepo-design.md`).
