/* SPDX-License-Identifier: GPL-3.0-only */
/* The host core (firmware/src/core/knobi_core.h, the plugin's engine; plugin plan Task 1):
 *   core_matches_host     the firmware's host build (tests/host.c running firmware/src/app.c) plays
 *                         tests/scripts/core_ref.txt (MIDI in: notes, CC 74, bend, pressure, sustain) into
 *                         ../build/unit/core_ref.wav (the Makefile makes it first); the core, given the same messages at
 *                         the same 256-frame blocks, gives the same samples, every one (> 1 s of them)
 *   param_roundtrip       kc_param_set / kc_param_get for every live id: the value clamped to its range, before and
 *                         after a render; a non-live id is not a parameter (set: nothing, get: 0)
 *   param_info_exposed    kc_param_info answers exactly the live ids (Phase 1's, DISTORTION, BPM, CLOCK DIV, ARP *):
 *                         not the stored-only FX ids, not P_SEQ_GEN; kind, range, default and labels as params.c
 *   odd_frames            rendering in chunks of 1, 37 and 4096 frames gives the samples of 256-frame chunks (MIDI
 *                         given before the same internal block)
 *   program_state         kc_program_get / set: the init program, number 0, unedited; an edit marks it edited; a
 *                         program + sequence set comes back as set, with its edited flag; a broken sequence refused;
 *                         the bank's user layer (kc_bank_set) loaded by a MIDI program change (CC 32 + PC) and by
 *                         kc_program_select, a slot it does not hold: init
 * Per-instance state (plan Task 2):
 *   two_instances_interleaved  two instances (different programs, notes, globals, the arp on in one) rendered in turn
 *                         on one thread: each gives the samples it gives alone
 *   two_instances_parallel     the same two on two threads at once (make -C tests tsan: under ThreadSanitizer)
 *   bank_swap_threads     kc_bank_set from another thread while an instance renders and loads programs (tsan too)
 *   bank_free_rule        a thread sets banks from a pool and "frees" (overwrites with FREED) each replaced bank as soon
 *                         as kc_bank_in_use, asked after the replacing set, returns another bank (knobi_core.h); the
 *                         instance, rendering and loading on another thread, never loads a FREED program (tsan too)
 *   create_recreate       create / destroy / create any number of times, several alive at once: each a fresh instance
 *   no_mutable_globals    the core's object (../build/unit/knobi_core.o, -O0) has no writable data but the binding's
 *                         pthread key (kc_key*: made once, read-only after): nm / objdump
 *   param_while_pending   kc_param_set after a kc_program_set the engine could not take yet edits the program set
 *   bank_validated        kc_bank_set refuses a bank with an entry the firmware would not store; kc_bank_in_use
 *   first_render_no_alloc (macOS) the core as a dylib (dlopen'd, as in the plugin bundle): a thread's first kc_render
 *                         allocates nothing (malloc zone statistics; a lazily allocated thread-local would)
 * Sample-accurate events (plan Task 3, kc_render_exact):
 *   split_matches_host    the firmware's host build plays tests/scripts/core_split.txt ("split": a block rendered in two
 *                         parts, the MIDI in between) into ../build/unit/core_split.wav; the core, given the same
 *                         messages between kc_render_exact calls cut at the same frames, gives the same samples
 *   exact_aligned         kc_render_exact in 256-frame calls gives kc_render's samples; after a part of a block taken
 *                         with kc_render, kc_render_exact hands out the rest of that block first
 *   exact_onset           a note given at frame 300 (kc_render_exact up to it) starts exactly 300 frames after the same
 *                         note given at frame 0; at 301, 301 frames
 *   exports_only_kc       the core as a shared library (the plugin's build flags: -fvisibility=hidden, KC_API) exports
 *                         the kc_* calls and nothing else (none of the firmware's own non-static functions)
 * The host's transport (plan Task 5, kc_clock_host; a host at 44.1 kHz: its transport before each block, kc_render_exact):
 *   core_transport_start_bar  SEQ PLAY on, then play from bar 1: step 0 at the start's frame, the 16ths on the grid,
 *                         the sequence's notes from its step 1
 *   core_transport_mid_bar    from ppq 1.5 / 5 / 1.6: the sequence's step matching the position (a resumed one inside
 *                         its first half, its gate what is left on the grid)
 *   core_transport_loop   a 1-bar loop, the wrap inside a block: 16 steps a pass, none doubled or skipped; a 12-step
 *                         sequence plays 1..12, 1..4 every pass
 *   core_transport_locate     forward and backward jumps: the matching step at once
 *   core_transport_tempo_ramp 100 -> 140 BPM: every 16th within 1 frame of the ideal grid
 *   core_transport_stop_internal  P_BPM kept, not used while playing (bpm_param_greyed); stopped: a release, the
 *                         internal clock at P_BPM from the position, the sequence on (SEQ PLAY), the arp with a key held
 *   core_no_stuck_notes   a tied sequence across loop wraps, locates, a stop and a restart; the arp with a chord across
 *                         a loop, a locate and a stop: every note-on has its note-off, the output silent at the end
 * The scenarios from Task 1 still run in child processes (they were written for one instance per process). */
#define _DEFAULT_SOURCE 1                       /* (glibc under -std=c11: fork, waitpid, MAP_ANON) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include "core/knobi_core.h"
#include "core/knobi_core.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define REF_WAV "../build/unit/core_ref.wav"
#define REF_SCRIPT "scripts/core_ref.txt"
#define MAXF (KC_RATE * 3u)

/* a scenario in a child process (one core instance per process for now); out: shared with the child */
static int16_t *shared_buf(uint32_t frames)
{
    void *p = mmap(NULL, (size_t)frames * 4u, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(2); }
    memset(p, 0, (size_t)frames * 4u);
    return p;
}
static int in_child(int (*fn)(void *), void *arg)
{
    int st = 0;
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(2); }
    if (pid == 0) { fflush(stdout); _exit(fn(arg)); }
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 99;
}

/* the reference script's MIDI: at block blk */
typedef struct { uint32_t blk; uint8_t m[3]; } ev_t;
static ev_t evs[64];
static int nev, ev_bad;
static uint32_t end_ms;
static void read_script(void)
{
    char line[256], what[16];
    unsigned t, a, b, c;
    FILE *f = fopen(REF_SCRIPT, "r");
    if (!f) { perror(REF_SCRIPT); exit(2); }
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || sscanf(line, "%u %15s", &t, what) != 2) continue;
        if (!strcmp(what, "end")) { end_ms = t; continue; }
        if (strcmp(what, "midi") || sscanf(line, "%*u %*s %i %u %u", (int *)&a, &b, &c) != 3 || nev >= 64) { ev_bad++; continue; }
        if (t * 441u / 2560u != (t + 1u) * 441u / 2560u) ev_bad++;   /* (a block at T + 1: not this test's timing) */
        evs[nev].blk = t * 441u / 2560u;
        evs[nev].m[0] = (uint8_t)a, evs[nev].m[1] = (uint8_t)b, evs[nev].m[2] = (uint8_t)c;
        nev++;
    }
    fclose(f);
}
static uint32_t msg_len(uint8_t st) { return (st & 0xF0u) == 0xC0u || (st & 0xF0u) == 0xD0u ? 2u : st >= 0xF8u ? 1u : 3u; }

/* the core plays the script: chunk sizes from sizes[] (in turn), each message given while the next block to render is
 * its block */
typedef struct { int16_t *out; uint32_t frames; const uint32_t *sizes; uint32_t nsizes; } play_t;
static int play_script(void *arg)
{
    play_t *a = arg;
    kc_t *k = kc_create();
    uint32_t done = 0, i = 0;
    int e = 0;
    if (!k) return 3;
    while (done < a->frames) {
        uint32_t n = a->sizes[i++ % a->nsizes], next = (done + KC_BLOCK - 1u) / KC_BLOCK;
        while (e < nev && evs[e].blk <= next) { kc_midi(k, evs[e].m, msg_len(evs[e].m[0])); e++; }
        if (n > a->frames - done) n = a->frames - done;
        if (e < nev) {                          /* not past the next message's block */
            uint32_t lim = evs[e].blk * KC_BLOCK;
            if (done < lim && done + n > lim) n = lim - done;
        }
        kc_render(k, a->out + 2u * done, n);
        done += n;
    }
    kc_destroy(k);
    return 0;
}

static int16_t *read_wav(const char *path, uint32_t *frames)
{
    FILE *f = fopen(path, "rb");
    uint8_t h[44];
    uint32_t bytes;
    int16_t *w;
    if (!f) { perror(path); return NULL; }
    if (fread(h, 1, 44, f) != 44 || memcmp(h, "RIFF", 4) || memcmp(h + 36, "data", 4)) { fclose(f); return NULL; }
    bytes = (uint32_t)h[40] | (uint32_t)h[41] << 8 | (uint32_t)h[42] << 16 | (uint32_t)h[43] << 24;
    w = malloc(bytes ? bytes : 4u);
    *frames = (uint32_t)fread(w, 4, bytes / 4u, f);
    fclose(f);
    return w;
}

