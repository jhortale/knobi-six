// SPDX-License-Identifier: GPL-3.0-only
// The installer page: the app's own chrome (header, breadcrumb, footer) around the shared installer panel
// (@knobi/installer-ui, which also draws the living field behind it).
import { Logo } from "@knobi/ui/components/logo";
import { InstallerPanel, type Installer } from "@knobi/installer-ui";

/** docs/BACKUP.md, published with the site (vite.config.ts) */
export const BACKUP_URL = "backup.txt";

export function Page({ installer }: { installer: Installer }) {
  return (
    <div className="relative z-[1] mx-auto max-w-[760px] px-4 pt-6 pb-12">
      <header className="pb-5">
        {/* the brand is the page's title: the logo, then where we are */}
        <div className="flex items-center justify-between gap-4">
          <h1 className="m-0">
            <a href="./" className="text-foreground hover:no-underline"><Logo size={52} /></a>
            <span className="sr-only">: install KNOBI-SIX (knobi-flash)</span>
          </h1>
          <span className="hidden text-right text-xs text-muted-foreground sm:block">
            Firmwares for MIDI keyboards,<br />installed from the browser</span>
        </div>
        <nav aria-label="Breadcrumb" className="mt-5">
          <ol className="flex flex-wrap items-center gap-x-2 text-sm tracking-[0.06em] text-muted-foreground">
            <li><a href="./" className="text-muted-foreground hover:text-foreground">midi·chlorian</a></li>
            <li aria-hidden="true" className="opacity-50">/</li>
            <li>KNOBI-SIX</li>
            <li aria-hidden="true" className="opacity-50">/</li>
            <li aria-current="page" className="text-foreground">Install</li>
          </ol>
        </nav>
        <p className="mt-1.5 text-sm text-muted-foreground">
          Firmware installer for the M-VAVE SMK-37 family (knobi-flash). Unofficial · not made by M-VAVE or Sequential.
        </p>
      </header>

      <InstallerPanel installer={installer} backupUrl={BACKUP_URL} />

      <footer className="mt-5 text-xs text-muted-foreground">
        KNOBI-SIX and knobi-flash are free software (GPL-3.0-only). Each release links its complete source. M-VAVE,
        SMK-37, MKE-P37 and OB-6 belong to their owners.
      </footer>
    </div>
  );
}
