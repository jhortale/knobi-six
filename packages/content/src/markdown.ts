import { Marked } from "marked";

// Raw HTML in the Markdown is shown as text, never passed through.
const escapeHtml = (s: string): string =>
  s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;").replace(/'/g, "&#39;");

const marked = new Marked({
  tokenizer: {
    // No HTML blocks: a line starting with a tag is a paragraph, so Markdown after the tag still renders.
    html: () => undefined,
  },
  renderer: {
    html: ({ text }) => escapeHtml(text),
  },
});

export function renderMarkdown(md: string): string {
  return marked.parse(md, { async: false });
}
