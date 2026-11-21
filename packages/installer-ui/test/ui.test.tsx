// @vitest-environment jsdom
// SPDX-License-Identifier: GPL-3.0-only
// The page against a simulated keyboard: the backup gate, a full install, permission denied, a stale catalog, and
// an install cut off during the write, then finished.
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { beforeAll, describe, expect, it } from "vitest";
import { act } from "@testing-library/react";
import { createRoot } from "react-dom/client";
import { Installer, InstallerPanel, type InstallerDeps } from "../src/index.ts";
import { FakeKeyboard, type FakeOpts } from "@knobi/device-sim";
import { DELAY } from "@knobi/installer-core";
import { InstallError } from "@knobi/installer-core";
import { loadPackage } from "@knobi/installer-core";
import { makePackage } from "@knobi/device-sim";
import { mergeCatalog, type Catalog } from "@knobi/catalog";

const base = mergeCatalog(
  JSON.parse(readFileSync(`${import.meta.dirname}/../../catalog/instruments.json`, "utf8")),
  [JSON.parse(readFileSync(`${import.meta.dirname}/../../../synths/knobi-six/catalog.json`, "utf8"))]);
const pkg = makePackage({ product: "SMK-37 Pro_956" });
const { image } = loadPackage(pkg);
const sha = (b: Uint8Array) => createHash("sha256").update(b).digest("hex");

function catalog(hash = sha(pkg)): Catalog {
  const c = structuredClone(base);
  c.firmwares[0].releases = [{ version: "1.0", date: "2026-11-20", source: "firmware/knobi-six/1.0/src.tar.gz", packages: [
    { instrument: "smk37-pro", file: "firmware/knobi-six/1.0/knobi-six-1.0.fwsc", identity: "SMK-37 Pro_956", sha256: hash, size: pkg.length },
  ] }];
  return c;
}

(globalThis as { IS_REACT_ACT_ENVIRONMENT?: boolean }).IS_REACT_ACT_ENVIRONMENT = true;

beforeAll(() => {
  Object.assign(DELAY, { open: 0, start: 10, reply: 0, loader: 10, reboot: 10, retry: 20, poll: 50, hs: 100,
    idleCheck: 400, idleWrite: 400, waitLoader: 2000, waitReboot: 2000 });
});

function setup(opts: Partial<FakeOpts> = {}, cat = catalog(), backendErr?: Error, pkgStatus = 200, pageDialog = false) {
  document.body.innerHTML = '<div id="app"></div>';
  const root = document.getElementById("app")!;
  const dev = new FakeKeyboard({ image, ...opts });
  const confirms: string[] = [];
  const fetcher = (async (url: string) => {
    if (url === "catalog.json") return new Response(JSON.stringify(cat), { status: 200 });
    if (url.endsWith(".fwsc")) return pkgStatus === 200 ? new Response(pkg as Uint8Array<ArrayBuffer>, { status: 200 }) : new Response("", { status: pkgStatus });
    return new Response("", { status: 404 });
  }) as typeof fetch;
  const installer = new Installer({
    backend: async () => {
      if (backendErr) throw backendErr;
      return dev;
    },
    fetch: fetcher, catalogUrl: "catalog.json",
    confirm: pageDialog ? undefined : async (t) => {
      confirms.push(t);
      return true;
    },
  } satisfies InstallerDeps);
  act(() => { createRoot(root).render(<InstallerPanel installer={installer} />); });
  const app = { ready: installer.ready, connect: installer.connect, install: installer.install, installer };
  const $ = <T extends HTMLElement>(sel: string) => root.querySelector<T>(sel)!;
  return { root, dev, app, confirms, $ };
}

const until = async (cond: () => boolean, ms = 4000) => {
  const end = Date.now() + ms;
  while (!cond()) {
    if (Date.now() > end) throw new Error("timed out");
    await new Promise((r) => setTimeout(r, 10));
  }
};

