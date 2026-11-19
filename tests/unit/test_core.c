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
 * The core is one instance per process until per-instance state (plan Task 2): each scenario runs in a child process. */
#define _DEFAULT_SOURCE 1                       /* (glibc under -std=c11: fork, waitpid, MAP_ANON) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
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

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    core_matches_host();
    param_roundtrip();
    param_info_exposed();
    odd_frames();
    program_state();
    if (fails) { printf("test_core: %d FAILED\n", fails); return 1; }
    printf("test_core: all passed\n");
    return 0;
}