static void core_matches_host(void)
{
    uint32_t nref = 0, i, diff = 0, first = 0, peak = 0;
    int16_t *ref = read_wav(REF_WAV, &nref), *out;
    static const uint32_t blk[] = {KC_BLOCK};
    play_t a;
    int rc;
    read_script();
    if (!ref || nref < KC_RATE) {
        printf("core_matches_host: no reference %s (%u frames; make -C tests unit makes it)\n", REF_WAV, nref);
        CHECK(0);
        return;
    }
    if (nref > MAXF) nref = MAXF;
    out = shared_buf(nref);
    a.out = out, a.frames = nref, a.sizes = blk, a.nsizes = 1;
    rc = in_child(play_script, &a);
    for (i = 0; i < 2u * nref; i++) {
        uint32_t m = (uint32_t)abs(ref[i]);
        if (m > peak) peak = m;
        if (out[i] != ref[i] && !diff++) first = i / 2u;
    }
    printf("core_matches_host: %d messages, %u frames (%u ms), reference peak %u; %u samples differ%s", nev, nref,
           nref * 1000u / KC_RATE, peak, diff, diff ? "" : "\n");
    if (diff) printf(", the first at frame %u (ref %d %d, core %d %d)\n", first, ref[2u * first], ref[2u * first + 1u],
                     out[2u * first], out[2u * first + 1u]);
    CHECK(rc == 0 && ev_bad == 0 && nev == 9 && nref >= KC_RATE && peak > 1000u && diff == 0);
    free(ref);
}

static int odd_child(void *arg) { return play_script(arg); }
static void odd_frames(void)
{
    static const uint32_t aligned[] = {KC_BLOCK}, odd[] = {1, 37, 4096};
    uint32_t n = KC_RATE + 4097u, diff = 0, i;
    int16_t *x = shared_buf(n), *y = shared_buf(n);
    play_t a = {x, n, aligned, 1}, b = {y, n, odd, 3};
    int r1, r2;
    if (!nev) read_script();
    r1 = in_child(odd_child, &a);
    r2 = in_child(odd_child, &b);
    for (i = 0; i < 2u * n; i++) diff += x[i] != y[i];
    printf("odd_frames: %u frames in chunks of 1, 37, 4096 vs 256: %u samples differ\n", n, diff);
    CHECK(r1 == 0 && r2 == 0 && diff == 0);
}

/* ---- sample-accurate events (kc_render_exact) */
#define SPLIT_WAV "../build/unit/core_split.wav"
#define SPLIT_SCRIPT "scripts/core_split.txt"
typedef struct { uint32_t at; uint8_t m[3]; } sev_t;   /* a message at frame 'at' (kc_midi before that frame) */
static sev_t sevs[64];
static int nsev, sev_bad;
static int read_split_script(void)
{
    char line[256], what[16];
    unsigned t, a, b, c;
    uint32_t split_ms[32], split_f[32], nsplit = 0;
    FILE *f = fopen(SPLIT_SCRIPT, "r");
    if (!f) { perror(SPLIT_SCRIPT); return -1; }
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || sscanf(line, "%u %15s", &t, what) != 2 || !strcmp(what, "end")) continue;
        if (!strcmp(what, "split")) {           /* the block rendered at t (one must be) is split at F */
            if (sscanf(line, "%*u %*s %u", &a) != 1 || nsplit >= 32 || !a || a >= KC_BLOCK ||
                t * 441u / 2560u == (t - 1u) * 441u / 2560u) { sev_bad++; continue; }
            split_ms[nsplit] = t, split_f[nsplit++] = a;
            continue;
        }
        if (strcmp(what, "midi") || sscanf(line, "%*u %*s %i %u %u", (int *)&a, &b, &c) != 3 || nsev >= 64) { sev_bad++; continue; }
        sevs[nsev].at = t * 441u / 2560u * KC_BLOCK;   /* (no split: before the next block, as core_ref.txt) */
        if (t * 441u / 2560u != (t + 1u) * 441u / 2560u) sev_bad++;
        for (uint32_t i = 0; i < nsplit; i++)   /* (a split at t: at its frame in the block rendered at t) */
            if (split_ms[i] == t) sevs[nsev].at = (t * 441u / 2560u - 1u) * KC_BLOCK + split_f[i];
        sevs[nsev].m[0] = (uint8_t)a, sevs[nsev].m[1] = (uint8_t)b, sevs[nsev].m[2] = (uint8_t)c;
        nsev++;
    }
    fclose(f);
    return (int)nsplit;
}
/* the plugin's way (plugin/src/CoreRunner.h): render up to each message's frame and to each block's end with
 * kc_render_exact, the messages at their frames */
static void play_exact(kc_t *k, const sev_t *ev, int n, int16_t *out, uint32_t frames)
{
    uint32_t done = 0;
    int e = 0;
    while (done < frames) {
        uint32_t end = (done / KC_BLOCK + 1u) * KC_BLOCK;
        while (e < n && ev[e].at <= done) { kc_midi(k, ev[e].m, msg_len(ev[e].m[0])); e++; }
        if (e < n && ev[e].at < end) end = ev[e].at;
        if (end > frames) end = frames;
        kc_render_exact(k, out + 2u * done, end - done);
        done = end;
    }
}
typedef struct { int16_t *out; uint32_t frames; } split_arg_t;
static int split_child(void *arg)
{
    split_arg_t *a = arg;
    kc_t *k = kc_create();
    if (!k) return 3;
    play_exact(k, sevs, nsev, a->out, a->frames);
    kc_destroy(k);
    return 0;
}
static void split_matches_host(void)
{
    uint32_t nref = 0, i, diff = 0, first = 0, peak = 0;
    int16_t *ref = read_wav(SPLIT_WAV, &nref), *out;
    int nsplit = read_split_script(), rc;
    split_arg_t a;
    if (!ref || nref < KC_RATE) {
        printf("split_matches_host: no reference %s (%u frames; make -C tests unit makes it)\n", SPLIT_WAV, nref);
        CHECK(0);
        return;
    }
    if (nref > MAXF) nref = MAXF;
    out = shared_buf(nref);
    a.out = out, a.frames = nref;
    rc = in_child(split_child, &a);
    for (i = 0; i < 2u * nref; i++) {
        uint32_t m = (uint32_t)abs(ref[i]);
        if (m > peak) peak = m;
        if (out[i] != ref[i] && !diff++) first = i / 2u;
    }
    printf("split_matches_host: %d splits, %d messages, %u frames, reference peak %u; %u samples differ%s", nsplit, nsev,
           nref, peak, diff, diff ? "" : "\n");
    if (diff) printf(", the first at frame %u (ref %d %d, core %d %d)\n", first, ref[2u * first], ref[2u * first + 1u],
                     out[2u * first], out[2u * first + 1u]);
    CHECK(rc == 0 && sev_bad == 0 && nsplit == 8 && nsev == 12 && peak > 1000u && diff == 0);
    free(ref);
}

typedef struct { int16_t *x, *y, *z; uint32_t n; } exact_arg_t;
static int exact_aligned_child(void *arg)
{
    exact_arg_t *a = arg;
    static const uint8_t on[3] = {0x90, 60, 100}, on2[3] = {0x90, 67, 90};
    kc_t *k1 = kc_create(), *k2 = kc_create(), *k3 = kc_create();
    uint32_t i;
    if (!k1 || !k2 || !k3) return 3;
    kc_param_set(k1, P_ARP_ON, 1), kc_param_set(k2, P_ARP_ON, 1), kc_param_set(k3, P_ARP_ON, 1);   /* (the step path) */
    kc_midi(k1, on, 3), kc_midi(k2, on, 3), kc_midi(k3, on, 3);
    for (i = 0; i < a->n; i += KC_BLOCK) {
        if (i == 40u * KC_BLOCK) kc_midi(k1, on2, 3), kc_midi(k2, on2, 3), kc_midi(k3, on2, 3);
        kc_render(k1, a->x + 2u * i, KC_BLOCK);
        kc_render_exact(k2, a->y + 2u * i, KC_BLOCK);
        kc_render(k3, a->z + 2u * i, 100);      /* (a block rendered, 100 of it handed out: kc_render_exact the rest) */
        kc_render_exact(k3, a->z + 2u * i + 200u, KC_BLOCK - 100u);
    }
    kc_destroy(k1), kc_destroy(k2), kc_destroy(k3);
    return 0;
}
static void exact_aligned(void)
{
    uint32_t n = 100u * KC_BLOCK, dy = 0, dz = 0, peak = 0;
    exact_arg_t a = {shared_buf(n), shared_buf(n), shared_buf(n), n};
    int rc = in_child(exact_aligned_child, &a);
    for (uint32_t i = 0; i < 2u * n; i++) {
        dy += a.x[i] != a.y[i], dz += a.x[i] != a.z[i];
        if ((uint32_t)abs(a.x[i]) > peak) peak = (uint32_t)abs(a.x[i]);
    }
    printf("exact_aligned: %u frames (arp on), peak %u: kc_render_exact by blocks %u differ, after part of a block %u\n",
           n, peak, dy, dz);
    CHECK(rc == 0 && peak > 1000u && dy == 0 && dz == 0);
}

