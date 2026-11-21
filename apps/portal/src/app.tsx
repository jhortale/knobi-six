// SPDX-License-Identifier: GPL-3.0-only
// Starts the site: the page for the path (routes.ts), once the site's data (content.json, catalog.json) is loaded.
import { StrictMode, useEffect, useState } from "react";
import { createRoot } from "react-dom/client";
import { loadSite, type SiteData } from "./data.ts";
import { Home } from "./Home.tsx";
import { Layout } from "./Layout.tsx";
import { NotFound } from "./NotFound.tsx";
import { routeOf, type Site } from "./routes.ts";
import { SynthPage, type SynthDeps } from "./SynthPage.tsx";

export interface PortalDeps extends SynthDeps, Site {
  /** location.pathname */
  pathname: string;
}

type Load = { state: "loading" } | { state: "ready"; data: SiteData } | { state: "failed"; message: string };

function Portal({ deps }: { deps: PortalDeps }) {
  const route = routeOf(deps.pathname, deps.base);
  const site: Site = { base: deps.base, query: deps.query };
  const [load, setLoad] = useState<Load>({ state: "loading" });
  useEffect(() => {
    if (route.page === "notfound") return;
    loadSite(deps.base, deps.fetch).then(
      (data) => setLoad({ state: "ready", data }),
      (e) => setLoad({ state: "failed", message: (e as Error).message }));
  }, [route.page, deps]);

  if (route.page === "notfound") return <Layout site={site} crumb="Not found"><NotFound site={site} /></Layout>;
  if (load.state === "loading") {
    return <Layout site={site}><p className="text-muted-foreground" aria-live="polite">Loading…</p></Layout>;
  }
  if (load.state === "failed") {
    return (
      <Layout site={site}>
        <section data-page="error" role="alert" className="py-6">
          <p>The site's data could not be loaded ({load.message}). Reload the page.</p>
        </section>
      </Layout>
    );
  }
  const { content, catalog } = load.data;
  if (route.page === "home") {
    return <Layout site={site}><Home site={site} synths={content.synths} catalog={catalog} /></Layout>;
  }
  const synth = content.synths.find((s) => s.id === route.id);
  if (!synth) return <Layout site={site} crumb="Not found"><NotFound site={site} /></Layout>;
  return (
    <Layout site={site} crumb={synth.name} footer={synth.name}>
      <SynthPage site={site} synth={synth} catalog={catalog} deps={deps} />
    </Layout>
  );
}

export function startPortal(root: HTMLElement, deps: PortalDeps): void {
  createRoot(root).render(<StrictMode><Portal deps={deps} /></StrictMode>);
}
