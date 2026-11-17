#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""KNOBI-SIX CPU gate (Task 11): cycles per 256-frame block from the BLOCKS=40 and BLOCKS=8 harness runs
(difference / 32), in both cycle models, as a share of the block (5.805 ms) at 240 and 360 MHz, the gate (< 50 % at
240 MHz in both models), and the hot spots per function. After spike/cpu/analyze.py (jhortale/smk37-analog).
Runs p*: one core; runs d* (Phase 2 Task 3, harness DUAL=1): the cycles split by core (pc bit 31 = core 1)."""
import glob, os, re, sys

out = sys.argv[1]
K = 32
BLOCK_S = 256 / 44100
NAMES = {0: 'init chord', 1: 'extreme, C3..C5', 2: 'extreme, high chord 120..127', 3: 'musical stress, C3..C5',
         4: 'musical stress, C5..C7'}


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


runs = sorted({os.path.basename(p)[:-len('-40.cost')] for p in glob.glob(f'{out}/p*-40.cost')},
              key=lambda n: (-int(n.split('-v')[1]), [0, 3, 4, 1, 2].index(int(n[1]))))
duals = sorted({os.path.basename(p)[:-len('-40.cost')] for p in glob.glob(f'{out}/d*-40.cost')},
               key=lambda n: (-int(n.split('-v')[1]), [0, 3, 4, 1, 2].index(int(n[1]))))
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


def owner(s, pc):
    pc &= 0x7FFFFFFF
    for addr, size, sym in s:
        if addr <= pc < addr + max(size, 2):
            return sym
    return '?'


def dual_check(log):
    t = open(log, errors='replace').read()
    m = re.search(r'0x01c70000:\s+([0-9a-f]{8})\s+([0-9a-f]{8})\s+([0-9a-f]{8})\s+([0-9a-f]{8})\s+([0-9a-f]{8})', t, re.I)
    if not m or m.group(1).lower() != '50524f42':
        return None
    return int(m.group(2), 16), m.group(4).lower(), int(m.group(5), 16)


if duals:
    # DUAL=1 (harness.c): core 1's pcs have bit 31 set. CPU0's work leaves out its wait (c0_join: the emulator steps
    # the cores an instruction each, so the wait's length is in instructions, not in the cycle models); CPU1's job
    # leaves out its idle poll (c1_main). A = CPU0's cycles in the functions CPU1 also ran (set A's voices), B = CPU1's
    # job. The block's critical path is estimated as CPU0's serial work + max(A, B) (per period the larger side waits
    # for nothing; the sum over the block assumes the same side is larger in every period, as with 3 + 3 voices).
    print('\n### Dual core (DUAL=1: set B on CPU1 per control period; cycles per block)\n')
    print('| run | patch | voices | one core, emu / hw | CPU0 work (A + serial), emu / hw | CPU0 set A, emu / hw '
          '| CPU1 job (set B), emu / hw | CPU0 wait, insns | critical path (est.), emu / hw '
          '| % of block at 240 MHz (emu / hw) | % at 360 MHz (emu / hw) | change vs one core (emu / hw) '
          '| checksum = one core | CPU1 jobs |')
    print('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    for n in duals:
        a, b = load(f'{out}/{n}-40.cost'), load(f'{out}/{n}-8.cost')
        d = {pc: tuple(x - y for x, y in zip(a.get(pc, (0, 0, 0)), b.get(pc, (0, 0, 0)))) for pc in set(a) | set(b)}
        s = syms(f'{out}/{n}.nm')
        own = {pc: owner(s, pc) for pc in d}
        c0 = {pc: v for pc, v in d.items() if not pc >> 31}
        c1 = {pc: v for pc, v in d.items() if pc >> 31}
        job_fns = {own[pc] for pc, v in c1.items() if v[0] > 0} - {'c1_main', 'cpu1_job_voices'}

        def tot(rows, pred):
            return [sum(v[i] for pc, v in rows.items() if pred(own[pc])) / K for i in range(3)]
        c0_all = tot(c0, lambda f: True)
        wait = tot(c0, lambda f: f == 'c0_join')
        work0 = [x - y for x, y in zip(c0_all, wait)]
        a_set = tot(c0, lambda f: f in job_fns)
        job = tot(c1, lambda f: f != 'c1_main')
        crit = [w - x + max(x, y) for w, x, y in zip(work0, a_set, job)]
        single = n.replace('d', 'p', 1)
        sa, sb = load(f'{out}/{single}-40.cost'), load(f'{out}/{single}-8.cost')
        one = [(sum(v[i] for v in sa.values()) - sum(v[i] for v in sb.values())) / K for i in range(3)]
        cd, cs = dual_check(f'{out}/{n}-40.log'), check(f'{out}/{single}-40.log')
        good = cd is not None and cs is not None and cd[1] == cs[2] and cd[2] == 16 * 40
        ok &= good
        e = lambda v: f'{v[1] / 8:,.0f} / {v[2] / 8:,.0f}'
        p240, p360 = BLOCK_S * 240e6, BLOCK_S * 360e6
        print(f'| {n} | {NAMES[int(n[1])]} | {n.split("-v")[1]} | {e(one)} | {e(work0)} | {e(a_set)} | {e(job)} | '
              f'{wait[0]:,.0f} | {e(crit)} | {100 * crit[1] / 8 / p240:.1f} / {100 * crit[2] / 8 / p240:.1f} | '
              f'{100 * crit[1] / 8 / p360:.1f} / {100 * crit[2] / 8 / p360:.1f} | '
              f'{100 * (crit[1] / one[1] - 1):+.1f} / {100 * (crit[2] / one[2] - 1):+.1f} % | '
              f'{"yes, " + cd[1] if good else "NO"} | {cd[2] if cd else "RUN FAILED"} |')
    print('\nfm1-emu has no cache and no bus-contention model: both cores fetch and load at full speed here. On the chip '
          'they share one I-cache and the XIP flash; prior art measured -27..-41 % on devices '
          '(docs/research/second-core.md), not the ideal ~-50 %.')
if not ok:
    print('\nERROR: a run did not finish with the result words (or a dual run differs from the one-core run)')
    sys.exit(1)