static uint32_t onset(const int16_t *x, uint32_t n)   /* the first frame not silent */
{
    for (uint32_t i = 0; i < n; i++) if (x[2u * i] || x[2u * i + 1u]) return i;
    return n;
}
static int onset_child(void *arg)
{
    exact_arg_t *a = arg;
    static const sev_t at0 = {0, {0x90, 60, 100}}, at300 = {300, {0x90, 60, 100}}, at301 = {301, {0x90, 60, 100}};
    kc_t *k1 = kc_create(), *k2 = kc_create(), *k3 = kc_create();
    if (!k1 || !k2 || !k3) return 3;
    play_exact(k1, &at0, 1, a->x, a->n);
    play_exact(k2, &at300, 1, a->y, a->n);
    play_exact(k3, &at301, 1, a->z, a->n);
    kc_destroy(k1), kc_destroy(k2), kc_destroy(k3);
    return 0;
}
static void exact_onset(void)
{
    uint32_t n = 8u * KC_BLOCK, o0, o300, o301;
    exact_arg_t a = {shared_buf(n), shared_buf(n), shared_buf(n), n};
    int rc = in_child(onset_child, &a);
    o0 = onset(a.x, n), o300 = onset(a.y, n), o301 = onset(a.z, n);
    printf("exact_onset: a note at frame 0 sounds from frame %u, at 300 from %u, at 301 from %u\n", o0, o300, o301);
    CHECK(rc == 0 && o0 < 64u && o300 == o0 + 300u && o301 == o0 + 301u);
}

/* the shared library's exported symbols (nm: defined, external): kc_* only */
static void exports_only_kc(void)
{
#if defined __APPLE__
    const char *cmd = "nm -gU ../build/unit/libknobi_core.dylib 2>/dev/null";
#else
    const char *cmd = "nm -D --defined-only ../build/unit/libknobi_core.so 2>/dev/null";
#endif
    char line[512];
    int kc = 0, other = 0, api = 0;
    FILE *p = popen(cmd, "r");
    if (!p) { CHECK(0); return; }
    while (fgets(line, sizeof line, p)) {
        char addr[64], type[8], name[400];
        if (sscanf(line, "%63s %7s %399s", addr, type, name) != 3) continue;
        const char *nm = name[0] == '_' ? name + 1 : name;   /* (Mach-O's leading underscore) */
        if (type[0] == 'w' || type[0] == 'V') continue;   /* (weak: the toolchain's) */
        if (!strncmp(nm, "kc_", 3)) kc++;
        else if (!strcmp(nm, "_init") || !strcmp(nm, "_fini") || !strncmp(nm, "__", 2) || !strcmp(type, "A")) continue;
        else { if (other < 10) printf("  exported: %s\n", nm); other++; }
    }
    pclose(p);
    if ((p = fopen("../firmware/src/core/knobi_core.h", "r")) != NULL) {   /* (the calls declared KC_API) */
        while (fgets(line, sizeof line, p)) api += !strncmp(line, "KC_API ", 7);
        fclose(p);
    }
    printf("exports_only_kc: %d kc_* exported (%d declared KC_API), %d others\n", kc, api, other);
    CHECK(api >= 18 && kc == api && other == 0);
}

static int roundtrip_child(void *arg)
{
    kc_t *k = kc_create();
    int16_t buf[2 * KC_BLOCK];
    int bad = 0, n = 0;
    (void)arg;
    if (!k) return 3;
    for (uint32_t id = 0; id < KC_PARAM_ID_LIMIT + 4u; id++) {
        kc_param_info_t in;
        int32_t tries[5], got;
        if (kc_param_info((uint16_t)id, &in) != 0) {
            int32_t before = kc_param_get(k, (uint16_t)id);
            kc_param_set(k, (uint16_t)id, 5);
            if (kc_param_get(k, (uint16_t)id) != 0 || before != 0) bad++, printf("  id %u: not a param, get %d\n", id, kc_param_get(k, (uint16_t)id));
            continue;
        }
        n++;
        tries[0] = in.min, tries[1] = in.max, tries[2] = in.def, tries[3] = in.min - 1000, tries[4] = in.max + 1000;
        for (int t = 0; t < 5; t++) {
            int32_t want = tries[t] < in.min ? in.min : tries[t] > in.max ? in.max : tries[t];
            kc_param_set(k, (uint16_t)id, tries[t]);
            got = kc_param_get(k, (uint16_t)id);
            kc_render(k, buf, KC_BLOCK);
            if (got != want || kc_param_get(k, (uint16_t)id) != want)
                bad++, printf("  %s: set %d, want %d, got %d / %d\n", in.name, tries[t], want, got, kc_param_get(k, (uint16_t)id));
        }
        kc_param_set(k, (uint16_t)id, (in.min + in.max) / 2);
        if (kc_param_get(k, (uint16_t)id) != (in.min + in.max) / 2) bad++;
        kc_param_set(k, (uint16_t)id, in.def);
    }
    printf("param_roundtrip: %d live ids, %d wrong\n", n, bad);
    kc_destroy(k);
    return bad || n != 73;
}
static void param_roundtrip(void) { CHECK(in_child(roundtrip_child, NULL) == 0); }

static void param_info_exposed(void)
{
    int n = 0, bad = 0;
    for (uint32_t id = 0; id < 512u; id++) {
        kc_param_info_t in;
        int rc = kc_param_info((uint16_t)id, &in), live = id < P_COUNT_RESERVED && P_IS_LIVE(id);
        const param_t *p = live ? param_get((uint16_t)id) : NULL;
        if ((rc == 0) != live) { bad++, printf("  id %u: info %d, live %d\n", id, rc, live); continue; }
        if (rc) continue;
        n++;
        if (!in.name || strcmp(in.name, p->name) || in.min != p->min || in.max != p->max || in.def != p->def ||
            in.bipolar != p->bipolar || in.section != p->section ||
            in.kind != (p->kind == PK_SWITCH ? KC_KIND_SWITCH : p->kind == PK_SELECT ? KC_KIND_SELECT : KC_KIND_KNOB))
            bad++, printf("  %s: info differs\n", p->name);
        if (in.kind == KC_KIND_SWITCH && (in.min != 0 || in.max != 1)) bad++;
        if (in.kind == KC_KIND_SELECT) {
            if (!in.labels) bad++, printf("  %s: no labels\n", p->name);
            else for (int32_t v = in.min; v <= in.max; v++) if (!in.labels[v - in.min] || !in.labels[v - in.min][0]) bad++;
        }
    }
    {
        kc_param_info_t in;
        static const uint16_t hidden[] = {P_FX_ON, P_FXA_TYPE, P_FXA_MIX, P_FXA_P1, P_FXA_P2, P_FXA_SYNC, P_FXA_DIV, P_FXB_TYPE,
                                          P_FXB_MIX, P_FXB_P1, P_FXB_P2, P_FXB_SYNC, P_FXB_DIV, P_SEQ_GEN};
        static const uint16_t shown[] = {P_DISTORTION, P_BPM, P_CLK_DIV, P_ARP_ON, P_ARP_MODE, P_ARP_RANGE, P_ARP_HOLD, 0, P_UNISON_VOICES};
        for (unsigned i = 0; i < sizeof hidden / sizeof hidden[0]; i++) if (kc_param_info(hidden[i], &in) == 0) bad++;
        for (unsigned i = 0; i < sizeof shown / sizeof shown[0]; i++) if (kc_param_info(shown[i], &in) != 0) bad++;
        if (kc_param_info(P_VCO1_FREQ, NULL) != 0) bad++;   /* (NULL: is it exposed) */
    }
    printf("param_info_exposed: %d ids exposed (66 Phase 1 + DISTORTION + BPM, CLOCK DIV, 4 ARP), %d wrong\n", n, bad);
    CHECK(n == 73 && bad == 0);
}

