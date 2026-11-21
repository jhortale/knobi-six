// SPDX-License-Identifier: GPL-3.0-only
// The site's data, written next to the app by the deploy (Ruling 8): content.json (what the pages show) and
// catalog.json (what the installer offers, and the keyboards' names).
import { validateCatalog, type Catalog } from "@knobi/catalog";
import type { PortalContent } from "@knobi/content";

export interface SiteData {
  content: PortalContent;
  catalog: Catalog;
}

async function json(fetcher: typeof fetch, url: string): Promise<unknown> {
  const r = await fetcher(url, { cache: "no-cache" });
  if (!r.ok) throw new Error(`${url}: HTTP ${r.status}`);
  return r.json();
}

export async function loadSite(base: string, fetcher: typeof fetch): Promise<SiteData> {
  const [content, catalog] = await Promise.all([json(fetcher, `${base}content.json`), json(fetcher, `${base}catalog.json`)]);
  const c = content as PortalContent;
  if (!Array.isArray(c?.synths)) throw new Error("content.json: no synths");
  return { content: c, catalog: validateCatalog(catalog) };
}

/** the keyboards' model names ("SMK-37 Pro"), in the catalog's order */
export function keyboardNames(catalog: Catalog, ids: string[]): string[] {
  return catalog.instruments.filter((i) => ids.includes(i.id)).map((i) => i.model);
}

/** an instrument's model name, or its id when the catalog does not know it */
export function keyboardName(catalog: Catalog, id: string): string {
  return catalog.instruments.find((i) => i.id === id)?.model ?? id;
}

/** an instrument's position in the catalog (unknown ones last) */
export function keyboardOrder(catalog: Catalog, id: string): number {
  const i = catalog.instruments.findIndex((x) => x.id === id);
  return i < 0 ? catalog.instruments.length : i;
}
