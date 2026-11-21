// SPDX-License-Identifier: GPL-3.0-only
// The home page: the tagline, a card per synth (name, summary, current version, keyboards), a "coming" card, and how
// it works.
import { useEffect } from "react";
import type { Catalog } from "@knobi/catalog";
import type { PortalSynth } from "@knobi/content";
import { Badge } from "@knobi/ui/components/badge";
import { keyboardNames } from "./data.ts";
import { TAGLINE, UNOFFICIAL } from "./Layout.tsx";
import { fileHref, synthHref, type Site } from "./routes.ts";

const card = "block overflow-hidden rounded-lg border border-white/10 bg-black/40 text-foreground backdrop-blur-sm";

function SynthCard({ site, synth, catalog }: { site: Site; synth: PortalSynth; catalog: Catalog }) {
  const current = synth.releases.find((r) => r.current);
  return (
    <a href={synthHref(site, synth.id)}
      className={`${card} transition-colors hover:border-primary hover:no-underline focus-visible:border-primary`}>
      <article data-synth={synth.id}>
        {synth.image
          ? <img src={fileHref(site, synth.image)} alt="" className="aspect-[16/7] w-full object-cover" />
          : <div aria-hidden="true" className="aspect-[16/7] w-full"
              style={{ background: "radial-gradient(circle at 30% 40%, rgb(95 227 255 / 0.25), transparent 60%), " +
                "radial-gradient(circle at 75% 65%, rgb(157 140 255 / 0.22), transparent 55%), var(--wood)" }} />}
        <div className="p-4">
          <h2 className="m-0 text-lg tracking-[0.06em]">{synth.name}</h2>
          <p className="mt-1 text-sm text-muted-foreground">{synth.summary}</p>
          <div className="mt-3 flex flex-wrap items-center gap-2">
            {current
              ? <Badge data-version>{current.version}</Badge>
              : <Badge variant="outline">no release yet</Badge>}
            {synth.keyboards.length > 0 &&
              <span className="text-xs text-muted-foreground">{keyboardNames(catalog, synth.keyboards).join(" · ")}</span>}
          </div>
        </div>
      </article>
    </a>
  );
}

export function Home({ site, synths, catalog }: { site: Site; synths: PortalSynth[]; catalog: Catalog }) {
  useEffect(() => { document.title = "midi·chlorian"; }, []);
  return (
    <>
      <h1 className="m-0 text-xl">{TAGLINE}.</h1>
      <p className="mt-1.5 text-sm text-muted-foreground">{UNOFFICIAL}.</p>
      <div className="mt-6 grid gap-4 sm:grid-cols-2">
        {[...synths].sort((a, b) => a.order - b.order).map((s) =>
          <SynthCard key={s.id} site={site} synth={s} catalog={catalog} />)}
        <div data-coming className={`${card} p-4 opacity-60`}>
          <h2 className="m-0 text-lg tracking-[0.06em]">Next synth</h2>
          <p className="mt-1 text-sm text-muted-foreground">The next firmware appears here when its first release is published.</p>
          <div className="mt-3"><Badge variant="outline">coming</Badge></div>
        </div>
      </div>
      <section className="mt-8 text-sm text-muted-foreground">
        <h2 className="mb-1 text-[15px] tracking-[0.08em] uppercase">How it works</h2>
        <p>Pick a synth, plug your keyboard in with a USB data cable, press Install. It runs in Chrome or Edge (Web
          MIDI). Each release links its complete source (GPL-3.0).</p>
      </section>
    </>
  );
}