static int program_child(void *arg)
{
    kc_t *k = kc_create();
    static program_t p, q, init;
    static seq_t s, t;
    static const uint8_t nt[2] = {60, 64}, vl[2] = {100, 80};
    int16_t buf[2 * KC_BLOCK];
    int32_t num = -5;
    int ed = -1, bad = 0;
    (void)arg;
    if (!k) return 3;
    program_init(&init);
    kc_program_get(k, &num, &p, &s, &ed);
    if (num != 0 || ed != 0 || memcmp(p.v, init.v, sizeof p.v) || strcmp(p.name, "INIT") || s.len != 0) bad++, printf("  init: %d %d\n", num, ed);
    kc_param_set(k, P_FLT_FREQ, 10);
    kc_render(k, buf, KC_BLOCK);
    kc_program_get(k, &num, &p, &s, &ed);
    if (ed != 1 || p.v[P_FLT_FREQ] != 10) bad++, printf("  edit: edited %d\n", ed);
    kc_param_set(k, P_FLT_FREQ, init.v[P_FLT_FREQ]);
    kc_program_get(k, &num, &p, &s, &ed);
    if (ed != 0) bad++, printf("  edit undone: edited %d\n", ed);

    program_init(&q);
    memcpy(q.name, "CORE TEST", 10);
    q.v[P_VCO1_SHAPE] = 200, q.v[P_ARP_MODE] = 2;
    seq_clear(&t);
    (void)seq_rec_step(&t, nt, vl, 2), (void)seq_rec_rest(&t), (void)seq_rec_step(&t, nt, vl, 1), (void)seq_rec_tie(&t);
    if (kc_program_set(k, 123, &q, &t, 1) != 0) bad++;
    kc_render(k, buf, KC_BLOCK);
    kc_program_get(k, &num, &p, &s, &ed);
    if (num != 123 || ed != 1 || strcmp(p.name, "CORE TEST") || p.v[P_VCO1_SHAPE] != 200 || p.v[P_ARP_MODE] != 2 ||
        kc_param_get(k, P_VCO1_SHAPE) != 200 || memcmp(&s, &t, sizeof s))
        bad++, printf("  set: %d %d %s\n", num, ed, p.name);
    if (kc_program_set(k, 7, &q, &t, 0) != 0) bad++;
    kc_program_get(k, &num, &p, &s, &ed);
    if (num != 7 || ed != 0) bad++, printf("  set unedited: %d %d\n", num, ed);
    t.step[0].notes[0] = 3;                     /* (below SEQ_NOTE_MIN: not a sequence the firmware stores) */
    if (kc_program_set(k, 8, &q, &t, 0) == 0) bad++, printf("  a broken sequence taken\n");
    kc_program_get(k, &num, &p, &s, &ed);
    if (num != 7) bad++;

    {                                           /* the bank's user layer: program 105 = q + t; a MIDI program change
                                                 * (CC 32 bank 1, program 5) loads it, unedited; program 106: init */
        static kc_bank_t bank;
        static const uint8_t cc32[3] = {0xB0, 32, 1}, pc[2] = {0xC0, 5}, pc6[2] = {0xC0, 6};
        t.step[0].notes[0] = 60;
        bank.used[105] = 1, bank.program[105] = q, bank.seq[105] = t;
        kc_bank_set(k, &bank);
        kc_midi(k, cc32, 3), kc_midi(k, pc, 2);
        kc_render(k, buf, KC_BLOCK);
        kc_program_get(k, &num, &p, &s, &ed);
        if (num != 105 || ed != 0 || strcmp(p.name, "CORE TEST") || memcmp(&s, &t, sizeof s))
            bad++, printf("  bank program change: %d %d %s, %u steps\n", num, ed, p.name, s.len);
        kc_midi(k, cc32, 3), kc_midi(k, pc6, 2);
        kc_render(k, buf, KC_BLOCK);
        kc_program_get(k, &num, &p, &s, &ed);
        if (num != 106 || ed != 0 || strcmp(p.name, "INIT") || s.len) bad++, printf("  init from the bank: %d %s\n", num, p.name);
        if (kc_program_select(k, 105) != 0) bad++;
        kc_render(k, buf, KC_BLOCK);
        kc_program_get(k, &num, &p, &s, &ed);
        if (num != 105 || strcmp(p.name, "CORE TEST") || kc_program_select(k, 500) == 0) bad++, printf("  select: %d\n", num);
        kc_bank_set(k, NULL);
    }
    printf("program_state: %d wrong\n", bad);
    kc_destroy(k);
    return bad;
}
static void program_state(void) { CHECK(in_child(program_child, NULL) == 0); }

/* ---- per-instance state (plan Task 2) */
#define SC_BLOCKS 400u                          /* 2.3 s */
#define SC_FRAMES (SC_BLOCKS * KC_BLOCK)
/* scenario 0: the init program, a chord, CC 74 moves, a bend; 1: a set program (saw, ARP ON, UP+DN), TRANSPOSE +5, MONO,
 * other notes, sustain; both: notes off and on again later */
static void sc_setup(kc_t *k, int which)
{
    if (which == 1) {
        static program_t q;
        static seq_t t;
        program_init(&q);
        memcpy(q.name, "SCENE B", 8);
        q.v[P_VCO1_SHAPE] = 200, q.v[P_ARP_ON] = 1, q.v[P_ARP_MODE] = 2, q.v[P_FLT_FREQ] = 90;
        seq_clear(&t);
        kc_program_set(k, 42, &q, &t, 0);
        kc_global_set(k, G_TRANSPOSE, 5);
        kc_global_set(k, G_STEREO, 1);
    }
}
static void sc_midi(kc_t *k, int which, uint32_t blk)
{
    static const uint8_t a[][3] = {{0x90, 60, 100}, {0x90, 64, 90}, {0xB0, 74, 20}, {0xE0, 0, 80}, {0x80, 60, 0},
                                   {0x90, 67, 110}, {0xB0, 74, 100}, {0x80, 64, 0}, {0x80, 67, 0}};
    static const uint8_t b[][3] = {{0x90, 48, 70}, {0x90, 55, 120}, {0x90, 67, 64}, {0xB0, 64, 127}, {0x80, 48, 0},
                                   {0x80, 55, 0}, {0xB0, 64, 0}, {0x90, 50, 100}, {0x80, 67, 0}};
    static const uint32_t at[] = {3, 9, 40, 80, 120, 150, 200, 260, 330};
    for (uint32_t i = 0; i < 9u; i++)
        if (at[i] == blk) kc_midi(k, which ? b[i] : a[i], 3);
}
static int sc_render(kc_t *k, int which, int16_t *out)   /* all of it, on this thread */
{
    sc_setup(k, which);
    for (uint32_t blk = 0; blk < SC_BLOCKS; blk++) {
        sc_midi(k, which, blk);
        kc_render(k, out + 2u * blk * KC_BLOCK, KC_BLOCK);
    }
    return 0;
}
static int sc_alone(int which, int16_t *out)
{
    kc_t *k = kc_create();
    if (!k) return -1;
    sc_render(k, which, out);
    kc_destroy(k);
    return 0;
}
static uint32_t sc_diff(const int16_t *x, const int16_t *y)
{
    uint32_t d = 0;
    for (uint32_t i = 0; i < 2u * SC_FRAMES; i++) d += x[i] != y[i];
    return d;
}
static uint32_t sc_peak(const int16_t *x)
{
    uint32_t p = 0;
    for (uint32_t i = 0; i < 2u * SC_FRAMES; i++) if ((uint32_t)abs(x[i]) > p) p = (uint32_t)abs(x[i]);
    return p;
}
static int16_t *sc_ref[2];
static int sc_refs(void)                        /* each scenario alone (once) */
{
    for (int w = 0; w < 2; w++) {
        if (sc_ref[w]) continue;
        sc_ref[w] = calloc(2u * SC_FRAMES, sizeof(int16_t));
        if (sc_alone(w, sc_ref[w])) { free(sc_ref[w]); sc_ref[w] = NULL; return -1; }
    }
    return 0;
}

static void two_instances_interleaved(void)
{
    int16_t *o[2] = {calloc(2u * SC_FRAMES, 2), calloc(2u * SC_FRAMES, 2)};
    kc_t *k[2];
    uint32_t d0 = ~0u, d1 = ~0u, dab = 0;
    int ok = sc_refs() == 0;
    k[0] = kc_create(), k[1] = kc_create();
    if (ok && k[0] && k[1]) {
        sc_setup(k[0], 0), sc_setup(k[1], 1);
        for (uint32_t blk = 0; blk < SC_BLOCKS; blk++)
            for (int w = 0; w < 2; w++) {         /* (in turn, in chunks of 100 and 156 frames) */
                sc_midi(k[w], w, blk);
                kc_render(k[w], o[w] + 2u * blk * KC_BLOCK, 100);
                kc_render(k[w], o[w] + 2u * (blk * KC_BLOCK + 100u), KC_BLOCK - 100u);
            }
        d0 = sc_diff(o[0], sc_ref[0]), d1 = sc_diff(o[1], sc_ref[1]), dab = sc_diff(sc_ref[0], sc_ref[1]);
    }
    printf("two_instances_interleaved: instances %s; A vs alone %u, B vs alone %u samples differ (A vs B %u; peaks %u %u)\n",
           k[0] && k[1] ? "made" : "NOT made", d0, d1, dab, sc_ref[0] ? sc_peak(sc_ref[0]) : 0, sc_ref[1] ? sc_peak(sc_ref[1]) : 0);
    CHECK(ok && k[0] && k[1] && d0 == 0 && d1 == 0 && dab > SC_FRAMES && sc_peak(sc_ref[0]) > 1000 && sc_peak(sc_ref[1]) > 1000);
    if (k[0]) kc_destroy(k[0]);
    if (k[1]) kc_destroy(k[1]);
    free(o[0]), free(o[1]);
}

typedef struct { int which, rc; int16_t *out; atomic_int *go; } th_t;
static void *sc_thread(void *arg)
{
    th_t *t = arg;
    kc_t *k = kc_create();
    atomic_fetch_add(t->go, 1);                 /* (both made, then both render at once) */
    while (atomic_load(t->go) < 2) {}
    t->rc = k ? sc_render(k, t->which, t->out) : -1;
    if (k) kc_destroy(k);
    return NULL;
}
static void two_instances_parallel(void)
{
    enum { ROUNDS = 3 };
    uint32_t bad = 0;
    int ok = sc_refs() == 0;
    for (int r = 0; ok && r < ROUNDS; r++) {
        pthread_t th[2];
        atomic_int go = 0;
        th_t a[2];
        for (int w = 0; w < 2; w++) {
            a[w].which = w, a[w].rc = -2, a[w].out = calloc(2u * SC_FRAMES, 2), a[w].go = &go;
            pthread_create(&th[w], NULL, sc_thread, &a[w]);
        }
        for (int w = 0; w < 2; w++) pthread_join(th[w], NULL);
        for (int w = 0; w < 2; w++) {
            uint32_t d = a[w].rc ? ~0u : sc_diff(a[w].out, sc_ref[w]);
            if (d) bad++, printf("  round %d, instance %c: rc %d, %u samples differ from alone\n", r, 'A' + w, a[w].rc, d);
            free(a[w].out);
        }
    }
    printf("two_instances_parallel: %d rounds of two instances on two threads, %u wrong\n", ROUNDS, bad);
    CHECK(ok && bad == 0);
}

