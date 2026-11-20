// SPDX-License-Identifier: GPL-3.0-only
import { readFileSync } from "node:fs";
import { defineConfig } from "vitest/config";

export default defineConfig({
  base: "./",
  plugins: [{
    // the backup instructions (docs/BACKUP.md) on the site itself: the source repo is private
    name: "knobi-backup-doc",
    generateBundle() {
      this.emitFile({ type: "asset", fileName: "backup.txt",
        source: readFileSync(new URL("../docs/BACKUP.md", import.meta.url), "utf8") });
    },
  }],
  build: { sourcemap: true },
  test: { environment: "node", include: ["test/**/*.test.ts"] },
});
