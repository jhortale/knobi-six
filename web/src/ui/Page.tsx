// SPDX-License-Identifier: GPL-3.0-only
// The page: one column, the steps in order (docs/superpowers/specs/2026-11-20-web-installer-design.md), built from
// shadcn/ui components in the KNOBI-SIX panel style (src/index.css). The behaviour is in installer.ts.
import { useSyncExternalStore, type ReactNode } from "react";
import { Alert, AlertDescription, AlertTitle } from "@/components/ui/alert";
import {
  AlertDialog, AlertDialogAction, AlertDialogCancel, AlertDialogContent, AlertDialogDescription, AlertDialogFooter,
  AlertDialogHeader, AlertDialogTitle,
} from "@/components/ui/alert-dialog";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Card, CardContent } from "@/components/ui/card";
import { Checkbox } from "@/components/ui/checkbox";
import { Collapsible, CollapsibleContent, CollapsibleTrigger } from "@/components/ui/collapsible";
import { Label } from "@/components/ui/label";
import { Progress } from "@/components/ui/progress";
import { RadioGroup, RadioGroupItem } from "@/components/ui/radio-group";
import { cn } from "@/lib/utils";
import { STEPS, type Installer, type InstallerState } from "./installer.ts";

/** docs/BACKUP.md, published with the site (vite.config.ts) */
export const BACKUP_URL = "backup.txt";

const kb = (n: number) => `${Math.round(n / 1024)} KB`;

function Section({ n, title, children }: { n: number; title: string; children: ReactNode }) {
  return (
    <section className="border-b border-border py-4 last:border-b-0">
      <h2 className="mb-3 flex items-center gap-2 text-[15px] tracking-[0.08em] text-muted-foreground uppercase">
        <span className="grid size-[22px] place-items-center rounded-full bg-primary text-xs text-primary-foreground">{n}</span>
        {title}
      </h2>
      {children}
    </section>
  );
}

function Device({ s }: { s: InstallerState }) {
  const d = s.device;
  let body: ReactNode;
  switch (d.kind) {
    case "none": body = "Plug the keyboard in with a USB data cable."; break;
    case "searching": body = "Looking for the keyboard…"; break;
    case "notfound":
      body = "No keyboard answered. Check the USB data cable and that no other app is using it" +
        (d.inputs.length ? ` (MIDI inputs: ${d.inputs.join(", ")})` : " (no MIDI inputs)") + ".";
      break;
    case "foreign":
      body = <>✗ <b>{d.text}</b> on "{d.port}": the update mode of another firmware. Finish that update with its own
        updater, then connect again.</>;
      break;
    case "found":
      body = <>✓ <b>{d.text}</b> on "{d.port}" ({d.loader
        ? "in update mode: the last install did not finish; Install finishes it" : "running"})</>;
      break;
  }
  return <p id="device" className="mt-3" aria-live="polite">{body}</p>;
}

function Packages({ s, installer }: { s: InstallerState; installer: Installer }) {
  if (s.device.kind !== "found") {
    return <div id="packages"><p className="text-sm text-muted-foreground">Connect first.</p></div>;
  }
  if (!s.offers.length) {
    return <div id="packages"><p className="text-warn">No package for {s.device.text} in this installer.</p></div>;
  }
  return (
    <div id="packages" className="grid gap-2">
      <RadioGroup value={s.selected === null ? "" : String(s.selected)} onValueChange={(v) => installer.select(Number(v))}
        disabled={s.busy} className="gap-2">
        {s.offers.map((o, i) => (
          <Label key={`${o.firmware.id}-${o.release.version}-${o.pkg.file}`} htmlFor={`pkg-${i}`}
            className={cn("flex cursor-pointer items-start gap-3 rounded-md border bg-secondary p-3 font-normal leading-snug",
              s.selected === i && "border-primary", !o.exact && "opacity-85")}>
            <RadioGroupItem id={`pkg-${i}`} value={String(i)} className="mt-0.5" />
            <span className="min-w-0 [overflow-wrap:anywhere]">
              <b>{o.firmware.name} {o.release.version}</b> for {o.instrument.name}
              {s.resume && o.exact && i === 0 && <Badge className="ml-2 bg-led text-white">finish the install</Badge>}
              <br />
              <small className="text-muted-foreground">
                {o.pkg.identity} · {kb(o.pkg.size)} · sha256 {o.pkg.sha256.slice(0, 12)}…
                {o.release.source && <> · <a href={o.release.source}>source</a></>}
              </small>
              {!o.exact && <small className="block text-warn">For the {o.instrument.model}: the same keyboard under
                another name. Only if you know you need it.</small>}
            </span>
          </Label>
        ))}
      </RadioGroup>
      {s.offers[0].firmware.notes.length > 0 &&
        <p className="text-sm text-muted-foreground">{s.offers[0].firmware.notes.join(" ")}</p>}
    </div>
  );
}

function Steps({ s }: { s: InstallerState }) {
  return (
    <ol className="my-4 flex flex-wrap gap-x-3.5 gap-y-1.5 text-sm">
      {STEPS.map(([id, label]) => {
        const st = s.steps[id];
        return (
          <li key={id} data-step={id} data-state={st || "idle"}
            className={cn("flex items-center gap-1.5", st ? "text-foreground" : "text-muted-foreground")}>
            <span aria-hidden className={cn("size-2 rounded-full border border-black bg-led-off",
              st === "now" && "bg-led shadow-[0_0_6px_var(--led)]", st === "ok" && "bg-ok")} />
            {label}
          </li>
        );
      })}
    </ol>
  );
}

