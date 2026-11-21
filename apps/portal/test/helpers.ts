// SPDX-License-Identifier: GPL-3.0-only
// Starts the portal in jsdom on a path, against the fixture site and a simulated keyboard.
import { FakeKeyboard, type FakeOpts } from "@knobi/device-sim";
import { DELAY, loadPackage } from "@knobi/installer-core";
import { startPortal } from "../src/app.tsx";
import { fixtureFetch } from "./fixtures/fetch.ts";
import { fixtureFile } from "./fixtures/packages.ts";

export const BASE = "/midi-chlorian/";

// quick timings for the simulated keyboard (as in @knobi/installer-ui's tests)
Object.assign(DELAY, { open: 0, start: 10, reply: 0, loader: 10, reboot: 10, retry: 20, poll: 50, hs: 100,
  idleCheck: 400, idleWrite: 400, waitLoader: 2000, waitReboot: 2000 });

export function open(pathname: string, opts: { keyboard?: Partial<FakeOpts>; fetch?: typeof fetch } = {}) {
  document.body.innerHTML = '<div id="app"></div>';
  const root = document.getElementById("app")!;
  const { image } = loadPackage(fixtureFile("firmware/knobi-six/1.0/knobi-six-1.0.fwsc") as Uint8Array);
  const dev = new FakeKeyboard({ image, ...opts.keyboard });
  const site = fixtureFetch(BASE);
  const confirms: string[] = [];
  startPortal(root, {
    base: BASE, pathname, fetch: opts.fetch ?? site.fetch, backend: async () => dev,
    confirm: async (t) => {
      confirms.push(t);
      return true;
    },
  });
  const $ = <T extends HTMLElement = HTMLElement>(sel: string) => root.querySelector<T>(sel);
  const $$ = <T extends HTMLElement = HTMLElement>(sel: string) => [...root.querySelectorAll<T>(sel)];
  return { root, dev, requests: site.requests, confirms, $, $$ };
}

export async function until(cond: () => boolean, ms = 4000) {
  const end = Date.now() + ms;
  while (!cond()) {
    if (Date.now() > end) throw new Error("timed out");
    await new Promise((r) => setTimeout(r, 10));
  }
}

export const text = (el: Element | null | undefined) => (el?.textContent ?? "").replace(/\s+/g, " ");
