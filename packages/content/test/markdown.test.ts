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
      // character references the browser decodes inside the attribute
      "[x](&#106;avascript:alert(1))", "[x](&#x6A;avascript:alert(1))", "[x](javascript&colon;alert(1))",
      "[x](javascript&#58;alert(1))", "[x](java&#9;script:alert(1))", "[x](javascript&#0000058alert(1))",
      "![](data&colon;image/svg+xml,<svg>)", "![](&#106;avascript:alert(1))", "[x](/a&#47;b)",
    ];
    it.each(bad)("renders %s as plain text, no href or src", (md) => {
      const html = renderMarkdown(md);
      expect(html).not.toMatch(/href=|src=|<a\b|<img/i);
    });
    it("keeps an https or mailto URL with & (its scheme is literal), the & escaped in the attribute", () => {
      expect(renderMarkdown("[x](https://example.com/?a=1&b=2)")).toContain('href="https://example.com/?a=1&amp;b=2"');
      expect(renderMarkdown("![x](https://example.com/a.png?a=1&b=2)")).toContain('src="https://example.com/a.png?a=1&amp;b=2"');
      expect(renderMarkdown("[x](mailto:a@b.example?subject=a&body=b)")).toContain('href="mailto:a@b.example?subject=a&amp;body=b"');
    });
    it("refuses a relative URL with & or : (a character reference could hide a scheme)", () => {
      expect(renderMarkdown("[x](a/b?c=1&d=2)")).not.toContain("href");
      expect(renderMarkdown("[x](a:b)")).not.toContain("href");
    });
    it("every rendered href and src starts with http://, https://, mailto: or has no scheme and no &", () => {
      const html = renderMarkdown(bad.join("\n\n") + "\n\n[ok](https://example.com/?a=1&b=2) [rel](x/y.png)");
      for (const m of html.matchAll(/(?:href|src)="([^"]*)"/g)) {
        expect(m[1]).toMatch(/^(https?:\/\/|mailto:)|^[^:&]+$/i);
      }
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