/* another thread swaps the bank (two banks, program 105 differs) while this one renders and selects 105 */
static kc_bank_t swap_bank[2];
static atomic_int swap_stop;
static void *swap_thread(void *arg)
{
    kc_t *k = arg;
    uint32_t n = 0;
    while (!atomic_load(&swap_stop)) kc_bank_set(k, &swap_bank[n++ & 1u]);
    kc_bank_set(k, &swap_bank[0]);
    return NULL;
}
static void bank_swap_threads(void)
{
    kc_t *k = kc_create();
    pthread_t th;
    int16_t buf[2 * KC_BLOCK];
    int bad = 0, seen[2] = {0, 0};
    if (!k) { printf("bank_swap_threads: no instance\n"); CHECK(0); return; }
    for (int b = 0; b < 2; b++) {
        program_init(&swap_bank[b].program[105]);
        memcpy(swap_bank[b].program[105].name, b ? "BANK B" : "BANK A", 7);
        seq_clear(&swap_bank[b].seq[105]);
        swap_bank[b].used[105] = 1;
    }
    atomic_store(&swap_stop, 0);
    kc_bank_set(k, &swap_bank[0]);              /* (before the thread starts: 105 is always one of the two) */
    pthread_create(&th, NULL, swap_thread, k);
    for (int i = 0; i < 300; i++) {
        program_t p;
        int32_t num;
        kc_program_select(k, i & 1 ? 105 : 3);
        kc_render(k, buf, KC_BLOCK);
        kc_program_get(k, &num, &p, NULL, NULL);
        if (num == 105) {
            if (!strcmp(p.name, "BANK A")) seen[0]++;
            else if (!strcmp(p.name, "BANK B")) seen[1]++;
            else bad++;
        }
    }
    atomic_store(&swap_stop, 1);
    pthread_join(th, NULL);
    kc_render(k, buf, KC_BLOCK);
    if (kc_bank_in_use(k) != &swap_bank[0]) bad++;
    printf("bank_swap_threads: program 105 from bank A %d times, B %d, other %d; in use after: %s\n", seen[0], seen[1], bad,
           kc_bank_in_use(k) == &swap_bank[0] ? "A" : "not A");
    CHECK(bad == 0 && seen[0] + seen[1] == 150);
    kc_destroy(k);
}

/* the free rule of knobi_core.h kc_bank_set: banks from a pool; a replaced bank is overwritten ("FREED", as a free
 * would leave it) once kc_bank_in_use, asked after the set that replaced it, returns another bank */
#define FREE_POOL 6
static kc_bank_t free_pool[FREE_POOL];
static atomic_int free_state[FREE_POOL];        /* 0 free, 1 set or in use, 2 replaced: waiting */
static atomic_int free_stop, free_done;
static void free_fill(int i, const char *name)
{
    program_init(&free_pool[i].program[105]);
    strcpy(free_pool[i].program[105].name, name);
    seq_clear(&free_pool[i].seq[105]);
    free_pool[i].used[105] = 1;
}
static void *free_thread(void *arg)
{
    kc_t *k = arg;
    int cur = 0, sets = 0;
    while (!atomic_load(&free_stop)) {
        int next = -1;
        for (int i = 0; i < FREE_POOL; i++)
            if (atomic_load(&free_state[i]) == 0) { next = i; break; }
        if (next >= 0) {
            free_fill(next, "LIVE");
            atomic_store(&free_state[next], 1);
            kc_bank_set(k, &free_pool[next]);
            atomic_store(&free_state[cur], 2);
            cur = next, sets++;
        }
        for (int i = 0; i < FREE_POOL; i++) {
            const kc_bank_t *u = kc_bank_in_use(k);
            if (atomic_load(&free_state[i]) == 2 && u != &free_pool[i]) {
                free_fill(i, "FREED");
                atomic_store(&free_state[i], 0);
            }
        }
    }
    atomic_store(&free_done, sets);
    return NULL;
}
static void bank_free_rule(void)
{
    kc_t *k = kc_create();
    pthread_t th;
    int16_t buf[2 * 32];
    int freed = 0, live = 0;
    if (!k) { printf("bank_free_rule: no instance\n"); CHECK(0); return; }
    for (int i = 0; i < FREE_POOL; i++) atomic_store(&free_state[i], 0);
    free_fill(0, "LIVE");
    atomic_store(&free_state[0], 1);
    kc_bank_set(k, &free_pool[0]);
    atomic_store(&free_stop, 0);
    pthread_create(&th, NULL, free_thread, k);
    for (int i = 0; i < 4000; i++) {
        program_t p;
        kc_program_select(k, i & 1 ? 105 : 3);
        kc_render(k, buf, 32);
        kc_program_get(k, NULL, &p, NULL, NULL);
        if (i & 1) {
            if (!strcmp(p.name, "LIVE")) live++;
            else freed++;
        }
    }
    atomic_store(&free_stop, 1);
    pthread_join(th, NULL);
    printf("bank_free_rule: %d banks set while rendering; program 105 loaded live %d times, from a freed bank %d\n",
           atomic_load(&free_done), live, freed);
    CHECK(freed == 0 && live == 2000 && atomic_load(&free_done) > 10);
    kc_destroy(k);
}

static void create_recreate(void)
{
    int16_t *o = calloc(2u * SC_FRAMES, 2);
    kc_t *many[4];
    int bad = 0, made = 0;
    if (sc_refs()) bad++;
    for (int i = 0; i < 5 && !bad; i++) {
        if (sc_alone(i & 1, o)) { bad++, printf("  create %d: NULL\n", i); break; }
        if (sc_diff(o, sc_ref[i & 1])) bad++, printf("  create %d: not a fresh instance\n", i);
    }
    for (int i = 0; i < 4; i++) made += (many[i] = kc_create()) != NULL;
    for (int i = 0; i < 4; i++) if (many[i]) kc_destroy(many[i]);
    printf("create_recreate: 5 create / render / destroy, 4 alive at once (%d made): %d wrong\n", made, bad);
    CHECK(bad == 0 && made == 4);
    free(o);
}

#ifndef CORE_OBJ
#define CORE_OBJ "../build/unit/knobi_core.o"
#endif
static void no_mutable_globals(void)
{
    char line[512], bad_names[2048] = "";
    int bad = 0, keys = 0, lines = 0;
#ifdef __APPLE__
    FILE *f = popen("nm -m " CORE_OBJ " 2>&1", "r");   /* addr (seg,sect) kind name */
#else
    FILE *f = popen("objdump -t " CORE_OBJ " 2>&1", "r");   /* addr flags section size name */
#endif
    if (!f) { perror("popen"); CHECK(0); return; }
    while (fgets(line, sizeof line, f)) {
        char *name = strrchr(line, ' '), *nl;
        int writable;
        if (!name) continue;
        name++;
        if ((nl = strchr(name, '\n'))) *nl = 0;
        lines++;
#ifdef __APPLE__
        writable = strstr(line, "(__DATA,__data)") || strstr(line, "(__DATA,__bss)") || strstr(line, "(__DATA,__common)") ||
                   strstr(line, "(__DATA,__thread_");
        if (name[0] == '_') name++;
        if (!strncmp(name, "ltmp", 4) || (name[0] == 'l' && name[1] == '_')) continue;   /* (the assembler's labels) */
#else
        {
            /* "addr flags section<TAB>size name": addr any width, flags up to 7 columns (some blank), the section
             * the last word before the tab */
            char *tab = strchr(line, '\t'), *sec, *flags = line;
            if (!tab) continue;
            *tab = 0;
            while (*flags && *flags != ' ') flags++;  /* (past the address) */
            sec = strrchr(line, ' ');
            if (!sec || sec < flags) continue;
            *sec++ = 0;
            if (strpbrk(flags, "FfdD")) continue;     /* (functions, files, sections, debug: not data) */
            writable = (!strncmp(sec, ".data", 5) && strncmp(sec, ".data.rel.ro", 12)) || !strncmp(sec, ".bss", 4) ||
                       !strncmp(sec, ".tbss", 5) || !strncmp(sec, ".tdata", 6) || !strcmp(sec, "*COM*");
        }
#endif
        if (!writable) continue;
        if (!strncmp(name, "kc_key", 6)) { keys++; continue; }
        bad++;
        if (strlen(bad_names) + strlen(name) + 2 < sizeof bad_names) strcat(bad_names, " "), strcat(bad_names, name);
    }
    pclose(f);
    printf("no_mutable_globals: %s: %d symbols, %d for the binding's key (kc_key*), %d other writable:%s\n", CORE_OBJ, lines,
           keys, bad, bad_names);
    CHECK(lines > 100 && keys > 0 && bad == 0);
}

