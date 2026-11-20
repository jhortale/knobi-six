// SPDX-License-Identifier: GPL-3.0-only
import { defineConfig } from "vitest/config";

export default defineConfig({
  base: "./",
  build: { sourcemap: true },
  test: { environment: "node", include: ["test/**/*.test.ts"] },
});
