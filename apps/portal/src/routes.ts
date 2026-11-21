// SPDX-License-Identifier: GPL-3.0-only
// Where we are: the page for location.pathname under the site's base (Ruling 7: "/midi-chlorian/" when published),
// and the links between pages. "/" is the home page, "/<synth>/" a synth's page, anything else NotFound.
export type Route = { page: "home" } | { page: "synth"; id: string } | { page: "notfound" };

/** synth ids are the synths/<id>/ folder names */
const SYNTH_ID = /^[a-z0-9-]+$/;

export function routeOf(pathname: string, base: string): Route {
  const b = base.endsWith("/") ? base : `${base}/`;
  if (pathname === b.slice(0, -1)) return { page: "home" };
  if (!pathname.startsWith(b)) return { page: "notfound" };
  const rest = pathname.slice(b.length).replace(/(^|\/)index\.html$/, "$1");
  if (rest === "") return { page: "home" };
  const m = /^([^/]+)\/?$/.exec(rest);
  return m && SYNTH_ID.test(m[1]!) ? { page: "synth", id: m[1]! } : { page: "notfound" };
}

/** the site's base and what every page link keeps (the dev server's "?mock") */
export interface Site {
  base: string;
  query?: string;
}

export const homeHref = (s: Site) => `${s.base}${s.query ?? ""}`;
export const synthHref = (s: Site, id: string) => `${s.base}${id}/${s.query ?? ""}`;
/** a file of the site: content.json's urls are relative to the site root */
export const fileHref = (s: Site, path: string) => `${s.base}${path}`;