static void param_while_pending(void)
{
    kc_t *k = kc_create();
    static program_t q;
    static seq_t t;
    program_t p;
    int16_t buf[2 * KC_BLOCK];
    int32_t num = -1, v0, v1;
    int bad = 0;
    if (!k) { printf("param_while_pending: no instance\n"); CHECK(0); return; }
    program_init(&q);
    q.v[P_FLT_FREQ] = 30;
    seq_clear(&t);
    kc_program_select(k, 7);                    /* (a load queued: the engine's buffer busy until the next block) */
    if (kc_program_set(k, 123, &q, &t, 0)) bad++;
    kc_param_set(k, P_FLT_FREQ, 77);            /* (on program 123: the one set) */
    v0 = kc_param_get(k, P_FLT_FREQ);
    kc_param_set(k, P_FLT_RESO, 100000);       /* (clamped) */
    v1 = kc_param_get(k, P_FLT_RESO);
    kc_render(k, buf, KC_BLOCK);
    kc_program_get(k, &num, &p, NULL, NULL);
    if (v0 != 77 || num != 123 || p.v[P_FLT_FREQ] != 77 || kc_param_get(k, P_FLT_FREQ) != 77) bad++;
    if (v1 != param_get(P_FLT_RESO)->max || p.v[P_FLT_RESO] != v1) bad++;
    printf("param_while_pending: get %d (77), after the load %d on program %d; RESO %d / %d (max %d): %d wrong\n", v0,
           p.v[P_FLT_FREQ], num, v1, p.v[P_FLT_RESO], param_get(P_FLT_RESO)->max, bad);
    CHECK(bad == 0);
    kc_destroy(k);
}

static void bank_validated(void)
{
    kc_t *k = kc_create();
    static kc_bank_t good, broken;
    static const uint8_t pc[2] = {0xC0, 5}, cc32[3] = {0xB0, 32, 1};
    int16_t buf[2 * KC_BLOCK];
    program_t p;
    int32_t num;
    int bad = 0, r1, r2, r3;
    if (!k) { printf("bank_validated: no instance\n"); CHECK(0); return; }
    program_init(&good.program[105]);
    memcpy(good.program[105].name, "GOOD", 5);
    seq_clear(&good.seq[105]);
    good.used[105] = 1;
    broken = good;
    memcpy(broken.program[105].name, "BROKEN", 7);
    broken.seq[105].len = 1, broken.seq[105].step[0].count = 1, broken.seq[105].step[0].notes[0] = 3;   /* (below SEQ_NOTE_MIN) */
    r1 = kc_bank_set(k, &good);
    r2 = kc_bank_set(k, &broken);               /* refused: good stays */
    kc_render(k, buf, KC_BLOCK);
    if (kc_bank_in_use(k) != &good) bad++;
    kc_midi(k, cc32, 3), kc_midi(k, pc, 2);
    kc_render(k, buf, KC_BLOCK);
    kc_program_get(k, &num, &p, NULL, NULL);
    if (num != 105 || strcmp(p.name, "GOOD")) bad++;
    if (kc_program_valid(&broken.program[105], &broken.seq[105]) == 0 || kc_program_valid(&good.program[105], &good.seq[105]))
        bad++;
    r3 = kc_bank_set(k, NULL);
    kc_render(k, buf, KC_BLOCK);
    if (kc_bank_in_use(k) != NULL) bad++;
    printf("bank_validated: set good %d, broken %d (-1), none %d; program 105 \"%s\"; %d wrong\n", r1, r2, r3, p.name, bad);
    CHECK(r1 == 0 && r2 == -1 && r3 == 0 && bad == 0);
    kc_destroy(k);
}

#ifdef __APPLE__
#include <dlfcn.h>
#include <malloc/malloc.h>
#ifndef CORE_DYLIB
#define CORE_DYLIB "../build/unit/libknobi_core.dylib"
#endif
static size_t blocks_used(void) { malloc_statistics_t st; malloc_zone_statistics(NULL, &st); return st.blocks_in_use; }
typedef struct { void (*render)(kc_t *, int16_t *, uint32_t); kc_t *k; long delta[2]; } alloc_t;
static void *alloc_thread(void *arg)
{
    alloc_t *a = arg;
    static int16_t buf[2 * KC_BLOCK * 4];
    for (int i = 0; i < 2; i++) {               /* the first render on this thread, then the next */
        size_t u0 = blocks_used();
        a->render(a->k, buf, KC_BLOCK * 4u);
        a->delta[i] = (long)(blocks_used() - u0);
    }
    return NULL;
}
static void first_render_no_alloc(void)
{
    void *h = dlopen(CORE_DYLIB, RTLD_NOW | RTLD_LOCAL);
    kc_t *(*create)(void) = h ? (kc_t *(*)(void))dlsym(h, "kc_create") : NULL;
    void (*destroy)(kc_t *) = h ? (void (*)(kc_t *))dlsym(h, "kc_destroy") : NULL;
    alloc_t a = {h ? (void (*)(kc_t *, int16_t *, uint32_t))dlsym(h, "kc_render") : NULL, NULL, {-1, -1}};
    int bad = 0;
    if (!create || !destroy || !a.render) { printf("first_render_no_alloc: %s: %s\n", CORE_DYLIB, dlerror()); CHECK(0); return; }
    a.k = create();
    for (int t = 0; t < 3; t++) {               /* three fresh threads, as a host's audio workers */
        pthread_t th;
        pthread_create(&th, NULL, alloc_thread, &a);
        pthread_join(th, NULL);
        printf("first_render_no_alloc: thread %d: blocks allocated by the first kc_render %ld, the second %ld\n", t,
               a.delta[0], a.delta[1]);
        bad += a.delta[0] != 0 || a.delta[1] != 0;
    }
    destroy(a.k);
    CHECK(bad == 0);
}
#else
static void first_render_no_alloc(void) { printf("first_render_no_alloc: macOS only: skipped\n"); }
#endif

/* ---- the host's transport (plan Task 5: kc_clock_host; synth/clock.h's own tests: test_clock_host.c). A host at
 * 44.1 kHz: before each host block its transport for the block's first frame (and a loop's wrap inside it at its
 * frame), then kc_render_exact of the block. The program: a sequence of 16ths, step i plays note 40 + i (velocity 100) */
#include <math.h>
static int16_t tr_buf[2u * 4096u];
static void tr_prog(kc_t *k, int len, int tie_last)
{
    program_t p;
    seq_t q;
    int32_t num;
    kc_program_get(k, &num, &p, NULL, NULL);
    seq_clear(&q);
    q.len = (uint8_t)len;
    for (int i = 0; i < len; i++) q.step[i].count = 1, q.step[i].notes[0] = (uint8_t)(40 + i), q.step[i].vels[0] = 100;
    if (tie_last) q.step[len - 1].tie = 1, q.step[len - 1].notes[0] = (uint8_t)(40 + len - 2);   /* (a tie: a copy) */
    p.v[P_CLK_DIV] = CLK_DIV_16TH;
    CHECK(kc_program_set(k, 0, &p, &q, 0) == 0);
}
/* the host: frames [*t, *t + frames) in blocks of hb at a steady tempo from ppq p0 (a loop [ls, le) when le > ls) */
static void tr_host(kc_t *k, uint64_t *t, uint64_t frames, uint32_t hb, int playing, double bpm, double p0, double ls,
                    double le)
{
    const double fpq = 44100.0 * 60.0 / bpm;     /* frames a quarter */
    double ppq = p0;
    for (uint64_t d = 0; d < frames; d += hb) {
        uint32_t n = frames - d < hb ? (uint32_t)(frames - d) : hb;
        kc_clock_host(k, playing, bpm, ppq, 0);
        ppq += n / fpq;
        if (playing && le > ls && ppq >= le) {    /* (a wrap inside the block: at its frame, the loop's start) */
            double w = (le - (ppq - n / fpq)) * fpq;
            uint32_t at = (uint32_t)ceil(w - 1e-9);
            if (at < n) kc_clock_host(k, 1, bpm, ls + (at - w) / fpq, at);
            ppq -= le - ls;
        }
        kc_render_exact(k, tr_buf, n);
        *t += n;
    }
}
static kc_step_t tr_st[KC_LOG];
static kc_note_t tr_nt[4096];
static uint32_t tr_nn;
static void tr_take_notes(kc_t *k) { uint32_t m; while ((m = kc_notes(k, tr_nt + tr_nn, 4096u - tr_nn)) && tr_nn < 4096u) tr_nn += m; }
/* the note-ons from frame f on, into on[] (up to max) */
static uint32_t tr_ons(uint64_t f, int *on, uint32_t max)
{
    uint32_t m = 0;
    for (uint32_t i = 0; i < tr_nn; i++) if (tr_nt[i].frame >= f && tr_nt[i].vel && m < max) on[m++] = tr_nt[i].note;
    return m;
}
/* notes sounding after the log (on minus off, per note; must be 0 or 1): how many */
static int tr_sounding(int *bad)
{
    int c[128] = {0}, n = 0;
    *bad = 0;
    for (uint32_t i = 0; i < tr_nn; i++) {
        c[tr_nt[i].note] += tr_nt[i].vel ? 1 : -1;
        if (c[tr_nt[i].note] < 0 || c[tr_nt[i].note] > 1) (*bad)++;
    }
    for (int i = 0; i < 128; i++) n += c[i] != 0;
    return n;
}

