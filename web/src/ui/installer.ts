// SPDX-License-Identifier: GPL-3.0-only
// The page's behaviour, apart from how it is drawn: connect, offer the catalog's packages, check, install, report.
// A small store (state + subscribe) that the React page renders (App.tsx) and the tests drive.
import { familyRe, fetchCatalog, ownLoaderRe, packagesFor, portMatcher, type Offer } from "../catalog/load.ts";
import type { Catalog } from "../catalog/schema.ts";
import type { MidiBackend } from "@knobi/installer-core";
import { NO_MIDI } from "@knobi/web-midi";
import { InstallError, isLoader } from "@knobi/installer-core";
import type { Identity } from "@knobi/installer-core";
import { loadPackage } from "@knobi/installer-core";
import { Updater, type Step } from "@knobi/installer-core";

export const STEPS = [
  ["connect", "Connect"], ["verify", "Verify"], ["update", "Update mode"], ["write", "Write"], ["restart", "Restart"],
  ["done", "Done"],
] as const;
export type StepId = (typeof STEPS)[number][0];
export type StepState = "ok" | "now" | "";

export interface InstallerDeps {
  backend: () => Promise<MidiBackend>;
  fetch: typeof fetch;
  catalogUrl: string;
  /** asks before writing; without it the page shows its own dialog (state.confirm) */
  confirm?: (text: string) => Promise<boolean>;
}

export type DeviceView =
  | { kind: "none" }
  | { kind: "searching" }
  | { kind: "notfound"; inputs: string[] }
  | { kind: "foreign"; text: string; port: string }
  | { kind: "found"; text: string; port: string; loader: boolean };

export interface InstallerState {
  catalogLoaded: boolean;
  device: DeviceView;
  offers: Offer[];
  /** index into offers */
  selected: number | null;
  /** the device is in its update loader: the first exact offer finishes the install */
  resume: boolean;
  backup: boolean;
  searchAll: boolean;
  busy: boolean;
  steps: Record<StepId, StepState>;
  pct: number;
  error: { message: string; next?: string } | null;
  result: string | null;
  log: string[];
  confirm: { text: string } | null;
}

const NEXT_STEP: Partial<Record<string, string>> = {
  lost: "Plug the keyboard back in, press Connect and Install again: it stays in update mode until the install is done.",
  badreq: "Replug the USB cable, press Connect and Install again. If it happens again, Copy log (under Log) and report it.",
  stopped: "Replug the USB cable, press Connect and Install again.",
  noloader: "Replug the USB cable, press Connect and Install again.",
  noreturn: "Power the keyboard off and on, then press Connect to check what it runs.",
};

const noSteps = (): Record<StepId, StepState> =>
  Object.fromEntries(STEPS.map(([id]) => [id, ""])) as Record<StepId, StepState>;

async function sha256(b: Uint8Array): Promise<string> {
  const d = await crypto.subtle.digest("SHA-256", b as Uint8Array<ArrayBuffer>);
  return [...new Uint8Array(d)].map((x) => x.toString(16).padStart(2, "0")).join("");
}

export class Installer {
  private s: InstallerState = {
    catalogLoaded: false, device: { kind: "none" }, offers: [], selected: null, resume: false, backup: false,
    searchAll: false, busy: false, steps: noSteps(), pct: 0, error: null, result: null, log: [], confirm: null,
  };
  private listeners = new Set<() => void>();
  private catalog: Catalog | null = null;
  private backend: MidiBackend | null = null;
  private id: Identity | null = null;
  private answer: ((ok: boolean) => void) | null = null;
  private readonly t0 = performance.now();
  private readonly deps: InstallerDeps;
  readonly ready: Promise<void>;

  constructor(deps: InstallerDeps) {
    this.deps = deps;
    this.ready = fetchCatalog(deps.catalogUrl, deps.fetch).then(
      (c) => {
        this.catalog = c;
        this.set({ catalogLoaded: true });
        this.log(`catalog: ${c.firmwares.map((f) => `${f.name} ${f.releases.map((r) => r.version).join(", ") || "(none)"}`).join("; ")}`);
      },
      (e) => this.fail(new Error(`The firmware list could not be loaded (${(e as Error).message}). Reload the page.`)),
    );
  }

