// SPDX-License-Identifier: GPL-3.0-only
// The page's behaviour: connect, offer the catalog's packages, check, install, report.
import { familyRe, fetchCatalog, ownLoaderRe, packagesFor, portMatcher, type Offer } from "../catalog/load.ts";
import type { Catalog } from "../catalog/schema.ts";
import type { MidiBackend } from "../midi/backend.ts";
import { NO_MIDI } from "../midi/webmidi.ts";
import { InstallError, isLoader } from "../protocol/identity.ts";
import type { Identity } from "../protocol/messages.ts";
import { loadPackage } from "../protocol/package.ts";
import { Updater, type Step } from "../protocol/updater.ts";
import { h, render, type StepId, type View } from "./view.ts";

export interface AppDeps {
  backend: () => Promise<MidiBackend>;
  fetch: typeof fetch;
  catalogUrl: string;
  confirm: (text: string) => Promise<boolean>;
}

export interface App {
  /** the catalog is loaded (or failed to load) */
  ready: Promise<void>;
  connect(): Promise<void>;
  install(): Promise<void>;
}

const NEXT_STEP: Partial<Record<string, string>> = {
  lost: "Plug the keyboard back in, press Connect and Install again: it stays in update mode until the install is done.",
  badreq: "Replug the USB cable, press Connect and Install again. If it happens again, Copy log (under Log) and report it.",
  stopped: "Replug the USB cable, press Connect and Install again.",
  noloader: "Replug the USB cable, press Connect and Install again.",
  noreturn: "Power the keyboard off and on, then press Connect to check what it runs.",
};

async function sha256(b: Uint8Array): Promise<string> {
  const d = await crypto.subtle.digest("SHA-256", b as Uint8Array<ArrayBuffer>);
  return [...new Uint8Array(d)].map((x) => x.toString(16).padStart(2, "0")).join("");
}

const kb = (n: number) => `${Math.round(n / 1024)} KB`;

