// SPDX-License-Identifier: GPL-3.0-only
// The MIDI backend in the browser: Web MIDI with SysEx (Chrome, Edge; https or localhost).
import { InstallError } from "../protocol/identity.ts";
import { DELAY } from "../protocol/updater.ts";
import { SysexQueue, sleep, type Link, type MidiBackend } from "./backend.ts";

export const NO_MIDI = "Web MIDI with SysEx is not available here. Use Chrome or Edge, open the page over https, " +
  "and allow access to MIDI devices when the browser asks. On Windows, Chrome may need " +
  "chrome://flags/#use-winrt-midi-api turned on (then relaunch).";

const byName = <T extends MIDIPort>(m: Map<string, T> | MIDIInputMap | MIDIOutputMap, name: string) => {
  for (const p of (m as Map<string, T>).values()) if (p.name === name) return p;
  return undefined;
};

class WebMidiLink implements Link {
  readonly name: string;
  private q = new SysexQueue();
  private access: MIDIAccess;
  private input: MIDIInput;
  private output: MIDIOutput;

  constructor(access: MIDIAccess, input: MIDIInput, output: MIDIOutput) {
    this.access = access;
    this.input = input;
    this.output = output;
    this.name = input.name ?? "";
    input.onmidimessage = (e) => {
      const d = e.data;
      if (d && d[0] === 0xf0) this.q.put(Uint8Array.from(d));
    };
  }

  get lost() {
    const still = [...(this.access.inputs as unknown as Map<string, MIDIInput>).values()].includes(this.input);
    return !still || this.input.state === "disconnected";
  }

  send(pkt: Uint8Array) {
    try {
      this.output.send(pkt);
      return true;
    } catch {
      return false;
    }
  }

  read(t: number) {
    return this.q.read(t);
  }

  drain() {
    this.q.drain();
  }

  close() {
    this.input.onmidimessage = null;
    void this.input.close().catch(() => {});
    void this.output.close().catch(() => {});
  }
}

export class WebMidiBackend implements MidiBackend {
  readonly access: MIDIAccess;

  constructor(access: MIDIAccess) {
    this.access = access;
  }

  inputNames() {
    return [...(this.access.inputs as unknown as Map<string, MIDIInput>).values()].map((p) => p.name ?? "");
  }

  outputNames() {
    return [...(this.access.outputs as unknown as Map<string, MIDIOutput>).values()].map((p) => p.name ?? "");
  }

  async open(inName: string, outName: string): Promise<Link> {
    const input = byName<MIDIInput>(this.access.inputs, inName);
    const output = byName<MIDIOutput>(this.access.outputs, outName);
    if (!input || !output) throw new Error("no such port");
    await input.open();
    await output.open();
    const link = new WebMidiLink(this.access, input, output);
    await sleep(DELAY.open);      // CoreMIDI may drop a message sent just after opening
    return link;
  }
}

/** MIDI access with SysEx (asks the user), or InstallError("denied") */
export async function requestBackend(nav: Navigator | undefined = globalThis.navigator): Promise<MidiBackend> {
  if (!nav || typeof nav.requestMIDIAccess !== "function") throw new InstallError("denied", NO_MIDI);
  try {
    return new WebMidiBackend(await nav.requestMIDIAccess({ sysex: true }));
  } catch {
    throw new InstallError("denied", NO_MIDI);
  }
}
