// SPDX-License-Identifier: GPL-3.0-only
// port of tools/fm1_install.py's Updater: Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments;
// M. Geisler (genkigenki/makkuro)
// The same update as fm1_install.py: step 1, the running firmware reads parts of the package and starts the update
// loader; step 2, the loader reads the whole package and writes it. In both steps the device asks (read requests on
// the logical image) and we answer. Then the keyboard restarts and the installed identity is checked.
import { pairOutput, sleep, type Link, type MidiBackend } from "./backend.ts";
import { InstallError, isLoader, isOwnLoader, sameKeyboard } from "./identity.ts";
import {
  FINISH_CHECK, FINISH_WRITE, HS_QUERY, MAXDATA, SUCCESS, UPGRADE, parseIdentity, parseRequest, response,
  type Identity,
} from "./messages.ts";

/** milliseconds (fm1_install.py's DELAY); the tests shorten them */
export const DELAY = {
  open: 300, start: 2000, reply: 10, loader: 3000, reboot: 3000, retry: 1000, poll: 1000, hs: 1000,
  idleCheck: 8000, idleWrite: 180000, waitLoader: 30000, waitReboot: 40000,
};

export type Step =
  | { k: "start"; id: string }
  | { k: "check"; n: number }
  | { k: "loader" }
  | { k: "write"; pct: number }
  | { k: "reboot" }
  | { k: "done"; id: string };

export interface Device {
  link: Link;
  id: Identity;
  name: string;
}

export interface UpdaterOpts {
  /** port names worth probing (catalog family ports) */
  ports: RegExp;
  /** models that are the same keyboard, anchored (catalog family match) */
  family: RegExp;
  /** update loaders this installer may resume, anchored (catalog protocol ownLoader) */
  ownLoader: RegExp;
  /** probe every MIDI port, not only matching ones */
  searchAll?: boolean;
}

const now = () => performance.now();

export async function handshake(link: Link, tries = 3): Promise<Identity | null> {
  link.drain();
  for (let t = 0; t < tries; t++) {
    if (!link.send(HS_QUERY)) return null;
    const end = now() + DELAY.hs;
    for (let left = DELAY.hs; left > 0; left = end - now()) {
      const p = await link.read(left);
      if (!p) break;
      const id = parseIdentity(p);
      if (id) return id;
    }
  }
  return null;
}

export class Updater {
  readonly backend: MidiBackend;
  readonly opts: UpdaterOpts;

  constructor(backend: MidiBackend, opts: UpdaterOpts) {
    this.backend = backend;
    this.opts = opts;
  }

  candidates(): string[] {
    const names = [...new Set(this.backend.inputNames())];
    return this.opts.searchAll ? names : names.filter((n) => this.opts.ports.test(n));
  }

  /** the first matching port that answers the handshake (and want(identity)) */
  async find(want?: (id: Identity) => boolean): Promise<Device | null> {
    // only outputs that match too: pairOutput's "the only output" fallback must never pick unrelated gear
    const outs = [...new Set(this.backend.outputNames())].filter((o) => this.opts.searchAll || this.opts.ports.test(o));
    for (const name of this.candidates()) {
      const out = pairOutput(name, outs);
      if (!out) continue;
      let link: Link;
      try {
        link = await this.backend.open(name, out);
      } catch {
        continue;
      }
      const id = await handshake(link, 2);
      if (id && (!want || want(id))) return { link, id, name };
      link.close();
    }
    return null;
  }

  async waitFor(want: (id: Identity) => boolean, ms: number): Promise<Device | null> {
    const end = now() + ms;
    while (now() < end) {
      const dev = await this.find(want);
      if (dev) return dev;
      await sleep(DELAY.retry);
    }
    return null;
  }

