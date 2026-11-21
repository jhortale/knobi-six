// SPDX-License-Identifier: GPL-3.0-only
// midi-chlorian: served at https://jhortale.github.io/midi-chlorian/. The base is absolute (Ruling 7) so that the
// pages at /<synth>/ and GitHub Pages' 404.html (served at any depth) load the same assets; SITE_BASE overrides it
// for a local run. The build is the app shell only: the deploy adds content.json, catalog.json, firmware/, synths/.
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";
import { defineConfig } from "vitest/config";

const base = process.env.SITE_BASE || "/midi-chlorian/";

export default defineConfig({
  base,
  resolve: { alias: { "@": fileURLToPath(new URL("./src", import.meta.url)) } },
  plugins: [react(), tailwindcss(), {
    // the pages' relative links (the installer panel's, from catalog.json) resolve under the base, at any depth
    name: "portal-base-href",
    transformIndexHtml: () => [{ tag: "base", attrs: { href: base }, injectTo: "head-prepend" }],
  }, {
    // the backup instructions (docs/BACKUP.md) on the site itself: the source repo is private
    name: "knobi-backup-doc",
    generateBundle() {
      this.emitFile({ type: "asset", fileName: "backup.txt",
        source: readFileSync(new URL("../../docs/BACKUP.md", import.meta.url), "utf8") });
    },
  }],
  build: { sourcemap: true },
  test: { environment: "node", include: ["test/**/*.test.ts", "test/**/*.test.tsx"] },
});
