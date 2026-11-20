// SPDX-License-Identifier: GPL-3.0-only
// The Web MIDI backend over a stand-in MIDIAccess (no browser, no device).
import { describe, expect, it } from "vitest";
import { WebMidiBackend, requestBackend } from "../src/index.ts";
import { DELAY } from "@knobi/installer-core";

class Port {
  state = "connected";
  connection = "closed";
  name: string;
  onmidimessage: ((e: { data: Uint8Array }) => void) | null = null;
  sent: Uint8Array[] = [];
  constructor(name: string) {
    this.name = name;
  }
  async open() {
    this.connection = "open";
  }
  async close() {
    this.connection = "closed";
  }
  send(d: Uint8Array) {
    if (this.state !== "connected") throw new Error("disconnected");
    this.sent.push(d);
  }
}

function access(ins: string[], outs: string[]) {
  const i = new Map(ins.map((n, k) => [`i${k}`, new Port(n)]));
  const o = new Map(outs.map((n, k) => [`o${k}`, new Port(n)]));
  return { inputs: i, outputs: o, onstatechange: null };
}

describe("WebMidiBackend", () => {
  DELAY.open = 0;
  it("lists port names, opens a pair, receives SysEx only, sends", async () => {
    const a = access(["X0X FM-1"], ["X0X FM-1"]);
    const b = new WebMidiBackend(a as unknown as MIDIAccess);
    expect(b.inputNames()).toEqual(["X0X FM-1"]);
    const link = await b.open("X0X FM-1", "X0X FM-1");
    const inp = a.inputs.get("i0")!;
    inp.onmidimessage!({ data: Uint8Array.of(0x90, 60, 100) });
    inp.onmidimessage!({ data: Uint8Array.of(0xf0, 1, 0xf7) });
    expect(await link.read(50)).toEqual(Uint8Array.of(0xf0, 1, 0xf7));
    expect(await link.read(10)).toBeNull();
    expect(link.send(Uint8Array.of(0xf0, 2, 0xf7))).toBe(true);
    expect(a.outputs.get("o0")!.sent).toHaveLength(1);
    expect(link.lost).toBe(false);
  });
  it("is lost when the input disconnects or vanishes, and a send then fails", async () => {
    const a = access(["X0X FM-1"], ["X0X FM-1"]);
    const b = new WebMidiBackend(a as unknown as MIDIAccess);
    const link = await b.open("X0X FM-1", "X0X FM-1");
    a.inputs.get("i0")!.state = "disconnected";
    a.outputs.get("o0")!.state = "disconnected";
    expect(link.lost).toBe(true);
    expect(link.send(Uint8Array.of(0xf0, 0xf7))).toBe(false);
    a.inputs.delete("i0");
    expect(link.lost).toBe(true);
  });
  it("requestBackend: no Web MIDI or a refusal is 'denied'", async () => {
    await expect(requestBackend(undefined)).rejects.toMatchObject({ code: "denied" });
    const nav = { requestMIDIAccess: async () => { throw new Error("SecurityError"); } } as unknown as Navigator;
    await expect(requestBackend(nav)).rejects.toMatchObject({ code: "denied" });
  });
});

describe("WebMidiBackend ports (review minors)", () => {
  DELAY.open = 0;
  it("two keyboards with the same name: distinct names, the nth input pairs with the nth output", async () => {
    const a = access(["X0X FM-1", "X0X FM-1"], ["X0X FM-1", "X0X FM-1"]);
    const b = new WebMidiBackend(a as unknown as MIDIAccess);
    expect(b.inputNames()).toEqual(["X0X FM-1", "X0X FM-1 #2"]);
    expect(b.outputNames()).toEqual(["X0X FM-1", "X0X FM-1 #2"]);
    const link = await b.open("X0X FM-1 #2", "X0X FM-1 #2");
    expect(a.inputs.get("i1")!.connection).toBe("open");
    expect(a.inputs.get("i0")!.connection).toBe("closed");
    link.send(Uint8Array.of(0xf0, 0xf7));
    expect(a.outputs.get("o1")!.sent).toHaveLength(1);
    expect(a.outputs.get("o0")!.sent).toHaveLength(0);
  });
  it("an output that fails to open leaves the input closed", async () => {
    const a = access(["X0X FM-1"], ["X0X FM-1"]);
    a.outputs.get("o0")!.open = async () => { throw new Error("busy"); };
    const b = new WebMidiBackend(a as unknown as MIDIAccess);
    await expect(b.open("X0X FM-1", "X0X FM-1")).rejects.toThrow();
    expect(a.inputs.get("i0")!.connection).toBe("closed");
    expect(a.inputs.get("i0")!.onmidimessage).toBeNull();
  });
});
