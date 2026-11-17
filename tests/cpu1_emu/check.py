#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""KNOBI-SIX second core in fm1-emu (tests/cpu1_emu/run.sh): read each case's log (the emulator's report and the
RAM it dumped at the end: fm1_cpu1_mb for the plain image, cpu1_test for the test hooks, platform.c) and check it.
Prints a Markdown summary; exits 1 if a case is not as it should be.
  python3 tests/cpu1_emu/check.py OUTDIR MS RATE"""
import re
import sys

out, ms, rate = sys.argv[1], int(sys.argv[2]), sys.argv[3]
dms, wms = (int(sys.argv[4]), int(sys.argv[5])) if len(sys.argv) > 5 else (None, None)
pms = int(sys.argv[6]) if len(sys.argv) > 6 else None
TICKS_PER_US = 24
BLOCK_US = 256 * 1_000_000 // 44100                       # one audio block (HALF_FRAMES at 44.1 kHz): 5804 us
STATE = {0: "OFF", 1: "IDLE", 2: "BUSY", 3: "FAIL"}
WHY = {0: "-", 1: "MISREAD", 2: "LATE", 3: "BARRED", 4: "FAULT", 5: "SILENT"}
TEST = ["magic", "state", "why", "post", "join", "took", "failed", "pending", "pings", "rejected", "jobs", "barred",
        "at_ms", "flash", "main_post", "refused", "fl_rc", "fl_at", "pings_after", "join_none"]
MB = ["request", "fn", "arg", "check", "_", "_", "_", "_", "ready", "complete", "rejected", "misread", "jobs"]
def min_halves(ms):
    return int((ms - 400) / 1000 * 44100 / 256 * 0.95)


MIN_HALVES = min_halves(ms)   # the audio ran from the end of the boot (~0.3 s in the
                                                            # emulator, one core or two; nostart waits 20 ms more)


def load(case):
    log = open(f"{out}/{case}.log", errors="replace").read()
    words = []
    for m in re.finditer(r"^  0x[0-9a-f]+:((?: [0-9a-f]{8})+)$", log, re.M):
        words += [int(w, 16) for w in m.group(1).split()]
    halves = re.search(r"(\d+) audio halves", log)
    return {"log": log, "words": words, "halves": int(halves.group(1)) if halves else 0,
            "halt": bool(re.search(r"^halt|stopped after .*\(halt", log, re.M)),
            "reset": "chip reset" in log, "released": "cpu1 released" in log,
            "misreads": int(m.group(1)) if (m := re.search(r"flaky core 1 .*: (\d+) misreads", log)) else None,
            "c1stop": "cpu1 stopped" in log}


rows, bad = [], []


def case(name, what, ok, seen):
    rows.append((name, what, "ok" if all(ok.values()) else "FAIL", seen))
    for k, v in ok.items():
        if not v:
            bad.append(f"{name}: {k}")


def common(r, resets=0, halves=MIN_HALVES):
    n = r["log"].count("chip reset")
    return {"no halt": not r["halt"], f"{resets} chip reset(s)": n == resets,
            "cpu1 released": r["released"], f"audio ran (>= {halves} halves)": r["halves"] >= halves}


def test(r):
    w = r["words"]
    return dict(zip(TEST, w + [None] * (len(TEST) - len(w))))


r = load("plain")
mb = dict(zip(MB, r["words"] + [None] * (len(MB) - len(r["words"]))))
case("plain", "KNOBI_CPU1=1, no test hook: start, boot ping, idle in RAM",
     {**common(r), "ready": mb["ready"] == 1, "the ping ran (jobs 1, complete = request = 1)":
      mb["jobs"] == 1 and mb["complete"] == mb["request"] == 1, "nothing refused": mb["rejected"] == 0},
     f"ready {mb['ready']}, request {mb['request']}, complete {mb['complete']}, jobs {mb['jobs']}, "
     f"rejected {mb['rejected']}; {r['halves']} audio halves")

r = load("start")
t = test(r)
case("start", "start -> ping round trip -> join OK, then a ping every 100 ms",
     {**common(r), "magic": t["magic"] == 0x54314B43, "IDLE": t["state"] == 1, "no failure": t["why"] == 0,
      "pings came back (>= 5)": (t["pings"] or 0) >= 5, "jobs = pings": t["jobs"] == t["pings"],
      "boot guard: failed 0, pending 1": (t["failed"], t["pending"]) == (0, 1), "not barred": t["barred"] == 0,
      "flash_ok (the boot's flash reads passed the gate)": t["flash"] == 1,
      "a post from the main loop refused": t["main_post"] == 0xFFFFFFFF and (t["refused"] or 0) > 0,
      "join with nothing outstanding = 0": t["join_none"] == 0,
      "erase + write + sliced erase + sliced write, read back": (t["fl_at"] or 0) > 0 and t["fl_rc"] == 0,
      "pings went on after the flash work": (t["pings"] or 0) > (t["pings_after"] or 0) > 0},
     f"{STATE.get(t['state'])}, {t['pings']} pings back ({t['jobs']} jobs), rejected {t['rejected']}; boot guard "
     f"failed {t['failed']} pending {t['pending']}; flash_ok {t['flash']}; main-loop post {t['main_post'] - (1 << 32)}"
     f" ({t['refused']} refused), join with none outstanding {t['join_none']}; flash work at {t['fl_at']} ms rc "
     f"{t['fl_rc']} ({t['pings_after']} pings before it); {r['halves']} audio halves")

r = load("nostart")
t = test(r)
case("nostart", "CPU1 never reports in: one core, the boot guard unaffected",
     {**common(r), "FAIL": t["state"] == 3, "why SILENT": t["why"] == 5, "no job ran": t["jobs"] == 0,
      "boot guard: failed 0, pending 1": (t["failed"], t["pending"]) == (0, 1), "not barred": t["barred"] == 0},
     f"{STATE.get(t['state'])} ({WHY.get(t['why'])}); boot guard failed {t['failed']} pending {t['pending']}; "
     f"{r['halves']} audio halves")

r = load("hang")
t = test(r)
took_us = (t["took"] or 0) / TICKS_PER_US
case("hang", "a job that never returns: join timeout -> retired within one block",
     {**common(r), "posted": t["post"] == 0, "join < 0": t["join"] == 0xFFFFFFFF, "FAIL": t["state"] == 3,
      "why LATE": t["why"] == 2, f"retired within one block ({BLOCK_US} us)": 4000 <= took_us <= BLOCK_US,
      "barred until power-off": t["barred"] == 1, "boot guard: failed 0, pending 1": (t["failed"], t["pending"]) == (0, 1)},
     f"join {t['join'] - (1 << 32) if t['join'] else t['join']} after {took_us:.0f} us; {STATE.get(t['state'])} "
     f"({WHY.get(t['why'])}), barred {t['barred']}; boot guard failed {t['failed']} pending {t['pending']}; "
     f"{r['halves']} audio halves")

r = load("flaky")
t = test(r)
case("flaky", f"--flaky-core1 {rate} --seed 7: a misread -> the check -> retired",
     {**common(r), "misreads injected": (r["misreads"] or 0) > 0, "FAIL": t["state"] == 3,
      "why MISREAD": t["why"] == 1, "refused a job (rejected > 0)": (t["rejected"] or 0) > 0,
      "barred until power-off": t["barred"] == 1, "boot guard: failed 0, pending 1": (t["failed"], t["pending"]) == (0, 1),
      "retired: join with nothing outstanding = 0": t["join_none"] == 0},
     f"{r['misreads']} misreads injected; {STATE.get(t['state'])} ({WHY.get(t['why'])}) at {t['at_ms'] or 'boot'} ms "
     f"after {t['pings']} good pings, rejected {t['rejected']}, barred {t['barred']}; {r['halves']} audio halves"
     + ("; the emulator stopped cpu1 on a wild access" if r["c1stop"] else ""))

r = load("reset")
t = test(r)
case("reset", "a warm reset in the start window: counted once, then one core (barred)",
     {**common(r, resets=1, halves=MIN_HALVES - 60), "OFF (not started)": t["state"] == 0,
      "barred by the start mark": t["barred"] == 1, "boot guard: failed 1, pending 1": (t["failed"], t["pending"]) == (1, 1),
      "no job ran on the second boot": t["jobs"] == 0},
     f"1 chip reset; second boot: {STATE.get(t['state'])}, barred {t['barred']}; boot guard failed {t['failed']} "
     f"pending {t['pending']}; {r['halves']} audio halves")

# Task 3: dual-core voice rendering (tests/cpu1_emu/dual_hook.c, dual_test)
DT = ["magic", "done", "blocks"] + [f"sum{p}{i}" for p in (0, 1) for i in range(4)] + \
     [f"jobs{p}{i}" for p in (0, 1) for i in range(4)] + [f"used{p}{i}" for p in (0, 1) for i in range(4)] + \
     [f"c1load{i}" for i in range(4)] + ["failed", "state", "why", "rejected", "mbjobs"] + \
     [f"step{p}{i}" for p in (0, 1) for i in range(4)] + [f"peak{p}{i}" for p in (0, 1) for i in range(4)] + \
     ["status_on", "status_off", "dual", "wr_rc", "writes",
     "write_at", "jobs_before", "jobs_write", "saves", "save_at", "jobs_save", "dual_read", "jobs_after", "at_fail"]
PATCHES = ["init chord", "musical C3..C5", "musical C5..C7", "extreme C3..C5"]
BLOCK_TICKS = 256 * 5442 // 10                              # engine.c: the block's budget in TIMER4 ticks
dual_rows = []


def dt(r):
    w = r["words"]
    return dict(zip(DT, w + [None] * (len(DT) - len(w))))


if dms:
    for name, hook in (("dual", 6), ("dualfail", 7), ("dualmid", 9)):
        r = load(name)
        t = dt(r)
        eq = [t[f"sum0{i}"] is not None and t[f"sum0{i}"] == t[f"sum1{i}"] for i in range(4)]
        same = all(eq)
        ok = {**common(r, halves=min_halves(dms)), "magic": t["magic"] == 0x54554444, "script done": t["done"] == 1,
              "no CPU1 job with DUAL CORE off": all(t[f"jobs0{i}"] == 0 for i in range(4)),
              "C1 status after: on -> " + ("ON" if hook == 6 else "FAIL") + ", off -> OFF":
                  (t["status_on"], t["status_off"]) == ((1 if hook == 6 else 2), 0)}
        if hook == 9:
            ok["dual = one core except the patch where the job stopped part way"] = eq[0] and not eq[1] and eq[2] and eq[3]
            ok["that glitch bounded: largest step <= the one-core pass's, peak < full scale"] = \
                (t["step11"] or 0) <= (t["step01"] or 0) and (t["peak11"] or 99999) < 32767
        else:
            ok["dual output = one-core output, every patch (checksums)"] = same
        if hook == 6:
            ok.update({"CPU1 jobs with it on, every patch (> 500 periods)": all((t[f"jobs1{i}"] or 0) > 500 for i in range(4)),
                       "IDLE, nothing failed or refused": (t["state"], t["why"], t["failed"], t["rejected"]) == (1, 0, 0, 0)})
        else:
            ok.update({"FAIL (LATE), eng_c1_failed": (t["state"], t["why"], t["failed"]) == (3, 2, 1),
                       "jobs until the 1000th, none after": t["jobs10"] + t["jobs11"] == 999 and t["jobs12"] == 0 and t["jobs13"] == 0})
        seen = "; ".join(f"{PATCHES[i]} {t[f'sum1{i}']:08x}" for i in range(4) if t[f"sum1{i}"] is not None)
        seen += f"; jobs {[t[f'jobs1{i}'] for i in range(4)]}; {STATE.get(t['state'])} ({WHY.get(t['why'])})"
        if hook != 6:
            seen += f" at {t['at_fail']} ms"
        if hook == 9:
            seen += (f"; musical C3..C5 largest step {t['step11']} (one core {t['step01']}), peak {t['peak11']} "
                     f"(one core {t['peak01']})")
        seen += f"; C1 status on/off {t['status_on']}/{t['status_off']}"
        seen += f"; {r['halves']} audio halves"
        case(name, {6: "DUAL CORE off vs on, same image and run",
                    7: "the 1000th voice job hangs: set B on CPU0, one core after",
                    9: "the 1000th job stops after one voice: set B again on CPU0, the glitch bounded"}[hook], ok, seen)
        if hook == 6 and same:
            for i in range(4):
                dual_rows.append((PATCHES[i], 100 * t[f"used0{i}"] / 24 / BLOCK_TICKS, 100 * t[f"used1{i}"] / 24 / BLOCK_TICKS,
                                  t[f"c1load{i}"], t[f"jobs1{i}"]))
    r = load("write")
    t = dt(r)
    case("write", "DUAL CORE on: WRITE (slot erase + write) and the globals' autosave, a chord before and after",
         {**common(r, halves=min_halves(wms) - 60), "script done": t["done"] == 1, "DUAL CORE on": t["dual"] == 1,
          "WRITE stored (rc 1, 1 write)": (t["wr_rc"], t["writes"]) == (1, 1),
          "autosave: the extended record, read back DUAL CORE 1": (t["saves"] or 0) >= 1 and t["dual_read"] == 1,
          "jobs before the WRITE, during its fade, after the autosave":
              0 < (t["jobs_before"] or 0) < (t["jobs_write"] or 0) and (t["jobs_after"] or 0) > (t["jobs_save"] or 0),
          "CPU1 IDLE throughout (never retired by the flash gate), nothing refused":
              (t["state"], t["why"], t["failed"], t["rejected"], t["at_fail"]) == (1, 0, 0, 0, 0)},
         f"WRITE rc {t['wr_rc']} at {t['write_at']} ms; autosave at {t['save_at']} ms ({t['saves']} saves), DUAL CORE read "
         f"back {t['dual_read']}; jobs {t['jobs_before']} before, {t['jobs_write']} after the WRITE, {t['jobs_save']} at the "
         f"autosave, {t['jobs_after']} after; {STATE.get(t['state'])}, rejected {t['rejected']}; {r['halves']} audio halves")

# Task 4: the CPU1 probe (firmware/src/cpu1_probe.c c1probe)
PR = ["magic", "blocks", "pings", "errs", "c0_mis", "c1_mis", "c0_bits", "c1_bits", "rounds", "fpu_ok", "fpu_bad",
      "fpu_trap", "fpu_c0", "fpu_c1", "dual_ok", "dual_bad", "dual_none", "sum1", "sum2", "jobs2", "load1", "load2",
      "loadj", "passes", "state", "why", "rejected", "mbjobs", "c1_failed", "fail", "cost_peak", "cost_max", "steps"]
FAILWHY = {0: "-", 1: "PATTERN", 2: "FPU", 3: "DUAL"}
if pms:
    def pr(r):
        w = r["words"]
        return dict(zip(PR, w + [None] * (len(PR) - len(w))))

    r = load("probe")
    t = pr(r)
    mis = (t["c0_mis"] or 0) + (t["c1_mis"] or 0) + (t["rejected"] or 0)
    pings = pms // 1000 - 1                               # (one a second from the first block, ~0.3 s in)
    case("probe", "the CPU1 probe image (--probe-cpu1): ping, 16 KB patterns, FPU, dual vs single, timing",
         {**common(r, halves=min_halves(pms)), "magic": t["magic"] == 0x52503143,
          f"pings counted (>= {pings}), no error": (t["pings"] or 0) >= pings and t["errs"] == 0,
          "pattern rounds of 16 KB (>= 60), no misread": (t["rounds"] or 0) >= 60 and mis == 0,
          "its own time measured, under 25 % of a block": 0 < (t["cost_max"] or 0) < 25,
          "FPU: CPU1 = CPU0, every check; no trap": (t["fpu_ok"] or 0) >= pings and t["fpu_bad"] == 0
              and t["fpu_trap"] == 0 and t["fpu_c0"] == t["fpu_c1"],
          "DUAL=SINGLE: every pass equal, with CPU1 jobs": (t["dual_ok"] or 0) >= 1 and t["dual_bad"] == 0
              and t["dual_none"] == 0 and t["sum1"] == t["sum2"] and (t["jobs2"] or 0) > 0,
          "timed (LOAD 1C, 2C, JOB)": (t["passes"] or 0) >= 1 and (t["load1"] or 0) > 0 and (t["load2"] or 0) > 0,
          "CPU1 IDLE, no failure": (t["state"], t["why"], t["c1_failed"], t["fail"]) == (1, 0, 0, 0)},
         f"PING {t['pings']} ERR {t['errs']} MISREAD {mis} ({t['rounds']} rounds of 16 KB); FPU {t['fpu_ok']} ok "
         f"{t['fpu_bad']} bad {t['fpu_trap']} trap ({(t['fpu_c0'] or 0):08x}); DUAL=SINGLE {t['dual_ok']} ok "
         f"{t['dual_bad']} bad ({(t['sum1'] or 0):08x}, {t['jobs2']} CPU1 jobs a pass); LOAD 1C {t['load1']}% 2C "
         f"{t['load2']}% JOB {t['loadj']}% (emulator time); PROBE {t['cost_peak']}% (highest {t['cost_max']}% of a block, "
         f"emulator time); {STATE.get(t['state'])}; {r['halves']} audio halves")
    r = load("probeflaky")
    t = pr(r)
    mis = (t["c0_mis"] or 0) + (t["c1_mis"] or 0) + (t["rejected"] or 0)
    case("probeflaky", f"the probe with --flaky-core1 {rate} --seed 7: the misreads show, CPU1 retired",
         {**common(r, halves=min_halves(pms)), "misreads injected": (r["misreads"] or 0) > 0,
          "MISREAD > 0 and ERR > 0 on the screen": mis > 0 and (t["errs"] or 0) > 0,
          "FAIL (MISREAD)": (t["state"], t["why"]) == (3, 1)},
         f"{r['misreads']} misreads injected; PING {t['pings']} ERR {t['errs']} MISREAD {mis} (patterns "
         f"{t['c0_mis']} + {t['c1_mis']}, mailbox {t['rejected']}); {STATE.get(t['state'])} ({WHY.get(t['why'])}); "
         f"{r['halves']} audio halves")

if pms:
    r = load("probemis")
    t = pr(r)
    case("probemis", "the probe's test hook: CPU1 reads 0x00200000 where 0 is in round 40",
         {**common(r, halves=min_halves(pms)), "MISREAD 1, CPU1's": (t["c1_mis"], t["c0_mis"], t["rejected"]) == (1, 0, 0),
          "BIT 00200000": (t["c1_bits"] or 0) | (t["c0_bits"] or 0) == 0x00200000,
          "retired: FAIL PATTERN (MISREAD), ERR 1": (t["state"], t["why"], t["fail"], t["errs"]) == (3, 1, 1, 1),
          "stopped at round 40": t["rounds"] == 40 and t["steps"] == 161},
         f"c1_mis {t['c1_mis']} c0_mis {t['c0_mis']} BIT {(t['c1_bits'] or 0):08x}; {STATE.get(t['state'])} "
         f"({WHY.get(t['why'])}, probe {FAILWHY.get(t['fail'])}), ERR {t['errs']}, rounds {t['rounds']} "
         f"(steps {t['steps']}), PING {t['pings']}; {r['halves']} audio halves")

print(f"### Second core in fm1-emu ({ms} ms each; dual and dualfail {dms} ms, write {wms} ms"
      + (f", probe {pms} ms" if pms else "") + ")\n")
print("| case | what | result | seen |")
print("| --- | --- | --- | --- |")
for row in rows:
    print("| " + " | ".join(str(x) for x in row) + " |")
if dual_rows:
    print("\nCase dual: CPU0's render (synth_render, the joins' waits included: the CPU guard's number) in the emulator's own "
          "time (no cost model), per block over blocks 8..31 of each segment, and CPU1's job load (eng_c1_load):\n")
    print("| patch, 6 voices | DUAL CORE off: CPU0 | on: CPU0 | change | on: CPU1 job (C1 ON n%) | CPU1 jobs |")
    print("| --- | --- | --- | --- | --- | --- |")
    for p, a, b, c, j in dual_rows:
        print(f"| {p} | {a:.1f} % | {b:.1f} % | {100 * (b / a - 1):+.1f} % | {c} % | {j} |")
if bad:
    print("\nFAILED: " + "; ".join(bad))
    sys.exit(1)
