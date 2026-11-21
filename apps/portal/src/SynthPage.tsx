// SPDX-License-Identifier: GPL-3.0-only
// A synth's page: its header and side box, "Install on my keyboard" (the shared installer, offering only the selected
// release: the current one, or an older one picked with its "Install <version>"), the downloads of that release, and
// the releases with their notes (the current one open, the older ones folded).
import { useEffect, useMemo, useRef, useState } from "react";
import type { Catalog } from "@knobi/catalog";
import type { PortalRelease, PortalSynth } from "@knobi/content";
import { Installer, InstallerPanel, type InstallerDeps } from "@knobi/installer-ui";
import { Badge } from "@knobi/ui/components/badge";
import { Button } from "@knobi/ui/components/button";
import { keyboardName, keyboardNames, keyboardOrder } from "./data.ts";
import { releaseFetch } from "./release.ts";
import { fileHref, type Site } from "./routes.ts";

export type SynthDeps = Pick<InstallerDeps, "backend" | "fetch" | "confirm">;

const panel = "rounded-lg border border-white/10 bg-black/45 p-4 backdrop-blur-sm sm:p-5";
const h2 = "mb-3 text-[15px] tracking-[0.08em] text-muted-foreground uppercase";

/** 8212 -> "8 KB", 2.1 MB, 41 B */
export function size(n: number): string {
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${Math.round(n / 1024)} KB`;
  return `${(n / (1024 * 1024)).toFixed(1)} MB`;
}

const dirOf = (url: string) => url.slice(0, url.lastIndexOf("/") + 1);

function Downloads({ site, synth, release, catalog }: { site: Site; synth: PortalSynth; release: PortalRelease; catalog: Catalog }) {
  const keyboards = release.builds.filter((b) => b.kind === "keyboard")
    .sort((a, b) => keyboardOrder(catalog, a.instrument) - keyboardOrder(catalog, b.instrument));
  const desktop = release.builds.filter((b) => b.kind === "desktop");
  const row = (forWhat: string, file: string, url: string, bytes: number, sha: string) => (
    <tr key={file} className="border-t border-white/10 align-top">
      <td className="py-2 pr-3">{forWhat}</td>
      <td className="py-2 pr-3 [overflow-wrap:anywhere]"><a href={fileHref(site, url)}>{file}</a></td>
      <td className="py-2 pr-3 whitespace-nowrap">{size(bytes)}</td>
      <td className="py-2 font-mono text-xs break-all text-muted-foreground">{sha}</td>
    </tr>
  );
  return (
    <section data-downloads className={panel}>
      <h2 className={h2}>Download to my computer · {synth.name} {release.version}</h2>
      <div className="overflow-x-auto">
        <table className="w-full min-w-[520px] text-left text-sm">
          <thead className="text-xs text-muted-foreground">
            <tr><th className="pb-2 font-normal">For</th><th className="pb-2 font-normal">File</th>
              <th className="pb-2 font-normal">Size</th><th className="pb-2 font-normal">SHA-256</th></tr>
          </thead>
          <tbody>
            {keyboards.map((b) => row(keyboardName(catalog, b.instrument), b.file, b.url, b.size, b.sha256))}
            {row("Source (GPL)", release.source.file, release.source.url, release.source.size, release.source.sha256)}
            {desktop.map((b) => row(`${b.os === "mac" ? "Mac" : "Windows"} · ${b.format.toUpperCase()}`, b.file, b.url, b.size, b.sha256))}
            {desktop.length === 0 && (
              <tr className="border-t border-white/10"><td className="py-2 pr-3">Mac · Windows</td>
                <td colSpan={3} className="py-2 text-muted-foreground">VST3 / AU: not released yet</td></tr>
            )}
          </tbody>
        </table>
      </div>
      <p className="mt-3 text-sm text-muted-foreground">
        All files: <a data-sums href={fileHref(site, `${dirOf(release.source.url)}SHA256SUMS`)}>SHA256SUMS</a>. Built by
        CI from the tag <code>{release.tag}</code>.
      </p>
    </section>
  );
}

function Releases({ releases, selected, onInstall }: {
  releases: PortalRelease[]; selected: string; onInstall: (version: string) => void;
}) {
  return (
    <section className={panel}>
      <h2 className={h2}>Releases</h2>
      {releases.map((r) => (
        <details key={r.version} data-release={r.version} open={r.current}
          className="border-t border-white/10 py-3 first-of-type:border-t-0">
          <summary className="flex cursor-pointer items-center justify-between gap-3">
            <span><b>{r.version}</b>{r.current && <Badge className="ml-2">current</Badge>}</span>
            <span className="text-sm text-muted-foreground">{r.date}</span>
          </summary>
          {/* notesHtml: rendered from Markdown at build time, raw HTML escaped (@knobi/content) */}
          <div className="notes mt-2 text-sm [&_a]:underline [&_ul]:list-disc [&_ul]:pl-5"
            dangerouslySetInnerHTML={{ __html: r.notesHtml }} />
          {r.version !== selected && (
            <Button data-install-release variant="secondary" size="sm" className="mt-2 border"
              onClick={() => onInstall(r.version)}>Install {r.version}</Button>
          )}
        </details>
      ))}
    </section>
  );
}

export function SynthPage({ site, synth, catalog, deps }: {
  site: Site; synth: PortalSynth; catalog: Catalog; deps: SynthDeps;
}) {
  const current = synth.releases.find((r) => r.current) ?? synth.releases[0];
  const [version, setVersion] = useState(current?.version);
  const release = synth.releases.find((r) => r.version === version);
  const install = useRef<HTMLElement>(null);
  const license = catalog.firmwares.find((f) => f.id === synth.id)?.license ?? "GPL-3.0-only";
  useEffect(() => { document.title = `${synth.name} · midi·chlorian`; }, [synth.name]);

  // a new installer for each release picked: it offers that release only (Ruling 9)
  const installer = useMemo(() => {
    if (!version) return null;
    const catalogUrl = `${site.base}catalog.json`;
    return new Installer({ ...deps, catalogUrl, fetch: releaseFetch(deps.fetch, site.base, catalogUrl, synth.id, version) });
  }, [version, synth.id, site.base, deps]);

  const pick = (v: string) => {
    setVersion(v);
    install.current?.scrollIntoView?.({ behavior: "smooth", block: "start" });
  };

  return (
    <div className="grid grid-cols-[minmax(0,1fr)] gap-5">
      <div className="grid grid-cols-[minmax(0,1fr)] gap-5 md:grid-cols-[minmax(0,1fr)_300px]">
        <div>
          <h1 className="m-0 text-3xl tracking-[0.06em]">{synth.name}</h1>
          <p className="mt-1 text-muted-foreground">{synth.summary}</p>
          {/* descriptionHtml: rendered from Markdown at build time, raw HTML escaped (@knobi/content) */}
          <div data-description className="mt-3 [&_a]:underline [&_p]:mb-2"
            dangerouslySetInnerHTML={{ __html: synth.descriptionHtml }} />
        </div>
        <aside data-side className={`${panel} text-sm`}>
          <dl className="grid grid-cols-[auto_1fr] gap-x-3 gap-y-2">
            <dt className="text-muted-foreground">Current release</dt>
            <dd>{current ? <><b>{current.version}</b> · {current.date}</> : "none yet"}</dd>
            <dt className="text-muted-foreground">Keyboards</dt>
            <dd>{keyboardNames(catalog, synth.keyboards).join(" · ") || "none yet"}</dd>
            <dt className="text-muted-foreground">Desktop</dt>
            <dd><Badge variant="outline">VST3 / AU coming</Badge></dd>
            <dt className="text-muted-foreground">Licence</dt>
            <dd>{license}{current && <> · <a data-source href={fileHref(site, current.source.url)}>source</a></>}</dd>
          </dl>
        </aside>
      </div>

      <section ref={install} className="scroll-mt-4">
        <h2 data-install-heading className="mb-2 text-lg tracking-[0.06em]">Install on my keyboard</h2>
        {release && installer ? (
          <>
            <p data-selected-release className="mb-3 text-sm text-muted-foreground">
              {synth.name} {release.version}
              {release.current ? " (the current release)" : <>
                , an older release ·{" "}
                <button data-use-current type="button" className="cursor-pointer text-primary underline"
                  onClick={() => current && pick(current.version)}>install the current {current?.version} instead</button>
              </>}
            </p>
            <InstallerPanel key={release.version} installer={installer} backupUrl={fileHref(site, "backup.txt")} />
          </>
        ) : <p className="text-muted-foreground">No release yet.</p>}
      </section>

      {release && <Downloads site={site} synth={synth} release={release} catalog={catalog} />}
      {synth.releases.length > 0 && <Releases releases={synth.releases} selected={version!} onInstall={pick} />}
    </div>
  );
}
