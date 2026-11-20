// SPDX-License-Identifier: GPL-3.0-only
// port of tests/install_test.py's FakeFM1: Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments;
// M. Geisler (genkigenki/makkuro)
// A simulated keyboard on a fake MIDI backend, for the tests and the dev server's ?mock page. It answers the
// handshake with its identity; on the upgrade command it asks for reads the way the real firmware does (the running
// firmware: a few reads, then the check finish, then it restarts as the update loader under another port; the loader:
// reads over the image, then the write finish, then it restarts as the new firmware) and checks every answer.
// Never part of the published page.
import { SysexQueue, type Link, type MidiBackend } from "@knobi/installer-core";
import { unpack7 } from "@knobi/installer-core";
import { FINISH_CHECK, FINISH_WRITE, HS_QUERY, SUCCESS, UPGRADE, identityReply, request } from "@knobi/installer-core";

export interface FakeOpts {
  image: Uint8Array;
  identity: string;
  name: string;
  afterWrite: string;
  afterWriteName: string;
  /** the update loader's identity and port; null: it never appears */
  loader: [string, string] | null;
  unplugAfter?: number;
  stallAfter?: number;
  badAddr?: number;
  /** other MIDI gear that never answers */
  extraPorts?: string[];
  /** ms before a restarted device is back */
  rebootMs?: number;
  /** read requests the loader makes (default six of 512 bytes; the ?mock page asks for the whole image) */
  loaderReads?: [number, number][];
}

const eq = (a: Uint8Array, b: Uint8Array) => a.length === b.length && a.every((x, i) => x === b[i]);

class FakeLink implements Link {
  readonly q = new SysexQueue();
  closed = false;
  readonly dev: FakeKeyboard;
  readonly gen: number;
  readonly name: string;
  readonly silent: boolean;

  constructor(dev: FakeKeyboard, gen: number, name: string, silent: boolean) {
    this.dev = dev;
    this.gen = gen;
    this.name = name;
    this.silent = silent;
  }

  get lost() {
    return this.silent ? !this.dev.extraPorts.includes(this.name) : this.gen !== this.dev.gen || !this.dev.connected;
  }

  send(pkt: Uint8Array) {
    if (this.lost) return false;
    if (!this.silent) this.dev.rx(pkt);
    return true;
  }

  read(t: number) {
    return this.q.read(t);
  }

  drain() {
    this.q.drain();
  }

  close() {
    this.closed = true;
  }
}

export class FakeKeyboard implements MidiBackend {
  readonly opts: FakeOpts;
  identity = "";
  name = "";
  connected = true;
  gen = 0;
  served = 0;
  bad = 0;
  upgrades = 0;
  readonly probed: string[] = [];
  readonly extraPorts: string[];
  private links: FakeLink[] = [];
  private waiting: [number, number] | null = null;
  private queue: [number, number][] = [];

  constructor(opts: Partial<FakeOpts> & { image: Uint8Array }) {
    this.opts = {
      identity: "SMK-37 Pro_016", name: "SMK-37 Pro", afterWrite: "SMK-37 Pro_956", afterWriteName: "X0X FM-1",
      loader: ["ota-SMK-37 Pro_900", "Felucca Update"], ...opts,
    };
    this.extraPorts = opts.extraPorts ?? [];
    this.boot(this.opts.identity, this.opts.name);
  }

  boot(identity: string, name: string) {
    this.gen++;
    this.identity = identity;
    this.name = name;
    this.connected = true;
    this.waiting = null;
    this.queue = [];
  }

  /** the keyboard is unplugged (the ?mock page's "unplug" button) */
  unplug() {
    this.connected = false;
  }

  /** links opened and not closed yet (the tests check none is left open) */
  get openLinks() {
    return this.links.filter((l) => !l.closed).length;
  }

  inputNames() {
    return [...this.extraPorts, ...(this.connected ? [this.name] : [])];   // other gear first: it must be skipped
  }

  outputNames() {
    return this.inputNames();
  }

  async open(inName: string, _outName: string): Promise<Link> {
    this.probed.push(inName);
    if (this.extraPorts.includes(inName)) return new FakeLink(this, -1, inName, true);
    if (inName !== this.name || !this.connected) throw new Error("no such port");
    const link = new FakeLink(this, this.gen, inName, false);
    this.links.push(link);
    return link;
  }

  private tx(pkt: Uint8Array) {
    for (const l of this.links) if (l.gen === this.gen && !l.closed) l.q.put(pkt);
  }

  rx(d: Uint8Array) {
    if (eq(d, HS_QUERY)) {
      this.tx(identityReply(this.identity));
    } else if (eq(d, UPGRADE)) {
      this.upgrades++;
      const first = this.opts.badAddr ?? 0;
      this.queue = this.identity.startsWith("ota-")
        ? [...(this.opts.loaderReads ?? Array.from({ length: 6 }, (_, k) => [first + k * 512, 512] as [number, number])),
          [FINISH_WRITE, 8]]
        : [[0, 64], [0x40, 160], [0x1000, 512], [FINISH_CHECK, 8]];
      this.next();
    } else if (this.waiting) {
      const u = unpack7(d.subarray(1, -1));
      const [addr, n] = this.waiting;
      const want = addr >= FINISH_CHECK ? SUCCESS : this.opts.image.subarray(addr, addr + n);
      if (u.length !== 15 + want.length || !eq(u.subarray(14, 14 + want.length), want)) this.bad++;
      this.waiting = null;
      this.served++;
      if (this.opts.unplugAfter && this.served >= this.opts.unplugAfter) {
        this.connected = false;
        return;
      }
      if (this.opts.stallAfter && this.served >= this.opts.stallAfter) return;
      const ms = this.opts.rebootMs ?? 50;
      if (addr === FINISH_CHECK) {
        this.connected = false;
        const l = this.opts.loader;
        if (l) setTimeout(() => this.boot(l[0], l[1]), ms);
      } else if (addr === FINISH_WRITE) {
        this.connected = false;
        setTimeout(() => this.boot(this.opts.afterWrite, this.opts.afterWriteName), ms);
      } else {
        this.next();
      }
    }
  }

  private next() {
    const r = this.queue.shift();
    if (!r) return;
    this.waiting = r;
    this.tx(request(r[0], r[1]));
  }
}
