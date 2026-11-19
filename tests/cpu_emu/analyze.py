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
dists = sorted({os.path.basename(p)[:-len('-40.cost')] for p in glob.glob(f'{out}/x*-40.cost')},
               key=lambda n: [0, 3, 4, 1, 2].index(int(n[1])))
if dists:
    # Phase 2 Task 7 (harness DIST=127): the distortion at full against the same patch at 0 (the p run), one core
    print('\n### Distortion (DIST=127 against 0; one core, cycles per block)\n')
    print('| run | patch | voices | amount 0, emu / hw | amount 127, emu / hw | delta, emu / hw | delta per frame, hw '
          '| % of block at 240 MHz, 0 -> 127 (hw) | voices sounding, checksum |')
    print('|---|---|---|---|---|---|---|---|---|')
    for n in dists:
        base = n.replace('x', 'p', 1)
        def blk(m):
            a, b = load(f'{out}/{m}-40.cost'), load(f'{out}/{m}-8.cost')
            return [(sum(v[i] for v in a.values()) - sum(v[i] for v in b.values())) / K / (1 if i == 0 else 8)
                    for i in range(3)]
        z, x = blk(base), blk(n)
        c = check(f'{out}/{n}-40.log')
        good = c is not None and c[0] == 6
        ok &= good
        p240 = BLOCK_S * 240e6
        print(f'| {n} | {NAMES[int(n[1])]} | {n.split("-v")[1]} | {z[1]:,.0f} / {z[2]:,.0f} | {x[1]:,.0f} / {x[2]:,.0f} | '
              f'{x[1] - z[1]:+,.0f} / {x[2] - z[2]:+,.0f} | {(x[2] - z[2]) / 256:+.1f} | '
              f'{100 * z[2] / p240:.1f} -> {100 * x[2] / p240:.1f} | {f"{c[0]}, {c[2]}" if good else "RUN FAILED"} |')
def per_block(n, k1, k0):
    """cycles per block (insns, emu, hw), the difference of the runs of k1 and k0 blocks; the cost files hold eighths"""
    a, b = load(f'{out}/{n}-{k1}.cost'), load(f'{out}/{n}-{k0}.cost')
    d = {pc: tuple(x - y for x, y in zip(a.get(pc, (0, 0, 0)), b.get(pc, (0, 0, 0)))) for pc in set(a) | set(b)}
    return d, (k1 - k0)


def one_core(n, k1, k0):
    d, kk = per_block(n, k1, k0)
    return [sum(v[i] for v in d.values()) / kk / (1 if i == 0 else 8) for i in range(3)]


def dual_path(n, k1, k0):
    """as the Dual core table: CPU0's serial work + max(set A, set B), cycles per block (emu, hw)"""
    d, kk = per_block(n, k1, k0)
    s = syms(f'{out}/{n}.nm')
    own = {pc: owner(s, pc) for pc in d}
    c0 = {pc: v for pc, v in d.items() if not pc >> 31}
    c1 = {pc: v for pc, v in d.items() if pc >> 31}
    job_fns = {own[pc] for pc, v in c1.items() if v[0] > 0} - {'c1_main', 'cpu1_job_voices'}

    def tot(rows, pred):
        return [sum(v[i] for pc, v in rows.items() if pred(own[pc])) / kk / 8 for i in (1, 2)]
    wait = tot(c0, lambda f: f == 'c0_join')
    work0 = [x - y for x, y in zip(tot(c0, lambda f: True), wait)]
    a_set = tot(c0, lambda f: f in job_fns)
    job = tot(c1, lambda f: f != 'c1_main')
    return [w - x + max(x, y) for w, x, y in zip(work0, a_set, job)], job


def fn_blocks(n, k1, k0):
    """cycles per block (insns, hw) of each function: the difference of the runs of k1 and k0 blocks"""
    d, kk = per_block(n, k1, k0)
    s = syms(f'{out}/{n}.nm')
    per = {}
    for pc, v in d.items():
        q = per.setdefault(owner(s, pc), [0, 0])
        q[0] += v[0] / kk
        q[1] += v[2] / kk / 8
    return per


