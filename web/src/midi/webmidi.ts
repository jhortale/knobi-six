// SPDX-License-Identifier: GPL-3.0-only
// The MIDI backend in the browser: Web MIDI with SysEx (Chrome, Edge; https or localhost).
import { InstallError } from "@knobi/installer-core";
import { DELAY } from "@knobi/installer-core";
import { SysexQueue, sleep, type Link, type MidiBackend } from "@knobi/installer-core";

export const NO_MIDI = "Web MIDI with SysEx is not available here. Use Chrome or Edge, open the page over https, " +
  "and allow access to MIDI devices when the browser asks. On Windows, Chrome may need " +
  "chrome://flags/#use-winrt-midi-api turned on (then relaunch).";

/** the ports with names made unique: a second port with the same name (two identical keyboards) is "NAME #2", and
 *  so on, in the browser's order, so the nth input pairs with the nth output of that name */
function named<T extends MIDIPort>(m: Map<string, T> | MIDIInputMap | MIDIOutputMap): [string, T][] {
  const seen = new Map<string, number>();
  return [...(m as Map<string, T>).values()].map((p) => {
    const base = p.name ?? "";
    const n = (seen.get(base) ?? 0) + 1;
    seen.set(base, n);
    return [n === 1 ? base : `${base} #${n}`, p];
  });
}

const byName = <T extends MIDIPort>(m: Map<string, T> | MIDIInputMap | MIDIOutputMap, name: string) =>
  named<T>(m).find(([n]) => n === name)?.[1];

class WebMidiLink implements Link {
  readonly name: string;
  private q = new SysexQueue();
  private access: MIDIAccess;
  private input: MIDIInput;
  private output: MIDIOutput;

  constructor(access: MIDIAccess, input: MIDIInput, output: MIDIOutput, name: string) {
    this.access = access;
    this.input = input;
    this.output = output;
    this.name = name;
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
    return named<MIDIInput>(this.access.inputs).map(([n]) => n);
  }

  outputNames() {
    return named<MIDIOutput>(this.access.outputs).map(([n]) => n);
  }

  async open(inName: string, outName: string): Promise<Link> {
    const input = byName<MIDIInput>(this.access.inputs, inName);
    const output = byName<MIDIOutput>(this.access.outputs, outName);
    if (!input || !output) throw new Error("no such port");
    await input.open();
    try {
      await output.open();
    } catch (e) {
      void input.close().catch(() => {});    // as fm1_install.py: never leave the input open
      throw e;
    }
    const link = new WebMidiLink(this.access, input, output, inName);
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
