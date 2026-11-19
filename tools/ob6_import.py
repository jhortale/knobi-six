#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Import OB-6 program dumps (a .syx file) as KNOBI-SIX's read-only factory bank.

    python3 tools/ob6_import.py OB6_Programs_v1.01.syx      -> firmware/gen/factory_bank.h + build/factory_bank_report.txt
    python3 tools/ob6_import.py FILE.syx -o OUT.h --report R.txt
    python3 tools/ob6_import.py --synthetic 500 -o OUT.h      (made-up programs, no OB-6 data: size checks, tests)

The OB-6 programs belong to Sequential (Dave Smith Instruments). The generated header holds their data: it is for your
own build only. It is ignored by git: never commit or redistribute it (docs/FACTORY_BANK.md).

Input: any number of SysEx program dumps, F0 01 2E 02 <bank 0-9> <program 0-99> <1171 bytes> F7 (other messages are
skipped with a warning). The 1171 bytes are "packed MS bit": groups of 8, the first byte of a group holds bit 7 of the
next 7 bytes (bit j -> byte j). Unpacked: 1024 bytes.

The unpacked layout is NOT the NRPN numbering (the name is at 107, NRPN 236): it is the OB-6's internal parameter
order, the Prophet-6's (Sequential's "P6 packed parameter data assignments", posted on forum.sequential.com) with the
Prophet-6's high-pass slots reused by the OB-6. Each byte was checked against the 500 factory programs (ranges,
distributions, names; the report lists them): see LAYOUT below and docs/FACTORY_BANK.md.