/* SEQ PLAY on with the host stopped (the internal clock plays it), then the host plays from bar 1 at frame 1024: step 0
 * there (a jump), note 40 first, then 41.. on 16ths of 120 BPM exactly (1024 + ceil(k x 5512.5)) */
static void core_transport_start_bar(void)
{
    kc_t *k = kc_create();
    uint64_t t = 0;
    uint32_t n, bad = 0, j = 0;
    int on[16], m;
    tr_prog(k, 16, 0);
    tr_nn = 0;
    tr_host(k, &t, 1024, 512, 0, 120.0, 0.0, 0, 0);   /* (the program set taken: kc_create's load drained first) */
    kc_seq_play(k, 1);
    kc_clock_steps(k, tr_st, KC_LOG), tr_take_notes(k), tr_nn = 0;
    tr_host(k, &t, 8u * 5512u, 512, 1, 120.0, 0.0, 0, 0);
    n = kc_clock_steps(k, tr_st, KC_LOG);
    tr_take_notes(k);
    for (uint32_t i = 0; i < n; i++) {
        if (tr_st[i].frame < 1024u) continue;
        bad += tr_st[i].frame != 1024u + ((uint64_t)j * 11025u + 1u) / 2u || tr_st[i].step != (int32_t)j ||
               !(tr_st[i].kind & CLK_EV_HOSTED);
        j++;
    }
    m = (int)tr_ons(1024, on, 16);
    printf("core_transport_start_bar: following %d, %u steps from frame 1024 (%u off the grid), first kind %02X, "
           "note-ons %d: %d %d %d ..\n", kc_clock_following(k), j, bad, n ? tr_st[0].kind : 0, m, m > 0 ? on[0] : -1,
           m > 1 ? on[1] : -1, m > 2 ? on[2] : -1);
    CHECK(kc_clock_following(k) && j == 8u && bad == 0 && m == 8 && on[0] == 40 && on[1] == 41 && on[7] == 47);
    kc_destroy(k);
}

/* start mid-bar: ppq 1.5 (beat 2.5): the sequence's step 7 (note 46) at once, then 47 on the grid; ppq 5 (bar 2 beat
 * 2): step 20 of the song = the 16-step sequence's step 5 (note 44); ppq 1.6: inside the 16th's first half: step 6
 * resumed (note 46 at once, its gate to ppq 1.625: 551.25 frames, 552 whole ones) */
static void core_transport_mid_bar(void)
{
    const double starts[3] = {1.5, 5.0, 1.6};
    const int want[3] = {46, 44, 46};
    for (int c = 0; c < 3; c++) {
        kc_t *k = kc_create();
        uint64_t t = 0;
        int on[4], m;
        uint32_t n;
        tr_prog(k, 16, 0);
        tr_host(k, &t, 2048, 512, 0, 120.0, 0.0, 0, 0);
        kc_seq_play(k, 1);
        kc_clock_steps(k, tr_st, KC_LOG), tr_nn = 0, tr_take_notes(k), tr_nn = 0;
        tr_host(k, &t, 2u * 5513u, 512, 1, 120.0, starts[c], 0, 0);
        n = kc_clock_steps(k, tr_st, KC_LOG);
        tr_take_notes(k);
        m = (int)tr_ons(0, on, 4);
        printf("core_transport_mid_bar: from ppq %.2f: step %d kind %02X gate %u at %llu, note-ons %d %d (want %d %d)\n",
               starts[c], n ? tr_st[0].step : -99, n ? tr_st[0].kind : 0, n ? tr_st[0].gate : 0,
               (unsigned long long)(n ? tr_st[0].frame : 0), m > 0 ? on[0] : -1, m > 1 ? on[1] : -1, want[c], want[c] + 1);
        CHECK(n >= 2u && tr_st[0].frame == 2048u && m >= 2 && on[0] == want[c] && on[1] == want[c] + 1);
        if (c == 2) CHECK(n >= 1u && (tr_st[0].kind & CLK_EV_KIND) == CLK_EV_RESUME && tr_st[0].gate == 552u);
        kc_destroy(k);
    }
}

/* a 1-bar loop [0, 4) of 16ths at 120 BPM, host blocks of 500 (the wrap, at 88200, inside a block: its own anchor),
 * a 12-step sequence: each pass plays steps 1..12 then 1..4 (the song's 16ths 0..15 modulo 12): note-ons 40..51,
 * 40..43, the same every pass; 16 steps a pass, one at each wrap (a jump), none doubled or skipped */
static void core_transport_loop(void)
{
    kc_t *k = kc_create();
    uint64_t t = 0;
    uint32_t n, bad = 0, hosted = 0;
    int on[64], m, okn = 1;
    tr_prog(k, 12, 0);
    tr_host(k, &t, 512, 512, 0, 120.0, 0.0, 0, 0);
    kc_seq_play(k, 1);
    kc_clock_steps(k, tr_st, KC_LOG), tr_nn = 0, tr_take_notes(k), tr_nn = 0;
    tr_host(k, &t, 3u * 88200u, 500, 1, 120.0, 0.0, 0.0, 4.0);
    n = kc_clock_steps(k, tr_st, KC_LOG);
    tr_take_notes(k);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t pass = hosted / 16u, j = hosted % 16u;
        if (!(tr_st[i].kind & CLK_EV_HOSTED)) continue;
        bad += tr_st[i].frame != 512u + pass * 88200u + ((uint64_t)j * 11025u + 1u) / 2u || tr_st[i].step != (int32_t)j;
        hosted++;
    }
    m = (int)tr_ons(512, on, 64);
    for (int i = 0; i < m; i++) okn &= on[i] == 40 + (i % 16) % 12;
    printf("core_transport_loop: 3 passes of a 1-bar loop: %u steps (want 48), %u off; %d note-ons, in order: %s\n",
           hosted, bad, m, okn ? "yes" : "no");
    CHECK(hosted == 48u && bad == 0 && m == 48 && okn);
    kc_destroy(k);
}

/* a locate while playing: at frame 512 x 10 to ppq 8.25 (the song's 16th 33 = the sequence's step 2: note 41), then
 * 42 on the grid; backwards to ppq 0.75 (step 4: note 43) */
static void core_transport_locate(void)
{
    kc_t *k = kc_create();
    uint64_t t = 0;
    int on[4], m;
    uint32_t n;
    tr_prog(k, 16, 0);
    tr_host(k, &t, 5120, 512, 1, 120.0, 0.0, 0, 0);
    kc_seq_play(k, 1);
    kc_clock_steps(k, tr_st, KC_LOG), tr_nn = 0, tr_take_notes(k), tr_nn = 0;
    tr_host(k, &t, 5514, 512, 1, 120.0, 8.25, 0, 0);
    n = kc_clock_steps(k, tr_st, KC_LOG);
    tr_take_notes(k);
    m = (int)tr_ons(5120, on, 4);
    printf("core_transport_locate: to ppq 8.25 at 5120: step %d kind %02X, note-ons %d %d (want 41 42)\n",
           n ? tr_st[0].step : -99, n ? tr_st[0].kind : 0, m > 0 ? on[0] : -1, m > 1 ? on[1] : -1);
    CHECK(n == 2u && tr_st[0].frame == 5120u && tr_st[0].step == 33 && (tr_st[0].kind & CLK_EV_KIND) == CLK_EV_JUMP &&
          m == 2 && on[0] == 41 && on[1] == 42);
    tr_nn = 0;
    tr_host(k, &t, 5514, 512, 1, 120.0, 0.75, 0, 0);
    tr_take_notes(k);
    m = (int)tr_ons(0, on, 4);
    printf("core_transport_locate: back to ppq 0.75: note-ons %d %d (want 43 44)\n", m > 0 ? on[0] : -1, m > 1 ? on[1] : -1);
    CHECK(m == 2 && on[0] == 43 && on[1] == 44);
    kc_destroy(k);
}

/* tempo automation 100 -> 140 BPM over 4 s (linear in time; each host block of 256 given its tempo at its start and
 * its exact position): every 16th within 1 frame of the ideal grid */
static double tr_ramp_ppq(double s) { return s < 4.0 ? (100.0 * s + 5.0 * s * s) / 60.0 : (400.0 + 80.0) / 60.0 + 140.0 * (s - 4.0) / 60.0; }
static void core_transport_tempo_ramp(void)
{
    kc_t *k = kc_create();
    uint32_t n, worst = 0, want = 0;
    kc_param_set(k, P_CLK_DIV, CLK_DIV_16TH);
    for (uint64_t S = 0; S < 5u * 44100u; S += 256u) {
        double s = (double)S / 44100.0;
        kc_clock_host(k, 1, s < 4.0 ? 100.0 + 10.0 * s : 140.0, tr_ramp_ppq(s), 0);
        kc_render_exact(k, tr_buf, 256);
    }
    n = kc_clock_steps(k, tr_st, KC_LOG);
    while (tr_ramp_ppq((5.0 * 44100.0 - 1.0) / 44100.0) * 4.0 >= (double)want) want++;
    for (uint32_t i = 0; i < n; i++) {
        double lo = 0.0, hi = 6.0, d;
        for (int it = 0; it < 80; it++) { double mid = (lo + hi) / 2.0; if (tr_ramp_ppq(mid) * 4.0 < (double)i) lo = mid; else hi = mid; }
        d = fabs((double)tr_st[i].frame - hi * 44100.0);
        if (d > worst) worst = (uint32_t)ceil(d);
        if (tr_st[i].step != (int32_t)i) worst = 999;
    }
    printf("core_transport_tempo_ramp: 100 -> 140 BPM: %u 16ths (want %u), worst %u frame(s) from the ideal grid\n", n,
           want, worst);
    CHECK(n == want && worst <= 1u);
    kc_destroy(k);
}

