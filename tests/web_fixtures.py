#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Fixtures for knobi-flash (web/): what tools/fm1_install.py answers for a fixed set of inputs, so the TypeScript port
# is checked against it (web/test/*.test.ts). package() is install_test.py's helper (that file runs its tests on import).
# port of tools/fm1_install.py: Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments;
# M. Geisler (genkigenki/makkuro)
#   python3 tests/web_fixtures.py --out packages/installer-core/test/fixtures/python.json
#   python3 tests/web_fixtures.py --check packages/installer-core/test/fixtures/python.json     (exit 1 when it differs)
import base64
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_install as I  # noqa: E402


def package(product="FM-1_900", marker=True, size=0x2000, damage=False, blocks=20, seed=7):
    """a package with a valid header and file list (one file: 0x400 to the end), as fm1pkg_make writes"""
    img = bytearray((i * seed) & 0xFF for i in range(size))
    img[0x1800:0x1800 + 16] = I.LOADER_MARK if marker else bytes(16)
    data = bytes(img[0x400:])
    e = bytearray(0x50)
    e[0:16] = (0).to_bytes(2, "little") + (0).to_bytes(2, "little") + I._crc16(data).to_bytes(2, "little") + bytes(2) + \
        (0x400).to_bytes(4, "little") + len(data).to_bytes(4, "little")
    e[0x40:0x49] = b"flash.bin"
    lst = I._dec(bytes(e))
    hdr = bytearray(0x40)
    hdr[4:8] = size.to_bytes(4, "little")
    hdr[8:10] = (1).to_bytes(2, "little")
    hdr[2:4] = I._crc16(lst).to_bytes(2, "little")
    hdr[0:2] = I._crc16(bytes(hdr[2:0x40])).to_bytes(2, "little")
    img[0:0x40] = I._dec(bytes(hdr))
    img[0x40:0x90] = lst
    if damage:
        img[0x1000] ^= 1
    raw = bytearray()
    for i in range(blocks):
        m = (ord(product[i]) + i + 1) & 0xFF if i < len(product) else 0x7D
        raw += img[i * I.KEEP:(i + 1) * I.KEEP] + bytes([m])
    raw += img[blocks * I.KEEP:]
    return bytes(raw)


def request(addr, n, fl=0):
    u = bytearray([0, 0x59, 0x30, 0, 0, 0, fl]) + addr.to_bytes(4, "little") + bytes([n & 0xFF, n >> 8, 0])
    u.append(~sum(u[6:14]) & 0xFF)
    return b"\xF0" + I.pack7(bytes(u)) + b"\xF7"


def identity_reply(text):
    t = text.encode()
    return b"\xF0" + I.pack7(bytes([0, 0x59, 0x11, 0, 0, 0]) + t + bytes(28 - len(t))) + b"\xF7"


def fixtures():
    h = bytes.hex
    pack = [b"", b"\x00", bytes(range(256)) * 3]
    responses = [(0x12345, b"\x01\x02\x03", 5), (0, bytes((i * 7) & 0xFF for i in range(512)), 0),
                 (I.FINISH_WRITE, b"success\0", 0)]
    good = request(0x1000, 512, 3)
    bad = bytearray(good)
    bad[-2] ^= 1
    wire = {
        "pack7": [{"in": h(d), "out": h(I.pack7(d))} for d in pack],
        "response": [{"addr": a, "data": h(d), "flags": f, "out": h(I.response(a, d, f))} for a, d, f in responses],
        "request": [{"pkt": h(p), "parsed": (lambda r: r and {"flags": r[0], "addr": r[1], "len": r[2]})(I.parse_request(p))}
                    for p in (good, bytes(bad))],
        "identity": [{"text": t, "pkt": h(identity_reply(t)),
                      "parsed": (lambda i: {"text": i.text, "model": i.model, "version": i.version})(
                          I.parse_identity(identity_reply(t)))}
                     for t in ("SMK-37 Pro_956", "ota-SMK-37 Pro_900", "FM-1_015")],
    }
    cases = [
        dict(product="FM-1_906"),
        dict(product="SMK-37 Pro_956"),
        dict(product="SMK-37 Pro_016", blocks=36),
        dict(product="SMK-37 Pro_956", damage=True),
        dict(product="SMK-37 Pro_956", marker=False),
        dict(product="STARRYKEY-37 PLAY_016", blocks=36, size=0x3000),
    ]
    packages = []
    for c in cases:
        raw = package(**c)
        image, blocks = I.deinterleave(raw)
        packages.append({"params": c, "raw": base64.b64encode(raw).decode(), "product": I.product_of(raw),
                         "blocks": blocks, "imageSha256": hashlib.sha256(image).hexdigest(),
                         "damage": I.package_damage(image), "loaderMark": I.LOADER_MARK in raw})
    return {"wire": wire, "packages": packages}


def main(argv):
    text = json.dumps(fixtures(), indent=1) + "\n"
    if len(argv) == 2 and argv[0] == "--out":
        Path(argv[1]).write_text(text)
    elif len(argv) == 2 and argv[0] == "--check":
        if Path(argv[1]).read_text() != text:
            print(f"{argv[1]} is out of date: python3 tests/web_fixtures.py --out {argv[1]}", file=sys.stderr)
            return 1
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
