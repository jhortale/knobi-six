# knobi-flash: a browser installer for KNOBI-SIX (and what comes after it)

Date: 2026-11-20. Status: approved by João, 2026-11-20.

## Intent

Today KNOBI-SIX goes on a keyboard only with `tools/fm1_install.py`: Python, mido, python-rtmidi and a terminal. João
wants the install to be what the M-Vave community already knows from genkigenki's SMK-37 installer page: open a page in
Chrome, plug in the keyboard, press Install. The page is called **knobi-flash** and is published from a public repo,
`jhortale/knobi-flash`, at `https://jhortale.github.io/knobi-flash/`, while KNOBI-SIX's source stays private.

It must not be a one-firmware page. KNOBI-SIX has three builds today (SMK-37 Pro, SMK-37 Elite, MKE-P37: one keyboard
under three names) and more firmwares and instruments may follow, so what the page offers comes from a catalog file
(instruments, firmwares, packages, identities, SHA-256 sums), not from code.

## Goals

- Install a KNOBI-SIX release on an SMK-37 Pro, SMK-37 Elite or MKE-P37 from Chrome or Edge over Web MIDI, with the
  same protocol, checks and refusals as `tools/fm1_install.py` (a port, not a re-design).
- Finish an install that was cut off (the keyboard is still in KNOBI-SIX's update loader, `ota-<model>_9NN`).
- Show the connected keyboard's identity (the Python tool's `--info`).
- Everything the page lists comes from `catalog.json`; adding a firmware, a release or an instrument of the same
  update protocol is a catalog change only.
- Packages are checked before anything is sent: SHA-256 against the catalog, then the package's own CRCs, its identity
  and the Felucca loader mark, as the Python tool does.
- Tested entirely against a simulated device, in CI.

## Non-goals

- No flash backup or restore in the browser: the dump needs the chip's mask-ROM bootloader, which shows up as a USB
  disk; browsers cannot talk to USB mass storage (WebUSB blocks that class). The page links to `docs/BACKUP.md`.
