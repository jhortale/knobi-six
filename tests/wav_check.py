#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Clicks and dropouts in a host render (tests/run_host.sh writes build/host/app.wav: 44.1 kHz, 16-bit stereo).

    python3 tests/wav_check.py WAV --steady A B --check C D [--ratio R] [--fade-max MS] [--gap-max MS] [--no-gap]

--steady A B   ms: a stretch of the steady sound. Its largest sample-to-sample step is the yardstick (a smooth,
               low-passed patch keeps it small, so a click, a jump of the waveform, stands out).
--check C D    ms: where the event happens. No step there may exceed R times the yardstick (default 1.5).
--fade-max MS  with a gap: each fade (the level from 90 % down to the gap, from the gap back up to 90 %, the level
               being the largest sample in a 4 ms window: keep the drone's lowest note above 250 Hz) at most MS ms,
               the two together at most 2 MS.
--gap-max MS   the dropout (both channels exactly 0) at most MS ms; --no-gap: there must be none (a sliced save).
Prints what it measured; exit 1 if a check fails."""
import struct
import sys


def read_wav(path):
    with open(path, "rb") as f:
        d = f.read()
    i = 12
    while i < len(d):
        tag, n = d[i:i + 4], struct.unpack("<I", d[i + 4:i + 8])[0]
        if tag == b"data":
            raw = d[i + 8:i + 8 + n]
            s = struct.unpack("<%dh" % (len(raw) // 2), raw)
            return s[0::2], s[1::2]
        i += 8 + n
    raise SystemExit("no data chunk in " + path)


def main(a):
    path, ratio, fade_max, gap_max, no_gap, steady, check = a[1], 1.5, None, None, False, None, None
    i = 2
    while i < len(a):
        if a[i] == "--steady": steady = (int(a[i + 1]), int(a[i + 2])); i += 3
        elif a[i] == "--check": check = (int(a[i + 1]), int(a[i + 2])); i += 3
        elif a[i] == "--ratio": ratio = float(a[i + 1]); i += 2
        elif a[i] == "--fade-max": fade_max = float(a[i + 1]); i += 2
        elif a[i] == "--gap-max": gap_max = float(a[i + 1]); i += 2
        elif a[i] == "--no-gap": no_gap = True; i += 1
        else: raise SystemExit(__doc__)
    if not steady or not check: raise SystemExit(__doc__)
    L, R = read_wav(path)
    fs, ms = 44100, 44.1
    fr = lambda t: int(t * ms)

    def maxstep(a0, a1):
        m, at = 0, a0
        for ch in (L, R):
            for k in range(max(a0, 1), min(a1, len(ch))):
                d = abs(ch[k] - ch[k - 1])
                if d > m: m, at = d, k
        return m, at

    def peak(a0, a1):
        return max(max(abs(x) for x in L[a0:a1]), max(abs(x) for x in R[a0:a1]))

    ok = True
    base, _ = maxstep(fr(steady[0]), fr(steady[1]))
    amp = peak(fr(steady[0]), fr(steady[1]))
    worst, at = maxstep(fr(check[0]), fr(check[1]))
    print("wav_check %s: steady %d..%d ms peak %d, largest step %d; %d..%d ms largest step %d at %.1f ms (%.2f x, limit %.2f)"
          % (path, steady[0], steady[1], amp, base, check[0], check[1], worst, at / ms, worst / max(base, 1), ratio))
    if base == 0 or amp < 1000:
        print("  FAIL: the steady stretch is silent"); ok = False
    if worst > ratio * base:
        print("  FAIL: a step %.2f x the steady sound's largest (a click)" % (worst / base)); ok = False

    a0, a1 = fr(check[0]), min(fr(check[1]), len(L))
    g0 = g1 = None                                  # the longest run of exact zeros (both channels)
    k = a0
    while k < a1:
        if L[k] == 0 and R[k] == 0:
            j = k
            while j < a1 and L[j] == 0 and R[j] == 0: j += 1
            if j - k > fr(1) and (g0 is None or j - k > g1 - g0): g0, g1 = k, j
            k = j
        else: k += 1
    if g0 is None:
        print("  no dropout (no ms of exact silence)")
        if gap_max is not None and not no_gap:
            print("  FAIL: expected a deliberate dropout"); ok = False
    else:
        W = fr(4)
        lvl = 0.9 * amp
        t = g0
        while t > a0 and peak(t - W, t) < lvl: t -= 1    # back from the gap until the level is 90 % again
        fade_out = (g0 - t) / ms
        t = g1
        while t < a1 and peak(t, t + W) < lvl: t += 1    # on from the gap until it is 90 % again
        fade_in = (t - g1) / ms
        gap = (g1 - g0) / ms
        print("  dropout at %.1f ms: fade out %.1f ms, silence %.1f ms, fade in %.1f ms (fades %.1f ms together)"
              % (g0 / ms, fade_out, gap, fade_in, fade_out + fade_in))
        if no_gap:
            print("  FAIL: a dropout where none was expected"); ok = False
        if fade_max is not None and (fade_out > fade_max or fade_in > fade_max or fade_out + fade_in > 2 * fade_max):
            print("  FAIL: a fade longer than %.0f ms" % fade_max); ok = False
        if gap_max is not None and gap > gap_max:
            print("  FAIL: silence longer than %.0f ms" % gap_max); ok = False
    print("  ok" if ok else "  FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
