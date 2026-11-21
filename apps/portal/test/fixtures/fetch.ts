// SPDX-License-Identifier: GPL-3.0-only
// A fetch that serves the fixture site under a base path (content.json, catalog.json, the made-up firmware files):
// the tests' and the dev server's ?mock page's stand-in for the deployed site. It records every URL it is asked for.
import content from "./content.json" with { type: "json" };
import catalog from "./catalog.json" with { type: "json" };
import { fixtureFile } from "./packages.ts";

export function fixtureFetch(base: string): { fetch: typeof fetch; requests: string[] } {
  const requests: string[] = [];
  const fetcher = (async (input: RequestInfo | URL) => {
    const url = String(input);
    requests.push(url);
    if (!url.startsWith(base)) return new Response("", { status: 404 });
    const path = url.slice(base.length);
    if (path === "content.json") return Response.json(content);
    if (path === "catalog.json") return Response.json(catalog);
    const file = fixtureFile(path);
    if (file === undefined) return new Response("", { status: 404 });
    return new Response(typeof file === "string" ? file : (file as Uint8Array<ArrayBuffer>));
  }) as typeof fetch;
  return { fetch: fetcher, requests };
}
