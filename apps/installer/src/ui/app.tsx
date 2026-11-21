// SPDX-License-Identifier: GPL-3.0-only
// Starts the page: the installer (behaviour) and its React view.
import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { Page } from "./Page.tsx";
import { Installer, type InstallerDeps } from "@knobi/installer-ui";

export type AppDeps = InstallerDeps;

export interface App {
  /** the catalog is loaded (or failed to load) */
  ready: Promise<void>;
  connect(): Promise<void>;
  install(): Promise<void>;
  installer: Installer;
}

export function startApp(root: HTMLElement, deps: AppDeps): App {
  const installer = new Installer(deps);
  createRoot(root).render(<StrictMode><Page installer={installer} /></StrictMode>);
  return { ready: installer.ready, connect: installer.connect, install: installer.install, installer };
}
