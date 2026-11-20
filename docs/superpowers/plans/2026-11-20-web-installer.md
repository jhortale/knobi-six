# knobi-flash Web Installer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Web MIDI page, knobi-flash, that installs KNOBI-SIX release packages on the SMK-37 Pro / Elite / MKE-P37
exactly as `tools/fm1_install.py` does, driven by `catalog.json`, tested only against a simulated keyboard, and
deployed (behind João's approval) to the public `jhortale/knobi-flash`.

**Architecture:** A self-contained Vite + TypeScript project in `web/`. `src/protocol/` is a line-for-line port of
`fm1_install.py` over a `MidiBackend` interface; `src/midi/webmidi.ts` implements it with Web MIDI, `src/dev/fakeDevice.ts`
with a simulated keyboard (port of `FakeFM1`). `src/catalog/` loads and validates the catalog; `scripts/make-catalog.ts`
generates it at deploy. `src/ui/` is plain DOM. A Python script emits fixtures from `fm1_install.py` so drift fails CI.

**Tech Stack:** TypeScript 5, Vite 6, Vitest 3, jsdom (dev dependencies only, exact versions in `package-lock.json`),
Node 24 (built-in TS type stripping for scripts), Python 3 for fixtures, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-11-20-web-installer-design.md` (read it first; it is binding).

## Global Constraints

- Never run anything against a real keyboard; no `tools/fm1_install.py` on hardware, no flashing (CLAUDE.md). All
  device behaviour is tested with `src/dev/fakeDevice.ts`.
- No runtime dependencies. Dev dependencies only: `vite`, `typescript`, `vitest`, `jsdom`.
- Every source file in `web/src/protocol/`, `web/src/dev/fakeDevice.ts` and `tests/web_fixtures.py` starts with
  `// SPDX-License-Identifier: GPL-3.0-only` and the credit "port of tools/fm1_install.py: Copyright (C) 2026 Leo
  Kuroshita (@kurogedelic), Hügelton Instruments; M. Geisler (genkigenki/makkuro)"; other new files carry the
  SPDX line.
- Wire constants verbatim: handshake `F0 00 32 45 00 00 00 40 7F F7`; upgrade `F0 22 24 35 7F F7`; finish addresses
  `0xE0000000` (check) and `0xF0000000` (write) answered with `success\0`; `MAXDATA` 512; blocks 20, block size
  `0x30`, kept bytes `0x2F`, `MAX_BLOCKS` 64; loader mark `FELUCCA-LOADER-1`.
- Device matching patterns are `fm1_install.py`'s `PORT_RE`, `SMK_FAMILY`, `OWN_LOADER_RE`, carried in the catalog.
- Never offer, host or deploy a `-local` or `cpu1` package; release files only: `knobi-six-<version>[-elite|-mke].fwsc`.
- The shipped build (`dist/`) must not contain the fake device or a "choose a file" option.
- Install is disabled until the backup checkbox is ticked.
- Commits: small, conventional, end with a blank line and `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- `LICENSING.md`: add rows, never drop any.

## Review Focus

1. **SysEx permission denied** (the user clicks Block, or the page is not https): a clear message naming Chrome/Edge,
   https and the Windows flag; nothing throws. Test in Task 6.
2. **Stale catalog vs. package after a deploy** (Pages caches ~10 min): a SHA-256 mismatch says "the site was just
   updated: reload the page", sends nothing; fetches use `cache: "no-cache"`. Test in Task 6.
3. **Unplugged during the write, then Install again**: the page shows the resume message, a second Connect finds the
   loader (`ota-SMK-37 Pro_900`), preselects the matching package and finishes. Test in Task 6.
4. **Windows port numbering** (`"X0X FM-1 1"` in / `"X0X FM-1 2"` out, or two keyboards): `pairOutput` pairs like
   Python; ambiguous pairs are skipped. Test in Task 4.
5. **The keyboard comes back under a different port name** after each reboot (loader port `Felucca Update`, app port
   `X0X FM-1`): `waitFor` re-scans all matching ports, not only the first one. Test in Task 4.

---

### Task 1: Project scaffold, seven-bit packing, messages, Python fixtures

**Files:**
- Create: `web/package.json`, `web/package-lock.json`, `web/tsconfig.json`, `web/vite.config.ts`, `web/index.html`
  (placeholder body), `web/.gitignore` (`node_modules`, `dist`), `web/README.md`
- Create: `web/src/protocol/sevenbit.ts`, `web/src/protocol/messages.ts`
- Create: `tests/web_fixtures.py`, `web/test/fixtures/python.json` (generated, committed)
- Test: `web/test/messages.test.ts`

**Interfaces:**
- Produces: `pack7(data: Uint8Array): Uint8Array`, `unpack7(s: Uint8Array): Uint8Array`;
  `HS_QUERY`, `UPGRADE: Uint8Array`; `FINISH_CHECK = 0xE0000000`, `FINISH_WRITE = 0xF0000000`, `MAXDATA = 512`;
  `response(addr: number, data: Uint8Array, flags = 0): Uint8Array`;
  `parseRequest(pkt: Uint8Array): {flags: number, addr: number, len: number} | null`;
  `type Identity = {text: string, model: string, version: number}`; `parseIdentity(pkt): Identity | null`;
  `identityReply(text: string): Uint8Array` (what a device sends; used by the fake device).
- Produces: `web/test/fixtures/python.json`, an object `{ wire: [...], packages: [...] }` read by Tasks 1–2.
- npm scripts: `dev`, `build` (`tsc --noEmit && vite build`), `test` (`vitest run`), `typecheck`.

- [ ] **Step 1:** Scaffold `web/` (`npm create vite` shape, vanilla-ts, then trim). `vite.config.ts`: `base: "./"`,
  `build.sourcemap: true`, Vitest `environment: "node"` by default (UI tests opt into jsdom per file).
- [ ] **Step 2:** Write `tests/web_fixtures.py`: imports `tools/fm1_install.py` and `tests/install_test.py`'s
  `package()` helper (import by path; do not run its tests). Emits to stdout or `--out FILE`, and `--check FILE` exits 1
  if the file differs. Contents: `wire`: for inputs `[]`, `[0]`, `bytes(range(256))*3` (hex), their `pack7`; three
  `response(addr, data, fl)` packets (addr `0x12345` data `010203` fl 5; addr `0` 512 bytes `i*7&255` fl 0;
  `FINISH_WRITE` with `success\0`); `parse_request` of a valid request and of one with a bad checksum (null);
  `parse_identity` of the replies for `SMK-37 Pro_956`, `ota-SMK-37 Pro_900`, `FM-1_015`. `packages`: for each of
  (`FM-1_906`, 20 blocks), (`SMK-37 Pro_956`, 20), (`SMK-37 Pro_016`, 36), (`SMK-37 Pro_956`, damage), (no marker),
  (`STARRYKEY-37 PLAY_016`, 36, size 0x3000) — the raw package as base64, and Python's `product_of`, `block_count`,
  the logical image's SHA-256, `package_damage`, and `LOADER_MARK in raw`.
- [ ] **Step 3:** Run `python3 tests/web_fixtures.py --out web/test/fixtures/python.json`; expect a ~100 KB JSON.
- [ ] **Step 4:** Write `web/test/messages.test.ts`: every `wire` fixture equals the TS result byte for byte;
  `unpack7(pack7(x))` starts with `x`; `parseIdentity` gives `{text: "SMK-37 Pro_956", model: "SMK-37 Pro", version: 956}`
  and `ota-SMK-37 Pro_900` → model `ota-SMK-37 Pro`; `parseRequest` round-trips a request the fake device would send.
- [ ] **Step 5:** `cd web && npm test` → FAIL (modules missing).
- [ ] **Step 6:** Implement `sevenbit.ts`, `messages.ts` (ports of `pack7`, `unpack7`, `response`, `parse_request`,
  `parse_identity`; identity text is latin-1, cut at the first NUL, matched with `^([^_]+)_(\d+)$`).
- [ ] **Step 7:** `npm test` → PASS; `npm run build` → `dist/` built; `python3 tests/web_fixtures.py --check web/test/fixtures/python.json` → exit 0.
- [ ] **Step 8:** Commit `feat(web): scaffold knobi-flash, seven-bit packing and messages`.

### Task 2: Package parsing and checks, identity rules

**Files:**
- Create: `web/src/protocol/package.ts`, `web/src/protocol/identity.ts`, `web/test/helpers/makePackage.ts`
- Test: `web/test/package.test.ts`

**Interfaces:**
- Consumes: fixtures from Task 1.
- Produces (package.ts): `identityBlocks(raw)`, `blockCount(raw)`, `logicalImage(raw, blocks = 20)`,
  `deinterleave(raw): {image: Uint8Array, blocks: number}`, `productOf(raw): string`,
  `packageDamage(image): string | null` (same strings as Python: `"too short"`, `"header CRC"`, `"file list"`,
  `"file list CRC"`, `"<name>: cut short"`, `"<name>: CRC"`), `hasLoaderMark(raw): boolean`,
  `loadPackage(raw: Uint8Array): {product: string, image: Uint8Array}` throwing `InstallError("badpkg", msg)`,
  `crc16(data, crc = 0)`, `jlDec(data)`.
- Produces (identity.ts): `class InstallError extends Error { code: ErrorCode }` with
  `type ErrorCode = "usage"|"badpkg"|"notfound"|"model"|"lost"|"stopped"|"badreq"|"noloader"|"noreturn"|"mismatch"|"foreign"|"denied"|"hash"`;
  `modelOf(text)`, `isLoader(id: Identity)`, `sameKeyboard(a: string, b: string, familyMatch: RegExp)`,
  `isOwnLoader(text: string, ownLoader: RegExp)`.
- Produces (test helper): `makePackage({product, marker = true, size = 0x2000, damage = false, blocks = 20, seed = 7}): Uint8Array`,
  a port of `install_test.py`'s `package()`.

- [ ] **Step 1:** Write `package.test.ts`: for each fixture package, `productOf`, `blockCount` (via `deinterleave`),
  SHA-256 of the image (`crypto.subtle`), `packageDamage`, `hasLoaderMark` equal Python's; `makePackage` with the
  fixture's parameters equals the fixture's raw bytes; `loadPackage` on the no-marker fixture throws code `badpkg`
  with "no Felucca update loader"; on the damaged one `badpkg` with "damaged package"; a 100-byte input → `badpkg` "too
  short". `sameKeyboard("SMK-37 Pro_956", "MKE-P37_956", SMK)` true; `("FM-1_906", "SMK-37 Pro_956")` false;
  `("ota-SMK-37 Pro_900", "SMK-37 Elite_956")` true; `isOwnLoader("ota-SMK-37 Pro_900")` true,
  `("ota-SMK-37 Pro_016")` false.
- [ ] **Step 2:** `npm test` → FAIL.
- [ ] **Step 3:** Implement both modules as ports of the matching Python functions (no `--force` path: the web page
  always requires the loader mark).
- [ ] **Step 4:** `npm test` → PASS. Also, when `../build/knobi-six.fwsc` exists, a test (skipped otherwise) checks
  `loadPackage` returns product `SMK-37 Pro_956`.
- [ ] **Step 5:** Commit `feat(web): package checks and identity rules, cross-checked with fm1_install.py`.

### Task 3: Catalog schema, loading, base catalog, drift test

**Files:**
- Create: `web/src/catalog/schema.ts`, `web/src/catalog/load.ts`, `web/catalog/base.json`
- Test: `web/test/catalog.test.ts`

**Interfaces:**
- Produces (schema.ts): types `Catalog`, `Protocol {ownLoader: string}`, `Family {name, models: string[], match: string, ports: string}`,
  `Instrument {id, name, family, model, protocol}`, `Firmware {id, name, summary, license, notes: string[], releases: Release[]}`,
  `Release {version, date, source?: string, packages: Package[]}`, `Package {instrument, file, identity, sha256, size}`.
  `schema` must be `1`.
- Produces (load.ts): `validateCatalog(json: unknown): Catalog` (throws `Error` listing every problem: wrong schema,
  unknown family/protocol/instrument ids, bad regex, sha256 not 64 hex, identity not `<model>_\d+` of its instrument's
  model, duplicate ids); `fetchCatalog(url = "catalog.json"): Promise<Catalog>` (`cache: "no-cache"`);
  `familyOf(cat, instrumentId)`, `portMatcher(cat): RegExp` (union of family `ports`, case-insensitive),
  `packagesFor(cat, identityText): {firmware, release, pkg, instrument, exact: boolean}[]` (exact = same model, listed
  first, newest release first; others are same-family other models), `ownLoaderRe(cat, protocolId): RegExp`.
- `base.json`: the spec's catalog without `releases[].packages` data for knobi-six (`"releases": []`), with
  `protocols.felucca-ota.ownLoader`, `families.smk37.{match, ports}` copied verbatim from Python, the three instruments,
  and the knobi-six firmware entry (summary "OB-6-inspired six-voice polysynth for the SMK-37", notes as in the spec).

- [ ] **Step 1:** Write `catalog.test.ts`: `validateCatalog(base)` passes; each invalid variant (schema 2, unknown
  instrument in a package, `sha256: "xyz"`, identity `MKE-P37_956` on instrument `smk37-pro`, regex `"("`) throws
  naming the problem; `packagesFor` on a catalog with the three packages returns Pro first for `SMK-37 Pro_956`
  (exact) and the other two after (`exact: false`); `packagesFor` for `ota-SMK-37 Pro_900` returns the Pro package
  first; for `FM-1_906` returns `[]`. **Drift test:** read `../tools/fm1_install.py`, extract the string literals of
  `PORT_RE`, `SMK_FAMILY`, `OWN_LOADER_RE` with a regex over `re.compile(r"...")`, and assert they equal
  `families.smk37.ports`, `families.smk37.match`, `protocols["felucca-ota"].ownLoader`.
- [ ] **Step 2:** `npm test` → FAIL. **Step 3:** Implement. **Step 4:** `npm test` → PASS.
- [ ] **Step 5:** Commit `feat(web): catalog schema and base catalog, patterns locked to fm1_install.py`.

### Task 4: MIDI backend interface, simulated keyboard, the updater

**Files:**
- Create: `web/src/midi/backend.ts`, `web/src/dev/fakeDevice.ts`, `web/src/protocol/updater.ts`
- Test: `web/test/updater.test.ts`

**Interfaces:**
- Produces (backend.ts): `interface Link { name: string; send(pkt: Uint8Array): boolean; read(timeoutMs: number): Promise<Uint8Array|null>; drain(): void; readonly lost: boolean; close(): void }`;
  `interface MidiBackend { inputNames(): string[]; outputNames(): string[]; open(inName: string, outName: string): Promise<Link> }`;
  `pairOutput(name: string, outs: string[]): string | null` (Python's, with the `\s+\d+$` Windows rule).
- Produces (fakeDevice.ts): `class FakeKeyboard implements MidiBackend` with constructor options
  `{image, identity = "SMK-37 Pro_016", name = "SMK-37 Pro", afterWrite = "SMK-37 Pro_956", afterWriteName = "X0X FM-1", loader = ["ota-SMK-37 Pro_900", "Felucca Update"], unplugAfter?, stallAfter?, badAddr?, extraPorts?: string[]}`
  and counters `served`, `bad`, `upgrades`, `sent: Uint8Array[]`; behaviour as `FakeFM1` (request queue for the running
  firmware `[(0,64),(0x40,160),(0x1000,512),FINISH_CHECK]`, for the loader six 512-byte reads then `FINISH_WRITE`;
  reboot 50 ms after each finish; replies checked byte for byte). `extraPorts` lists other gear that never answers.
- Produces (updater.ts): `DELAY` (ms, Python's values ×1000, mutable for tests);
  `type Step = {k: "start", id: string} | {k: "check", n: number} | {k: "loader"} | {k: "write", pct: number} | {k: "reboot"} | {k: "done", id: string}`;
  `type Device = {link: Link, id: Identity, name: string}`;
  `class Updater { constructor(backend: MidiBackend, opts: {ports: RegExp, family: RegExp, ownLoader: RegExp, searchAll?: boolean}); find(want?: (i: Identity) => boolean): Promise<Device|null>; install(dev: Device, image: Uint8Array, product: string, step: (s: Step) => void, signal?: AbortSignal): Promise<string> }`
  with `waitFor`, `serve`, `check`, `write`, `verify` as in Python; errors are `InstallError` with Python's messages,
  "FM-1" replaced by "keyboard".

- [ ] **Step 1:** Write `updater.test.ts` (DELAY shortened as in `install_test.py`): full install from
  `SMK-37 Pro_016` → steps in order start, check…, loader, write 0…100, reboot, done `SMK-37 Pro_956`, `bad === 0`,
  `upgrades === 2`; resume when the device starts as `ota-SMK-37 Pro_900` (one upgrade, done); model refusal
  (`FM-1_015` vs an SMK package → `model`, nothing sent but the handshake); foreign loader `ota-SMK-37 Pro_016` →
  `foreign`; unplug during check (`unplugAfter: 2`) → `lost` "nothing was written"; unplug during write
  (`unplugAfter: 6`) → `lost` "run the install again to finish"; stall → `stopped`; `badAddr` past the image →
  `badreq`; loader never appears → `noloader`; device returns `SMK-37 Pro_955` → `mismatch`. `pairOutput`:
  `("X0X FM-1 1", ["X0X FM-1 2"])` → `"X0X FM-1 2"`; `("A 1", ["A 2", "A 3", "B"])` → null;
  `("X", ["Y"])` → `"Y"`. `find` skips an `extraPorts` entry `"Some Synth"` unless `searchAll` (then it is probed and
  times out, and the keyboard is still found). After the check, the loader under `"Felucca Update"` is found by
  `waitFor` although the first port `"X0X FM-1"` vanished.
- [ ] **Step 2:** `npm test` → FAIL. **Step 3:** Implement the three modules. **Step 4:** `npm test` → PASS.
- [ ] **Step 5:** Commit `feat(web): updater port of fm1_install.py, tested against a simulated keyboard`.

### Task 5: Catalog generator

**Files:**
- Create: `web/scripts/make-catalog.ts`
- Test: `web/test/make-catalog.test.ts`

**Interfaces:**
- Consumes: `validateCatalog`, `loadPackage`, `productOf` (Tasks 2–3).
- Produces: CLI `node scripts/make-catalog.ts --base catalog/base.json --version 1.0 --dir <dir with fwsc + SHA256SUMS> --out dist/catalog.json --files dist/firmware/knobi-six/1.0 --source firmware/knobi-six/1.0/knobi-six-1.0-src.tar.gz [--date YYYY-MM-DD]`
  and exported `makeCatalog(opts): Promise<Catalog>` (pure apart from file reads; the CLI copies the files).
  Must run under plain `node` 24 (type stripping: no enums, no parameter properties, `.ts` import extensions,
  `allowImportingTsExtensions` in tsconfig).
- Refusals (each an `Error` naming the file): name not `^knobi-six-<version>(-elite|-mke)?\.fwsc$`; any `*-local*` or
  `*cpu1*` file in `--dir`; missing or mismatching `SHA256SUMS` line; `loadPackage` failure; identity not
  `<model>_956`; model without an instrument; version with `-` (prerelease). Other firmwares' complete entries in
  `base.json` are copied through after their committed files' hashes are checked (`web/public/firmware/<id>/<version>/`).

- [ ] **Step 1:** Write tests in a temp dir with `makePackage` packages for `SMK-37 Pro_956`, `SMK-37 Elite_956`,
  `MKE-P37_956`: the catalog lists three packages with correct sha256/size/instrument and validates; each refusal
  above is triggered by one variant (e.g. adding `knobi-six-local.fwsc`, a wrong hash, `SMK-37 Pro_900`, version `1.0-rc1`).
- [ ] **Step 2:** FAIL. **Step 3:** Implement. **Step 4:** PASS, and `node scripts/make-catalog.ts --help` prints usage.
- [ ] **Step 5:** Commit `feat(web): catalog generator with release-only refusals`.

### Task 6: Web MIDI backend and the page

**Files:**
- Create: `web/src/midi/webmidi.ts`, `web/src/ui/app.ts`, `web/src/ui/view.ts`, `web/src/ui/style.css`,
  `web/src/main.ts`; modify `web/index.html`
- Test: `web/test/ui.test.ts` (`// @vitest-environment jsdom`), `web/test/dist.test.ts`

**Interfaces:**
- Consumes: everything above.
- Produces: `requestBackend(): Promise<MidiBackend>` (throws `InstallError("denied", …)` when `requestMIDIAccess` is
  missing or rejects); `WebMidiBackend` (opens ports, 300 ms pause after open, `lost` from the input's `state`
  becoming `disconnected` or vanishing from `access.inputs`); `startApp(root: HTMLElement, deps: {backend: () => Promise<MidiBackend>, fetch: typeof fetch, catalogUrl: string, confirm: (text: string) => Promise<boolean>})`.
  `main.ts` passes the Web MIDI backend, or in dev with `?mock` a `FakeKeyboard` loaded by dynamic import inside
  `if (import.meta.env.DEV)` so it is tree-shaken from `dist/`.
- Page: the spec's layout and copy (header "KNOBI-FLASH", "unofficial · not made by M-VAVE or Sequential"; steps
  Connect / Firmware / Before / Install; the step row Connect, Verify, Update mode, Write, Restart, Done; log with Copy
  log; backup checkbox "I have a verified full flash backup" linking to
  `https://github.com/jhortale/knobi-six/blob/phase1/docs/BACKUP.md` (spec: the public README also explains it); each
  package links its `source`). Style after `docs/design/standalone-ui/panel-style-v2.html`: dark panel, font stack
  `"Arial Rounded MT Bold","Helvetica Neue",Arial,sans-serif`, colours as CSS custom properties; works at 375 px.
  Verify on Install: fetch the package (`cache: "no-cache"`), SHA-256 vs catalog (mismatch → code `hash`, "the site
  was just updated: reload the page"), `loadPackage`, `sameKeyboard`, own-loader check; then `confirm`, then
  `install`; `beforeunload` set while installing.

- [ ] **Step 1:** Write `ui.test.ts` with a stub fetch serving a test catalog and `makePackage` packages, and a
  `FakeKeyboard` backend: Install disabled until the checkbox; a full install shows Done and `SMK-37 Pro_956`;
  `backend` rejecting → the denied message containing "Chrome or Edge"; package bytes not matching the catalog hash →
  the reload message and the fake device's `upgrades === 0`; unplug during the write → the resume message, then a new
  Connect shows the loader identity with the Pro package preselected and labelled "finish the install", and Install
  reaches Done. `dist.test.ts` (skipped if `dist/` missing): no file in `dist/` contains `FakeKeyboard` or
  `type="file"`.
- [ ] **Step 2:** FAIL. **Step 3:** Implement. **Step 4:** `npm test` and `npm run build` → PASS; then `npm test`
  again so `dist.test.ts` runs on the fresh build.
- [ ] **Step 5:** Look at it: `npm run dev`, open `http://localhost:5173/?mock` in the browser pane, run a mock
  install at desktop and 375 px widths, screenshot both. Fix what looks wrong.
- [ ] **Step 6:** Commit `feat(web): the knobi-flash page over Web MIDI`.

### Task 7: CI, deploy, licensing and docs

**Files:**
- Modify: `.github/workflows/build.yml` (new job `web`), `.github/workflows/release.yml` (artifact + `site` job),
  `LICENSING.md`, `README.md` (one line pointing to the installer), `CLAUDE.md` (the `web/` commands)
- Create: `web/site/README.md` (the public knobi-flash README), `web/site/LICENSE` (copy of `LICENSE`),
  `docs/WEB_INSTALLER.md` (João's one-time setup and the release flow)

**Interfaces:** none new.

- [ ] **Step 1:** `build.yml`: add `pull_request` to `on`, and job `web` (ubuntu-24.04, `actions/setup-node@v4` node 24,
  `setup-python`): `python3 tests/web_fixtures.py --check web/test/fixtures/python.json`, then in `web/`: `npm ci`,
  `npm run typecheck`, `npm run build`, `npm test`. The existing jobs keep their triggers (add
  `if: github.event_name != 'pull_request'` to them only if they cannot run on PRs; check).
- [ ] **Step 2:** `release.yml`: the firmware job also uploads `build/SHA256SUMS` (tags) and a
  `knobi-six-<version>-src.tar.gz` made with `git archive --format=tar.gz --prefix=knobi-six-<version>/ $GITHUB_REF_NAME`
  after asserting `git ls-files firmware/gen/factory_bank.h` is empty. New job `site`: `needs: firmware`,
  `if: github.ref_type == 'tag' && !contains(github.ref_name, '-')` (or `workflow_dispatch` with input `tag`),
  `environment: knobi-flash`; downloads the artifact, `npm ci && npm run build` in `web/`, runs `make-catalog.ts`
  with the source path, copies packages, `SHA256SUMS`, the source tarball and `web/site/*` into `web/dist/`; fails if
  `find web/dist -name '*-local*' -o -name '*cpu1*'` prints anything; pushes with
  `peaceiris/actions-gh-pages@v4` (`deploy_key: ${{ secrets.KNOBI_FLASH_DEPLOY_KEY }}`,
  `external_repository: jhortale/knobi-flash`, `publish_branch: gh-pages`, `force_orphan: true`).
- [ ] **Step 3:** Validate the workflows: `python3 -c "import yaml,sys; [yaml.safe_load(open(f)) for f in sys.argv[1:]]" .github/workflows/*.yml`
  (and `actionlint` if available).
- [ ] **Step 4:** `LICENSING.md`: a row for `web/` (the protocol port: Leo Kuroshita / M. Geisler, GPL-3.0-only; the
  page, catalog and generator: KNOBI-SIX, GPL-3.0-only). `web/site/README.md`: what the site is, unofficial notice,
  licence, credits from `LICENSING.md`, where each release's source archive is. `docs/WEB_INSTALLER.md`: create the
  public repo `jhortale/knobi-flash` with Pages on `gh-pages`; deploy key (write) on it, private half as secret
  `KNOBI_FLASH_DEPLOY_KEY` in knobi-six; environment `knobi-flash` with João as required reviewer; first deploy only
  after João's own install from `npm run dev` on localhost with a release package; how to add an instrument or a
  firmware to `base.json`.
- [ ] **Step 5:** Full local check: `python3 tests/install_test.py` (unchanged, still passes), `cd web && npm ci &&
  npm run typecheck && npm run build && npm test`.
- [ ] **Step 6:** Commit `ci(web): test knobi-flash on every push, deploy behind approval on release tags` and
  `docs: knobi-flash setup and licensing`.