export function Page({ installer }: { installer: Installer }) {
  const s = useSyncExternalStore(installer.subscribe, installer.getState);
  const canInstall = !s.busy && s.device.kind === "found" && s.selected !== null && s.backup;
  const copyLog = () => void navigator.clipboard?.writeText(s.log.join("\n")).catch(() => {});

  return (
    <div className="mx-auto max-w-[760px] px-4 pt-6 pb-12">
      <header className="pt-4" style={{ borderTop: "10px solid", borderImage: "var(--wood) 1" }}>
        <h1 className="text-[28px] tracking-[0.12em]">KNOBI-FLASH</h1>
        <p className="mt-1 mb-5 text-sm text-muted-foreground">
          Firmware installer for the M-VAVE SMK-37 family. Unofficial · not made by M-VAVE or Sequential.
        </p>
      </header>

      <Card className="gap-0 rounded border-[6px] border-black py-1 shadow-none">
        <CardContent className="px-5">
          <Section n={1} title="Connect">
            <div className="flex flex-wrap items-center gap-3">
              <Button id="connect" variant="secondary" className="border" disabled={s.busy}
                onClick={() => void installer.connect()}>Connect keyboard</Button>
              <div className="flex items-center gap-2">
                <Checkbox id="search-all" checked={s.searchAll} disabled={s.busy}
                  onCheckedChange={(v) => installer.setSearchAll(v === true)} />
                <Label htmlFor="search-all" className="font-normal text-muted-foreground">search all MIDI ports</Label>
              </div>
            </div>
            <Device s={s} />
          </Section>

          <Section n={2} title="Firmware">
            <Packages s={s} installer={installer} />
          </Section>

          <Section n={3} title="Before you install">
            <div className="flex items-start gap-2">
              <Checkbox id="backup" checked={s.backup} className="mt-1"
                onCheckedChange={(v) => installer.setBackup(v === true)} />
              <Label htmlFor="backup" className="block text-base leading-normal font-normal">
                I have a verified full flash backup of this keyboard (<a href={BACKUP_URL} target="_blank"
                  rel="noopener">how</a>).
              </Label>
            </div>
            <p className="mt-2 text-sm text-muted-foreground">
              The backup is the only copy of your keyboard's calibration and pairings. A browser cannot make it. Keep the
              USB cable in and this tab open until Done.
            </p>
          </Section>

          <Section n={4} title="Install">
            <Button id="install" disabled={!canInstall} onClick={() => void installer.install()}>Install</Button>
            <Steps s={s} />
            <Progress value={s.pct} aria-label="write progress"
              className="h-1.5 rounded-sm border border-black bg-[#120606] [&>[data-slot=progress-indicator]]:bg-led" />
            {s.error && (
              <Alert id="error" variant="destructive" className="mt-3 border-l-[3px] border-led bg-led-off text-foreground">
                <AlertTitle>Stopped</AlertTitle>
                <AlertDescription className="text-foreground [overflow-wrap:anywhere]">
                  <p>{s.error.message}</p>
                  {s.error.next && <p>{s.error.next}</p>}
                </AlertDescription>
              </Alert>
            )}
            {s.result && <p id="result" className="mt-3 text-ok" aria-live="polite">
              Done: the keyboard runs <b>{s.result}</b>.</p>}
            <Collapsible className="mt-3 text-sm text-muted-foreground">
              <div className="flex items-center gap-2">
                <CollapsibleTrigger className="cursor-pointer hover:text-foreground">▸ Log</CollapsibleTrigger>
                <Button id="copy-log" variant="secondary" size="sm" className="h-6 border px-2 text-xs"
                  onClick={copyLog}>Copy log</Button>
              </div>
              <CollapsibleContent forceMount className="data-[state=closed]:hidden">
                <pre id="log" className="mt-2 max-h-56 overflow-auto rounded-sm bg-black p-2 text-xs whitespace-pre-wrap">
                  {s.log.join("\n")}
                </pre>
              </CollapsibleContent>
            </Collapsible>
          </Section>
        </CardContent>
      </Card>

      <footer className="mt-5 text-xs text-muted-foreground">
        KNOBI-SIX and knobi-flash are free software (GPL-3.0-only). Each release links its complete source. M-VAVE,
        SMK-37, MKE-P37 and OB-6 belong to their owners.
      </footer>

      <AlertDialog open={s.confirm !== null} onOpenChange={(open) => { if (!open) installer.answerConfirm(false); }}>
        <AlertDialogContent>
          <AlertDialogHeader>
            <AlertDialogTitle>Install?</AlertDialogTitle>
            <AlertDialogDescription className="whitespace-pre-line">{s.confirm?.text}</AlertDialogDescription>
          </AlertDialogHeader>
          <AlertDialogFooter>
            <AlertDialogCancel onClick={() => installer.answerConfirm(false)}>Cancel</AlertDialogCancel>
            <AlertDialogAction onClick={() => installer.answerConfirm(true)}>Install</AlertDialogAction>
          </AlertDialogFooter>
        </AlertDialogContent>
      </AlertDialog>
    </div>
  );
}
