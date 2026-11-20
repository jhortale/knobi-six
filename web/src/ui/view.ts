// SPDX-License-Identifier: GPL-3.0-only
// The page's markup: one column, the steps in order (docs/superpowers/specs/2026-11-20-web-installer-design.md).

export const BACKUP_URL = "https://github.com/jhortale/knobi-six/blob/phase1/docs/BACKUP.md";
export const STEPS = [
  ["connect", "Connect"], ["verify", "Verify"], ["update", "Update mode"], ["write", "Write"], ["restart", "Restart"],
  ["done", "Done"],
] as const;
export type StepId = (typeof STEPS)[number][0];

export function h<K extends keyof HTMLElementTagNameMap>(tag: K, attrs: Record<string, string> = {},
  ...kids: (Node | string | null | undefined)[]): HTMLElementTagNameMap[K] {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
  for (const c of kids) if (c != null) e.append(c);
  return e;
}

export interface View {
  connect: HTMLButtonElement;
  searchAll: HTMLInputElement;
  device: HTMLElement;
  packages: HTMLElement;
  backup: HTMLInputElement;
  install: HTMLButtonElement;
  steps: Record<StepId, HTMLElement>;
  bar: HTMLElement;
  barFill: HTMLElement;
  error: HTMLElement;
  result: HTMLElement;
  log: HTMLElement;
  copyLog: HTMLButtonElement;
}

export function render(root: HTMLElement): View {
  const connect = h("button", { id: "connect", type: "button", class: "btn" }, "Connect keyboard");
  const searchAll = h("input", { id: "search-all", type: "checkbox" });
  const device = h("p", { id: "device", class: "status", "aria-live": "polite" }, "Plug the keyboard in with a USB data cable.");
  const packages = h("div", { id: "packages", class: "packages" }, h("p", { class: "muted" }, "Connect first."));
  const backup = h("input", { id: "backup", type: "checkbox" });
  const install = h("button", { id: "install", type: "button", class: "btn primary", disabled: "" }, "Install");
  const steps = {} as Record<StepId, HTMLElement>;
  const stepList = h("ol", { class: "steps" });
  for (const [id, label] of STEPS) stepList.append((steps[id] = h("li", { "data-step": id }, label)));
  const barFill = h("div", { class: "fill" });
  const bar = h("div", { class: "bar", role: "progressbar", "aria-valuemin": "0", "aria-valuemax": "100", "aria-valuenow": "0" }, barFill);
  const error = h("div", { id: "error", class: "error", role: "alert" });
  const result = h("p", { id: "result", class: "result", "aria-live": "polite" });
  const log = h("pre", { id: "log", class: "log" });
  const copyLog = h("button", { id: "copy-log", type: "button", class: "btn small" }, "Copy log");

  const section = (n: string, title: string, ...kids: Node[]) =>
    h("section", { class: "step" }, h("h2", {}, h("span", { class: "num" }, n), title), ...kids);

  root.replaceChildren(
    h("header", { class: "top" },
      h("h1", {}, "KNOBI-FLASH"),
      h("p", { class: "tag" }, "Firmware installer for the M-VAVE SMK-37 family. Unofficial · not made by M-VAVE or Sequential."),
    ),
    h("main", { class: "panel" },
      section("1", "Connect",
        h("div", { class: "row" }, connect,
          h("label", { class: "check small" }, searchAll, "search all MIDI ports")),
        device),
      section("2", "Firmware", packages),
      section("3", "Before you install",
        h("label", { class: "check" }, backup,
          h("span", {}, "I have a verified full flash backup of this keyboard (",
            h("a", { href: BACKUP_URL, target: "_blank", rel: "noopener" }, "how"), ").")),
        h("p", { class: "muted" },
          "The backup is the only copy of your keyboard's calibration and pairings. A browser cannot make it. " +
          "Keep the USB cable in and this tab open until Done.")),
      section("4", "Install",
        h("div", { class: "row" }, install),
        stepList, bar, error, result,
        h("details", { class: "logbox" }, h("summary", {}, "Log ", copyLog), log)),
    ),
    h("footer", { class: "foot" },
      "KNOBI-SIX and knobi-flash are free software (GPL-3.0-only). Each release links its complete source. ",
      "M-VAVE, SMK-37, MKE-P37 and OB-6 belong to their owners."),
  );
  return { connect, searchAll, device, packages, backup, install, steps, bar, barFill, error, result, log, copyLog };
}
