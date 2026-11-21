import { Marked } from "marked";

// Raw HTML in the Markdown is shown as text, never passed through.
const escapeHtml = (s: string): string =>
  s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;").replace(/'/g, "&#39;");

// A link or image keeps its URL only when it is certain what scheme the browser will see (it decodes HTML character
// references in an attribute, and marked leaves them in place). Kept: a URL that literally starts with `http://`,
// `https://` or `mailto:`, and a relative URL with no `:` and no `&` (so no character reference can hide a scheme).
// Refused, and rendered as the plain text of the link or the image's alt: any URL with a control character or a space
// (U+0000-U+0020, U+007F-U+009F), an empty one, a bare `#fragment` (the pages sit under a <base href>, docs/PORTAL.md),
// any other scheme (javascript:, data:, vbscript:, in any case), and a relative URL containing `&` or `:`.
// A kept URL is written with every `&`, `<`, `>` and quote escaped.
function safeUrl(href: string): boolean {
  if (href === "" || href.startsWith("#") || /[\u0000-\u0020\u007f-\u009f]/.test(href)) return false;
  if (/^(https?:\/\/|mailto:)/i.test(href)) return true;
  return !/[&:]/.test(href);
}

// the URL as marked writes it (percent-encoded, existing %XX kept), then attribute-escaped
const attr = (href: string): string => escapeHtml(encodeURI(href).replace(/%25/g, "%"));
const titleAttr = (t?: string | null): string => (t ? ` title="${escapeHtml(t)}"` : "");

const marked = new Marked({
  tokenizer: {
    // No HTML blocks: a line starting with a tag is a paragraph, so Markdown after the tag still renders.
    html: () => undefined,
  },
  renderer: {
    html: ({ text }) => escapeHtml(text),
    link(token) {
      const inner = this.parser.parseInline(token.tokens);
      return safeUrl(token.href) ? `<a href="${attr(token.href)}"${titleAttr(token.title)}>${inner}</a>` : inner;
    },
    image(token) {
      return safeUrl(token.href) ? `<img src="${attr(token.href)}" alt="${escapeHtml(token.text)}"${titleAttr(token.title)}>` : escapeHtml(token.text);
    },
  },
});

export function renderMarkdown(md: string): string {
  return marked.parse(md, { async: false });
}
