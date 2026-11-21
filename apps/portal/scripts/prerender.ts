// SPDX-License-Identifier: GPL-3.0-only
// After `vite build`: a page for every synth in a content.json (dist/<synth>/index.html) and GitHub Pages' 404.html,
// each a copy of dist/index.html (the app routes from location.pathname; the absolute base makes the assets load at
// any depth). A synth's copy carries its name in <title>. The deploy runs it again with the published content.json.
//   node scripts/prerender.ts --dist DIR --content FILE
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const SYNTH_ID = /^[a-z0-9-]+$/;
const escapeHtml = (s: string) => s.replace(/[&<>"]/g, (c) => `&${({ "&": "amp", "<": "lt", ">": "gt", '"': "quot" })[c]};`);

export function prerender(dist: string, content: { synths: { id: string; name: string }[] }): string[] {
  const html = readFileSync(join(dist, "index.html"), "utf8");
  if (!/<title>[^<]*<\/title>/.test(html)) throw new Error(`${dist}/index.html: no <title>`);
  const written: string[] = [];
  for (const s of content.synths) {
    if (!SYNTH_ID.test(s.id)) throw new Error(`content: synth id ${JSON.stringify(s.id)} is not a folder name`);
    mkdirSync(join(dist, s.id), { recursive: true });
    writeFileSync(join(dist, s.id, "index.html"),
      html.replace(/<title>[^<]*<\/title>/, `<title>${escapeHtml(s.name)} · midi·chlorian</title>`));
    written.push(`${s.id}/index.html`);
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
