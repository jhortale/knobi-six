import { describe, expect, it } from "vitest";
import { renderMarkdown } from "../src/index.ts";

describe("renderMarkdown", () => {
  it("renders Markdown and escapes raw HTML", () => {
    const html = renderMarkdown("<script>alert(1)</script> **x**");
    expect(html).toContain("&lt;script&gt;");
    expect(html).not.toContain("<script>");
    expect(html).toContain("<strong>x</strong>");
  });

  it("escapes block-level raw HTML too", () => {
    const html = renderMarkdown("<div onclick=\"x()\">hi</div>\n\ntext");
    expect(html).not.toContain("<div");
    expect(html).toContain("&lt;div");
  });

  describe("links and images keep only http, https, mailto and relative URLs", () => {
    const bad = [
      "[x](javascript:alert(1))", "[x](JaVaScRiPt:alert(1))", "<javascript:alert(1)>",
      "![](javascript:alert(1))", "![a](JaVaScRiPt:alert(1))", "[x](data:text/html;base64,PHNjcmlwdD4=)", "![](data:image/svg+xml,<svg>)",
      "[x](vbscript:msgbox(1))", "[x](java\tscript:alert(1))",
    ];
    it.each(bad)("renders %s as plain text, no href or src", (md) => {
      const html = renderMarkdown(md);
      expect(html).not.toMatch(/href=|src=|<a\b|<img/i);
    });
    it("keeps the link text and the image alt as text", () => {
      expect(renderMarkdown("[click](javascript:alert(1))")).toContain("click");
      expect(renderMarkdown("![the alt](javascript:alert(1))")).toContain("the alt");
    });
    it.each(["https://a.example/x", "http://a.example/x", "mailto:a@b.example", "/midi-chlorian/x", "img/a.png", "../a.png"])(
      "keeps %s", (u) => {
        expect(renderMarkdown(`[x](${u})`)).toContain(`href="${u}"`);
        expect(renderMarkdown(`![x](${u})`)).toContain(`src="${u}"`);
      });
  });
});
