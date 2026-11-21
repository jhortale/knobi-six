// SPDX-License-Identifier: GPL-3.0-only
// midi·chlorian's chrome around every page: the logo (home), the unofficial notice, a breadcrumb, the footer.
import type { ReactNode } from "react";
import { Logo } from "@knobi/ui/components/logo";
import { fileHref, homeHref, type Site } from "./routes.ts";

export const TAGLINE = "Firmwares for MIDI keyboards, installed from the browser";
export const UNOFFICIAL = "Unofficial · not made by M-VAVE or Sequential";

export function Layout({ site, crumb, footer, children }: {
  site: Site;
  /** where we are, after "midi·chlorian" (none on the home page) */
  crumb?: string;
  /** who the footer names as free software, before midi-chlorian */
  footer?: string;
  children: ReactNode;
}) {
  return (
    <div className="relative z-[1] mx-auto max-w-[960px] px-4 pt-6 pb-12">
      <header className="pb-5">
        <div className="flex items-center justify-between gap-4">
          <a href={homeHref(site)} className="text-foreground hover:no-underline">
            <Logo size={52} /><span className="sr-only">midi·chlorian, home</span>
          </a>
          {crumb && <span className="hidden text-right text-xs text-muted-foreground sm:block">
            {TAGLINE.replace(", ", ", ")}</span>}
        </div>
        {crumb && (
          <nav aria-label="Breadcrumb" className="mt-5">
            <ol className="flex flex-wrap items-center gap-x-2 text-sm tracking-[0.06em] text-muted-foreground">
              <li><a href={homeHref(site)} className="text-muted-foreground hover:text-foreground">midi·chlorian</a></li>
              <li aria-hidden="true" className="opacity-50">/</li>
              <li aria-current="page" className="text-foreground">{crumb}</li>
            </ol>
          </nav>
        )}
        {crumb && <p className="mt-1.5 text-sm text-muted-foreground">{UNOFFICIAL}.</p>}
      </header>

      <main>{children}</main>

      <footer className="mt-8 text-xs text-muted-foreground">
        {footer ? `${footer} and midi-chlorian are` : "midi-chlorian and its synths are"} free software (GPL-3.0-only).
        Each release links its complete source; the source of this site: <a data-source href={fileHref(site, "source/")}>Source (GPL-3.0)</a>.
        M-VAVE, SMK-37, MKE-P37 and OB-6 belong to their owners.
      </footer>
    </div>
  );
}
