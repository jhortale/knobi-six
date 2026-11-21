// SPDX-License-Identifier: GPL-3.0-only
// Anything that is not the home page or a synth's page (also GitHub Pages' 404.html).
import { useEffect } from "react";
import { homeHref, type Site } from "./routes.ts";

export function NotFound({ site }: { site: Site }) {
  useEffect(() => { document.title = "Not found · midi·chlorian"; }, []);
  return (
    <section data-page="notfound" className="py-10">
      <h1 className="mb-2 text-2xl">Page not found</h1>
      <p className="text-muted-foreground">There is nothing at this address.</p>
      <p className="mt-4"><a data-home href={homeHref(site)}>Back to midi·chlorian</a></p>
    </section>
  );
}
