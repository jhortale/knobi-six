#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""KNOBI-SIX CPU gate (Task 11): cycles per 256-frame block from the BLOCKS=40 and BLOCKS=8 harness runs
(difference / 32), in both cycle models, as a share of the block (5.805 ms) at 240 and 360 MHz, the gate (< 50 % at
240 MHz in both models), and the hot spots per function. After spike/cpu/analyze.py (jhortale/smk37-analog)."""
import glob, os, re, sys

out = sys.argv[1]
K = 32
BLOCK_S = 256 / 44100
NAMES = {0: 'init chord', 1: 'worst, C3..C5', 2: 'worst, high chord 120..127'}


def load(path):
    rows = {}
    for line in open(path):
        pc, n, c, a = line.split()
        rows[int(pc, 16)] = (int(n), int(c), int(a))
    return rows


def syms(nm):
    s = []
    for line in open(nm):
        m = re.match(r'^([0-9a-f]+) ([0-9a-f]+) [tTdDwW] (\S+)', line)
        if m:
            s.append((int(m.group(1), 16), int(m.group(2), 16), m.group(3)))
    return sorted(s)


def check(log):
    t = open(log, errors='replace').read()
    m = re.search(r'0x01c70000:\s+([0-9a-f]{8})\s+([0-9a-f]{8})\s+([0-9a-f]{8})\s+([0-9a-f]{8})', t, re.I)
    if not m or m.group(1).lower() != '50524f42':
        return None
    return int(m.group(2), 16), int(m.group(3), 16), m.group(4).lower()


runs = sorted({os.path.basename(p)[:-len('-40.cost')] for p in glob.glob(f'{out}/*-40.cost')},
              key=lambda n: (-int(n.split('-v')[1]), int(n[1])))
print('| run | patch | voices | insns/block | cycles/block, emu | cycles/block, hw | % of block at 240 MHz (emu / hw) '
      '| % at 360 MHz (emu / hw) | < 50 % at 240 MHz | voices sounding, sizeof synth_t, checksum |')
print('|---|---|---|---|---|---|---|---|---|---|')
hot, ok = {}, True
for n in runs:
    a, b = load(f'{out}/{n}-40.cost'), load(f'{out}/{n}-8.cost')
    d = {pc: tuple(x - y for x, y in zip(a.get(pc, (0, 0, 0)), b.get(pc, (0, 0, 0)))) for pc in set(a) | set(b)}
    tot = [sum(v[i] for v in d.values()) / K for i in range(3)]
    emu, hw = tot[1] / 8, tot[2] / 8
    p240, p360 = BLOCK_S * 240e6, BLOCK_S * 360e6
    c40, c8 = check(f'{out}/{n}-40.log'), check(f'{out}/{n}-8.log')
    good = c40 is not None and c8 is not None
    ok &= good
    gate = 'yes' if 100 * emu / p240 < 50 and 100 * hw / p240 < 50 else 'NO'
    pat, nv = int(n[1]), n.split('-v')[1]
    chk = f'{c40[0]}, {c40[1]}, {c40[2]}' if good else 'RUN FAILED'
    print(f'| {n} | {NAMES[pat]} | {nv} | {tot[0]:,.0f} | {emu:,.0f} | {hw:,.0f} | {100 * emu / p240:.1f} / '
          f'{100 * hw / p240:.1f} | {100 * emu / p360:.1f} / {100 * hw / p360:.1f} | {gate} | {chk} |')
    s = syms(f'{out}/{n}.nm')
    per = {}
    for pc, v in d.items():
        name = '?'
        for addr, size, sym in s:
            if addr <= pc < addr + max(size, 2):
                name = sym
        q = per.setdefault(name, [0, 0, 0])
        for i in range(3):
            q[i] += v[i]
    hot[n] = sorted(per.items(), key=lambda kv: -kv[1][2])
print(f'\nBudget: one block = 256 frames at 44.1 kHz = {BLOCK_S * 1e3:.3f} ms = {BLOCK_S * 240e6:,.0f} cycles at 240 MHz, '
      f'{BLOCK_S * 360e6:,.0f} at 360 MHz; the gate is < 50 %.')
print('\nHot spots per block (instructions / emu cycles / hw cycles):\n')
for n, rows in hot.items():
    print(f'- **{n}**: ' + '; '.join(f'{name} {v[0] / K:,.0f} / {v[1] / 8 / K:,.0f} / {v[2] / 8 / K:,.0f}'
                                     for name, v in rows[:10]))
if not ok:
    print('\nERROR: a run did not finish with the result words')
    sys.exit(1)