/* P_BPM while the host plays: kept (kc_param_get), not used (the steps on the host's 120); stopped at frame 512 x 12
 * (6144: inside the 16th that started at 5513): a release there, the internal clock at P_BPM 60 from the position
 * (the rest of that 16th, 11025 - 6144 = 4881 frames at 120 = 9762 at 60: the next step at 15906), the sequence on (SEQ PLAY
 * on: the firmware's rule), its steps 11025 frames apart; SEQ PLAY off: no more notes, none left sounding; the arp
 * with a key held, the host stopped: it plays (internal clock at 60) */
static void core_transport_stop_internal(void)
{
    kc_t *k = kc_create();
    uint64_t t = 0;
    uint32_t n, i0;
    int on[8], m;
    tr_prog(k, 16, 0);
    tr_host(k, &t, 2048, 512, 1, 120.0, 0.0, 0, 0);
    kc_seq_play(k, 1);
    kc_param_set(k, P_BPM, 60);
    tr_host(k, &t, 4096, 512, 1, 120.0, 2048.0 / 22050.0, 0, 0);
    n = kc_clock_steps(k, tr_st, KC_LOG);
    printf("bpm_param_greyed (core): P_BPM %d while playing: following %d, steps at %llu %llu (want 0 5513)\n",
           (int)kc_param_get(k, P_BPM), kc_clock_following(k), (unsigned long long)(n > 0 ? tr_st[0].frame : 0),
           (unsigned long long)(n > 1 ? tr_st[1].frame : 0));
    CHECK(kc_param_get(k, P_BPM) == 60 && kc_clock_following(k) && n == 2u && tr_st[1].frame == 5513u);
    tr_nn = 0, tr_take_notes(k), tr_nn = 0;
    tr_host(k, &t, 40000, 512, 0, 120.0, 0.0, 0, 0);
    n = kc_clock_steps(k, tr_st, KC_LOG);
    tr_take_notes(k);
    for (i0 = 0; i0 < n && tr_st[i0].frame < 6144u; i0++) {}
    printf("core_transport_stop_internal: stopped at 6144: following %d, kind %02X at %llu, then steps at %llu %llu "
           "(want 15906 26931)\n", kc_clock_following(k), n > i0 ? tr_st[i0].kind : 0,
           (unsigned long long)(n > i0 ? tr_st[i0].frame : 0), (unsigned long long)(n > i0 + 1 ? tr_st[i0 + 1].frame : 0),
           (unsigned long long)(n > i0 + 2 ? tr_st[i0 + 2].frame : 0));
    CHECK(!kc_clock_following(k) && n > i0 + 2 && tr_st[i0].frame == 6144u && tr_st[i0].kind == CLK_EV_RELEASE &&
          tr_st[i0 + 1].frame == 15906u && tr_st[i0 + 2].frame == 26931u && kc_seq_playing(k));
    m = (int)tr_ons(6144, on, 8);
    CHECK(m >= 2 && on[0] == 42 && on[1] == 43);  /* (the sequence on: its steps 3, 4 on the internal clock) */
    kc_seq_play(k, 0);
    tr_nn = 0;
    tr_host(k, &t, 30000, 512, 0, 120.0, 0.0, 0, 0);
    tr_take_notes(k);
    m = (int)tr_ons(0, on, 8);
    printf("core_transport_stop_internal: SEQ PLAY off: %d note-ons after\n", m);
    CHECK(m == 0);
    {                                             /* the arp, the host stopped: a key held plays it */
        uint8_t non[3] = {0x90, 60, 100};
        kc_param_set(k, P_ARP_ON, 1);
        tr_host(k, &t, 512, 512, 0, 120.0, 0.0, 0, 0);
        kc_midi(k, non, 3);
        tr_nn = 0;
        tr_host(k, &t, 3u * 11025u, 512, 0, 120.0, 0.0, 0, 0);
        tr_take_notes(k);
        m = (int)tr_ons(0, on, 8);
        printf("core_transport_stop_internal: the arp with a key held, host stopped: %d note-ons (%d ..)\n", m, m ? on[0] : -1);
        CHECK(m >= 3 && on[0] == 60);
    }
    kc_destroy(k);
}

/* no note left sounding across the transport: a 12-step sequence with a tie on its last step (its notes held into the
 * next step) in a 1-bar loop (the wrap cuts the tie), locates mid-step (resumed steps), a stop and a restart, then SEQ
 * PLAY off; the arp with a chord held across a locate, a loop and a stop, then the keys released: every note-on has
 * its note-off, nothing sounds at the end (the audio silent) */
static void core_no_stuck_notes(void)
{
    kc_t *k = kc_create();
    uint64_t t = 0;
    int bad, left, quiet = 1;
    tr_prog(k, 12, 1);
    tr_host(k, &t, 512, 512, 0, 120.0, 0.0, 0, 0);
    kc_seq_play(k, 1);
    tr_nn = 0;
    tr_host(k, &t, 2u * 88200u + 1234u, 500, 1, 120.0, 0.0, 0.0, 4.0);   /* (the wraps cut the tie) */
    tr_host(k, &t, 30000, 512, 1, 120.0, 2.8, 0, 0);       /* (a locate inside a step: resumed) */
    tr_host(k, &t, 7000, 512, 0, 120.0, 0.0, 0, 0);        /* (stopped: the internal clock plays on) */
    tr_host(k, &t, 20000, 333, 1, 133.0, 1.13, 0, 0);      /* (playing again inside a step) */
    kc_seq_play(k, 0);
    tr_host(k, &t, 2048, 512, 1, 133.0, 9.0, 0, 0);
    tr_take_notes(k);
    left = tr_sounding(&bad);
    printf("core_no_stuck_notes: the sequencer: %u notes logged, %d sounding at the end, %d unpaired\n", tr_nn, left, bad);
    CHECK(tr_nn > 40u && left == 0 && bad == 0);
    {
        uint8_t m1[3] = {0x90, 60, 100}, m2[3] = {0x90, 64, 100}, m3[3] = {0x90, 67, 100};
        uint8_t o1[3] = {0x80, 60, 0}, o2[3] = {0x80, 64, 0}, o3[3] = {0x80, 67, 0};
        kc_param_set(k, P_ARP_ON, 1);
        kc_param_set(k, P_CLK_DIV, CLK_DIV_32ND);
        kc_midi(k, m1, 3), kc_midi(k, m2, 3), kc_midi(k, m3, 3);
        tr_nn = 0;
        tr_host(k, &t, 50000, 500, 1, 120.0, 0.0, 0.0, 1.0);   /* (a 1-beat loop: wraps) */
        tr_host(k, &t, 9000, 512, 1, 120.0, 7.1, 0, 0);     /* (a locate) */
        tr_host(k, &t, 9000, 512, 0, 120.0, 0.0, 0, 0);     /* (stopped: the arp on the internal clock) */
        tr_host(k, &t, 9000, 512, 1, 120.0, 3.3, 0, 0);
        kc_midi(k, o1, 3), kc_midi(k, o2, 3), kc_midi(k, o3, 3);
        tr_host(k, &t, 4096, 512, 1, 120.0, 5.0, 0, 0);
        tr_take_notes(k);
        left = tr_sounding(&bad);
        printf("core_no_stuck_notes: the arp: %u notes logged, %d sounding after the keys' release, %d unpaired\n", tr_nn,
               left, bad);
        CHECK(tr_nn > 40u && left == 0 && bad == 0);
    }
    tr_host(k, &t, 3u * 44100u, 512, 1, 120.0, 7.0, 0, 0);  /* (the releases' tails) */
    kc_render_exact(k, tr_buf, 2048);
    for (int i = 0; i < 4096; i++) quiet &= tr_buf[i] == 0;
    printf("core_no_stuck_notes: silent at the end: %s\n", quiet ? "yes" : "no");
    CHECK(quiet);
    kc_destroy(k);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
#ifdef KC_TSAN                                  /* (make -C tests tsan: the threads only) */
    two_instances_parallel();
    bank_swap_threads();
    bank_free_rule();
#else
    core_matches_host();
    param_roundtrip();
    param_info_exposed();
    odd_frames();
    program_state();
    two_instances_interleaved();
    two_instances_parallel();
    bank_swap_threads();
    bank_free_rule();
    create_recreate();
    no_mutable_globals();
    param_while_pending();
    bank_validated();
    first_render_no_alloc();
    split_matches_host();
    exact_aligned();
    exact_onset();
    exports_only_kc();
    core_transport_start_bar();
    core_transport_mid_bar();
    core_transport_loop();
    core_transport_locate();
    core_transport_tempo_ramp();
    core_transport_stop_internal();
    core_no_stuck_notes();
#endif
    if (fails) { printf("test_core: %d FAILED\n", fails); return 1; }
    printf("test_core: all passed\n");
    return 0;
}
