// SPDX-License-Identifier: GPL-3.0-only
// port of tools/fm1_install.py: Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments;
// M. Geisler (genkigenki/makkuro)
// What the updater needs from MIDI: port names, and one in/out pair with a SysEx queue (Web MIDI or the simulated
// keyboard).

export interface Link {
  readonly name: string;
  send(pkt: Uint8Array): boolean;
  /** the next SysEx message, or null after timeoutMs */
  read(timeoutMs: number): Promise<Uint8Array | null>;
  drain(): void;
  readonly lost: boolean;
  close(): void;
}

export interface MidiBackend {
  inputNames(): string[];
  outputNames(): string[];
  open(inName: string, outName: string): Promise<Link>;
}

/** the output that goes with an input: the same name, or the same name but Windows' number, or the only one */
export function pairOutput(name: string, outs: string[]): string | null {
  if (outs.includes(name)) return name;
  const base = (s: string) => s.replace(/\s+\d+$/, "");
  const same = outs.filter((o) => base(o) === base(name));
  if (same.length === 1) return same[0];
  return outs.length === 1 ? outs[0] : null;
}

/** a SysEx queue with waiting readers, for Link implementations */
export class SysexQueue {
  private q: Uint8Array[] = [];
  private waiters: ((p: Uint8Array | null) => void)[] = [];

  put(pkt: Uint8Array): void {
    const w = this.waiters.shift();
    if (w) w(pkt);
    else this.q.push(pkt);
  }

  read(timeoutMs: number): Promise<Uint8Array | null> {
    const p = this.q.shift();
    if (p) return Promise.resolve(p);
    return new Promise((resolve) => {
      const w = (x: Uint8Array | null) => {
        clearTimeout(t);
        resolve(x);
      };
      const t = setTimeout(() => {
        this.waiters = this.waiters.filter((x) => x !== w);
        resolve(null);
      }, Math.max(timeoutMs, 0));
      this.waiters.push(w);
    });
  }

  drain(): void {
    this.q = [];
  }
}

export const sleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));