export function startApp(root: HTMLElement, deps: AppDeps): App {
  const v: View = render(root);
  let catalog: Catalog | null = null;
  let backend: MidiBackend | null = null;
  let id: Identity | null = null;
  let offers: Offer[] = [];
  let selected: Offer | null = null;
  let busy = false;
  const t0 = performance.now();

  const log = (msg: string) => {
    v.log.textContent += `${((performance.now() - t0) / 1000).toFixed(1).padStart(6)}s  ${msg}\n`;
  };
  const fail = (e: unknown) => {
    const err = e instanceof InstallError ? e : null;
    const msg = err ? err.message : String((e as Error)?.message ?? e);
    // lost before anything was written (the check step): the keyboard restarts its own firmware, not update mode
    const next = err?.code === "lost" && err.message.includes("nothing was written")
      ? "Plug the keyboard back in (it still runs its firmware), press Connect and Install again."
      : err ? NEXT_STEP[err.code] : undefined;
    v.error.replaceChildren(h("strong", {}, "Stopped: "), msg, ...(next ? [h("br"), next] : []));
    log(`error${err ? ` (${err.code})` : ""}: ${msg}`);
  };
  const mark = (s: StepId, state: "ok" | "now" | "") => {
    v.steps[s].className = state;
  };
  const resetSteps = () => {
    for (const s of Object.keys(v.steps) as StepId[]) mark(s, "");
    setBar(0);
  };
  const setBar = (pct: number) => {
    v.barFill.style.width = `${pct}%`;
    v.bar.setAttribute("aria-valuenow", String(pct));
  };
  const refresh = () => {
    v.install.disabled = busy || !id || !selected || !v.backup.checked;
    v.connect.disabled = busy;
  };

  const ready = fetchCatalog(deps.catalogUrl, deps.fetch).then(
    (c) => {
      catalog = c;
      log(`catalog: ${c.firmwares.map((f) => `${f.name} ${f.releases.map((r) => r.version).join(", ") || "(none)"}`).join("; ")}`);
    },
    (e) => fail(new Error(`The firmware list could not be loaded (${(e as Error).message}). Reload the page.`)),
  );

  function updater(offer?: Offer): Updater {
    const c = catalog!;
    const inst = offer?.instrument ?? c.instruments[0];
    return new Updater(backend!, {
      ports: portMatcher(c), family: familyRe(c, inst.family), ownLoader: ownLoaderRe(c, inst.protocol),
      searchAll: v.searchAll.checked,
    });
  }

  function showPackages() {
    v.packages.replaceChildren();
    selected = null;
    if (!id) return;
    offers = packagesFor(catalog!, id.text);
    if (!offers.length) {
      v.packages.append(h("p", { class: "warn" }, `No package for ${id.text} in this installer.`));
      return;
    }
    const resume = isLoader(id);
    offers.forEach((o, i) => {
      const radio = h("input", { type: "radio", name: "pkg", value: String(i) });
      radio.addEventListener("change", () => {
        selected = o;
        refresh();
      });
      const label = h("label", { class: `pkg${o.exact ? "" : " other"}` }, radio,
        h("span", {},
          h("b", {}, `${o.firmware.name} ${o.release.version}`), ` for ${o.instrument.name}`,
          resume && o.exact && i === 0 ? h("em", {}, " · finish the install") : null,
          h("br"),
          h("small", {}, `${o.pkg.identity} · ${kb(o.pkg.size)} · sha256 ${o.pkg.sha256.slice(0, 12)}…`,
            o.release.source ? h("span", {}, " · ", h("a", { href: o.release.source }, "source")) : null),
          o.exact ? null : h("small", { class: "warn" }, h("br"),
            `For the ${o.instrument.model}: the same keyboard under another name. Only if you know you need it.`)));
      v.packages.append(label);
      if (i === 0 && o.exact) {
        radio.checked = true;
        selected = o;
      }
    });
    if (offers[0].firmware.notes.length) v.packages.append(h("p", { class: "muted" }, offers[0].firmware.notes.join(" ")));
  }

  async function connect() {
    await ready;
    if (!catalog || busy) return;
    v.error.replaceChildren();
    v.result.textContent = "";
    id = null;
    try {
      backend ??= await deps.backend();
    } catch (e) {
      fail(new InstallError("denied", NO_MIDI));   // whatever the browser said: the guidance
      return;
    }
    v.device.textContent = "Looking for the keyboard…";
    const dev = await updater().find();
    if (!dev) {
      const names = backend.inputNames();
      v.device.textContent = "No keyboard answered. Check the USB data cable and that no other app is using it" +
        (names.length ? ` (MIDI inputs: ${names.join(", ")})` : " (no MIDI inputs)") + ".";
      log(`not found; inputs: ${names.join(", ") || "(none)"}`);
      mark("connect", "");
      showPackages();
      refresh();
      return;
    }
    dev.link.close();
    const c = catalog;
    if (isLoader(dev.id) && !Object.keys(c.protocols).some((p) => ownLoaderRe(c, p).test(dev.id.text))) {
      v.device.replaceChildren("✗ ", h("b", {}, dev.id.text), ` on "${dev.name}": the update mode of another ` +
        "firmware. Finish that update with its own updater, then connect again.");
      log(`found ${dev.id.text} on "${dev.name}": another firmware's update loader`);
      showPackages();
      refresh();
      return;
    }
    id = dev.id;
    const mode = isLoader(dev.id)
      ? "in update mode: the last install did not finish; Install finishes it"
      : "running";
    v.device.replaceChildren("✓ ", h("b", {}, dev.id.text), ` on "${dev.name}" (${mode})`);
    log(`found ${dev.id.text} on "${dev.name}"`);
    mark("connect", "ok");
    showPackages();
    refresh();
  }

  const onUnload = (e: BeforeUnloadEvent) => {
    e.preventDefault();
    e.returnValue = "";
  };

  async function install() {
    if (!selected || !id || !v.backup.checked || busy || !catalog) return;
    const offer = selected;
    busy = true;
    refresh();
    v.error.replaceChildren();
    v.result.textContent = "";
    resetSteps();
    mark("connect", "ok");
    mark("verify", "now");
    try {
      log(`download ${offer.pkg.file}`);
      const r = await deps.fetch(offer.pkg.file, { cache: "no-cache" });
      if (!r.ok) {
        throw new InstallError("hash", `The package could not be downloaded (HTTP ${r.status}): the site was probably ` +
          "just updated: reload the page (if it persists, wait a few minutes). Nothing was sent to the keyboard.");
      }
      const raw = new Uint8Array(await r.arrayBuffer());
      if ((await sha256(raw)) !== offer.pkg.sha256 || raw.length !== offer.pkg.size) {
        throw new InstallError("hash", "The package does not match this page's list: the site was just updated: reload the page and " +
          "try again. Nothing was sent to the keyboard.");
      }
      const { product, image } = loadPackage(raw);
      if (product !== offer.pkg.identity) {
        throw new InstallError("badpkg", `the package is ${product}, the list says ${offer.pkg.identity}; nothing was sent`);
      }
      log(`package ${product}: sha256, CRCs and loader checked`);
      const up = updater(offer);
      const dev = await up.find();
      if (!dev) throw new InstallError("notfound", "the keyboard no longer answers: check the cable and press Connect");
      const no = up.refusal(dev.id, product);
      if (no) {
        dev.link.close();
        throw no;
      }
      mark("verify", "ok");
      const ok = await deps.confirm(`Install ${offer.firmware.name} ${offer.release.version} (${product}) on the ` +
        `keyboard that reports ${dev.id.text}?\n\nKeep the USB cable in and do not unplug the keyboard or close this ` +
        "tab while it writes.");
      if (!ok) {
        dev.link.close();
        log("cancelled");
        resetSteps();
        mark("connect", "ok");
        return;
      }
      addEventListener("beforeunload", onUnload);
      const step = (s: Step) => {
        switch (s.k) {
          case "start": mark("update", "now"); log(`starting the update on ${s.id}`); break;
          case "check": break;
          case "loader": log("switching to update mode"); break;
          case "write":
            if (s.pct === 0) { mark("update", "ok"); mark("write", "now"); log("writing"); }
            setBar(s.pct);
            if (s.pct === 100) { mark("write", "ok"); log("written"); }
            break;
          case "reboot": mark("restart", "now"); log("restarting"); break;
          case "done": mark("restart", "ok"); mark("done", "ok"); log(`done: the keyboard runs ${s.id}`); break;
        }
      };
      const now = await up.install(dev, image, product, step);
      id = { text: now, model: now.split("_")[0], version: parseInt(now.split("_")[1], 10) };
      v.result.replaceChildren("Done: the keyboard runs ", h("b", {}, now), ".");
      v.device.replaceChildren("✓ ", h("b", {}, now), " (running)");
    } catch (e) {
      for (const s of Object.keys(v.steps) as StepId[]) if (v.steps[s].className === "now") mark(s, "");
      fail(e);
    } finally {
      removeEventListener("beforeunload", onUnload);
      busy = false;
      refresh();
    }
  }

  v.connect.addEventListener("click", () => void connect());
  v.install.addEventListener("click", () => void install());
  v.backup.addEventListener("change", refresh);
  v.copyLog.addEventListener("click", (e) => {
    e.preventDefault();
    void navigator.clipboard?.writeText(v.log.textContent ?? "").catch(() => {});
  });
  return { ready, connect, install };
}
