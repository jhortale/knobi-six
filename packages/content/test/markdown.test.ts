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
});