  // ------------------------------------------------------------------ store ---

  get state(): InstallerState {
    return this.s;
  }

  subscribe = (fn: () => void) => {
    this.listeners.add(fn);
    return () => void this.listeners.delete(fn);
  };

  getState = () => this.s;

  private set(patch: Partial<InstallerState>) {
    this.s = { ...this.s, ...patch };
    for (const fn of this.listeners) fn();
  }

  private log(msg: string) {
    this.set({ log: [...this.s.log, `${((performance.now() - this.t0) / 1000).toFixed(1).padStart(6)}s  ${msg}`] });
  }

  private mark(step: StepId, st: StepState) {
    this.set({ steps: { ...this.s.steps, [step]: st } });
  }

  private fail(e: unknown) {
    const err = e instanceof InstallError ? e : null;
    const message = err ? err.message : String((e as Error)?.message ?? e);
    // lost before anything was written (the check step): the keyboard restarts its own firmware, not update mode
    const next = err?.code === "lost" && err.message.includes("nothing was written")
      ? "Plug the keyboard back in (it still runs its firmware), press Connect and Install again."
      : err ? NEXT_STEP[err.code] : undefined;
    this.set({ error: { message, next } });
    this.log(`error${err ? ` (${err.code})` : ""}: ${message}`);
  }

  /** Install is possible: a device, a package, the backup confirmed, nothing running */
  get canInstall(): boolean {
    const s = this.s;
    return !s.busy && !!this.id && s.selected !== null && s.backup;
  }

  // -------------------------------------------------------------- controls ---

  setBackup = (backup: boolean) => this.set({ backup });
  setSearchAll = (searchAll: boolean) => this.set({ searchAll });
  select = (i: number) => this.set({ selected: i });

  /** the in-page confirm dialog's answer */
  answerConfirm = (ok: boolean) => {
    const a = this.answer;
    this.answer = null;
    this.set({ confirm: null });
    a?.(ok);
  };

  private ask(text: string): Promise<boolean> {
    if (this.deps.confirm) return this.deps.confirm(text);
    return new Promise((resolve) => {
      this.answer = resolve;
      this.set({ confirm: { text } });
    });
  }

  private updater(offer?: Offer): Updater {
    const c = this.catalog!;
    const inst = offer?.instrument ?? c.instruments[0];
    return new Updater(this.backend!, {
      ports: portMatcher(c), family: familyRe(c, inst.family), ownLoader: ownLoaderRe(c, inst.protocol),
      searchAll: this.s.searchAll,
    });
  }

  private showPackages() {
    if (!this.id) {
      this.set({ offers: [], selected: null, resume: false });
      return;
    }
    const offers = packagesFor(this.catalog!, this.id.text);
    this.set({ offers, selected: offers.length && offers[0].exact ? 0 : null, resume: isLoader(this.id) });
  }

  connect = async () => {
    await this.ready;
    if (!this.catalog || this.s.busy) return;
    this.set({ error: null, result: null });
    this.id = null;
    try {
      this.backend ??= await this.deps.backend();
    } catch {
      this.fail(new InstallError("denied", NO_MIDI));   // whatever the browser said: the guidance
      return;
    }
    this.set({ device: { kind: "searching" } });
    const dev = await this.updater().find();
    if (!dev) {
      const inputs = this.backend.inputNames();
      this.set({ device: { kind: "notfound", inputs } });
      this.log(`not found; inputs: ${inputs.join(", ") || "(none)"}`);
      this.mark("connect", "");
      this.showPackages();
      return;
    }
    dev.link.close();
    const c = this.catalog;
    if (isLoader(dev.id) && !Object.keys(c.protocols).some((p) => ownLoaderRe(c, p).test(dev.id.text))) {
      this.set({ device: { kind: "foreign", text: dev.id.text, port: dev.name } });
      this.log(`found ${dev.id.text} on "${dev.name}": another firmware's update loader`);
      this.showPackages();
      return;
    }
    this.id = dev.id;
    this.set({ device: { kind: "found", text: dev.id.text, port: dev.name, loader: isLoader(dev.id) } });
    this.log(`found ${dev.id.text} on "${dev.name}"`);
    this.mark("connect", "ok");
    this.showPackages();
  };

