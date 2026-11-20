// SPDX-License-Identifier: GPL-3.0-only
import { describe, expect, it } from "vitest";
import { pack7, unpack7 } from "../src/protocol/sevenbit.ts";
import {
  FINISH_CHECK, FINISH_WRITE, HS_QUERY, MAXDATA, UPGRADE, identityReply, parseIdentity, parseRequest, response,
} from "../src/protocol/messages.ts";
import { fixtures, hex, unhex } from "./helpers/fixtures.ts";

const wire = fixtures.wire;

describe("seven-bit packing (fm1_install.py pack7 / unpack7)", () => {
  it.each(wire.pack7)("pack7 of $in.length hex digits equals Python's", (f) => {
    expect(hex(pack7(unhex(f.in)))).toBe(f.out);
  });
  it("unpack7(pack7(x)) starts with x", () => {
    const x = Uint8Array.from({ length: 768 }, (_, i) => i & 0xff);
    expect(unpack7(pack7(x)).subarray(0, x.length)).toEqual(x);
  });
});

describe("messages", () => {
  it("constants are the wire's", () => {
    expect(hex(HS_QUERY)).toBe("f0003245000000407ff7");
    expect(hex(UPGRADE)).toBe("f02224357ff7");
    expect(FINISH_CHECK).toBe(0xe0000000);
    expect(FINISH_WRITE).toBe(0xf0000000);
    expect(MAXDATA).toBe(512);
  });
  it.each(wire.response)("response(addr $addr, flags $flags) equals Python's", (f) => {
    expect(hex(response(f.addr, unhex(f.data), f.flags))).toBe(f.out);
  });
  it.each(wire.request)("parseRequest equals Python's parse_request", (f) => {
    expect(parseRequest(unhex(f.pkt))).toEqual(f.parsed);
  });
  it.each(wire.identity)("parseIdentity($text) and identityReply equal Python's", (f) => {
    expect(hex(identityReply(f.text))).toBe(f.pkt);
    expect(parseIdentity(unhex(f.pkt))).toEqual(f.parsed);
  });
  it("splits SMK-37 Pro_956 into model and version", () => {
    expect(parseIdentity(identityReply("SMK-37 Pro_956"))).toEqual({ text: "SMK-37 Pro_956", model: "SMK-37 Pro", version: 956 });
  });
  it("refuses a reply that is not an identity", () => {
    expect(parseIdentity(Uint8Array.of(0xf0, 1, 2, 0xf7))).toBeNull();
    expect(parseIdentity(identityReply("no version"))).toBeNull();
  });
});
