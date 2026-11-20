// SPDX-License-Identifier: GPL-3.0-only
// @knobi/catalog: what the installer offers (catalog.json, schema 1): its types, its checks, its queries. Browser-safe;
// the Node-only parts (reading synths/ from disk, the generator) are in "@knobi/catalog/node".
export type * from "./schema.ts";
export * from "./load.ts";
