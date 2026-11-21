// SPDX-License-Identifier: GPL-3.0-only
// After `vite build`: a page for every synth in a content.json (dist/<synth>/index.html) and GitHub Pages' 404.html,
// each a copy of dist/index.html (the app routes from location.pathname; the absolute base makes the assets load at
// any depth). A synth's copy carries its name in <title> and a marker; a directory holding only a marked index.html of
// a synth no longer in the content is removed (nothing else ever is). The deploy runs it with the published content.json
// after `vite build` (the app build itself has no pages but index.html: Ruling 8).
//   node scripts/prerender.ts --dist DIR --content FILE
import { mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const SYNTH_ID = /^[a-z0-9-]+$/;
/** in every synth page this script writes, and only there */
const MARK = '<meta name="midi-chlorian-page" content="synth" />';

/** a directory this script wrote: exactly one file, index.html, carrying the mark */
function isSynthPageDir(dir: string): boolean {
  const entries = readdirSync(dir, { withFileTypes: true });
  return entries.length === 1 && entries[0]!.isFile() && entries[0]!.name === "index.html" &&
    readFileSync(join(dir, "index.html"), "utf8").includes(MARK);
}
const escapeHtml = (s: string) => s.replace(/[&<>"]/g, (c) => `&${({ "&": "amp", "<": "lt", ">": "gt", '"': "quot" })[c]};`);

export function prerender(dist: string, content: { synths: { id: string; name: string }[] }): string[] {
  const html = readFileSync(join(dist, "index.html"), "utf8");
  if (!/<title>[^<]*<\/title>/.test(html)) throw new Error(`${dist}/index.html: no <title>`);
  const written: string[] = [];
  for (const s of content.synths) {
    if (!SYNTH_ID.test(s.id)) throw new Error(`content: synth id ${JSON.stringify(s.id)} is not a folder name`);
    mkdirSync(join(dist, s.id), { recursive: true });
    writeFileSync(join(dist, s.id, "index.html"),
      html.replace(/<title>[^<]*<\/title>/, `<title>${escapeHtml(s.name)} · midi·chlorian</title>\n    ${MARK}`));
    written.push(`${s.id}/index.html`);
  }
  const ids = new Set(content.synths.map((s) => s.id));
  for (const e of readdirSync(dist, { withFileTypes: true })) {
    if (e.isDirectory() && !ids.has(e.name) && isSynthPageDir(join(dist, e.name))) {
      rmSync(join(dist, e.name), { recursive: true });
    }
  }
  writeFileSync(join(dist, "404.html"), html);
  return [...written, "404.html"];
}

if (import.meta.main) {
  const arg = (flag: string) => {
    const i = process.argv.indexOf(flag);
    const v = i > 0 ? process.argv[i + 1] : undefined;
    if (!v) {
      console.error("usage: node scripts/prerender.ts --dist DIR --content FILE");
      process.exit(2);
    }
    return v;
  };
  const dist = arg("--dist");
  const content = JSON.parse(readFileSync(arg("--content"), "utf8"));
  console.log(`prerender: ${prerender(dist, content).join(", ")}`);
}
