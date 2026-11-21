// @vitest-environment jsdom
// SPDX-License-Identifier: GPL-3.0-only
// A synth page against the fixture site: the header and side box, the installer (only the selected release; a mock
// install reaches Done), the downloads table, the releases (current notes, older ones folded, "Install <version>").
import { describe, expect, it } from "vitest";
import content from "./fixtures/content.json" with { type: "json" };
import { BASE, open, text, until } from "./helpers.ts";

const PAGE = "/midi-chlorian/knobi-six/";
const v10 = content.synths[0]!.releases[0]!;

async function page(opts?: Parameters<typeof open>[1]) {
  const o = open(PAGE, opts);
  await until(() => !!o.$("#connect") && !!o.$("[data-downloads]"));
  return o;
}

describe("synth page: header", () => {
  it("shows the name, the description as HTML, the side box, the breadcrumb and the chrome", async () => {
    const { root, $ } = await page();
    expect(text($("h1"))).toContain("KNOBI-SIX");
    expect($("[data-description] p")).not.toBeNull();   // descriptionHtml inserted as HTML, not as text
    expect(text($("[data-description]"))).toContain("runs on the M-VAVE SMK-37 Pro");
    const side = text($("[data-side]"));
    expect(side).toContain("1.0");
    expect(side).toContain("2026-11-20");
    expect(side).toContain("SMK-37 Pro · SMK-37 Elite · MKE-P37");
    expect(side).toContain("VST3 / AU coming");
    expect(side).toContain("GPL-3.0-only");
    expect($<HTMLAnchorElement>("[data-side] a[data-source]")!.getAttribute("href"))
      .toBe("/midi-chlorian/firmware/knobi-six/1.0/knobi-six-1.0-src.tar.gz");
    const crumb = $("nav[aria-label=Breadcrumb]")!;
    expect(crumb.querySelector("a")!.getAttribute("href")).toBe(BASE);
    expect(text(crumb)).toContain("KNOBI-SIX");
    expect(text(root)).toContain("Unofficial · not made by M-VAVE or Sequential");
    expect(text($("footer"))).toContain("GPL-3.0-only");
    expect(root.innerHTML.toLowerCase()).not.toContain("knobi-flash");
    expect(document.title).toBe("KNOBI-SIX · midi·chlorian");
  });
});

describe("synth page: downloads", () => {
  it("has the three .fwsc files, the source, sizes and full SHA-256, SHA256SUMS and the tag", async () => {
    const { $, $$ } = await page();
    const rows = $$("[data-downloads] tbody tr");
    const fwsc = rows.filter((r) => /\.fwsc/.test(text(r)));
    expect(fwsc.map((r) => text(r.querySelector("a")))).toEqual(
      ["knobi-six-1.0.fwsc", "knobi-six-1.0-elite.fwsc", "knobi-six-1.0-mke.fwsc"]);
    expect(text(fwsc[0])).toContain("SMK-37 Pro");
    expect(fwsc[0]!.querySelector("a")!.getAttribute("href")).toBe("/midi-chlorian/firmware/knobi-six/1.0/knobi-six-1.0.fwsc");
    for (const b of v10.builds) {
      const row = fwsc.find((r) => text(r).includes(b.file))!;
      expect(text(row)).toContain("8 KB");
      expect(text(row)).toContain(b.sha256);
    }
    const src = rows.find((r) => text(r).includes("knobi-six-1.0-src.tar.gz"))!;
    expect(text(src)).toContain("Source");
    expect(text(src)).toContain(v10.source.sha256);
    expect(text(src)).toContain("41 B");
    expect(text(rows.at(-1))).toContain("not released yet");
    expect($<HTMLAnchorElement>("[data-downloads] a[data-sums]")!.getAttribute("href"))
      .toBe("/midi-chlorian/firmware/knobi-six/1.0/SHA256SUMS");
    expect(text($("[data-downloads]"))).toContain("Built by CI from the tag knobi-six-v1.0");
  });
});

describe("synth page: releases", () => {
  it("shows the current release's notes, the older ones folded", async () => {
    const { $ } = await page();
    const cur = $<HTMLDetailsElement>("[data-release='1.0']")!;
    expect(cur.open).toBe(true);
    expect(text(cur)).toContain("current");
    expect(cur.querySelector("li")).not.toBeNull();   // notesHtml as HTML
    expect(text(cur)).toContain("First public release");
    const old = $<HTMLDetailsElement>("[data-release='0.9']")!;
    expect(old.open).toBe(false);
    expect(text(old)).toContain("Beta for testers.");
    expect(text(old)).toContain("2026-11-14");
    expect(old.querySelector("button[data-install-release]")).not.toBeNull();
  });
});

describe("synth page: install", () => {
  it("offers only the current release, and a mock install reaches Done", async () => {
    const { $, requests, confirms, dev } = await page();
    expect(text($("h2[data-install-heading]"))).toBe("Install on my keyboard");
    $("#connect")!.click();
    await until(() => text($("#device")).includes("SMK-37 Pro_016"));
    expect(text($("#packages"))).toContain("KNOBI-SIX 1.0");
    expect(text($("#packages"))).not.toContain("0.9");
    $("#backup")!.click();
    await until(() => !$<HTMLButtonElement>("#install")!.disabled);
    $("#install")!.click();
    await until(() => text($("#result")).includes("SMK-37 Pro_956"), 8000);
    expect($('[data-step="done"]')!.getAttribute("data-state")).toBe("ok");
    expect(requests).toContain("/midi-chlorian/firmware/knobi-six/1.0/knobi-six-1.0.fwsc");
    expect(confirms[0]).toContain("KNOBI-SIX 1.0");
    expect(dev.bad).toBe(0);
  });

  it("Install 0.9 on an older release switches the installer to 0.9, and back", async () => {
    const { $ } = await page();
    $("[data-release='0.9'] button[data-install-release]")!.click();
    await until(() => text($("[data-selected-release]")).includes("0.9"));
    $("#connect")!.click();
    await until(() => text($("#packages")).includes("KNOBI-SIX 0.9"));
    expect(text($("#packages"))).not.toContain("KNOBI-SIX 1.0");
    $("[data-use-current]")!.click();
    await until(() => !$("[data-use-current]"));
    $("#connect")!.click();
    await until(() => text($("#packages")).includes("KNOBI-SIX 1.0"));
  });

  it("the backup instructions link points at the site's backup.txt", async () => {
    const { $ } = await page();
    expect($("label[for=backup] a")!.getAttribute("href")).toBe("/midi-chlorian/backup.txt");
  });
});
