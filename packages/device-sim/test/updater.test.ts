// SPDX-License-Identifier: GPL-3.0-only
// The updater against a simulated keyboard (port of tests/install_test.py's installs() and errors()).
import { beforeAll, describe, expect, it } from "vitest";
import { pairOutput } from "@knobi/installer-core";
import { FakeKeyboard, type FakeOpts } from "../src/index.ts";
import { DELAY, Updater, type Step } from "@knobi/installer-core";
import { InstallError } from "@knobi/installer-core";
import { loadPackage } from "@knobi/installer-core";
import { makePackage } from "../src/index.ts";

const PORTS = /fm-1|smk|elite|mke-p37|x0x|ota|felucca|usb-midi/i;
const FAMILY = /^(?:SMK-37[ +].*|MKE-P37|STARRYKEY-37.*)$/i;
const OWN = /^(?:ota-(FM-1|SMK-37 Pro|SMK-37 Elite|MKE-P37)_9\d\d)$/i;
const { product, image } = loadPackage(makePackage({ product: "SMK-37 Pro_956" }));

beforeAll(() => {
  Object.assign(DELAY, { open: 0, start: 10, reply: 0, loader: 10, reboot: 10, retry: 20, poll: 50, hs: 100,
    idleCheck: 400, idleWrite: 400, waitLoader: 2000, waitReboot: 2000 });
});

async function run(opts: Partial<FakeOpts> = {}, searchAll = false) {
  const dev = new FakeKeyboard({ image, ...opts });
  const up = new Updater(dev, { ports: PORTS, family: FAMILY, ownLoader: OWN, searchAll });
  const steps: Step[] = [];
  const found = await up.find();
  if (!found) return { dev, steps, error: new InstallError("notfound", "not found"), found };
  try {
    const id = await up.install(found, image, product, (s) => steps.push(s));
    return { dev, steps, id, found };
  } catch (e) {
    return { dev, steps, error: e as InstallError, found };
  }
}

describe("install", () => {
  it("running stock firmware -> loader -> KNOBI-SIX, every answer right", async () => {
    const r = await run();
    expect(r.error).toBeUndefined();
    expect(r.id).toBe("SMK-37 Pro_956");
    const kinds = r.steps.map((s) => s.k).filter((k, i, a) => k !== a[i - 1]);
    expect(kinds).toEqual(["start", "check", "loader", "write", "reboot", "done"]);
    const pct = r.steps.flatMap((s) => (s.k === "write" ? [s.pct] : []));
    expect(pct[0]).toBe(0);
    expect(pct.at(-1)).toBe(100);
    expect(r.dev.bad).toBe(0);
    expect(r.dev.upgrades).toBe(2);
  });
  it("finishes an install that was cut off (device already in its update loader)", async () => {
    const r = await run({ identity: "ota-SMK-37 Pro_900", name: "Felucca Update" });
    expect(r.id).toBe("SMK-37 Pro_956");
    expect(r.dev.upgrades).toBe(1);
  });
  it("finds the loader under another port name after the first port vanished", async () => {
    const r = await run({ name: "X0X FM-1", loader: ["ota-SMK-37 Pro_900", "Felucca Update"] });
    expect(r.found!.name).toBe("X0X FM-1");
    expect(r.id).toBe("SMK-37 Pro_956");
  });
});

describe("refusals and failures", () => {
  const err = async (opts: Partial<FakeOpts>, code: string, text?: string) => {
    const r = await run(opts);
    expect(r.error?.code).toBe(code);
    if (text) expect(r.error?.message).toContain(text);
    return r;
  };
  it("another model: refused before anything is sent", async () => {
    const r = await err({ identity: "FM-1_015", name: "FM-1" }, "model");
    expect(r.dev.upgrades).toBe(0);
  });
  it("another firmware's update loader: refused", async () => {
    const r = await err({ identity: "ota-SMK-37 Pro_016", name: "SMK-37 Pro" }, "foreign");
    expect(r.dev.upgrades).toBe(0);
  });
  it("unplugged during the check: nothing was written", () => err({ unplugAfter: 2 }, "lost", "nothing was written"));
  it("unplugged during the write: run the install again", () => err({ unplugAfter: 6 }, "lost", "run the install again to finish"));
  it("stops answering: stopped", () => err({ stallAfter: 2 }, "stopped"));
  it("asks outside the package: badreq", () =>
    err({ identity: "ota-SMK-37 Pro_900", name: "Felucca Update", badAddr: image.length }, "badreq", "outside the package"));
  it("the update loader never appears: noloader", () => err({ loader: null }, "noloader"));
  it("another identity after the restart: mismatch", () => err({ afterWrite: "SMK-37 Pro_955" }, "mismatch", "SMK-37 Pro_955"));
});

describe("ports", () => {
  it("pairOutput: Windows numbering, ambiguity, a single output", () => {
    expect(pairOutput("X0X FM-1 1", ["X0X FM-1 2"])).toBe("X0X FM-1 2");
    expect(pairOutput("X0X FM-1", ["IAC", "X0X FM-1"])).toBe("X0X FM-1");
    expect(pairOutput("A 1", ["A 2", "A 3", "B"])).toBeNull();
    expect(pairOutput("X", ["Y"])).toBe("Y");
  });
  it("other gear is never probed unless 'search all MIDI ports'", async () => {
    const dev = new FakeKeyboard({ image, extraPorts: ["Some Synth"] });
    const up = new Updater(dev, { ports: PORTS, family: FAMILY, ownLoader: OWN });
    expect((await up.find())?.name).toBe("SMK-37 Pro");
    expect(dev.probed).not.toContain("Some Synth");
    const all = new Updater(dev, { ports: PORTS, family: FAMILY, ownLoader: OWN, searchAll: true });
    expect((await all.find())?.name).toBe("SMK-37 Pro");
    expect(dev.probed).toContain("Some Synth");
  });
});

describe("links and ports (review minors)", () => {
  it("a bad request closes the link", async () => {
    const r = await run({ identity: "ota-SMK-37 Pro_900", name: "Felucca Update", badAddr: image.length });
    expect(r.error?.code).toBe("badreq");
    expect(r.dev.openLinks).toBe(0);
  });
  it("never pairs a matching input with an unrelated single output", async () => {
    const opened: string[] = [];
    const backend = {
      inputNames: () => ["X0X FM-1"],
      outputNames: () => ["Some Synth"],
      open: async (i: string, o: string) => {
        opened.push(`${i} -> ${o}`);
        throw new Error("no");
      },
    };
    const up = new Updater(backend, { ports: PORTS, family: FAMILY, ownLoader: OWN });
    expect(await up.find()).toBeNull();
    expect(opened).toEqual([]);
  });
});
