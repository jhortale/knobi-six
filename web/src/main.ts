// SPDX-License-Identifier: GPL-3.0-only
// knobi-flash's entry: the Web MIDI backend; in the dev server with ?mock, a simulated keyboard instead (never
// built into the published page: import.meta.env.DEV is false there and the import is dropped).
import "./index.css";
import { requestBackend } from "./midi/webmidi.ts";
import { startApp, type AppDeps } from "./ui/app.tsx";

const root = document.getElementById("app")!;
const deps: AppDeps = {
  backend: () => requestBackend(),
  fetch: (input, init) => fetch(input, init),
  catalogUrl: "catalog.json",
  // no confirm: the page asks with its own dialog
};

if (import.meta.env.DEV && new URLSearchParams(location.search).has("mock")) {
  import("./dev/mock.ts").then((m) => startApp(root, m.mockDeps(deps)));
} else {
  startApp(root, deps);
}
