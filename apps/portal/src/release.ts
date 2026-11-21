// SPDX-License-Identifier: GPL-3.0-only
// Ruling 9: on a synth page the installer offers only that synth's selected release. The Installer (unchanged,
// @knobi/installer-ui) fetches the catalog through this fetch, which serves the real catalog.json cut down to that
// one firmware and release. Its paths stay relative to the site (validateCatalog requires it): this fetch resolves
// them under the site's base, as the page's <base href> (vite.config.ts) does for the panel's links, since the page
// itself is at /<synth>/.
import { validateCatalog } from "@knobi/catalog";

const absolute = /^([a-z][a-z0-9+.-]*:|\/)/i;

export function releaseFetch(fetcher: typeof fetch, base: string, catalogUrl: string, firmwareId: string, version: string): typeof fetch {
  return (async (input: RequestInfo | URL, init?: RequestInit) => {
    const url = typeof input === "string" && !absolute.test(input) ? `${base}${input}` : input;
    if (String(url) !== catalogUrl) return fetcher(url, init);
    const r = await fetcher(url, init);
    if (!r.ok) return r;
    const c = validateCatalog(await r.json());
    c.firmwares = c.firmwares.filter((f) => f.id === firmwareId)
      .map((f) => ({ ...f, releases: f.releases.filter((x) => x.version === version) }));
    return Response.json(c);
  }) as typeof fetch;
}