- No M-Vave stock firmware, no "back to stock" flow, no repair flow (genkigenki's page has those; knobi-flash does not).
- No firmware other than the catalog's: no "choose a .fwsc from disk" in the shipped page.
- No Firefox / Safari support beyond a clear "use Chrome or Edge" message.
- No telling the CPU1 probe build from the regular build on the device: both report `SMK-37 Pro_956`. Only the file's
  SHA-256 tells them apart, and the probe is never a release, so it is never in the catalog.

## Anti-goals (a failure even if it works)

- **A `-local` package is offered or hosted.** `knobi-six-local*.fwsc` holds Sequential's OB-6 bank
  (docs/FACTORY_BANK.md). Only `knobi-six-X.Y*.fwsc` from a release, which `build.py --release` refuses to make while
  the bank is present, ever reaches the site; the catalog generator and the deploy refuse anything else.
- **An install starts without the backup confirmation**, or against the wrong model, or against another firmware's
  update loader.
- **The page can talk to other MIDI gear**: it probes only ports whose names match the catalog's patterns, unless the
  user ticks "search all MIDI ports".
- **Something is installed on a real keyboard by an agent.** Development and tests use the simulated device only;
  João does the first real install.
- **The installer and `fm1_install.py` drift apart** without a test noticing.

## Constraints

- KNOBI-SIX's repo is private. GitHub Pages is served from the public `jhortale/knobi-flash`; a browser cannot read
  release files of a private repo, and GitHub's release download links send no CORS headers even for public repos, so
  the packages are copied onto the site and fetched from the same origin.
- Web MIDI with SysEx (`navigator.requestMIDIAccess({sysex: true})`) needs a secure context (https or localhost) and a
  user permission prompt; Chrome and Edge only in practice. On Windows, Chrome may need
  `chrome://flags/#use-winrt-midi-api` (the reference page's advice).
- Known device facts (2026-11-19/20 install on João's SMK-37 Pro): the running KNOBI-SIX's MIDI port is named
  `X0X FM-1` (USB 1209:0001), so matching on "SMK-37" alone fails; it answers `SMK-37 Pro_956`. KNOBI-SIX's update
  loader answers `ota-<model>_900` (`FELUCCA_ID`, `firmware/loader/loader.c`), whatever the app's IDENT; any
  `ota-(FM-1|SMK-37 Pro|SMK-37 Elite|MKE-P37)_9\d\d` is a Felucca-family loader the installer may resume, any other
  `ota-…` is refused. Packages are about 652 KB.
- Licence: the protocol code is a port of `tools/fm1_install.py` (GPL-3.0-only, Leo Kuroshita / M. Geisler); the
  installer is GPL-3.0-only with their credit, and `LICENSING.md` gets a row (never drop one).
- CLAUDE.md: no agent installs, flashes or uses the USB rescue; that covers this page too.

## Approaches considered

- **A. Vanilla ES modules, no build step**, tested with `node:test`. Fewest moving parts, matches the repo's
  no-dependency style. Not picked.
- **B. Vite + TypeScript** (picked by João). Typed catalog schema and protocol, a real dev server for the UI, Vitest for
  tests; costs npm dependencies and a build step, contained in `web/`.
- **C. Source in the public knobi-flash repo**, pulling releases with a token. Splits the JS protocol from the Python
  it must match. Not picked.

## Design

### Where things live

- Source: `web/` in knobi-six (private), a self-contained Vite + TypeScript project (`web/package.json`,
  `package-lock.json`, `tsconfig.json`, `vite.config.ts`, `index.html`, `src/`, `test/`). Tested in CI with every push.
- Published: `jhortale/knobi-flash` (public), branch `gh-pages` (or `main`, builder's call), holding only the built
  page, `catalog.json`, the release packages and `SHA256SUMS`, plus a README and LICENSE that point to the source
  licence. GitHub Pages serves it.
- The public repo and its deploy key are created by João (an outward-facing step); the workflow expects a secret
  `KNOBI_FLASH_DEPLOY_KEY` in knobi-six.

### Modules (`web/src/`)

- `protocol/sevenbit.ts`: `pack7` / `unpack7`.
- `protocol/messages.ts`: the handshake query `F0 00 32 45 00 00 00 40 7F F7`, the upgrade command
  `F0 22 24 35 7F F7`, `parseIdentity` (command 0x11, 27-byte text, `([^_]+)_(\d+)`), `parseRequest` (command 0x30,
  flags, address, length, checksum), `response(addr, data, flags)`, the finish addresses `0xE0000000` / `0xF0000000`
  answered with `success\0`, `MAXDATA` 512.
- `protocol/package.ts`: `identityBlocks`, `blockCount`, `logicalImage`, `deinterleave`, `productOf`,
  `packageDamage` (JieLi ENC decode, CRC-16/XMODEM, header, file list, CRCs of `flash.bin` / `ota.bin` only),
  `hasLoaderMark` (`FELUCCA-LOADER-1`), `loadPackage(bytes) -> {product, image}`.
- `protocol/identity.ts`: `modelOf`, `sameKeyboard` (the SMK family regex), `isLoader`, `isOwnLoader`.
- `protocol/updater.ts`: the `Updater` of `fm1_install.py` as async code over a `MidiBackend` interface: `find`,
  `waitFor`, `serve`, `check` (step 1), `write` (step 2), `verify`, `install`; the same timeouts (`DELAY`, overridable
  for tests) and the same error codes (`notfound`, `model`, `foreign`, `lost`, `stopped`, `badreq`, `noloader`,
  `noreturn`, `mismatch`, `badpkg`) with messages that say what to do next. Progress is reported as events
  (`start`, `check n`, `loader`, `write %`, `reboot`, `done`).
- `midi/backend.ts`: the interface (`inputNames`, `outputNames`, `open(in, out) -> Link`; `Link.send`, `read(timeout)`,
  `drain`, `lost`, `close`), the same shape as the Python backend so the port stays line-for-line.
- `midi/webmidi.ts`: the Web MIDI implementation. Ports by name; `pairOutput` as in Python (Windows numbering); loss
  detected from `statechange` / the port's `state`; a short pause after opening (CoreMIDI drops an early message).
- `catalog/schema.ts`, `catalog/load.ts`: types, validation (refuse the whole catalog if malformed), and the queries
  the UI needs (instruments matching a port name, packages for a detected identity).
- `ui/`: the page (below). Plain TypeScript and DOM, no UI framework (builder may pick a tiny one if it pays off).
- `dev/fakeDevice.ts`: the simulated keyboard (below). Imported only by tests and by the dev server's `?mock` mode; the
  production build must not contain it (checked by a test on `dist/`).

### The catalog (`catalog.json`, schema 1)

```json
{
  "schema": 1,
  "generated": "2026-11-20T12:00:00Z",
  "protocols": { "felucca-ota": { "ownLoader": "ota-(FM-1|SMK-37 Pro|SMK-37 Elite|MKE-P37)_9\\d\\d" } },
  "families": {
    "smk37": { "name": "M-VAVE SMK-37 family", "models": ["SMK-37 Pro", "SMK-37 Elite", "MKE-P37"],
               "match": "SMK-37[ +].*|MKE-P37|STARRYKEY-37.*",
               "ports": "fm-1|smk|elite|mke-p37|starry|felucca|sloop|clipper|melodee|fomni|x0x|makkuro|maku|rainbow|snoop|vector|vox|ota|composite|sinco|usb-midi" }
  },
  "instruments": [
    { "id": "smk37-pro",   "name": "M-VAVE SMK-37 Pro",   "family": "smk37", "model": "SMK-37 Pro",   "protocol": "felucca-ota" },
    { "id": "smk37-elite", "name": "M-VAVE SMK-37 Elite", "family": "smk37", "model": "SMK-37 Elite", "protocol": "felucca-ota" },
    { "id": "mke-p37",     "name": "MKE-P37",             "family": "smk37", "model": "MKE-P37",      "protocol": "felucca-ota" }
  ],
  "firmwares": [
    { "id": "knobi-six", "name": "KNOBI-SIX", "summary": "OB-6-inspired six-voice polysynth",
      "license": "GPL-3.0-only", "notes": ["Replaces the stored firmware and its saved data; calibration is not written."],
      "releases": [
        { "version": "1.0", "date": "2026-11-20", "packages": [
          { "instrument": "smk37-pro", "file": "firmware/knobi-six/1.0/knobi-six-1.0.fwsc",
            "identity": "SMK-37 Pro_956", "sha256": "…", "size": 652288 }
        ] }
      ] }
  ]
}
```

- The source of truth for everything but the release data is `web/catalog/base.json` (instruments, families,
  protocols, firmware descriptions). A script, `web/scripts/make-catalog.ts` (run with Node 24's built-in TypeScript
  support, no extra tool), takes that, the release version and the directory with the `.fwsc` files and `SHA256SUMS`,
  checks each file's hash against `SHA256SUMS`, reads each package's identity with `protocol/package.ts`, maps it to
  an instrument by model, and writes `catalog.json`. It refuses: any file name that is not `knobi-six-<version>[-elite|-mke].fwsc`
  (so `-local`, `-cpu1probe`, dev builds), a hash mismatch, a damaged package, a package without the loader mark, an
  identity that is not `<model>_956`, a model with no instrument.
- Device matching is `fm1_install.py`'s, word for word: the family's `ports` is `PORT_RE`, its `match` is
  `SMK_FAMILY` (so the Starrykey 37 Play and the SMK-37+VA mod count as the same keyboard), the protocol's `ownLoader` is
  `OWN_LOADER_RE`. `sameKeyboard`, port probing and the loader check read these from the catalog; a test parses
  `tools/fm1_install.py` and fails if the catalog's patterns differ from it. (Decided in review.)
- Each deploy replaces the site; the generator writes KNOBI-SIX's release being deployed (latest only at launch).
  Another firmware is added as a complete entry in `base.json` (with `sha256`, `size`, `identity`) and its package
  file committed under `web/public/firmware/<id>/<version>/`; the generator copies such entries through unchanged after
  checking their files' hashes, so a KNOBI-SIX deploy never drops them. Release entries may be marked
  `"source": "<url>"` for the GPL source (below). (Builder's call, recorded in review.)

### The install flow (what the user sees)

The page is one column, steps in order, in the KNOBI-SIX panel style (dark panel, the rounded sans of
`docs/design/standalone-ui/`, the reference page's step list):

```
KNOBI-FLASH                                     unofficial · not made by M-VAVE or Sequential
───────────────────────────────────────────────────────────────────────────────
1 Connect     [Connect keyboard]   ☐ search all MIDI ports
              ✓ SMK-37 Pro_956 on "X0X FM-1"  (running)
2 Firmware    ◉ KNOBI-SIX 1.0 for SMK-37 Pro   (652 KB, sha256 3f2a…)   [other model ▾]
3 Before      ☐ I have a verified full flash backup (how: docs/BACKUP.md)
              Keep the USB cable in and this tab open until Done.
4 Install     [Install]
              Connect ✓  Verify ✓  Update mode ✓  Write ███████░░ 78%  Restart  Done
              ┌ log ─────────────────────────────────┐ [Copy log]
```

- **Connect**: asks for MIDI access with SysEx, probes the matching ports with the handshake, shows identity, port and
  mode (running / update loader: "the last install did not finish; Install finishes it").
- **Firmware**: the catalog's packages for the detected model are listed and the newest is preselected; "other model"
  shows the family's other builds (as the reference page and `same_keyboard` allow) with a warning. When the device
  is in KNOBI-SIX's update loader, the matching package is preselected and the label says "finish the install".
- **Verify** (when Install is pressed, before anything is sent): download, SHA-256 against the catalog, then
  `loadPackage` (identity, loader mark, CRCs), then the model check against the device (`sameKeyboard`), then the
  foreign-loader check. Any failure stops with nothing sent.
- **Confirm**: a dialog naming the device identity, the package and "do not unplug while writing".
- **Install**: steps and a progress bar from the updater's events; `beforeunload` warns while installing; the log
  keeps every step with times and can be copied.
- **Done**: the identity the keyboard reports after the restart, matched against the package's.
- Errors show the updater's message and the next step (replug and run again to finish; power-cycle; use the right
  model's package).
- No Web MIDI: a plain message (Chrome or Edge, https, the Windows flag).

### The simulated device and the tests

- `dev/fakeDevice.ts` is a port of `FakeFM1` in `tests/install_test.py`: one keyboard on a fake backend, answering the
  handshake with its identity, and on the upgrade command asking for reads the way the real firmware does (the
  running firmware: a few reads then `FINISH_CHECK`, then it reboots as the loader under another port name; the loader:
  reads over the whole image then `FINISH_WRITE`, then it reboots as the new firmware). It checks every answer's bytes
  and can be told to unplug, stall, ask for an address outside the package, or come back with the wrong identity.
- Vitest suites: seven-bit packing and messages (vectors produced by `fm1_install.py`), package parsing and damage
  (synthetic packages made like `install_test.py`'s, 20- and 36-block layouts), catalog validation and the generator's
  refusals, the updater against the fake device (full install, resume from the loader, wrong model, foreign loader,
  unplug during check and during write, stall, bad request, no loader, no return, wrong identity after restart), and
  a cross-check: a small Python script emits JSON fixtures from `fm1_install.py` (product, logical image SHA-256,
  damage verdicts, response packets) for the same inputs and the TS results must equal them, plus
  `build/knobi-six.fwsc` when a build is present.
- A UI test in jsdom with a mocked `navigator.requestMIDIAccess` backed by the fake device: the backup checkbox gates
  Install; a full install reaches Done; an unplug shows the resume message.
- `npm run dev` with `?mock` runs the page on the fake device in a real browser, for looking at it by hand.
- CI: a `web` job in `.github/workflows/build.yml` runs `npm ci`, the type check, the tests and `npm run build` on every
  push and pull request (it needs no JieLi toolchain).

### Deploy

A `site` job in `release.yml`, on `v*` tags only, after the firmware job, in a GitHub environment `knobi-flash` with
João as required reviewer: every deploy waits for his approval (the first one after his own real install from the dev
server on localhost). Tags with a `-` (prereleases) never deploy. A `workflow_dispatch` input redeploys the page for an
existing tag, through the same gate.

The job: takes the release's `.fwsc` files and `SHA256SUMS` (the firmware job uploads both), builds `web/`, runs
`make-catalog.ts`, copies the packages to `dist/firmware/knobi-six/<version>/`, and pushes `dist/` to
`jhortale/knobi-flash` with the deploy key. It refuses to deploy if any file in `dist/` matches `*-local*` or `*cpu1*`.

**GPL source, from the same place** (João's choice): the deploy also publishes, next to the packages,
`knobi-six-<version>-src.tar.gz`, a `git archive` of the tag (the OB-6 bank `firmware/gen/factory_bank.h` is never
committed, so it is never in it; the job checks that), and the installer's own source is part of that archive (`web/`).
The built JS ships with source maps. The page links each release's package to its source archive, and knobi-flash's
README states the licence (GPL-3.0-only), the credits from `LICENSING.md`, and where the source is. The release
entry's `source` field points to the archive.

Note: tags `v0.1`..`v0.1.4` exist but no GitHub release was ever made (`release.yml` has not run on them); the first
release for the site is a new tag.

## Left to the builder

- Exact file split inside `src/`, CSS details within the panel style, the UI's DOM structure.
- The push action (e.g. `peaceiris/actions-gh-pages` with `external_repository`) and the published branch name.
- Vitest's environment setup, the Python fixture script's location (`tests/` or `web/test/fixtures/`).
- Dependency versions (pin exact versions in `package-lock.json`; only Vite, TypeScript, Vitest, jsdom as dev
  dependencies; no runtime dependencies).

## Amendment, 2026-11-20: the page in React with shadcn/ui (João)

João asked for the page to be React with shadcn/ui, keeping the panel look. What changes:

- `web/src/ui/` is React 19: `installer.ts` holds the page's behaviour as a small store (state + subscribe), `Page.tsx`
  draws it with shadcn/ui components (Button, Checkbox, RadioGroup, Card, Progress, Alert, AlertDialog, Collapsible,
  Badge, Label in `web/src/components/ui/`, copied in by the shadcn CLI as shadcn intends), `app.tsx` starts it. The
  protocol, catalog, Web MIDI, generator and simulated keyboard are unchanged.
- Styling is Tailwind CSS v4 with shadcn's theme variables set to the KNOBI-SIX panel colours (`web/src/index.css`),
  always dark.
- The confirm step is the page's own dialog (shadcn AlertDialog), not the browser's `window.confirm`.
- This replaces "no runtime dependencies" and "no UI framework": the page now ships React, React DOM, Radix UI,
  class-variance-authority, clsx, tailwind-merge and lucide-react (all MIT), about 330 KB of JavaScript (105 KB
  gzipped) instead of 24 KB. Versions are pinned exactly in `web/package.json`.