Output: program n = bank * 100 + program, packed with program_pack's format (params.c: version, name[20], v[] as LE
int16, chord[6], CRC-16/CCITT-FALSE), FACTORY_COUNT records of FACTORY_SIZE bytes; and the programs' sequences
(offset 128, decode_seq) in seq_pack's format as FACTORY_SEQ_COUNT / factory_seq_at / factory_seq_data
(params/prog_seq.h), when any program has one.
"""
import argparse
import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# ---- KNOBI-SIX's program model (firmware/src/params/params.h): ids in enum order, ranges as PARAMS / PARAMS_RESERVED
PHASE1 = [  # (name, min, max)
    ("VCO1_FREQ", 0, 60), ("VCO1_SHAPE", 0, 255), ("VCO1_PW", 0, 255), ("VCO1_SYNC", 0, 1), ("VCO2_FREQ", 0, 60),
    ("VCO2_FINE", -50, 50), ("VCO2_SHAPE", 0, 254), ("VCO2_PW", 0, 255), ("VCO2_LOWFREQ", 0, 1), ("VCO2_KEYBOARD", 0, 1),
    ("MIX_VCO1", 0, 127), ("MIX_VCO2", 0, 127), ("MIX_SUB", 0, 127), ("MIX_NOISE", 0, 127), ("FLT_FREQ", 0, 164),
    ("FLT_RESO", 0, 255), ("FLT_MODE", 0, 127), ("FLT_BP", 0, 1), ("FLT_TRACK", 0, 2), ("FLT_ENV_AMT", -127, 127),
    ("FLT_VEL", 0, 1), ("FENV_A", 0, 127), ("FENV_D", 0, 127), ("FENV_S", 0, 127), ("FENV_R", 0, 127),
    ("LENV_A", 0, 127), ("LENV_D", 0, 127), ("LENV_S", 0, 127), ("LENV_R", 0, 127), ("LENV_AMT", 0, 127),
    ("LENV_VEL", 0, 1), ("LFO_FREQ", 0, 254), ("LFO_SYNC", 0, 1), ("LFO_SHAPE", 0, 4), ("LFO_AMT", 0, 255),
    ("LFO_D_VCO1", 0, 1), ("LFO_D_VCO2", 0, 1), ("LFO_D_PW", 0, 3), ("LFO_D_AMP", 0, 1), ("LFO_D_FFREQ", 0, 1),
    ("LFO_D_FMODE", 0, 1), ("XM_FENV", -127, 127), ("XM_VCO2", -127, 127), ("XM_D_VCO1", 0, 1), ("XM_D_SHAPE1", 0, 1),
    ("XM_D_PW1", 0, 1), ("XM_D_FFREQ", 0, 1), ("XM_D_FMODE", 0, 1), ("XM_D_BP", 0, 1), ("AT_AMT", -127, 127),
    ("AT_D_VCO1", 0, 1), ("AT_D_VCO2", 0, 1), ("AT_D_LFOAMT", 0, 1), ("AT_D_AMP", 0, 1), ("AT_D_FFREQ", 0, 1),
    ("AT_D_FMODE", 0, 1), ("DETUNE", 0, 127), ("PAN_SPREAD", 0, 127), ("BEND_RANGE", 0, 12), ("KEY_MODE", 0, 5),
    ("PRG_VOL", 0, 127), ("PORTA_ON", 0, 1), ("PORTA_MODE", 0, 3), ("PORTA_RATE", 0, 127), ("UNISON_ON", 0, 1),
    ("UNISON_VOICES", 0, 6),
]
RESERVED = [  # (name, min, max, default), params.c PARAMS_RESERVED
    ("DISTORTION", 0, 127, 0), ("FX_ON", 0, 1, 0),
    ("FXA_TYPE", 0, 9, 0), ("FXA_MIX", 0, 127, 0), ("FXA_P1", 0, 255, 0), ("FXA_P2", 0, 127, 0), ("FXA_SYNC", 0, 1, 0),
    ("FXA_DIV", 0, 10, 0),
    ("FXB_TYPE", 0, 13, 0), ("FXB_MIX", 0, 127, 0), ("FXB_P1", 0, 255, 0), ("FXB_P2", 0, 127, 0), ("FXB_SYNC", 0, 1, 0),
    ("FXB_DIV", 0, 10, 0),
    ("BPM", 30, 250, 120), ("CLK_DIV", 0, 9, 1), ("ARP_ON", 0, 1, 0), ("ARP_MODE", 0, 4, 0), ("ARP_RANGE", 0, 2, 0),
    ("ARP_HOLD", 0, 1, 0),
]
IDS = {n: i for i, (n, *_r) in enumerate(PHASE1 + [r[:3] for r in RESERVED])}
RANGE = {n: (lo, hi) for n, lo, hi in PHASE1}
RANGE.update({n: (lo, hi) for n, lo, hi, _d in RESERVED})
N_PHASE1 = len(PHASE1)                    # 66 = P_COUNT_PHASE1
N_VALUES = N_PHASE1 + len(RESERVED)       # 86 = P_COUNT_RESERVED
PROGRAM_VERSION = 2
NAME_LEN = 20
RECORD_SIZE = 1 + NAME_LEN + 2 * N_VALUES + 6 + 2   # program_packed_size(2)

# ---- the OB-6 dump layout: unpacked byte -> (our param, conversion, the byte's own range, note)
# conversions: "=" as is; "bi" bipolar 0..254 (centre 127) -> -127..+127; "cents" 0..254 -> -50..+50 cents;
# "ff" filter cutoff 0..255 -> 0..164; "fm" filter mode 0..254 -> 0..127
LAYOUT = [
    (0, "VCO1_FREQ", "=", (0, 60), "semitones"),
    (1, "VCO2_FREQ", "=", (0, 60), "semitones"),
    (2, "VCO2_FINE", "cents", (0, 254), "0..254, centre 127 = +-50 cents"),
    (3, "VCO1_SHAPE", "=", (0, 255), "saw .. pulse"),
    (4, "VCO2_SHAPE", "=", (0, 254), "tri .. saw .. pulse"),
    (5, "VCO1_PW", "=", (0, 255), ""),
    (6, "VCO2_PW", "=", (0, 255), ""),
    (7, "MIX_VCO1", "=", (0, 127), ""),
    (8, "MIX_VCO2", "=", (0, 127), ""),
    (9, "MIX_SUB", "=", (0, 127), ""),
    (10, "MIX_NOISE", "=", (0, 127), ""),
    (11, "VCO1_SYNC", "=", (0, 1), ""),
    (12, "VCO2_KEYBOARD", "=", (0, 1), ""),
    (13, "VCO2_LOWFREQ", "=", (0, 1), ""),
    (14, "PORTA_RATE", "=", (0, 127), "the Prophet-6's glide rate slot"),
    (15, "PORTA_MODE", "=", (0, 3), "rate, rate-A, time, time-A"),
    (16, "PORTA_ON", "=", (0, 1), ""),
    (17, "BEND_RANGE", "=", (0, 12), "semitones"),
    (18, "DETUNE", "=", (0, 127), "voice slop"),
    (19, "FLT_FREQ", "ff", (0, 255), "the dump keeps 0..255; the NRPN (and KNOBI-SIX) 0..164"),
    (20, "FLT_RESO", "=", (0, 255), ""),
    (21, "FLT_TRACK", "=", (0, 2), "off, half, full"),
    (22, "FLT_VEL", "=", (0, 1), ""),
    (23, "FLT_MODE", "fm", (0, 254), "the Prophet-6's HP cutoff slot; the dump keeps 0..254, the NRPN 0..127"),
    (25, "XM_D_BP", "=", (0, 1), "INFERRED: the Prophet-6's HP key slot; set almost only where an X-MOD amount is"),
    (26, "FLT_BP", "=", (0, 1), "the Prophet-6's HP velocity slot"),
    (27, "PRG_VOL", "=", (0, 127), ""),
    (28, "PAN_SPREAD", "=", (0, 127), ""),
    (29, "FLT_ENV_AMT", "bi", (0, 254), ""),
    (31, "LENV_AMT", "=", (0, 127), ""),
    (35, "FENV_A", "=", (0, 127), "the envelopes interleave: filter A, loudness A, filter D, ..."),
    (36, "LENV_A", "=", (0, 127), ""),
    (37, "FENV_D", "=", (0, 127), ""),
    (38, "LENV_D", "=", (0, 127), ""),
    (39, "FENV_S", "=", (0, 127), ""),
    (40, "LENV_S", "=", (0, 127), ""),
    (41, "FENV_R", "=", (0, 127), ""),
    (42, "LENV_R", "=", (0, 127), ""),
    (43, "LENV_VEL", "=", (0, 1), ""),
    (44, "FXA_TYPE", "=", (0, 9), "10 types (the NRPN table's 0-5 is wrong)"),
    (45, "FXB_TYPE", "=", (0, 13), "14 types incl. the 4 reverbs (the NRPN table's 0-9 is wrong)"),
    (46, "FX_ON", "=", (0, 1), ""),
    (48, "FXA_MIX", "=", (0, 127), ""),
    (49, "FXB_MIX", "=", (0, 127), ""),
    (50, "FXA_P1", "=", (0, 255), ""),
    (51, "FXB_P1", "=", (0, 255), ""),
    (52, "FXA_P2", "=", (0, 127), ""),
    (53, "FXB_P2", "=", (0, 127), ""),
    (54, "FXA_SYNC", "=", (0, 1), ""),
    (55, "FXB_SYNC", "=", (0, 1), ""),
    (56, "FXA_DIV", "=", (0, 10), "the synced delay's division (not in the NRPN table)"),
    (57, "FXB_DIV", "=", (0, 10), ""),
    (58, "DISTORTION", "=", (0, 127), ""),
    (59, "LFO_FREQ", "=", (0, 254), ""),
    (61, "LFO_SYNC", "=", (0, 1), ""),
    (62, "LFO_SHAPE", "=", (0, 4), "sine, saw, reverse saw, square, random"),
    (63, "LFO_AMT", "=", (0, 255), ""),
    (64, "LFO_D_VCO1", "=", (0, 1), ""),
    (65, "LFO_D_VCO2", "=", (0, 1), ""),
    (66, "LFO_D_PW", "=", (0, 3), "none, PW1, PW2, both"),
    (67, "LFO_D_FFREQ", "=", (0, 1), ""),
    (68, "LFO_D_FMODE", "=", (0, 1), ""),
    (69, "LFO_D_AMP", "=", (0, 1), ""),
    (70, "AT_AMT", "bi", (0, 254), ""),
    (71, "AT_D_VCO1", "=", (0, 1), ""),
    (72, "AT_D_VCO2", "=", (0, 1), ""),
    (73, "AT_D_FFREQ", "=", (0, 1), ""),
    (74, "AT_D_FMODE", "=", (0, 1), ""),
    (75, "AT_D_AMP", "=", (0, 1), ""),
    (76, "AT_D_LFOAMT", "=", (0, 1), ""),
    (77, "XM_FENV", "bi", (0, 254), ""),
    (78, "XM_VCO2", "bi", (0, 254), ""),
    (79, "XM_D_VCO1", "=", (0, 1), ""),
    (80, "XM_D_SHAPE1", "=", (0, 1), ""),
    (81, "XM_D_PW1", "=", (0, 1), ""),
    (82, "XM_D_FFREQ", "=", (0, 1), ""),
    (83, "XM_D_FMODE", "=", (0, 1), ""),
    (84, "UNISON_ON", "=", (0, 1), "the Prophet-6's unison slot"),
    (85, "UNISON_VOICES", "=", (0, 6), "0..5 = 1..6 voices, 6 = chord memory"),
    (86, "KEY_MODE", "=", (0, 5), "low, low-R, high, high-R, last, last-R"),
    (87, "BPM", "=", (30, 250), ""),
    (88, "ARP_HOLD", "off", (0, 1), "INFERRED slot, unverified: always imported as 0 (off)"),
    (89, "ARP_MODE", "=", (0, 4), ""),
    (90, "ARP_RANGE", "=", (0, 2), "1..3 octaves"),
    (91, "ARP_ON", "=", (0, 1), "the Prophet-6's arp on/off slot"),
    (92, "CLK_DIV", "=", (0, 9), ""),
]
UNMAPPED = {  # bytes read but not mapped (the report checks what they hold)
    24: "unused (the Prophet-6's HP resonance)", 30: "unused (the Prophet-6's HP env amount)",
    32: "unused (P6 env vel amount)", 33: "unused (P6 env vel amount)", 34: "unused (P6 env vel amount)",
    47: "unused (the Prophet-6's FX B on/off)", 60: "LFO retrigger (internal; 0 or 11)",
    93: "seq on", 94: "seq record", 95: "seq mode", 96: "seq play mode",
    97: "chord note 1 (offset, always 0 = the root)", 98: "chord note 2", 99: "chord note 3", 100: "chord note 4",
    101: "chord note 5", 102: "chord note 6", 103: "unused", 104: "unused", 105: "sound version",
    106: "editor byte", 127: "end of the sound parameters (a marker)",
}
NAME_AT = 107
CHORD_AT = 97                     # 6 offsets from the root in semitones, 127 = empty
CHORD_ROOT = 60                   # KNOBI-SIX's chord is absolute notes; only their intervals count (voices.c)
SEQ_AT = 128                      # the sequence: 6 lanes x (64 notes, 64 velocities), decode_seq
SEQ_LANES, SEQ_STEPS = 6, 64
SEQ_NOTE_MIN, SEQ_NOTE_MAX = 12, 108                 # synth/seq.h: what seq_unpack takes
SEQ_FILLER = 60                   # the note byte of a lane that holds nothing (and of a rest / tie step)


def unpack_ms(packed):
    out = bytearray()
    for g in range(0, len(packed), 8):
        msb = packed[g]
        for j, b in enumerate(packed[g + 1:g + 8]):
            out.append(b | ((msb >> j) & 1) << 7)
    return bytes(out)


def pack_ms(data):
    """The inverse of unpack_ms (the tests build .syx files with it)."""
    out = bytearray()
    for g in range(0, len(data), 7):
        chunk = data[g:g + 7]
        out.append(sum(((b >> 7) & 1) << j for j, b in enumerate(chunk)))
        out += bytes(b & 0x7F for b in chunk)
    return bytes(out)


def parse_syx(blob):
    """-> ([(bank, prog, data1024)], [warnings])"""
    progs, warns, i = [], [], 0
    while True:
        s = blob.find(b"\xf0", i)
        if s < 0:
            break
        e = blob.find(b"\xf7", s)
        if e < 0:
            warns.append(f"offset {s}: a SysEx message without its F7 end: ignored")
            break
        m, i = blob[s:e + 1], e + 1
        if m[1:4] != b"\x01\x2e\x02" or len(m) != 6 + 1171 + 1:
            warns.append(f"offset {s}: not an OB-6 program dump ({m[1:4].hex()}, {len(m)} B): skipped")
            continue
        data = unpack_ms(m[6:-1])
        if len(data) != 1024:
            warns.append(f"offset {s}: unpacked {len(data)} B, not 1024: skipped")
            continue
        progs.append((m[4], m[5], data))
    return progs, warns


def _conv(kind, x):
    if kind == "=":
        return x
    if kind == "off":                   # read and reported, never carried (ARP_HOLD: the slot is unverified)
        return 0
    if kind == "bi":
        return x - 127
    if kind == "cents":
        d = x - 127
        return (1 if d >= 0 else -1) * ((abs(d) * 50 + 63) // 127)
    if kind == "ff":
        return (x * 164 + 127) // 255
    if kind == "fm":
        return (x * 127 + 127) // 254
    raise ValueError(kind)


def convert(data):
    """One unpacked OB-6 program -> (name, v[N_VALUES], chord[6], [warnings])."""
    warns = []
    v = [0] * N_VALUES
    for i, (n, lo, hi, d) in enumerate(RESERVED):
        v[N_PHASE1 + i] = d
    for byte, pname, kind, (blo, bhi), _note in LAYOUT:
        x = data[byte]
        if not blo <= x <= bhi:
            warns.append(f"byte {byte} ({pname}) = {x}, outside {blo}..{bhi}: clamped")
        y = _conv(kind, x)
        lo, hi = RANGE[pname]
        if not lo <= y <= hi:
            y = max(lo, min(hi, y))
        v[IDS[pname]] = y
    raw = data[NAME_AT:NAME_AT + NAME_LEN]
    name = "".join(chr(c) if 32 <= c <= 126 else "?" for c in raw).rstrip()
    if any(not 32 <= c <= 126 for c in raw):
        warns.append(f"name {raw!r}: a character outside ASCII 32..126 shown as '?'")
    offs = list(data[CHORD_AT:CHORD_AT + 6])
    chord = []
    if any(o not in (0, 127) for o in offs):           # all 0 / 127: no chord captured (the OB-6 then stacks the root)
        for o in offs:
            if o == 127:
                continue
            if o > 127 - CHORD_ROOT:
                warns.append(f"chord offset {o} too high: dropped")
                continue
            note = CHORD_ROOT + o
            if note not in chord:
                chord.append(note)
    chord = (chord + [0] * 6)[:6]
    return name, v, chord, warns


def decode_seq(data):
    """One unpacked OB-6 program -> (steps, issues). The decoding (docs/FACTORY_BANK.md "The OB-6 sequence"): lane L
    (0..5) of step s (0..63) is note byte SEQ_AT + 128 L + s and velocity byte that + 64. A velocity byte's bit 7 = the
    lane holds a note (its velocity the low 7 bits). A step exists while lane 0 holds one: the length is the run of
    such steps from step 1 (the first step without it ends the sequence). Lane 0's note byte with bit 7 set = a tie (the
    previous step held on); else lane 0's velocity 0 with no other lane = a rest; else the lanes that hold a note, from
    lane 0 up to the first that holds none (a lane past it is ignored).
    steps: [(tie, [(note, vel), ...])], a rest = (0, []), a tie carries a copy of the previous step's notes (seq.h).
    Never what seq_unpack refuses: notes outside 12..108 are dropped (as seq_rec_step does), a step left with none is a
    rest, a tie at step 1 or after a rest is a rest. issues: Counter of what was odd (each counted, nothing kept)."""
    issues = Counter()
    nb = lambda lane, s: data[SEQ_AT + 128 * lane + s]          # noqa: E731
    vb = lambda lane, s: data[SEQ_AT + 128 * lane + 64 + s]     # noqa: E731
    steps = []
    for s in range(SEQ_STEPS):
        if not vb(0, s) & 0x80:
            break
        on = [lane for lane in range(SEQ_LANES) if vb(lane, s) & 0x80]
        run = next((k for k in range(SEQ_LANES) if k not in on), SEQ_LANES)
        if len(on) > run:                                       # the OB-6 fills lanes from lane 1 up: a lane past an
            issues["lane_gap"] += len(on) - run                 # empty one is not a note
            on = on[:run]
        issues["stray_lane"] += sum(1 for lane in range(SEQ_LANES) if 0 < vb(lane, s) < 0x80)
        if nb(0, s) & 0x80:                                     # a tie
            if nb(0, s) != 0x80 | SEQ_FILLER or vb(0, s) != 0x80 or len(on) > 1:
                issues["tie_bits"] += 1
            if not steps or not steps[-1][1]:
                issues["orphan_tie"] += 1
                steps.append((0, []))
            else:
                steps.append((1, list(steps[-1][1])))
            continue
        if vb(0, s) == 0x80 and len(on) == 1:                   # a rest
            if nb(0, s) != SEQ_FILLER:
                issues["rest_bits"] += 1
            steps.append((0, []))
            continue
        notes = []
        for lane in on:
            n = nb(lane, s)
            if not SEQ_NOTE_MIN <= n <= SEQ_NOTE_MAX:
                issues["note_range"] += 1
                continue
            notes.append((n, vb(lane, s) & 0x7F))
        if not notes:
            issues["empty_step"] += 1
        steps.append((0, notes))
    for s in range(len(steps) + 1, SEQ_STEPS):                  # past the end: nothing should be on
        if vb(0, s) & 0x80:
            issues["after_end"] += 1
    return steps, issues


def seq_pack(steps):
    """synth/seq.c seq_pack: length, then per step count | tie << 3 and the (note, velocity) pairs. Empty: 00."""
    out = bytearray([len(steps)])
    for tie, notes in steps:
        out.append(len(notes) | (tie << 3))
        for n, v in notes:
            out += bytes([n, v])
    return bytes(out)


def crc16(d):
    c = 0xFFFF
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def program_pack(name, v, chord):
    """params.c program_pack, record version 2."""
    out = bytearray([PROGRAM_VERSION])
    nb = name.encode("ascii", "replace")[:NAME_LEN]
    out += nb + bytes(NAME_LEN - len(nb))
    for x in v:
        out += (x & 0xFFFF).to_bytes(2, "little")
    out += bytes(chord)
    out += crc16(out).to_bytes(2, "little")
    assert len(out) == RECORD_SIZE
    return bytes(out)


def write_header(path, records, source, seqs=None):
    """records: {n: packed bytes}; seqs: {n: seq_pack bytes} (the factory sequences: emitted when any is not empty;
    an empty sequence and a gap are empty spans, "none")"""
    count = max(records) + 1 if records else 0
    seqs = {n: b for n, b in (seqs or {}).items() if b != b"\x00"}
    lines = [
        "/* firmware/gen/factory_bank.h: generated by tools/ob6_import.py from " + source + ".",
        " * DO NOT COMMIT OR REDISTRIBUTE: the programs are Sequential's (docs/FACTORY_BANK.md); for your own build only.",
        f" * {len(records)} programs, record version {PROGRAM_VERSION} (params.c program_pack), {RECORD_SIZE} B each. */",
        "#ifndef KNOBI_FACTORY_BANK_H",
        "#define KNOBI_FACTORY_BANK_H",
        f"#define FACTORY_COUNT {count}u",
        f"#define FACTORY_SIZE {RECORD_SIZE}u",
        "static const uint8_t factory_bank[FACTORY_COUNT][FACTORY_SIZE] = {",
    ]
    for n in range(count):
        r = records.get(n, bytes(RECORD_SIZE))     # (a gap: all zeros, which loads as the init program)
        nm = r[1:1 + NAME_LEN].rstrip(b"\0").decode("ascii", "replace").replace("*/", "* /")
        lines.append(f"    /* {n:03d} {nm} */")
        for k in range(0, len(r), 24):
            lines.append("    " + ("{" if k == 0 else " ") + ",".join(str(b) for b in r[k:k + 24]) +
                         ("}," if k + 24 >= len(r) else ","))
    lines.append("};")
    if seqs:
        sc = max(seqs) + 1
        at, data = [0], bytearray()
        for n in range(sc):
            data += seqs.get(n, b"")
            at.append(len(data))
        lines += [
            f"/* the factory sequences (params/prog_seq.h): {len(seqs)} programs carry one, {len(data)} B, seq_pack's"
            " format; an empty span = none */",
            f"#define FACTORY_SEQ_COUNT {sc}u",
            "static const uint32_t factory_seq_at[FACTORY_SEQ_COUNT + 1u] = {" + ",".join(f"{x}u" for x in at) + "};",
            f"static const uint8_t factory_seq_data[{len(data)}] = {{",
        ]
        for n in range(sc):
            b = seqs.get(n)
            if b:
                lines.append(f"    /* {n:03d} */ " + ",".join(str(x) for x in b) + ",")
        lines.append("};")
    lines += ["#endif", ""]
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text("\n".join(lines))
    return count


def verify(progs):
    """The layout against every program: each mapped byte's range, each unmapped byte's values, the distributions.
    -> (report lines, anomalies)"""
    lines, anomalies = [], []
    lines.append(f"{len(progs)} programs. Each OB-6 byte: its range, what the programs hold (min..max, distinct values,"
                 " the commonest), our parameter and conversion.")
    for byte, pname, kind, (blo, bhi), note in LAYOUT:
        vals = [d[byte] for _b, _p, d in progs]
        c = Counter(vals)
        lo, hi = min(vals), max(vals)
        flag = ""
        if lo < blo or hi > bhi:
            flag = "  ** OUT OF RANGE"
            anomalies.append(f"byte {byte} {pname}: {lo}..{hi} outside {blo}..{bhi}")
        lines.append(f"  {byte:3d} -> {pname:14s} {kind:5s} range {blo}..{bhi}: holds {lo}..{hi}, {len(c)} values,"
                     f" commonest {c.most_common(3)}{flag}{'  (' + note + ')' if note else ''}")
    nh = sum(1 for _b, _p, d in progs if d[88])
    lines.append(f"ARP HOLD bytes set: {nh} of {len(progs)} programs - imported as off")
    lines.append("Unmapped bytes:")
    for byte, what in sorted(UNMAPPED.items()):
        vals = [d[byte] for _b, _p, d in progs]
        c = Counter(vals)
        lines.append(f"  {byte:3d} {what}: {min(vals)}..{max(vals)}, commonest {c.most_common(4)}")
    other = [b for b in range(SEQ_AT) if b not in UNMAPPED and b not in {x[0] for x in LAYOUT}
             and not NAME_AT <= b < NAME_AT + NAME_LEN]
    for b in other:
        vals = {d[b] for _b, _p, d in progs}
        if vals != {0}:
            anomalies.append(f"byte {b}: not mapped, holds {sorted(vals)[:8]}")
    return lines, anomalies


def synthetic(n, seed=1):
    """n made-up OB-6 program dumps (no Sequential data): a deterministic spread of values in each byte's range."""
    out, s = bytearray(), seed
    for k in range(n):
        d = bytearray(1024)
        for byte, _p, _k, (blo, bhi), _n in LAYOUT:
            s = (s * 1103515245 + 12345) & 0x7FFFFFFF
            d[byte] = blo + s % (bhi - blo + 1)
        d[97:103] = bytes([0, 4, 7, 127, 127, 127]) if k % 2 else bytes([127] * 6)
        d[NAME_AT:NAME_AT + NAME_LEN] = f"SYNTH {k:03d}".ljust(NAME_LEN).encode()
        for lane in range(SEQ_LANES):                   # a made-up sequence in the OB-6's layout (decode_seq)
            d[SEQ_AT + 128 * lane:SEQ_AT + 128 * lane + 64] = bytes([SEQ_FILLER] * 64)
        length = 64 if k % 5 == 4 else 2 + (k * 7) % 40
        prev = 0
        for st in range(length):
            s = (s * 1103515245 + 12345) & 0x7FFFFFFF
            r = s >> 8
            if r % 8 == 0:                              # a rest
                d[SEQ_AT + 64 + st], prev = 0x80, 0
            elif r % 8 < 3 and prev:                    # a tie
                d[SEQ_AT + st], d[SEQ_AT + 64 + st] = 0x80 | SEQ_FILLER, 0x80
            else:                                       # 1..6 notes
                cnt = 1 + (r >> 4) % 6 if r % 3 == 0 else 1
                for lane in range(cnt):
                    d[SEQ_AT + 128 * lane + st] = 24 + (r >> (lane + 3)) % 72
                    d[SEQ_AT + 128 * lane + 64 + st] = 0x80 | 1 + (r >> lane) % 127
                prev = 1
        out += bytes([0xF0, 0x01, 0x2E, 0x02, k // 100, k % 100]) + pack_ms(bytes(d)) + b"\xf7"
    return bytes(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("syx", nargs="?", help="the OB-6 .syx file")
    ap.add_argument("-o", "--out", default=str(ROOT / "firmware/gen/factory_bank.h"))
    ap.add_argument("--report", default=str(ROOT / "build/factory_bank_report.txt"))
    ap.add_argument("--synthetic", type=int, metavar="N", help="N made-up programs instead of a file (no OB-6 data)")
    a = ap.parse_args(argv)
    if a.synthetic:
        blob, source = synthetic(a.synthetic), f"{a.synthetic} synthetic programs (no OB-6 data)"
    elif a.syx:
        blob, source = Path(a.syx).read_bytes(), Path(a.syx).name
    else:
        ap.error("a .syx file or --synthetic N")
    progs, warns = parse_syx(blob)
    if not progs:
        sys.exit("ob6_import: no OB-6 program dump in the input")
    records, rep, conv_warn, names, seqs, seq_iss = {}, [], 0, [], {}, {}
    for bank, prog, data in progs:
        n = bank * 100 + prog
        name, v, chord, w = convert(data)
        if n in records:
            warns.append(f"program {n:03d} twice: the last one kept")
        records[n] = program_pack(name, v, chord)
        steps, iss = decode_seq(data)
        seqs[n], seq_iss[n] = (seq_pack(steps), steps), +iss
        names.append((n, name))
        for x in w:
            warns.append(f"program {n:03d} {name!r}: {x}")
            conv_warn += 1
    count = write_header(a.out, records, source, {n: b for n, (b, _st) in seqs.items()})
    carried = {n: st for n, (b, st) in seqs.items() if st}
    allst = [x for st in carried.values() for x in st]
    lens = sorted(len(st) for st in carried.values())
    amb = {n: i for n, i in seq_iss.items() if i}
    seq_rep = [f"Sequences: {len(carried)} of {len(progs)} programs carry one,"
               f" {sum(len(seqs[n][0]) for n in carried)} B; steps {len(allst)}"
               f" (notes {sum(1 for t, nt in allst if nt and not t)}, rests {sum(1 for _t, nt in allst if not nt)},"
               f" ties {sum(1 for t, _nt in allst if t)});"
               f" lengths {lens[0] if lens else 0}..{lens[-1] if lens else 0}"
               f" (median {lens[len(lens) // 2] if lens else 0}); ambiguous programs: {len(amb)}",
               "  notes per note step: " + ", ".join(f"{k}: {c}" for k, c in sorted(Counter(
                   len(nt) for t, nt in allst if nt and not t).items())),
               "  oddities (decode_seq; counts): " + (", ".join(f"{k} {c}" for k, c in sorted(
                   sum(amb.values(), Counter()).items())) or "none")]
    seq_rep += [f"  program {n:03d}: " + ", ".join(f"{k} {c}" for k, c in sorted(i.items())) for n, i in amb.items()]
    vl, anomalies = verify(progs)
    ob = [(n, nm) for n, nm in names if re.search(r"OB|[Oo]ber", nm)]
    rep += [f"ob6_import: {source}: {len(progs)} programs -> {a.out} ({count} records x {RECORD_SIZE} B ="
            f" {count * RECORD_SIZE} B)", ""]
    rep += vl + [""]
    rep += ["Anomalies: " + ("none" if not anomalies else "")] + ["  " + x for x in anomalies] + [""]
    rep += [f"Warnings ({len(warns)}):"] + ["  " + w for w in warns] + [""]
    rep += [f"Names with 'OB' / 'Ober' ({len(ob)}):"] + [f"  {n:03d} {nm}" for n, nm in ob] + [""]
    rep += seq_rep + [""]
    rep += ["Not carried: the LFO retrigger byte, the editor / version bytes, the sequencer state bytes 93..96, the"
            " bytes 896..1023 (not the sequence)."]
    Path(a.report).parent.mkdir(parents=True, exist_ok=True)
    Path(a.report).write_text("\n".join(rep) + "\n")
    print(rep[0])
    print(f"  {len(LAYOUT)} bytes mapped; anomalies: {len(anomalies)}; warnings: {len(warns)}; report: {a.report}")
    print("  " + seq_rep[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())
