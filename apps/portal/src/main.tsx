// SPDX-License-Identifier: GPL-3.0-only
// midi-chlorian's entry: the Web MIDI backend and the site's own files; in the dev server with ?mock, the fixture site
// and a simulated keyboard instead (never built into the published site: import.meta.env.DEV is false there and the
// import is dropped).
import "./index.css";
import { requestBackend } from "@knobi/web-midi";
import { startPortal, type PortalDeps } from "./app.tsx";

const root = document.getElementById("app")!;
const deps: PortalDeps = {
  base: import.meta.env.BASE_URL,
  pathname: location.pathname,
  backend: () => requestBackend(),
  fetch: (input, init) => fetch(input, init),
  // no confirm: the installer panel asks with its own dialog
};

if (import.meta.env.DEV && new URLSearchParams(location.search).has("mock")) {
  import("./dev/mock.ts").then((m) => startPortal(root, m.mockDeps(deps)));
} else {
  startPortal(root, deps);
}
