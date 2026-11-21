// @vitest-environment jsdom
// SPDX-License-Identifier: GPL-3.0-only
// The home page and the routes: the synth cards from content.json, the chrome, and NotFound for anything else.
import { describe, expect, it } from "vitest";
import { routeOf } from "../src/routes.ts";
import { BASE, open, text, until } from "./helpers.ts";

describe("routeOf", () => {
  it.each([
    ["/midi-chlorian/", { page: "home" }],
    ["/midi-chlorian", { page: "home" }],
    ["/midi-chlorian/index.html", { page: "home" }],
    ["/midi-chlorian/knobi-six/", { page: "synth", id: "knobi-six" }],
    ["/midi-chlorian/knobi-six", { page: "synth", id: "knobi-six" }],
    ["/midi-chlorian/knobi-six/index.html", { page: "synth", id: "knobi-six" }],
    ["/midi-chlorian/knobi-six/extra/", { page: "notfound" }],
    ["/midi-chlorian/Not_A_Synth/", { page: "notfound" }],
    ["/midi-chlorian/404.html", { page: "notfound" }],
    ["/elsewhere/", { page: "notfound" }],
  ])("%s", (path, route) => expect(routeOf(path, BASE)).toEqual(route));
});

describe("home", () => {
  it("lists KNOBI-SIX with its current version and keyboards, linking its page", async () => {
    const { $, $$ } = open(BASE);
    await until(() => !!$("[data-synth=knobi-six]"));
    const card = $("[data-synth=knobi-six]")!;
    expect(text(card)).toContain("KNOBI-SIX");
    expect(text(card)).toContain("OB-6-inspired six-voice polysynth for the SMK-37");
    expect(text(card.querySelector("[data-version]"))).toBe("1.0");
    expect(text(card)).toContain("SMK-37 Pro · SMK-37 Elite · MKE-P37");
    expect(card.closest("a")!.getAttribute("href")).toBe("/midi-chlorian/knobi-six/");
    expect($$("[data-synth]").length).toBe(1);
    expect(text($("[data-coming]"))).toContain("coming");
  });

  it("has the chrome: logo linking home, tagline, the unofficial notice, how it works, footer; no knobi-flash", async () => {
    const { root, $ } = open(BASE);
    await until(() => !!$("[data-synth=knobi-six]"));
    expect($("header a")!.getAttribute("href")).toBe(BASE);
    expect(text(root)).toContain("Firmwares for MIDI keyboards, installed from the browser");
    expect(text(root)).toContain("Unofficial · not made by M-VAVE or Sequential");
    expect(text(root)).toContain("USB data cable");
    expect(text($("footer"))).toContain("GPL-3.0-only");
    const src = $("footer a[data-source]")!;
    expect(text(src)).toBe("Source (GPL-3.0)");
    expect(src.getAttribute("href")).toBe("/midi-chlorian/source/");
    expect(text($("footer"))).toContain("M-VAVE, SMK-37, MKE-P37 and OB-6 belong to their owners");
    expect(root.innerHTML.toLowerCase()).not.toContain("knobi-flash");
    expect(document.title).toBe("midi·chlorian");
  });

  it("says so, with a reload hint, when the site's data cannot be loaded", async () => {
    const { $ } = open(BASE, { fetch: (async () => new Response("", { status: 503 })) as typeof fetch });
    await until(() => !!$("[data-page=error]"));
    expect(text($("[data-page=error]"))).toMatch(/reload/i);
  });
});

describe("NotFound", () => {
  it.each(["/midi-chlorian/nothing-here/", "/midi-chlorian/a/b/c", "/midi-chlorian/404.html"])(
    "%s renders NotFound with a link home", async (p) => {
      const { $ } = open(p);
      await until(() => !!$("[data-page=notfound]"));
      expect(text($("[data-page=notfound]"))).toMatch(/not found/i);
      expect($<HTMLAnchorElement>("[data-page=notfound] a[data-home]")!.getAttribute("href")).toBe(BASE);
    });
});
