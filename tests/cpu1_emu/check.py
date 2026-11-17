#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""KNOBI-SIX second core in fm1-emu (tests/cpu1_emu/run.sh): read each case's log (the emulator's report and the
RAM it dumped at the end: fm1_cpu1_mb for the plain image, cpu1_test for the test hooks, platform.c) and check it.
Prints a Markdown summary; exits 1 if a case is not as it should be.
  python3 tests/cpu1_emu/check.py OUTDIR MS RATE"""
import re
import sys

out, ms, rate = sys.argv[1], int(sys.argv[2]), sys.argv[3]
TICKS_PER_US = 24
BLOCK_US = 256 * 1_000_000 // 44100                       # one audio block (HALF_FRAMES at 44.1 kHz): 5804 us
STATE = {0: "OFF", 1: "IDLE", 2: "BUSY", 3: "FAIL"}
WHY = {0: "-", 1: "MISREAD", 2: "LATE", 3: "BARRED", 4: "FAULT", 5: "SILENT"}
TEST = ["magic", "state", "why", "post", "join", "took", "failed", "pending", "pings", "rejected", "jobs", "barred",
        "at_ms", "flash"]
MB = ["request", "fn", "arg", "check", "_", "_", "_", "_", "ready", "complete", "rejected", "misread", "jobs"]
MIN_HALVES = int((ms - 400) / 1000 * 44100 / 256 * 0.95)   # the audio ran from the end of the boot (~0.3 s in the
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


def common(r):
    return {"no halt": not r["halt"], "no chip reset (the boot guard counts none)": not r["reset"],
            "cpu1 released": r["released"], f"audio ran (>= {MIN_HALVES} halves)": r["halves"] >= MIN_HALVES}


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
      "flash_ok (the boot's flash reads passed the gate)": t["flash"] == 1},
     f"{STATE.get(t['state'])}, {t['pings']} pings back ({t['jobs']} jobs), rejected {t['rejected']}; boot guard "
     f"failed {t['failed']} pending {t['pending']}; flash_ok {t['flash']}; {r['halves']} audio halves")

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
      "barred until power-off": t["barred"] == 1, "boot guard: failed 0, pending 1": (t["failed"], t["pending"]) == (0, 1)},
     f"{r['misreads']} misreads injected; {STATE.get(t['state'])} ({WHY.get(t['why'])}) at {t['at_ms'] or 'boot'} ms "
     f"after {t['pings']} good pings, rejected {t['rejected']}, barred {t['barred']}; {r['halves']} audio halves"
     + ("; the emulator stopped cpu1 on a wild access" if r["c1stop"] else ""))

print(f"### Second core in fm1-emu ({ms} ms each)\n")
print("| case | what | result | seen |")
print("| --- | --- | --- | --- |")
for row in rows:
    print("| " + " | ".join(str(x) for x in row) + " |")
if bad:
    print("\nFAILED: " + "; ".join(bad))
    sys.exit(1)