describe("the page", () => {
  it("Install stays disabled until the backup box is ticked", async () => {
    const { app, $ } = setup();
    await act(() => app.ready);
    await act(() => app.connect());
    expect($("#device").textContent).toContain("SMK-37 Pro_016");
    expect($<HTMLAnchorElement>("#packages a").getAttribute("href")).toBe("firmware/knobi-six/1.0/src.tar.gz");
    expect($("#packages").textContent).not.toContain(",");
    expect($<HTMLButtonElement>("#install").disabled).toBe(true);
    act(() => $("#backup").click());
    expect($<HTMLButtonElement>("#install").disabled).toBe(false);
  });

  it("a full install reaches Done with SMK-37 Pro_956", async () => {
    const { app, dev, confirms, $ } = setup();
    await act(() => app.ready);
    await act(() => app.connect());
    act(() => $("#backup").click());
    await act(() => app.install());
    expect($("#result").textContent).toContain("SMK-37 Pro_956");
    expect($('[data-step="done"]').getAttribute("data-state")).toBe("ok");
    expect(confirms[0]).toContain("SMK-37 Pro_016");
    expect(confirms[0]).toContain("do not unplug");
    expect(dev.bad).toBe(0);
    expect($("#log").textContent).toContain("done");
  });

  it("no MIDI access: says to use Chrome or Edge", async () => {
    const { app, $ } = setup({}, catalog(), new InstallError("denied", "no Web MIDI"));
    await act(() => app.ready);
    await act(() => app.connect());
    expect($("#error").textContent).toContain("Chrome or Edge");
  });

  it("a package that does not match the catalog: reload, nothing sent", async () => {
    const { app, dev, $ } = setup({}, catalog("0".repeat(64)));
    await act(() => app.ready);
    await act(() => app.connect());
    act(() => $("#backup").click());
    await act(() => app.install());
    expect($("#error").textContent).toContain("reload the page");
    expect(dev.upgrades).toBe(0);
  });

  it("unplugged during the write: the resume message, then Connect + Install finish it", async () => {
    const { app, dev, $ } = setup({ unplugAfter: 6 });
    await act(() => app.ready);
    await act(() => app.connect());
    act(() => $("#backup").click());
    await act(() => app.install());
    expect($("#error").textContent).toContain("run the install again to finish");
    dev.opts.unplugAfter = undefined;
    dev.boot("ota-SMK-37 Pro_900", "Felucca Update");   // plugged back in: still in update mode
    await act(() => app.connect());
    expect($("#device").textContent).toContain("update mode");
    expect($("#packages").textContent).toContain("finish the install");
    expect($('#packages [role="radio"]').getAttribute("aria-checked")).toBe("true");
    await act(() => app.install());
    await until(() => ($("#result").textContent ?? "").includes("SMK-37 Pro_956"));
  });

  it("an FM-1 or another keyboard: no package offered", async () => {
    const { app, $ } = setup({ identity: "FM-1_015", name: "FM-1" });
    await act(() => app.ready);
    await act(() => app.connect());
    expect($("#packages").textContent).toContain("No package");
    act(() => $("#backup").click());
    expect($<HTMLButtonElement>("#install").disabled).toBe(true);
  });

  it("the package is gone (a newer release replaced the site): reload, nothing sent", async () => {
    const { app, dev, $ } = setup({}, catalog(), undefined, 404);
    await act(() => app.ready);
    await act(() => app.connect());
    act(() => $("#backup").click());
    await act(() => app.install());
    expect($("#error").textContent).toContain("reload the page");
    expect(dev.upgrades).toBe(0);
  });

  it("another firmware's update loader: said so at Connect, nothing offered", async () => {
    const { app, $ } = setup({ identity: "ota-SMK-37 Pro_016", name: "SMK-37 Pro" });
    await act(() => app.ready);
    await act(() => app.connect());
    expect($("#device").textContent).toContain("another firmware");
    expect($("#device").textContent).not.toContain("Install finishes it");
    act(() => $("#backup").click());
    expect($<HTMLButtonElement>("#install").disabled).toBe(true);
  });

  it("the keyboard changed since Connect: refused before the confirm dialog", async () => {
    const { app, dev, confirms, $ } = setup();
    await act(() => app.ready);
    await act(() => app.connect());
    act(() => $("#backup").click());
    dev.boot("ota-SMK-37 Pro_016", "SMK-37 Pro");
    await act(() => app.install());
    expect(confirms).toHaveLength(0);
    expect($("#error").textContent).toContain("another firmware");
    expect(dev.upgrades).toBe(0);
  });

  it("the backup instructions are on the site itself, not in the private repo", () => {
    const { $ } = setup();
    const href = $<HTMLAnchorElement>('label[for="backup"] a').getAttribute("href")!;
    expect(href).not.toMatch(/^https?:/);
    expect(href).toBe("backup.txt");
  });
});

describe("next steps after a failure", () => {
  it("unplugged during the check: nothing was written, no 'update mode' hint", async () => {
    const { app, $ } = setup({ unplugAfter: 2 });
    await act(() => app.ready);
    await act(() => app.connect());
    act(() => $("#backup").click());
    await act(() => app.install());
    const t = $("#error").textContent!;
    expect(t).toContain("nothing was written");
    expect(t).not.toContain("stays in update mode");
    expect(t).toContain("press Connect");
  });
  it("the device asks outside the package: says what to do", async () => {
    const { app, $ } = setup({ identity: "ota-SMK-37 Pro_900", name: "Felucca Update", badAddr: image.length });
    await act(() => app.ready);
    await act(() => app.connect());
    act(() => $("#backup").click());
    await act(() => app.install());
    const t = $("#error").textContent!;
    expect(t).toContain("outside the package");
    expect(t).toContain("Install again");
    expect(t).toContain("Copy log");
  });
});

describe("the page's own confirm dialog", () => {
  const button = (name: string) =>
    [...document.querySelectorAll<HTMLButtonElement>('[role="alertdialog"] button')].find((b) => b.textContent === name)!;
  const startInstall = async (opts: Partial<FakeOpts> = {}) => {
    const t = setup(opts, catalog(), undefined, 200, true);
    await act(() => t.app.ready);
    await act(() => t.app.connect());
    act(() => t.$("#backup").click());
    let done!: Promise<void>;
    act(() => { done = t.app.install(); });
    await until(() => document.querySelector('[role="alertdialog"]') !== null);
    return { ...t, done };
  };
  it("asks before writing, then installs on Install", async () => {
    const { dev, done, $ } = await startInstall();
    expect(document.querySelector('[role="alertdialog"]')!.textContent).toContain("do not unplug");
    expect(dev.upgrades).toBe(0);
    await act(async () => { button("Install").click(); await done; });
    expect($("#result").textContent).toContain("SMK-37 Pro_956");
  });
  it("Cancel writes nothing", async () => {
    const { dev, done, $ } = await startInstall();
    await act(async () => { button("Cancel").click(); await done; });
    expect(dev.upgrades).toBe(0);
    expect($("#log").textContent).toContain("cancelled");
    expect(document.querySelector('[role="alertdialog"]')).toBeNull();
  });
});