# Phase 3 Task 7 (harness ENG=1): the sequencer's player through the engine, 250 BPM, 1/16, six notes a step. q runs:
# one core; r runs: DUAL=1. SEQ 0 = the six keys held (the baseline), 1 = a chord a step with a long release (six voices
# busy all the time: the worst case), 2 = a chord and 63 ties (a constant six-voice load: the player's own work),
# 3 = a chord a step with the patch's own release. 83 blocks = 8.03 steps (a step is 2646 frames = 10.34 blocks).
KS, K0, KB = 91, 8, 10
SEQ_NAMES = {1: 'a chord a step, long release (six voices always busy)', 2: 'ties (a constant six-voice load)',
             3: 'a chord a step, the patch\'s release (half the time sounding)'}
seq_runs = sorted({os.path.basename(p)[:-len(f'-{KS}.cost')] for p in glob.glob(f'{out}/q*-{KS}.cost')})
if seq_runs:
    p240 = BLOCK_S * 240e6
    print('\n### Sequencer playback (harness ENG=1: through eng_render; 250 BPM, 1/16, six notes a step; one core, '
          'cycles per block over 83 blocks = 8.03 steps)\n')
    print('| patch | no sequencer (six keys held), emu / hw | ' + ' | '.join(
        f'{SEQ_NAMES[q]}: cycles, emu / hw | delta, emu / hw | delta, % of the block at 240 MHz (emu / hw)' for q in (1, 2, 3))
        + ' |')
    print('|---|---|' + '---|---|---|' * 3)
    for pat in (3, 4):
        r = {sq: one_core(f'q{pat}{sq}-v6', KS, K0) for sq in (0, 1, 2, 3) if f'q{pat}{sq}-v6' in seq_runs}
        if len(r) < 4:
            ok = False
            continue
        for sq in (0, 1, 2, 3):
            ok &= check(f'{out}/q{pat}{sq}-v6-{KS}.log') is not None
        e = lambda v: f'{v[1]:,.0f} / {v[2]:,.0f}'
        dl = lambda v, b: f'{v[1] - b[1]:+,.0f} / {v[2] - b[2]:+,.0f}'
        pc = lambda v, b: f'{100 * (v[1] - b[1]) / p240:+.2f} / {100 * (v[2] - b[2]) / p240:+.2f}'
        cells = ' | '.join(f'{e(r[q])} | {dl(r[q], r[0])} | {pc(r[q], r[0])}' for q in (1, 2, 3))
        print(f'| {NAMES[pat]} | {e(r[0])} | {cells} |')
    print('\nThe baseline plays the same six notes through the same engine (event queue, clock, eng_render_steps with its '
          'fast path), so the delta is the sequencer: the player, the render split at every step and every gate end, the '
          'note-ons and note-offs, and (3) the voices that are not sounding for half of every step.')
    f0, f1, f2 = (fn_blocks(f'q3{sq}-v6', KS, K0) for sq in (0, 1, 2))
    print('\nWhere the sequencer\'s own cycles go (musical C3..C5, hw cycles per block): the functions that run only with a '
          'sequence (absent from the baseline\'s profile; the player, the note-on / note-off path) and `main` (the '
          'harness loop with engine.c inlined: eng_render, eng_render_steps, eng_drain, the clock):\n')
    print('| | chord a step, long release | ties |')
    print('|---|---|---|')
    only = lambda f: sorted(((v[1], k) for k, v in f.items() if k not in f0 and v[1] > 0), reverse=True)
    for lab, f in (('functions only a sequence runs', None),):
        t1 = sum(v for v, k in only(f1))
        t2 = sum(v for v, k in only(f2))
        m1, m2 = f1.get('main', [0, 0])[1] - f0.get('main', [0, 0])[1], f2.get('main', [0, 0])[1] - f0.get('main', [0, 0])[1]
        print(f'| {lab} | {t1:,.0f} | {t2:,.0f} |')
        print(f'| `main` (the engine loop), against the baseline | {m1:+,.0f} | {m2:+,.0f} |')
        print(f'| total, hw cycles a block (% of the block at 240 MHz) | {t1 + m1:,.0f} ({100 * (t1 + m1) / p240:.2f} %) '
              f'| {t2 + m2:,.0f} ({100 * (t2 + m2) / p240:.2f} %) |')
    print('\nThe biggest of them, chord a step: ' + ', '.join(f'{k} {v:,.0f}' for v, k in only(f1)[:6]) + '; ties: '
          + ', '.join(f'{k} {v:,.0f}' for v, k in only(f2)[:4]) + '.')
    print('\nOne block that holds a step (block 10 counting from 0: step 2 starts at frame 2646 of the run, all six notes '
          'retrigger), the difference of the runs of 11 and 10 blocks; one core, cycles, emu / hw:\n')
    print('| patch | no sequencer | chord a step, long release | delta | % of the block at 240 MHz (emu / hw) |')
    print('|---|---|---|---|---|')
    for pat in (3,):
        if f'q{pat}0-v6' in seq_runs and os.path.exists(f'{out}/q{pat}1-v6-{KB + 1}.cost'):
            z, q = one_core(f'q{pat}0-v6', KB + 1, KB), one_core(f'q{pat}1-v6', KB + 1, KB)
            print(f'| {NAMES[pat]} | {z[1]:,.0f} / {z[2]:,.0f} | {q[1]:,.0f} / {q[2]:,.0f} | {q[1] - z[1]:+,.0f} / '
                  f'{q[2] - z[2]:+,.0f} | {100 * q[1] / p240:.1f} / {100 * q[2] / p240:.1f} (no sequencer '
                  f'{100 * z[1] / p240:.1f} / {100 * z[2] / p240:.1f}) |')
        else:
            ok = False
    duals_q = sorted({os.path.basename(p)[:-len(f'-{KS}.cost')] for p in glob.glob(f'{out}/r*-{KS}.cost')})
    if duals_q:
        print('\n### Sequencer playback, two cores (harness ENG=1 DUAL=1, musical C3..C5; the split as in the Dual core '
              'table: CPU0 serial work + max(set A, set B); cycles per block over 83 blocks)\n')
        print('| | no sequencer, emu / hw | chord a step, long release | delta | ties | delta | as played | delta '
              '| % of the block at 240 MHz (emu / hw): no sequencer -> chord a step -> ties | checksums = one core '
              '| CPU1 jobs (0 / 1 / 2 / 3) |')
        print('|---|---|---|---|---|---|---|---|---|---|---|')
        r = {}
        good = True
        for sq in (0, 1, 2, 3):
            n = f'r3{sq}-v6'
            if n not in duals_q:
                good = False
                continue
            r[sq], _ = dual_path(n, KS, K0)
            cd, cs = dual_check(f'{out}/{n}-{KS}.log'), check(f'{out}/q3{sq}-v6-{KS}.log')
            good &= cd is not None and cs is not None and cd[1] == cs[2]
        ok &= good and len(r) == 4
        if len(r) == 4:
            jobs = ' / '.join(str(dual_check(f'{out}/r3{sq}-v6-{KS}.log')[2]) for sq in (0, 1, 2, 3))
            e = lambda v: f'{v[0]:,.0f} / {v[1]:,.0f}'
            dl = lambda v, b: f'{v[0] - b[0]:+,.0f} / {v[1] - b[1]:+,.0f}'
            pc = lambda v: f'{100 * v[0] / p240:.1f} / {100 * v[1] / p240:.1f}'
            print(f'| musical C3..C5, six voices | {e(r[0])} | {e(r[1])} | {dl(r[1], r[0])} | {e(r[2])} | {dl(r[2], r[0])} '
                  f'| {e(r[3])} | {dl(r[3], r[0])} | {pc(r[0])} -> {pc(r[1])} -> {pc(r[2])} | {"yes" if good else "NO"} '
                  f'| {jobs} |')

if not ok:
    print('\nERROR: a run did not finish with the result words (or a dual run differs from the one-core run)')
    sys.exit(1)
