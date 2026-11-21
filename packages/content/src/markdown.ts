import { Marked } from "marked";

// Raw HTML in the Markdown is shown as text, never passed through.
const escapeHtml = (s: string): string =>
  s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;").replace(/'/g, "&#39;");

// A link or image keeps its URL only when it is http, https or mailto, or relative (not a bare `#fragment`: the pages
// sit under a <base href>, docs/PORTAL.md). Any other scheme (javascript:, data:, vbscript:, in any case, with
// control characters or spaces hidden inside) renders as the plain text of the link or the image's alt.
function safeUrl(href: string): boolean {
  const u = href.replace(/[\u0000-\u0020\u007f-\u009f]/g, "");
  if (u === "" || u.startsWith("#")) return false;
  const scheme = /^([a-z][a-z0-9+.-]*):/i.exec(u);
  return scheme ? ["http", "https", "mailto"].includes(scheme[1]!.toLowerCase()) : true;
}

const marked = new Marked({
  tokenizer: {
    // No HTML blocks: a line starting with a tag is a paragraph, so Markdown after the tag still renders.
    html: () => undefined,
  },
  renderer: {
    html: ({ text }) => escapeHtml(text),
    // returning false = marked's own renderer
    link(token) {
      return safeUrl(token.href) ? false : this.parser.parseInline(token.tokens);
    },
    image(token) {
      return safeUrl(token.href) ? false : escapeHtml(token.text);
    },
  },
});

export function renderMarkdown(md: string): string {
  return marked.parse(md, { async: false });
}
