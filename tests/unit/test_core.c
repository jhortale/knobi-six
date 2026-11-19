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
 *   create_recreate       create / destroy / create any number of times, several alive at once: each a fresh instance
 *   no_mutable_globals    the core's object (../build/unit/knobi_core.o, -O0) has no writable data but the threads'
 *                         instance bindings (kc_tls_*): nm / objdump
 *   param_while_pending   kc_param_set after a kc_program_set the engine could not take yet edits the program set
 *   bank_validated        kc_bank_set refuses a bank with an entry the firmware would not store; kc_bank_in_use
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
    int bad = 0, tls = 0, lines = 0;
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
            char *tab = strchr(line, '\t'), *sec;   /* "addr flags(7) section\tsize name" */
            char flags[8];
            if (!tab || strlen(line) < 26) continue;
            memcpy(flags, line + 17, 7), flags[7] = 0;
            if (strpbrk(flags, "FfdD")) continue;     /* (functions, files, sections, debug: not data) */
            *tab = 0;
            sec = line + 25;
            writable = (!strncmp(sec, ".data", 5) && strncmp(sec, ".data.rel.ro", 12)) || !strncmp(sec, ".bss", 4) ||
                       !strncmp(sec, ".tbss", 5) || !strncmp(sec, ".tdata", 6) || !strcmp(sec, "*COM*");
        }
#endif
        if (!writable) continue;
        if (!strncmp(name, "kc_tls_", 7)) { tls++; continue; }
        bad++;
        if (strlen(bad_names) + strlen(name) + 2 < sizeof bad_names) strcat(bad_names, " "), strcat(bad_names, name);
    }
    pclose(f);
    printf("no_mutable_globals: %s: %d symbols, %d thread bindings (kc_tls_*), %d other writable:%s\n", CORE_OBJ, lines, tls,
           bad, bad_names);
    CHECK(lines > 100 && tls > 0 && bad == 0);
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

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
#ifdef KC_TSAN                                  /* (make -C tests tsan: the threads only) */
    two_instances_parallel();
    bank_swap_threads();
#else
    core_matches_host();
    param_roundtrip();
    param_info_exposed();
    odd_frames();
    program_state();
    two_instances_interleaved();
    two_instances_parallel();
    bank_swap_threads();
    create_recreate();
    no_mutable_globals();
    param_while_pending();
    bank_validated();
#endif
    if (fails) { printf("test_core: %d FAILED\n", fails); return 1; }
    printf("test_core: all passed\n");
    return 0;
}
