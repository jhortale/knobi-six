#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/ob6_import.py on made-up OB-6 dumps (no Sequential data in the repo): the MS-bit packing, the byte layout,
each unit conversion (hand-checked values), the name, the chord, program_pack's record and CRC, the header.

    python3 tests/ob6_import_test.py            the tests
    python3 tests/ob6_import_test.py --write    also (re)writes tests/unit/factory_test_bank.h, the synthetic bank
                                                the C tests (tests/unit/test_factory.c) load
With OB6_SYX=<your own copy of the factory .syx> it also checks that file's structure (500 programs, no anomalies,
clean conversions); nothing of it is in the repo."""
import os
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import ob6_import as m  # noqa: E402

FIXTURE = ROOT / "tests/unit/factory_test_bank.h"
fails = 0


def check(cond, what):
    global fails
    if not cond:
        fails += 1
        print("FAIL", what)


def dump(bank, prog, data):
    return bytes([0xF0, 0x01, 0x2E, 0x02, bank, prog]) + m.pack_ms(data) + b"\xf7"


def program(name, **bytes_at):
    """a 1024-byte OB-6 program: the OB-6 init values, then bytes_at (byte index -> value)"""
    d = bytearray(1024)
    init = {0: 24, 1: 24, 2: 127, 4: 254, 5: 127, 6: 117, 7: 127, 12: 1, 14: 70, 17: 2, 19: 255, 21: 1, 27: 127,
            29: 127, 31: 127, 40: 127, 42: 40, 44: 1, 45: 10, 48: 64, 49: 64, 50: 20, 51: 20, 52: 40, 53: 20, 56: 3,
            57: 3, 59: 134, 60: 11, 70: 127, 77: 127, 78: 127, 87: 120, 89: 2, 92: 6}
    for k, v in init.items():
        d[k] = v
    d[98:103] = bytes([127] * 5)
    for k, v in bytes_at.items():
        d[int(k[1:])] = v
    d[m.NAME_AT:m.NAME_AT + 20] = name.ljust(20).encode()
    return bytes(d)


# the test bank: three made-up programs with hand-chosen bytes (names are ours)
P0 = program("FACTORY ZERO", b2=0, b3=200, b19=128, b23=254, b29=254, b70=0, b77=200, b78=27, b84=1, b85=6,
             b97=0, b98=3, b99=7, b100=10, b101=127, b102=127, b26=1, b25=1, b58=99, b87=250, b91=1, b88=1, b45=13)
P1 = program("Factory One", b2=253, b19=0, b23=127, b35=11, b36=22, b37=33, b38=44, b39=55, b40=66, b41=77, b42=88,
             b66=3, b14=5, b15=2, b16=1, b17=12, b18=99, b86=5)
P2 = program("X", b10=127, b0=60, b1=0)
TEST_SYX = dump(0, 0, P0) + dump(0, 1, P1) + dump(0, 2, P2)


def test_packing():
    data = bytes(range(256)) * 4
    check(m.unpack_ms(m.pack_ms(data)) == data, "pack/unpack round trip")
    check(len(m.pack_ms(bytes(1024))) == 1171, "1024 B pack to 1171 B")
    pk = m.pack_ms(bytes([0x80, 1, 0xFF, 0, 0, 0, 0x81]))
    check(pk[0] == 0b1000101 and pk[1:] == bytes([0, 1, 0x7F, 0, 0, 0, 1]), "MSB byte: bit j = byte j's bit 7")


def test_parse():
    progs, warns = m.parse_syx(b"\xf0\x7e\x7f\x06\x01\xf7" + TEST_SYX)
    check(len(progs) == 3 and len(warns) == 1, f"3 programs and the inquiry skipped ({len(progs)}, {warns})")
    check(progs[1][:2] == (0, 1) and progs[1][2] == P1, "bank, program, data")


def test_convert():
    name, v, chord, w = m.convert(P0)
    g = lambda n: v[m.IDS[n]]  # noqa: E731
    check(name == "FACTORY ZERO" and not w, f"name, no warnings ({name!r} {w})")
    check(g("VCO2_FINE") == -50, "fine 0 -> -50 cents")
    check(g("VCO1_SHAPE") == 200, "shape as is")
    check(g("FLT_FREQ") == 82, f"cutoff 128 -> (128*164+127)//255 = 82 ({g('FLT_FREQ')})")
    check(g("FLT_MODE") == 127, "mode 254 -> 127")
    check(g("FLT_ENV_AMT") == 127 and g("AT_AMT") == -127, "bipolar 254 -> +127, 0 -> -127")
    check(g("XM_FENV") == 73 and g("XM_VCO2") == -100, "x-mod 200 -> +73, 27 -> -100")
    check(g("UNISON_ON") == 1 and g("UNISON_VOICES") == 6, "unison on, chord")
    check(chord == [60, 63, 67, 70, 0, 0], f"chord offsets 0 3 7 10 -> notes from 60 ({chord})")
    check(g("FLT_BP") == 1 and g("XM_D_BP") == 1, "BP (26), x-mod norm-BP (25)")
    check(g("DISTORTION") == 99 and g("BPM") == 250 and g("ARP_ON") == 1 and g("ARP_HOLD") == 1 and g("FXB_TYPE") == 13,
          "reserved: distortion, BPM, arp on, hold, FX B type")
    name, v, chord, w = m.convert(P1)
    g = lambda n: v[m.IDS[n]]  # noqa: E731
    check(g("VCO2_FINE") == 50, f"fine 253 -> +50 ({g('VCO2_FINE')})")
    check(g("FLT_FREQ") == 0 and g("FLT_MODE") == 64, f"cutoff 0, mode 127 -> 64 ({g('FLT_MODE')})")
    check([g(n) for n in ("FENV_A", "LENV_A", "FENV_D", "LENV_D", "FENV_S", "LENV_S", "FENV_R", "LENV_R")] ==
          [11, 22, 33, 44, 55, 66, 77, 88], "the envelopes interleave filter / loudness")
    check(g("LFO_D_PW") == 3 and g("PORTA_RATE") == 5 and g("PORTA_MODE") == 2 and g("PORTA_ON") == 1, "lfo pw, porta")
    check(g("BEND_RANGE") == 12 and g("DETUNE") == 99 and g("KEY_MODE") == 5, "bend, detune, key mode")
    check(chord == [0] * 6, "no chord captured: empty")
    _n, v2, _c, w = m.convert(program("bad", b0=61, b21=3))
    check(len(w) == 2 and v2[m.IDS["VCO1_FREQ"]] == 60 and v2[m.IDS["FLT_TRACK"]] == 2, f"out of range: clamped ({w})")


def test_pack_and_header():
    name, v, chord, _w = m.convert(P0)
    r = m.program_pack(name, v, chord)
    check(len(r) == 201 and r[0] == 2, "record version 2, 201 B")
    check(r[1:13] == b"FACTORY ZERO" and r[13:21] == bytes(8), "name NUL-padded")
    check(int.from_bytes(r[-2:], "little") == m.crc16(r[:-2]), "crc")
    check(m.crc16(b"123456789") == 0x29B1, "CRC-16/CCITT-FALSE check value")
    with tempfile.TemporaryDirectory() as t:
        syx = Path(t) / "t.syx"
        syx.write_bytes(TEST_SYX)
        out, rep = Path(t) / "bank.h", Path(t) / "rep.txt"
        check(m.main([str(syx), "-o", str(out), "--report", str(rep)]) == 0, "main")
        h = out.read_text()
        check("#define FACTORY_COUNT 3u" in h and "#define FACTORY_SIZE 201u" in h, "header defines")
        check("Anomalies: none" in rep.read_text(), "report: no anomalies")
        return h


def test_real_bank():
    syx = os.environ.get("OB6_SYX")
    if not syx:
        print("(OB6_SYX not set: the real factory bank is not checked)")
        return
    progs, warns = m.parse_syx(Path(syx).read_bytes())
    check(len(progs) == 500 and not warns, "500 programs")
    _l, anomalies = m.verify(progs)
    check(not anomalies, f"no anomalies {anomalies}")
    # structure only (the repo holds no OB-6 names or values): banks 0..4 complete, every program converts cleanly
    check(sorted(b * 100 + p for b, p, _d in progs) == list(range(500)), "programs 000..499, once each")
    for b, p, d in progs:
        name, _v, _c, w = m.convert(d)
        check(name != "" and not w, f"program {b * 100 + p:03d}: an empty name or warnings {w}")


test_packing()
test_parse()
test_convert()
h = test_pack_and_header()
test_real_bank()
if "--write" in sys.argv:
    FIXTURE.write_text(h.replace("from t.syx", "from tests/ob6_import_test.py (3 synthetic programs, no OB-6 data)")
                       .replace("firmware/gen/factory_bank.h", "tests/unit/factory_test_bank.h")
                       .replace(" * DO NOT COMMIT OR REDISTRIBUTE: the programs are Sequential's (docs/FACTORY_BANK.md); "
                                "for your own build only.\n", ""))
    print("wrote", FIXTURE)
elif FIXTURE.exists():
    want = h.replace("from t.syx", "from tests/ob6_import_test.py (3 synthetic programs, no OB-6 data)") \
        .replace("firmware/gen/factory_bank.h", "tests/unit/factory_test_bank.h") \
        .replace(" * DO NOT COMMIT OR REDISTRIBUTE: the programs are Sequential's (docs/FACTORY_BANK.md); "
                 "for your own build only.\n", "")
    check(FIXTURE.read_text() == want, "tests/unit/factory_test_bank.h is current (--write)")
print("ob6_import_test:", "ok" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