  /** answer read requests until the device asks for `finish` */
  async serve(link: Link, image: Uint8Array, finish: number, idle: number, progress: (served: number, end: number) => void) {
    let served = 0, last = now();
    for (;;) {
      const pkt = await link.read(DELAY.poll);
      if (!pkt) {
        if (link.lost) return { served, finished: false, lost: true };
        if (now() - last > idle) return { served, finished: false, lost: false };
        continue;
      }
      const r = parseRequest(pkt);
      if (!r) continue;
      last = now();
      if (r.addr === FINISH_CHECK || r.addr === FINISH_WRITE) {
        link.send(response(r.addr, SUCCESS));
        if (r.addr === finish) return { served, finished: true, lost: false };
        continue;
      }
      if (r.len > MAXDATA || r.addr + r.len > image.length) {
        throw new InstallError("badreq", `the device asked for 0x${r.addr.toString(16)}+${r.len}, outside the package`);
      }
      if (DELAY.reply) await sleep(DELAY.reply);
      if (!link.send(response(r.addr, image.subarray(r.addr, r.addr + r.len), r.flags))) {
        return { served, finished: false, lost: true };
      }
      served++;
      progress(served, r.addr + r.len);
    }
  }

  /** step 1: the running firmware checks the package and starts the loader */
  async check(dev: Device, image: Uint8Array, step: (s: Step) => void): Promise<Device> {
    step({ k: "start", id: dev.id.text });
    dev.link.send(UPGRADE);
    await sleep(DELAY.start);
    let r;
    try {
      r = await this.serve(dev.link, image, FINISH_CHECK, DELAY.idleCheck, (n) => step({ k: "check", n }));
    } finally {
      dev.link.close();    // also after a bad request, which throws
    }
    if (!r.finished) {
      throw new InstallError(r.lost ? "lost" : "stopped",
        `the keyboard ${r.lost ? "was disconnected" : "stopped answering"} after ${r.served} requests; nothing was written`);
    }
    step({ k: "loader" });
    await sleep(DELAY.loader);
    const ota = await this.waitFor((i) => isLoader(i), DELAY.waitLoader);
    if (!ota) {
      throw new InstallError("noloader", "the update loader did not appear. Replug the USB cable and run the install " +
        "again: the keyboard stays in update mode until it is done");
    }
    return ota;
  }

  /** step 2: the loader reads and writes the whole package, then restarts */
  async write(ota: Device, image: Uint8Array, step: (s: Step) => void): Promise<void> {
    step({ k: "write", pct: 0 });
    ota.link.send(UPGRADE);
    await sleep(DELAY.start);
    let r;
    try {
      r = await this.serve(ota.link, image, FINISH_WRITE, DELAY.idleWrite,
        (_n, end) => step({ k: "write", pct: Math.min(99, Math.floor((end * 100) / image.length)) }));
    } finally {
      ota.link.close();
    }
    if (!r.finished) {
      throw new InstallError(r.lost ? "lost" : "stopped",
        `the loader ${r.lost ? "was disconnected" : "stopped answering"} after ${r.served} requests. Replug the USB ` +
        "cable and run the install again to finish");
    }
    step({ k: "write", pct: 100 });
  }

  async verify(product: string, step: (s: Step) => void): Promise<string> {
    step({ k: "reboot" });
    await sleep(DELAY.reboot);
    const back = await this.waitFor((i) => !isLoader(i), DELAY.waitReboot);
    if (!back) throw new InstallError("noreturn", "the keyboard did not come back: power-cycle it");
    back.link.close();
    if (back.id.text !== product) {
      throw new InstallError("mismatch", `written, but the keyboard reports ${back.id.text} (expected ${product})`);
    }
    step({ k: "done", id: back.id.text });
    return back.id.text;
  }

  /** why this device must not take this package (another model, another firmware's update loader), or null */
  refusal(id: Identity, product: string): InstallError | null {
    if (!sameKeyboard(id.text, product, this.opts.family)) {
      return new InstallError("model", `the device is ${id.text}, the package is for ${product}`);
    }
    if (isLoader(id) && !isOwnLoader(id.text, this.opts.ownLoader)) {
      return new InstallError("foreign", `the keyboard is in the update mode of another firmware (${id.text}): ` +
        "finish that update with its own updater, then install again");
    }
    return null;
  }

  /** running firmware -> loader -> new firmware; dev from find(). A device already in update mode only needs the
   *  write. */
  async install(dev: Device, image: Uint8Array, product: string, step: (s: Step) => void = () => {}): Promise<string> {
    const no = this.refusal(dev.id, product);
    if (no) {
      dev.link.close();
      throw no;
    }
    const ota = isLoader(dev.id) ? dev : await this.check(dev, image, step);
    await this.write(ota, image, step);
    return this.verify(product, step);
  }
}
