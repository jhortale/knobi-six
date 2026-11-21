// SPDX-License-Identifier: GPL-3.0-only
// The dev server's ?mock page: the fixture site (test/fixtures: two made-up KNOBI-SIX releases) and a simulated
// SMK-37 Pro (stock firmware), so the site can be clicked through without a keyboard or deployed data. Links keep
// ?mock. In the console: knobiMock.unplug(), knobiMock.boot(identity, port).
import { FakeKeyboard } from "@knobi/device-sim";
import { loadPackage } from "@knobi/installer-core";
import type { PortalDeps } from "../app.tsx";
import { fixtureFetch } from "../../test/fixtures/fetch.ts";
import { fixtureFile } from "../../test/fixtures/packages.ts";

export function mockDeps(deps: PortalDeps): PortalDeps {
  const { image } = loadPackage(fixtureFile("firmware/knobi-six/1.0/knobi-six-1.0.fwsc") as Uint8Array);
  const reads: [number, number][] = [];
  for (let a = 0; a < image.length; a += 512) reads.push([a, Math.min(512, image.length - a)]);
  const dev = new FakeKeyboard({ image, name: "X0X FM-1", loaderReads: reads, rebootMs: 1500 });
  (globalThis as Record<string, unknown>).knobiMock = dev;
  return { ...deps, query: "?mock", backend: async () => dev, fetch: fixtureFetch(deps.base).fetch };
}
