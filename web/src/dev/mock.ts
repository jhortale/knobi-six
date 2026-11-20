// SPDX-License-Identifier: GPL-3.0-only
// The dev server's ?mock page: a simulated SMK-37 Pro (stock firmware) and a made-up KNOBI-SIX package, so the page
// can be clicked through without a keyboard. In the console: knobiMock.unplug(), knobiMock.boot(identity, port).
import base from "../../catalog/base.json";
import type { AppDeps } from "../ui/app.tsx";
import type { Catalog } from "../catalog/schema.ts";
import { loadPackage } from "@knobi/installer-core";
import { FakeKeyboard } from "./fakeDevice.ts";
import { makePackage } from "./makePackage.ts";

const FILE = "firmware/knobi-six/0.0-mock/knobi-six-0.0-mock.fwsc";

export function mockDeps(deps: AppDeps): AppDeps {
  const raw = makePackage({ product: "SMK-37 Pro_956", size: 0x10000 });
  const { image } = loadPackage(raw);
  const reads: [number, number][] = [];
  for (let a = 0; a < image.length; a += 512) reads.push([a, Math.min(512, image.length - a)]);
  const dev = new FakeKeyboard({ image, name: "X0X FM-1", loaderReads: reads, rebootMs: 1500 });
  (globalThis as Record<string, unknown>).knobiMock = dev;
  const ready = (async () => {
    const d = new Uint8Array(await crypto.subtle.digest("SHA-256", raw as Uint8Array<ArrayBuffer>));
    const cat = structuredClone(base) as Catalog;
    cat.firmwares[0].releases = [{ version: "0.0-mock", date: "2026-11-20", source: "#", packages: [{
      instrument: "smk37-pro", file: FILE, identity: "SMK-37 Pro_956", size: raw.length,
      sha256: [...d].map((x) => x.toString(16).padStart(2, "0")).join(""),
    }] }];
    return cat;
  })();
  return {
    ...deps,
    backend: async () => dev,
    fetch: (async (url: string) => {
      if (url === deps.catalogUrl) return new Response(JSON.stringify(await ready));
      if (url === FILE) return new Response(raw as Uint8Array<ArrayBuffer>);
      return new Response("", { status: 404 });
    }) as typeof fetch,
  };
}