  private onUnload = (e: BeforeUnloadEvent) => {
    e.preventDefault();
    e.returnValue = "";
  };

  install = async () => {
    if (!this.canInstall || !this.catalog) return;
    const offer = this.s.offers[this.s.selected!];
    this.set({ busy: true, error: null, result: null, steps: { ...noSteps(), connect: "ok", verify: "now" }, pct: 0 });
    try {
      this.log(`download ${offer.pkg.file}`);
      const r = await this.deps.fetch(offer.pkg.file, { cache: "no-cache" });
      if (!r.ok) {
        throw new InstallError("hash", `The package could not be downloaded (HTTP ${r.status}): the site was probably ` +
          "just updated: reload the page (if it persists, wait a few minutes). Nothing was sent to the keyboard.");
      }
      const raw = new Uint8Array(await r.arrayBuffer());
      if ((await sha256(raw)) !== offer.pkg.sha256 || raw.length !== offer.pkg.size) {
        throw new InstallError("hash", "The package does not match this page's list: the site was just updated: " +
          "reload the page and try again. Nothing was sent to the keyboard.");
      }
      const { product, image } = loadPackage(raw);
      if (product !== offer.pkg.identity) {
        throw new InstallError("badpkg", `the package is ${product}, the list says ${offer.pkg.identity}; nothing was sent`);
      }
      this.log(`package ${product}: sha256, CRCs and loader checked`);
      const up = this.updater(offer);
      const dev = await up.find();
      if (!dev) throw new InstallError("notfound", "the keyboard no longer answers: check the cable and press Connect");
      const no = up.refusal(dev.id, product);
      if (no) {
        dev.link.close();
        throw no;
      }
      this.mark("verify", "ok");
      const ok = await this.ask(`Install ${offer.firmware.name} ${offer.release.version} (${product}) on the ` +
        `keyboard that reports ${dev.id.text}?\n\nKeep the USB cable in and do not unplug the keyboard or close this ` +
        "tab while it writes.");
      if (!ok) {
        dev.link.close();
        this.log("cancelled");
        this.set({ steps: { ...noSteps(), connect: "ok" } });
        return;
      }
      addEventListener("beforeunload", this.onUnload);
      const step = (s: Step) => {
        switch (s.k) {
          case "start": this.mark("update", "now"); this.log(`starting the update on ${s.id}`); break;
          case "check": break;
          case "loader": this.log("switching to update mode"); break;
          case "write":
            if (s.pct === 0) {
              this.set({ steps: { ...this.s.steps, update: "ok", write: "now" } });
              this.log("writing");
            }
            this.set({ pct: s.pct });
            if (s.pct === 100) { this.mark("write", "ok"); this.log("written"); }
            break;
          case "reboot": this.mark("restart", "now"); this.log("restarting"); break;
          case "done":
            this.set({ steps: { ...this.s.steps, restart: "ok", done: "ok" } });
            this.log(`done: the keyboard runs ${s.id}`);
            break;
        }
      };
      const now = await up.install(dev, image, product, step);
      this.id = { text: now, model: now.split("_")[0], version: parseInt(now.split("_")[1], 10) };
      this.set({ result: now, device: { kind: "found", text: now, port: dev.name, loader: false }, resume: false });
    } catch (e) {
      const steps = { ...this.s.steps };
      for (const k of Object.keys(steps) as StepId[]) if (steps[k] === "now") steps[k] = "";
      this.set({ steps });
      this.fail(e);
    } finally {
      removeEventListener("beforeunload", this.onUnload);
      this.set({ busy: false });
    }
  };
}
