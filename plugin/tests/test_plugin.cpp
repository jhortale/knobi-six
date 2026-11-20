// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin tests (plan Task 3), the processor without a host (CTest: one test a name; no name: all):
//   render_44k_bitexact   session 44.1 kHz, host blocks of 64 / 128 / 512 / 1000: the output is the core's stream
//                         delayed by the reported latency (256), sample for sample: events on the 256 grid against
//                         kc_render's stream; events anywhere against the core driven by kc_render_exact split at the
//                         same frames (the reference's own loop, not CoreRunner's); every block size the same output
//   render_48k            48 / 88.2 / 96 kHz: the resampler on sines and a chord (level within 0.1 dB, everything else
//                         -- aliasing, images, the filter's error -- below -80 dB of the signal, the time exact at the
//                         reported latency); the processor = the resampler on the core's stream at that latency, sample
//                         for sample, the same for every host block size, the level within 0.1 dB of the 44.1 kHz
//                         stream, and the latency measured (the best-fitting delay against an independent long-kernel
//                         interpolation of the 44.1 kHz stream) equals the reported one
//   midi_sample_accurate  a note at offset 300 of a 512 block sounds exactly 300 frames after one at offset 0, at
//                         300 + latency (44.1 kHz); at 48 / 96 kHz within the 44.1 kHz grid
//   rt_no_alloc           an allocation + lock counter (alloc_hook.c, the whole process) armed around processBlock:
//                         notes, CCs, bend, pressure, program changes, SysEx, a block longer than prepared, a call
//                         served from another thread (CoreAccess), the host's transport (Task 5: play, a loop, a
//                         locate, a ramp, stop): zero allocations, zero locks
//   seam                  CoreAccess: with no audio running, run() takes the instance itself (real clock); with an
//                         audio thread rendering, run() is served by it between blocks (the seam's clock held: a
//                         FakeClock); never two at once (the processor's too)
// Fix round 1:
//   silent_block          blocks of 2048 at 44.1 kHz, the gaps before them up to just under 3 block periods on the seam's
//                         clock (a FakeClock the test advances: no real sleeps): run() calls are served by the audio
//                         thread (the idle threshold follows the block period: 3 blocks), no block is silent;
//                         then a collision forced (a call running on the asking thread after a real idle while the
//                         host resumes): that block is faded out from the last output (no hard cut), its note-off
//                         and note-on replayed at the start of the next block that enters (60 released, 67 sounds:
//                         a control without the 67 is silent by then), no note stuck, the timeline kept
//   prepare_keeps_pending a note-off still waiting for its frame when the host re-prepares (rate / block change) is
//                         given to the core, not lost: no stuck note
//   events_ordered        parameter changes (paramAt, queued before the block) and MIDI at offsets in between act in
//                         offset order: the same samples as the core driven directly in that order
// Plan Task 4:
//   params_layout         every id the core exposes is a parameter "p<id>" (in id order, nothing else: not the effect
//                         ids, not P_SEQ_GEN), its kind (bool / choice / int), range, default, labels and name the
//                         firmware's, every value converting both ways (a bipolar knob centred at 0.5)
//   automation_to_core    host changes (knob, bipolar, switch, select) before blocks of 300: the core's output equals
//                         the core driven directly with each change at its block's first sample; nothing reported back
//   core_to_host          a CC, a MIDI program change, the host's program list: the host's values follow (after the
//                         message thread's syncHost), with no gesture; the host's echo of an old report after the core
//                         moved on does not pull the core back; no loop; a real host change still applies; names
//   state_roundtrip       program number, program + sequence, edited flag restored exactly (as loaded / edited); the
//                         parameters follow; saved again: the same bytes
//   state_bank_changed    the bank's program changed since the set was saved: the stored copy wins, "differs from bank"
//   state_versioned       a later writer's state (unknown records and flag bits) loads; an older one (a version-1
//                         program record, no sequence, no flags) loads; the empty Task-3 state changes nothing; a newer
//                         format (minReader), a cut or broken state, a bad number: refused, nothing changed
// Plan Task 5 (the clock from the host's transport; a FakeHead as the AudioPlayHead):
//   transport_start_bar   play from bar 1 (44.1 kHz / 512, 48 kHz / 1000): the steps at the internal frame of the
//                         session sample where the host is at each 16th, numbered 0, 1, ...; following
//   transport_aligned     a sequence step at the host's ppq 0.25 (and 4.25) gives exactly the samples of a MIDI note the
//                         host places there (its note-off where the step's gate ends): 44.1 kHz / 512, 48 kHz / 480 and
//                         1000 -- the step sounds where the host's grid is, after the reported latency, as a clip does
//   transport_loop        a 1-bar loop as Live loops it (the wrap inside a block, or at a block's start): 16 steps a
//                         pass, 0..15 every pass on the grid, none doubled or skipped; a 12-step sequence's notes in order
//   transport_locate      a jump while playing (to ppq 8.25: the song's 16th 33 = the sequence's step 1; back to 0.6:
//                         resumed): the matching step at the block's first frame
//   transport_tempo_ramp  100 -> 140 BPM in 4 s (each block's tempo at its start, the exact position): every 16th within
//                         1 internal frame of the ideal grid (48 kHz / 1000, 44.1 kHz / 512)
//   transport_stop_internal  P_BPM automated while playing: reaches the core, not used (greyed); stopped: one release,
//                         the internal clock at the new P_BPM, the sequencer stopped with Live (SEQ PLAY on: armed,
//                         no note), resuming on the song's step at Live's next play
//   transport_loop_tempo  loops at tempos off the core's 1/64 BPM grid and under a ramp, blocks of 2048: no step
//                         doubled, skipped or out of order at the wraps
//   bpm_param_greyed      followingHost(): off stopped, on playing; the host's 150 used, not P_BPM 60
//   rt_params             every parameter set every block (setValue: what the wrappers call), CCs, MIDI program
//                         changes and setCurrentProgram from another thread: no allocation, no lock in processBlock
// Plan Task 6 (the shared library, Library.h; every file in a temporary directory, never ~/Music):
//   library_roundtrip     500 slots (programs + sequences) and the globals: written as a file, read back exactly (and
//                         its bytes encoded again the same); WRITE / clear through the API; a processor attached loads
//                         a slot as the library holds it; the default path is ~/Music/KNOBI-SIX/library.knobi
//   library_atomic        a child process (posix_spawn) killed (SIGKILL) between the temp file's fsync and the rename:
//                         the file is the old one byte for byte, readable; the stale temp file is removed by the next
//                         WRITE
//   library_two_writers   two libraries on one file (as two instances / processes) WRITE different slots at once, 40
//                         rounds, and two processes (posix_spawn) 20 slots each at once: every write lands; the same slot
//                         written by both at once: one of the two (last write wins), and both libraries end with the
//                         file's content after their watchers reload
//   library_corrupt       (fix round 1: a newer format's file refuses a WRITE, untouched; odd SLOT records kept)
//                         a CRC error, a bad magic, a cut file, a newer format: the factory layer, "unreadable", the
//                         file untouched (also by the watcher); a WRITE then keeps it as library.knobi.bad-<time> and
//                         writes a good file; one slot the firmware would not store: that slot factory, counted, the
//                         rest loaded, its bytes kept through a later WRITE of another slot
//   reload_off_audio_thread  an audio thread renders (its own allocations and locks counted) while another library
//                         WRITEs the file: the reload runs on the watcher thread, the audio thread sees the new bank
//                         (a MIDI program change loads the new slot) with no allocation or lock; the old snapshot is
//                         freed by collectBanks once the instance has moved on, never while it may read it
//   library_snapshot_consistent  a MIDI program change still waiting for the engine (kc_program_pending): WRITE and
//                         the set state store number N with program N's content, never the previous program's
//   library_write_restore WRITE from one processor: the file and another processor (another library on the file) have
//                         it; edited cleared; RESTORE FACTORY puts the factory program in (edited) and changes nothing
//                         stored; WRITE then empties the slot; the browser's bank / tens / ones
//   library_globals       a global set in one processor: in the file, in the other processor after its reload; MIDI
//                         channel stays omni; only Library::kGlobals accepted
//   library_write_plays_on  (fix round 1) WRITE without a load: a sequence playing with Live sounds the same, sample
//                         for sample, as without the WRITE; number, edited, differs right after; an edit made while
//                         the file is written stays, the program edited; a MIDI program change or a host program load
//                         while the file is written (fix round 2): not renamed, kStoredOnly
//   library_globals_order (fix round 2) two library snapshots delivered at once, the older one's globals applied
//                         last: dropped (the globals only move forward)
//   library_symlink       (fix round 1) a library path that is a symbolic link stays one (the file it names written,
//                         its lock beside it); listeners are told with the file's lock released
//   factory_gating        the default build: every factory program is the init program, named KNOBI-SIX (bundles
//                         KNOBI-SIX.component / .vst3); KNOBI_FACTORY_LOCAL=ON: factory programs, KNOBI-SIX-local
// Final review fixes:
//   state_timeout_keeps_last  the instance not answering (run() times out: an audio thread stuck mid-block, the
//                         seam's FakeClock): the set state saved is the last good one (saved or loaded), never INIT
//   seam_idle_clamped     the idle time for huge host blocks is at most a quarter of run()'s timeout (a call with no
//                         audio runs, never times out); usual blocks keep 3 block periods / kIdleMs
//   prepare_takes_instance  prepareToPlay waits while another thread's call holds the instance (CoreAccess::take)
//   transport_none        no play head, no position, a position without ppq: the clock is never given a transport;
//                         SEQ PLAY plays on the internal clock (never armed waiting for a play)
// Follow-up (export dropout):
//   transport_vanishes    a host that gave a position while playing stops giving one: the core is told "stopped" once
//                         (notes released, the sequencer armed, not following), then nothing
//   offline_no_silent     an offline render (isNonRealtime) slower than real time, calls arriving past the idle threshold
//                         and another thread holding the instance as blocks come: no call on the asking thread, no
//                         silent block, the output sample for sample an undisturbed render's
//   offline_run_waits     offline, run() never runs on the asking thread: no block, it times out (the set state the last
//                         good one); a block serves it; real time again, the idle rule takes it on the asking thread
// rt_no_alloc (and silent_block, for the silent path and its replay) covers processBlock as this file calls it (the
// processor's own code, CoreRunner, the core, CoreAccess's audio side): allocations (malloc family, typed and zone allocators, operator new) and pthread
// mutexes / rwlocks / os_unfair_lock. Not covered: what the plugin wrappers (JUCE's AU / VST3 glue) do around
// processBlock in a real host, condition variables, semaphores and dispatch calls (not counted).
#include <spawn.h>
extern char** environ;
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <vector>
#include "Processor.h"
#include "Resampler.h"
#include "alloc_hook.h"

using namespace knobi;

static int fails;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// the seam's time in a test (CoreAccess::setClock): it moves only when the test advances it, so "the host has been idle
// for idleMs()" happens exactly when a test says so, never because the machine scheduled a thread late
struct FakeClock final : CoreAccess::Clock {
    std::atomic<int64_t> t{0};
    int64_t nowUs() override { return t.load(std::memory_order_acquire); }
    void pause() override { std::this_thread::sleep_for(std::chrono::microseconds(100)); }
    void advanceMs(int64_t ms) { t.fetch_add(ms * 1000, std::memory_order_acq_rel); }
};

// the host's transport as the processor reads it (getPlayHead()->getPosition()); the test moves it block by block
struct FakeHead final : juce::AudioPlayHead {
    bool playing = false, looping = false;
    double bpm = 120.0, ppq = 0.0, ls = 0.0, le = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setIsPlaying(playing);
        i.setBpm(bpm);
        i.setPpqPosition(ppq);
        i.setIsLooping(looping);
        if (looping) i.setLoopPoints(LoopPoints{ls, le});
        return i;
    }
};
// ---- a script: MIDI messages at absolute session samples
struct Msg { int64_t at; uint8_t len, d[3]; };
static std::vector<Msg> script44()
{
    return {
        {300, 3, {0x90, 60, 100}}, {1000, 3, {0x90, 64, 90}}, {5000, 3, {0xB0, 74, 40}}, {7777, 3, {0xE0, 0, 80}},
        {8000, 2, {0xD0, 90, 0}}, {9001, 2, {0xC0, 3, 0}}, {9002, 3, {0x90, 67, 110}}, {12000, 3, {0x80, 60, 0}},
        {15000, 3, {0xB0, 64, 127}}, {15001, 3, {0x80, 64, 0}}, {20000, 3, {0x90, 72, 80}}, {25000, 3, {0xB0, 64, 0}},
        {26000, 3, {0x80, 67, 0}}, {33333, 3, {0x80, 72, 0}}, {40000, 3, {0x90, 48, 127}}, {52000, 3, {0x80, 48, 0}},
    };
}

// the processor as a host drives it: blocks of `block` samples, the messages at their offsets
static std::vector<float> runProcessor(double rate, int block, const std::vector<Msg>& msgs, int64_t frames,
                                       int* latency = nullptr, Processor* use = nullptr)
{
    std::unique_ptr<Processor> own;
    Processor* p = use;
    if (!p) own = std::make_unique<Processor>(), p = own.get();
    p->setPlayConfigDetails(0, 2, rate, block);
    p->prepareToPlay(rate, block);
    if (latency) *latency = p->getLatencySamples();
    std::vector<float> out((size_t)frames * 2u, 0.0f);
    juce::AudioBuffer<float> buf(2, block);
    juce::MidiBuffer midi;
    midi.ensureSize(4096);
    size_t m = 0;
    for (int64_t s = 0; s < frames; s += block) {
        int n = (int)std::min<int64_t>(block, frames - s);
        buf.setSize(2, n, false, false, true);
        midi.clear();
        for (; m < msgs.size() && msgs[m].at < s + n; m++) midi.addEvent(msgs[m].d, msgs[m].len, (int)(msgs[m].at - s));
        p->processBlock(buf, midi);
        for (int i = 0; i < n; i++) out[2 * (s + i)] = buf.getSample(0, i), out[2 * (s + i) + 1] = buf.getSample(1, i);
    }
    return out;
}

// the reference: a fresh core played directly, at 44.1 kHz, the firmware's blocks split at each message's frame
// (internal frame ceil(at x 44100 / rate)); exact = false: kc_render (each message before the block it falls in)
static std::vector<float> coreReference(const std::vector<Msg>& msgs, double rate, int64_t frames, bool exact = true)
{
    kc_t* k = kc_create();
    std::vector<int16_t> x((size_t)frames * 2u, 0);
    std::vector<int64_t> at;
    for (const Msg& m : msgs) at.push_back(((int64_t)m.at * 44100 + (int64_t)rate - 1) / (int64_t)rate);
    size_t e = 0;
    int64_t done = 0;
    while (done < frames) {
        int64_t end = (done / KC_BLOCK + 1) * KC_BLOCK;
        if (exact) {
            for (; e < msgs.size() && at[e] <= done; e++) kc_midi(k, msgs[e].d, msgs[e].len);
            if (e < msgs.size() && at[e] < end) end = at[e];
        } else
            for (; e < msgs.size() && at[e] < end; e++) kc_midi(k, msgs[e].d, msgs[e].len);
        if (end > frames) end = frames;
        if (exact) kc_render_exact(k, &x[(size_t)done * 2u], (uint32_t)(end - done));
        else kc_render(k, &x[(size_t)done * 2u], (uint32_t)(end - done));
        done = end;
    }
    kc_destroy(k);
    std::vector<float> f(x.size());
    for (size_t i = 0; i < x.size(); i++) f[i] = (float)x[i] * (1.0f / 32768.0f);
    return f;
}

static double rms(const float* x, size_t n, size_t stride = 1)
{
    double s = 0;
    for (size_t i = 0; i < n; i++) s += (double)x[i * stride] * x[i * stride];
    return std::sqrt(s / (double)(n ? n : 1));
}
static double dB(double r) { return 20.0 * std::log10(r); }

// ---------------------------------------------------------------------------------------------------------------
static void render_44k_bitexact()
{
    const int64_t frames = 60000;
    auto msgs = script44();
    auto ref = coreReference(msgs, 44100, frames);
    std::vector<Msg> grid = msgs;                   // the same on the 256 grid: kc_render's stream itself
    for (Msg& m : grid) m.at = (m.at + KC_BLOCK - 1) / KC_BLOCK * KC_BLOCK;
    auto refGrid = coreReference(grid, 44100, frames, false);
    double peak = 0;
    for (float v : ref) peak = std::max(peak, (double)std::fabs(v));
    std::vector<float> first;
    for (int block : {64, 128, 512, 1000}) {
        int lat = -1, latG = -1;
        auto out = runProcessor(44100, block, msgs, frames, &lat);
        auto outG = runProcessor(44100, block, grid, frames, &latG);
        long diff = 0, diffG = 0, same = 0;
        for (int64_t s = 0; s < frames; s++)
            for (int c = 0; c < 2; c++) {
                float want = s >= lat ? ref[(size_t)(2 * (s - lat) + c)] : 0.0f;
                float wantG = s >= latG ? refGrid[(size_t)(2 * (s - latG) + c)] : 0.0f;
                diff += out[(size_t)(2 * s + c)] != want;
                diffG += outG[(size_t)(2 * s + c)] != wantG;
            }
        if (first.empty()) first = out;
        else for (size_t i = 0; i < out.size(); i++) same += out[i] != first[i];
        std::printf("render_44k_bitexact: blocks of %4d: latency %d; vs the core split at the events %ld samples differ, "
                    "on the grid vs kc_render %ld; vs blocks of 64 %ld\n", block, lat, diff, diffG, same);
        CHECK(lat == 256 && latG == 256 && diff == 0 && diffG == 0 && same == 0);
    }
    std::printf("render_44k_bitexact: %lld frames, %zu messages, reference peak %.3f\n", (long long)frames, msgs.size(), peak);
    CHECK(peak > 0.05);
}

// ---------------------------------------------------------------------------------------------------------------
// the resampler at rate on sum of sines (Hz, amplitude): level and error against the exact sines at the latency's time
static void sineCheck(uint32_t rate, const std::vector<std::pair<double, double>>& tones, const char* what)
{
    Resampler rs;
    CHECK(rs.prepare(44100, rate));
    const uint32_t size = 1u << 16, mask = size - 1u;
    std::vector<float> ring(size);
    for (uint32_t j = 0; j < size; j++) {
        double v = 0;
        for (auto& t : tones) v += t.second * std::sin(2.0 * M_PI * t.first * j / 44100.0);
        ring[j] = (float)v;
    }
    double sig = 0, err = 0, got = 0;
    int n = 0;
    for (int64_t m = 2000; m < (int64_t)rate; m++) {   // (well inside the ring: no edge)
        int64_t q;
        uint32_t p;
        rs.timeOf(m, q, p);
        if (q + Resampler::kHalf >= (int64_t)size) break;
        double y = rs.at(ring.data(), mask, q, p), want = 0;
        for (auto& t : tones) want += t.second * std::sin(2.0 * M_PI * t.first * (double)m / (double)rate);
        sig += want * want, got += y * y, err += (y - want) * (y - want);
        n++;
    }
    double level = dB(std::sqrt(got / sig)), rest = dB(std::sqrt(err / sig));
    std::printf("render_48k: %6u Hz %-28s level %+.4f dB, error (aliasing, images, filter) %.1f dB, %d samples\n", rate,
                what, level, rest, n);
    CHECK(std::fabs(level) < 0.1 && rest < -80.0 && n > 20000);
}

// the 44.1 kHz stream x (interleaved) at time t (frames) by a long windowed sinc in double (independent of Resampler)
static double ideal(const std::vector<float>& x, int c, double t)
{
    const int half = 256;
    const double fc = 0.5 * 0.93, beta = 12.0;
    auto i0 = [](double v) { double s = 1, term = 1, y = v * v / 4; for (int k = 1; k < 80; k++) { term *= y / (k * k); s += term; } return s; };
    static const double i0b = i0(beta);
    int64_t j0 = (int64_t)std::floor(t);
    double acc = 0, wsum = 0;
    for (int64_t j = j0 - half + 1; j <= j0 + half; j++) {
        double d = t - (double)j, u = d / half;
        if (u * u >= 1) continue;
        double h = (d == 0 ? 2 * fc : std::sin(2 * M_PI * fc * d) / (M_PI * d)) * i0(beta * std::sqrt(1 - u * u)) / i0b;
        wsum += h;
        if (j >= 0 && (size_t)(2 * j + c) < x.size()) acc += h * x[(size_t)(2 * j + c)];
    }
    return acc / wsum;
}

static void render_48k()
{
    for (uint32_t rate : {48000u, 88200u, 96000u}) {
        sineCheck(rate, {{1000.0, 0.5}}, "sine 1 kHz");
        sineCheck(rate, {{15000.0, 0.5}}, "sine 15 kHz");
        sineCheck(rate, {{18000.0, 0.5}}, "sine 18 kHz (image near 26 kHz)");
        sineCheck(rate, {{220.0, 0.2}, {277.18, 0.2}, {329.63, 0.2}, {8000.0, 0.1}}, "chord (A3 C#4 E4 + 8 kHz)");
    }
    // the processor end to end: a chord from MIDI
    std::vector<Msg> chord = {{100, 3, {0x90, 57, 100}}, {101, 3, {0x90, 61, 100}}, {102, 3, {0x90, 64, 100}},
                              {30000, 3, {0x80, 57, 0}}, {30000, 3, {0x80, 61, 0}}, {30000, 3, {0x80, 64, 0}}};
    for (uint32_t rate : {48000u, 88200u, 96000u}) {
        const int64_t frames = (int64_t)rate * 7 / 10;
        int lat = -1;
        auto out = runProcessor(rate, 512, chord, frames, &lat);
        auto x = coreReference(chord, rate, frames * 44100 / rate + 2048);
        // the same resampler on the reference at the reported latency: sample for sample
        Resampler rs;
        rs.prepare(44100, rate);
        uint32_t size = 1;
        while (size < x.size() / 2) size <<= 1;
        std::vector<float> rl(size, 0.0f), rr(size, 0.0f);
        for (size_t j = 0; j < x.size() / 2 && j < size; j++) rl[j] = x[2 * j], rr[j] = x[2 * j + 1];
        long diff = 0;
        for (int64_t s = 0; s < frames; s++) {
            float wl = 0, wr = 0;
            if (s >= lat) {
                int64_t q;
                uint32_t p;
                rs.timeOf(s - lat, q, p);
                wl = rs.at(rl.data(), size - 1, q, p), wr = rs.at(rr.data(), size - 1, q, p);
            }
            diff += out[(size_t)(2 * s)] != wl, diff += out[(size_t)(2 * s + 1)] != wr;
        }
        long blocks = 0;
        for (int block : {64, 128, 1000}) {
            auto o = runProcessor(rate, block, chord, frames);
            for (size_t i = 0; i < o.size(); i++) blocks += o[i] != out[i];
        }
        // level: the 44.1 kHz stream's RMS vs the output's, over the same stretch of sound
        int64_t a44 = 2000, b44 = 28000;
        double l44 = rms(&x[(size_t)(2 * a44)], (size_t)(b44 - a44), 2);
        int64_t a = a44 * rate / 44100 + lat, b = b44 * rate / 44100 + lat;
        double lout = rms(&out[(size_t)(2 * a)], (size_t)(b - a), 2), level = dB(lout / l44);
        // the latency measured: the delay (output samples, 0.01 steps) where the output best fits the stream
        double best = 1e30, bestD = -1;
        int64_t m0 = 4000 * (int64_t)rate / 44100 + lat, m1 = m0 + 3000;
        for (double d = lat - 3.0; d <= lat + 3.0 + 1e-9; d += 0.01) {
            double e = 0;
            for (int64_t s = m0; s < m1; s += 3) {
                double y = ideal(x, 0, ((double)s - d) * 44100.0 / rate) - out[(size_t)(2 * s)];
                e += y * y;
            }
            if (e < best) best = e, bestD = d;
        }
        double sig = 0;
        for (int64_t s = m0; s < m1; s += 3) sig += (double)out[(size_t)(2 * s)] * out[(size_t)(2 * s)];
        std::printf("render_48k: %6u Hz processor: latency %d (%.2f ms), measured %.2f (fit %.1f dB); vs the resampler "
                    "on the core's stream %ld differ; other block sizes %ld differ; level %+.4f dB vs 44.1 kHz\n",
                    rate, lat, 1000.0 * lat / rate, bestD, dB(std::sqrt(best / sig)), diff, blocks, level);
        CHECK(diff == 0 && blocks == 0 && std::fabs(level) < 0.1 && std::fabs(bestD - lat) <= 0.02 && l44 > 0.01);
    }
}

// ---------------------------------------------------------------------------------------------------------------
static int64_t onset(const std::vector<float>& y, float thr = 0.0f)
{
    for (size_t i = 0; i < y.size() / 2; i++)
        if (std::fabs(y[2 * i]) > thr || std::fabs(y[2 * i + 1]) > thr) return (int64_t)i;
    return -1;
}
static void midi_sample_accurate()
{
    const int64_t frames = 4096;
    std::vector<Msg> at0 = {{0, 3, {0x90, 60, 100}}}, at300 = {{300, 3, {0x90, 60, 100}}};
    int lat = -1;
    auto y0 = runProcessor(44100, 512, at0, frames, &lat), y300 = runProcessor(44100, 512, at300, frames);
    auto ref = coreReference(at0, 44100, frames);
    int64_t o0 = onset(y0), o300 = onset(y300), oref = onset(ref);
    std::printf("midi_sample_accurate: 44.1 kHz, blocks of 512, latency %d: the core's note from frame %lld; "
                "offset 0 sounds at %lld, offset 300 at %lld (difference %lld)\n", lat, (long long)oref,
                (long long)o0, (long long)o300, (long long)(o300 - o0));
    CHECK(oref >= 0 && o0 == oref + lat && o300 == o0 + 300);
    for (uint32_t rate : {48000u, 96000u}) {      // (other rates: the 44.1 kHz grid; first sample over -60 dBFS)
        int64_t s1 = 300, s2 = 1300;
        std::vector<Msg> a = {{s1, 3, {0x90, 60, 100}}}, b = {{s2, 3, {0x90, 60, 100}}};
        auto ya = runProcessor(rate, 512, a, 8192), yb = runProcessor(rate, 512, b, 8192);
        int64_t oa = onset(ya, 1e-3f), ob = onset(yb, 1e-3f);
        double grid = (double)rate / 44100.0;
        std::printf("midi_sample_accurate: %u Hz: offsets %lld and %lld sound %lld samples apart (the 44.1 kHz grid: "
                    "%.2f)\n", rate, (long long)s1, (long long)s2, (long long)(ob - oa), grid);
        CHECK(oa > 0 && std::fabs((double)(ob - oa) - (double)(s2 - s1)) <= grid + 1.0);
    }
}

// ---------------------------------------------------------------------------------------------------------------
static void rt_no_alloc()
{
    // the counter itself: it sees malloc and operator new on this thread, and a mutex
    alloc_hook_arm(1);
    long a0 = alloc_hook_allocs(), l0 = alloc_hook_locks();
    void* volatile pm = std::malloc(16);
    int* volatile pn = new int(3);
    { std::mutex mx; std::lock_guard<std::mutex> g(mx); }
    long seenA = alloc_hook_allocs() - a0, seenL = alloc_hook_locks() - l0;
    alloc_hook_arm(0);
    std::free(pm);
    delete pn;
    std::printf("rt_no_alloc: the counter sees malloc + new: %ld, a mutex: %ld\n", seenA, seenL);
    CHECK(seenA >= 2 && seenL >= 1);

    for (double rate : {44100.0, 48000.0, 96000.0}) {
        Processor p;
        FakeClock clock;                          // (the seam's idle rule: never met here, whatever the scheduling)
        p.access().setClock(&clock);
        const int block = 256;
        p.setPlayConfigDetails(0, 2, rate, block);
        p.prepareToPlay(rate, block);
        FakeHead head;                            // the host's transport (plan Task 5): plays from block 50, a 1-bar
        p.setPlayHead(&head);                     // loop, a locate at 150, a tempo ramp, stopped from 300
        juce::AudioBuffer<float> buf(2, block * 3);   // (one block longer than prepared: taken in parts)
        juce::MidiBuffer midi;
        midi.ensureSize(65536);
        std::vector<juce::MidiBuffer> blocks(400);
        for (int b = 0; b < 400; b++) {               // notes, CCs, bend, pressure, program changes, SysEx
            blocks[(size_t)b].ensureSize(4096);
            int note = 48 + (b * 7) % 36;
            blocks[(size_t)b].addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)(40 + b % 80)), b % block);
            if (b % 3 == 0) blocks[(size_t)b].addEvent(juce::MidiMessage::noteOff(1, 48 + ((b - 3) * 7) % 36), 17);
            blocks[(size_t)b].addEvent(juce::MidiMessage::controllerEvent(1, 74, b % 128), 5);
            blocks[(size_t)b].addEvent(juce::MidiMessage::controllerEvent(1, 1, (b * 3) % 128), 99);
            blocks[(size_t)b].addEvent(juce::MidiMessage::pitchWheel(1, (b * 97) % 16384), 120);
            blocks[(size_t)b].addEvent(juce::MidiMessage::channelPressureChange(1, b % 128), 130);
            if (b % 40 == 7) {
                blocks[(size_t)b].addEvent(juce::MidiMessage::controllerEvent(1, 32, 0), 200);
                blocks[(size_t)b].addEvent(juce::MidiMessage::programChange(1, b % 100), 201);
            }
            if (b % 50 == 9) blocks[(size_t)b].addEvent(juce::MidiMessage::controllerEvent(1, 64, b % 100 < 50 ? 127 : 0), 3);
            if (b % 25 == 0) {
                const juce::uint8 sx[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
                blocks[(size_t)b].addEvent(sx, 6, 50);
            }
        }
        // a call posted from another thread, served by the audio thread (CoreAccess): copies only
        struct Snap { kc_t* k; program_t prog; seq_t seq; int32_t num; int edited; } snap{p.runner().core(), {}, {}, 0, 0};
        auto fn = [](void* c) { auto* s = (Snap*)c; kc_program_get(s->k, &s->num, &s->prog, &s->seq, &s->edited); };
        std::atomic<bool> served{false}, release{false};
        std::thread asker;
        long allocs = 0, locks = 0;
        for (int b = 0; b < 400; b++) {
            midi.clear();
            midi.addEvents(blocks[(size_t)b], 0, -1, 0);   // (outside the counted part)
            int n = b == 200 ? block * 3 : block;
            buf.setSize(2, n, false, false, true);
            head.playing = b >= 50 && b < 300, head.looping = true, head.ls = 0.0, head.le = 4.0;
            head.bpm = 100.0 + (b % 100) * 0.4;
            if (b == 150) head.ppq = 2.37;
            if (b == 100) {                           // (the asker's thread made, and its exit, outside the counted part)
                asker = std::thread([&] {
                    served = p.access().run(fn, &snap);
                    while (!release) std::this_thread::yield();
                });
                while (!p.access().posted()) std::this_thread::yield();
            }
            long a = alloc_hook_allocs(), l = alloc_hook_locks();
            alloc_hook_arm(1);
            p.processBlock(buf, midi);
            alloc_hook_arm(0);
            allocs += alloc_hook_allocs() - a, locks += alloc_hook_locks() - l;
            if (b == 100) { release = true; asker.join(); }
            if (head.playing) {
                head.ppq += n * head.bpm / 60.0 / rate;
                if (head.ppq >= head.le) head.ppq -= head.le - head.ls;
            }
        }
        std::printf("rt_no_alloc: %.0f Hz: 400 blocks (notes, CCs, bend, pressure, 10 program changes, SysEx, one block "
                    "3x the prepared size, a served call %s): %ld allocations, %ld locks in processBlock\n", rate,
                    served ? "ok" : "NOT served", allocs, locks);
        CHECK(allocs == 0 && locks == 0 && served && p.access().ranHere() == 0);   // (served by the audio thread)
    }
}

// ---------------------------------------------------------------------------------------------------------------
static void seam()
{
    CoreAccess acc;
    std::atomic<int> inside{0}, overlaps{0}, ran{0};
    struct Ctx { std::atomic<int>* inside; std::atomic<int>* overlaps; std::atomic<int>* ran; std::thread::id tid; } ctx{&inside, &overlaps, &ran, {}};
    auto fn = [](void* c) {
        auto* x = (Ctx*)c;
        if (x->inside->fetch_add(1) != 0) x->overlaps->fetch_add(1);
        x->tid = std::this_thread::get_id();
        x->ran->fetch_add(1);
        x->inside->fetch_sub(1);
    };
    // no audio: the asking thread runs it
    bool ok = acc.run(fn, &ctx);
    bool here = ctx.tid == std::this_thread::get_id();
    std::printf("seam: no audio thread: run() %s, on the asking thread: %s\n", ok ? "ok" : "FAILED", here ? "yes" : "no");
    CHECK(ok && here && acc.ranHere() == 1);
    // an audio thread rendering: served by it, never at the same time as its block. The seam's clock is held (a
    // FakeClock never advanced): the host is never idle by its measure, however late the machine runs the threads
    FakeClock held;
    acc.setClock(&held);
    std::atomic<bool> stop{false};
    std::thread::id audioId;
    std::thread audio([&] {
        audioId = std::this_thread::get_id();
        while (!stop) {
            if (acc.audioEnter()) {
                acc.audioServe();
                if (inside.fetch_add(1) != 0) overlaps++;
                std::this_thread::sleep_for(std::chrono::microseconds(300));   // (a block's work)
                inside.fetch_sub(1);
                acc.audioExit();
            }
            std::this_thread::sleep_for(std::chrono::microseconds(700));
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    int byAudio = 0, n = 50;
    for (int i = 0; i < n; i++) {
        ok = acc.run(fn, &ctx) && ok;
        byAudio += ctx.tid == audioId;
    }
    stop = true;
    audio.join();
    // and the processor: run() while another thread calls processBlock, the instance read meanwhile
    Processor p;
    p.access().setClock(&held);
    p.setPlayConfigDetails(0, 2, 44100, 256);
    p.prepareToPlay(44100, 256);
    std::atomic<bool> stop2{false};
    std::thread host([&] {
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi;
        int b = 0;
        while (!stop2) {
            midi.clear();
            if (b++ % 20 == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60 + b % 12, (juce::uint8)100), 10);
            p.processBlock(buf, midi);
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });
    struct Snap { kc_t* k; int32_t num; int edited; } snap{p.runner().core(), -1, -1};
    int got = 0;
    for (int i = 0; i < 20; i++)
        got += p.access().run([](void* c) { auto* s = (Snap*)c; kc_program_get(s->k, &s->num, nullptr, nullptr, &s->edited); }, &snap);
    stop2 = true;
    host.join();
    std::printf("seam: audio thread running: %d / %d calls served by it, overlaps %d; processor: %d / 20 calls served "
                "(program %d)\n", byAudio, n, overlaps.load(), got, snap.num);
    CHECK(ok && byAudio == n && overlaps == 0 && got == 20 && snap.num == 0);
}

// ---------------------------------------------------------------------------------------------------------------
static bool silentTail(const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < b.getNumSamples(); i++)
            if (b.getSample(c, i) != 0.0f) return false;
    return true;
}
static void silent_block()
{
    const int block = 2048;
    const double rate = 44100;
    const auto period = std::chrono::microseconds((int64_t)(1e6 * block / rate));   // 46.4 ms
    Processor p;
    p.setPlayConfigDetails(0, 2, rate, block);
    p.prepareToPlay(rate, block);
    juce::AudioBuffer<float> buf(2, block);
    juce::MidiBuffer midi;
    midi.ensureSize(1024);
    int64_t hostSamples = 0;
    auto host = [&](std::initializer_list<std::pair<int, juce::MidiMessage>> evs) {
        midi.clear();
        for (auto& e : evs) midi.addEvent(e.second, e.first);
        p.processBlock(buf, midi);
        hostSamples += block;
    };
    // 1. a host with long blocks; calls from another thread meanwhile, each answered by the next block after a gap
    //    of host time (the seam's clock, advanced by the test) up to just under the idle threshold (3 blocks: 140 ms):
    //    all served by the audio thread, none taken by the asking thread, no block silent
    FakeClock clock;
    p.access().setClock(&clock);
    const int idle = p.access().idleMs();
    const int64_t gaps[6] = {period.count() / 1000, period.count() / 1000, 2 * period.count() / 1000, 100, idle - 1,
                             idle - 1};
    std::atomic<bool> stop{false};
    std::atomic<int> served{0};
    struct Snap { kc_t* k; int32_t num; } snap{p.runner().core(), -1};
    auto get = [](void* c) { auto* s = (Snap*)c; kc_program_get(s->k, &s->num, nullptr, nullptr, nullptr); };
    std::thread asker([&] { for (int i = 0; i < 6; i++) served += p.access().run(get, &snap); stop = true; });
    for (int i = 0; i < 6; i++) {
        while (!p.access().posted()) std::this_thread::yield();   // (the call waits)
        clock.advanceMs(gaps[i]);                 // the host's gap: no block for that long
        host({});
    }
    asker.join();
    uint64_t here1 = p.access().ranHere(), silent1 = p.silentBlocks();
    std::printf("silent_block: blocks of %d at %.0f Hz, idle threshold %d ms; 6 calls each answered by the next block "
                "after %lld / %lld / %lld / %lld / %lld / %lld ms of the seam's clock: %d / 6 served, %llu run on the "
                "asking thread, %llu silent blocks\n", block, rate, idle, (long long)gaps[0], (long long)gaps[1],
                (long long)gaps[2], (long long)gaps[3], (long long)gaps[4], (long long)gaps[5], served.load(),
                (unsigned long long)here1, (unsigned long long)silent1);
    CHECK(idle == 140 && served == 6 && here1 == 0 && silent1 == 0 && stop);
    // 2. a collision: the host idle, a call taken by the asking thread (it holds the instance), the host resumes
    host({{10, juce::MidiMessage::noteOn(1, 60, (juce::uint8)110)}});   // a held note
    for (int i = 0; i < 4; i++) host({});
    float last = buf.getSample(0, block - 1);
    std::atomic<bool> holding{false}, release{false};
    struct Hold { std::atomic<bool>* holding; std::atomic<bool>* release; } h{&holding, &release};
    std::thread taker([&] {
        p.access().run([](void* c) {
            auto* x = (Hold*)c;
            *x->holding = true;
            while (!*x->release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }, &h);
    });
    while (!p.access().posted()) std::this_thread::yield();
    clock.advanceMs(idle);                        // the host idle for the threshold: the asking thread takes the call
    while (!holding) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const uint64_t taken = p.access().ranHere() + 1;   // (counted when it returns: after release)
    midi.clear();                                 // (the colliding block, counted: the silent path allocates nothing)
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 100);
    midi.addEvent(juce::MidiMessage::noteOn(1, 67, (juce::uint8)100), 200);
    long a0 = alloc_hook_allocs(), l0 = alloc_hook_locks();
    alloc_hook_arm(1);
    p.processBlock(buf, midi);
    alloc_hook_arm(0);
    long silentAllocs = alloc_hook_allocs() - a0, silentLocks = alloc_hook_locks() - l0;
    hostSamples += block;
    float first = buf.getSample(0, 0), jump = std::fabs(first - last);   // (collides: the call still holds it)
    bool tailSilent = true;
    for (int i = 64; i < block; i++) tailSilent = tailSilent && buf.getSample(0, i) == 0.0f && buf.getSample(1, i) == 0.0f;
    uint64_t silent2 = p.silentBlocks();
    release = true;
    taker.join();
    const bool takenHere = p.access().ranHere() == taken;
    midi.clear();                                 // (the block that replays it, counted too)
    a0 = alloc_hook_allocs(), l0 = alloc_hook_locks();
    alloc_hook_arm(1);
    p.processBlock(buf, midi);
    alloc_hook_arm(0);
    silentAllocs += alloc_hook_allocs() - a0, silentLocks += alloc_hook_locks() - l0;
    hostSamples += block;
    // after it: 60 released, 67 held (replayed). A control without the 67: 60's release alone is over by block 20
    for (int i = 0; i < 19; i++) host({});
    bool heldAt20 = !silentTail(buf);
    Processor ctl;
    ctl.setPlayConfigDetails(0, 2, rate, block);
    ctl.prepareToPlay(rate, block);
    juce::AudioBuffer<float> cb(2, block);
    for (int i = 0; i < 26; i++) {
        midi.clear();
        if (i == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 10);
        if (i == 5) midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        ctl.processBlock(cb, midi);
    }
    bool controlSilent = silentTail(cb);
    host({{0, juce::MidiMessage::noteOff(1, 67)}});
    for (int i = 0; i < 70; i++) host({});        // (3.25 s)
    bool endsSilent = silentTail(buf);
    int64_t session = p.runner().sessionSamples();
    std::printf("silent_block: collision: %llu silent block(s); its first sample %.4f after %.4f (step %.4f), silent "
                "after 64: %s; 20 blocks later the replayed note sounds: %s (control without it silent: %s); after its "
                "note-off the last block silent: %s; timeline %lld of %lld host samples; the silent block and the replay: %ld "
                "allocations, %ld locks\n",
                (unsigned long long)(silent2 - silent1), first, last, jump, tailSilent ? "yes" : "no",
                heldAt20 ? "yes" : "NO", controlSilent ? "yes" : "no", endsSilent ? "yes" : "NO (a stuck note)",
                (long long)session, (long long)hostSamples, silentAllocs, silentLocks);
    std::printf("silent_block: the call taken by the asking thread after %d ms of the seam's clock with no block: %s\n",
                idle, takenHere ? "yes" : "NO");
    CHECK(takenHere && silentAllocs == 0 && silentLocks == 0 && silent2 - silent1 == 1 && std::fabs(last) > 1e-4f && jump < std::fabs(last) * 0.1f + 1e-4f && tailSilent &&
          heldAt20 && controlSilent && endsSilent && session == hostSamples);
}

static void prepare_keeps_pending()
{
    Processor p;
    p.setPlayConfigDetails(0, 2, 44100, 100);
    p.prepareToPlay(44100, 100);
    juce::AudioBuffer<float> buf(2, 100);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 0);
    p.processBlock(buf, midi);
    for (int i = 0; i < 20; i++) { midi.clear(); p.processBlock(buf, midi); }   // (2000 samples: sounding)
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 90);   // (frame 2090: past the core's frontier, 2048: waiting)
    p.processBlock(buf, midi);
    p.setPlayConfigDetails(0, 2, 48000, 512);
    p.prepareToPlay(48000, 512);                  // (the host changes rate and block size)
    juce::AudioBuffer<float> b2(2, 512);
    for (int i = 0; i < 300; i++) { midi.clear(); p.processBlock(b2, midi); }   // (3.2 s)
    bool silent = silentTail(b2);
    std::printf("prepare_keeps_pending: a note-off waiting at re-prepare (44.1 kHz / 100 -> 48 kHz / 512): the last "
                "block %s\n", silent ? "silent" : "NOT silent (a stuck note)");
    CHECK(silent);
}

static void events_ordered()
{
    uint16_t id = 0;                              // a knob: the filter's cut-off if it is there, else the first knob
    for (uint16_t i = 0; i < KC_PARAM_ID_LIMIT; i++) {
        kc_param_info_t in;
        if (kc_param_info(i, &in) || in.kind != KC_KIND_KNOB) continue;
        if (!id) id = i;
        if (std::strstr(in.name, "FREQ")) { id = i; break; }
    }
    kc_param_info_t info;
    kc_param_info(id, &info);
    const int32_t lo = info.min + (info.max - info.min) / 8, hi = info.max - (info.max - info.min) / 8;
    // one host block of 2048: params at 900 and 1500 queued first, MIDI at 100, 1200, 1800
    Processor p;
    p.setPlayConfigDetails(0, 2, 44100, 2048);
    p.prepareToPlay(44100, 2048);
    juce::AudioBuffer<float> buf(2, 2048);
    juce::MidiBuffer midi;
    std::vector<float> out;
    for (int b = 0; b < 4; b++) {
        midi.clear();
        if (b == 0) {
            p.runner().paramAt(900, id, lo);
            p.runner().paramAt(1500, id, hi);
            midi.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8)110), 100);
            midi.addEvent(juce::MidiMessage::noteOn(1, 55, (juce::uint8)110), 1200);
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 1800);
        }
        p.processBlock(buf, midi);
        for (int i = 0; i < 2048; i++) out.push_back(buf.getSample(0, i)), out.push_back(buf.getSample(1, i));
    }
    // the reference: the core directly, in offset order, split at each
    kc_t* k = kc_create();
    struct E { int64_t at; int kind; uint8_t m[3]; int32_t v; };
    std::vector<E> ev = {{100, 0, {0x90, 48, 110}, 0}, {900, 1, {0, 0, 0}, lo}, {1200, 0, {0x90, 55, 110}, 0},
                         {1500, 1, {0, 0, 0}, hi}, {1800, 0, {0x90, 60, 110}, 0}};
    std::vector<int16_t> x(2 * 8192, 0);
    int64_t done = 0;
    size_t e = 0;
    while (done < 8192) {
        int64_t end = (done / KC_BLOCK + 1) * KC_BLOCK;
        for (; e < ev.size() && ev[e].at <= done; e++)
            if (ev[e].kind == 0) kc_midi(k, ev[e].m, 3); else kc_param_set(k, id, ev[e].v);
        if (e < ev.size() && ev[e].at < end) end = ev[e].at;
        kc_render_exact(k, &x[(size_t)(2 * done)], (uint32_t)(end - done));
        done = end;
    }
    kc_destroy(k);
    long diff = 0;
    for (int64_t s = 0; s < 8192; s++)
        for (int c = 0; c < 2; c++) {
            float want = s >= 256 ? (float)x[(size_t)(2 * (s - 256) + c)] * (1.0f / 32768.0f) : 0.0f;
            diff += out[(size_t)(2 * s + c)] != want;
        }
    std::printf("events_ordered: %s (id %u) at 900 and 1500 queued before MIDI at 100, 1200, 1800: %ld samples differ "
                "from the core in offset order\n", info.name, id, diff);
    CHECK(diff == 0);
}

// ===============================================================================================================
// plan Task 4: parameters, automation, set state
// ===============================================================================================================
static const juce::RangedAudioParameter* paramById(juce::AudioProcessor& p, const juce::String& pid)
{
    for (auto* q : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(q))
            if (r->getParameterID() == pid) return r;
    return nullptr;
}

// a program unlike init (values per a seed, a name) and a sequence of a few steps, as the firmware stores them
static void makeProgram(int seed, program_t& p, seq_t& s, const char* name)
{
    program_init(&p);
    std::memset(p.name, 0, sizeof p.name);
    std::strncpy(p.name, name, sizeof p.name);
    for (uint16_t id = 0; id < KC_PARAM_ID_LIMIT; id++) {
        kc_param_info_t in;
        if (kc_param_info(id, &in) || id == P_ARP_ON || id == P_PRG_VOL || id == P_LENV_S) continue;   // (it plays)
        const int32_t span = in.max - in.min + 1;
        p.v[id] = param_clamp(id, in.min + (int32_t)((uint32_t)(seed * 131 + id * 17) % (uint32_t)span));
    }
    seq_clear(&s);
    const uint8_t a[2] = {(uint8_t)(48 + seed % 12), (uint8_t)(55 + seed % 12)}, va[2] = {100, (uint8_t)(60 + seed)};
    seq_rec_step(&s, a, va, 2);
    seq_rec_tie(&s);
    seq_rec_rest(&s);
    const uint8_t b[1] = {(uint8_t)(60 + seed % 7)}, vb[1] = {90};
    seq_rec_step(&s, b, vb, 1);
}

static std::unique_ptr<kc_bank_t> makeBank()
{
    auto b = std::make_unique<kc_bank_t>();
    std::memset(b.get(), 0, sizeof *b);
    return b;
}

static void blocks(Processor& p, int count, int n = 256, juce::MidiBuffer* first = nullptr)
{
    juce::AudioBuffer<float> buf(2, n);
    juce::MidiBuffer midi;
    for (int b = 0; b < count; b++) {
        midi.clear();
        if (b == 0 && first) midi.addEvents(*first, 0, -1, 0);
        p.processBlock(buf, midi);
    }
}

static bool sameRecords(const program_t& a, const seq_t& as, const program_t& b, const seq_t& bs)
{
    return ProgramBank::same(a, as, b, bs);
}

// counts what the processor tells the host (the wrappers listen the same way)
struct HostSpy : juce::AudioProcessorListener {
    std::atomic<int> changed{0}, gestures{0}, programChanged{0};
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { changed++; }
    std::atomic<int> infoChanged{0};
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& d) override
    {
        programChanged += d.programChanged;
        infoChanged += d.parameterInfoChanged;
    }
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { gestures++; }
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { gestures++; }
};

static void params_layout()
{
    Processor p;
    int exposed = 0, ok = 0, bad = 0;
    for (uint16_t id = 0; id < KC_PARAM_ID_LIMIT + 8; id++) {
        kc_param_info_t in;
        const bool ex = id < KC_PARAM_ID_LIMIT && kc_param_info(id, &in) == 0;
        auto* q = paramById(p, "p" + juce::String(id));
        if (!ex) { if (q) { std::printf("params_layout: id %u not exposed by the core but a parameter\n", id); bad++; } continue; }
        exposed++;
        if (!q) { std::printf("params_layout: id %u (%s) missing\n", id, in.name); bad++; continue; }
        bool good = q->getName(64) == juce::String(in.name) && q->isAutomatable();
        if (in.kind == KC_KIND_SWITCH) {
            auto* b = dynamic_cast<const juce::AudioParameterBool*>(q);
            good = good && b && b->get() == (in.def != 0) && in.min == 0 && in.max == 1;
        } else if (in.kind == KC_KIND_SELECT) {
            auto* c = dynamic_cast<const juce::AudioParameterChoice*>(q);
            good = good && c && c->choices.size() == in.max - in.min + 1 && c->getIndex() == in.def - in.min;
            for (int32_t v = in.min; good && v <= in.max; v++) good = c->choices[v - in.min] == juce::String(in.labels[v - in.min]);
        } else {
            auto* k = dynamic_cast<const juce::AudioParameterInt*>(q);
            good = good && k && k->getRange().getStart() == in.min && k->getRange().getEnd() == in.max && k->get() == in.def;
            if (in.bipolar) good = good && in.min == -in.max && std::fabs(q->convertTo0to1(0.0f) - 0.5f) < 1e-6f;
        }
        good = good && Params::toCore(in, q->getDefaultValue()) == in.def;
        // every value of the range: the parameter's own conversion and the core's agree both ways
        auto* qq = const_cast<juce::RangedAudioParameter*>(q);
        for (int32_t v = in.min; good && v <= in.max; v++) {
            const float x = Params::toNormalised(in, v);
            static_cast<juce::AudioProcessorParameter*>(qq)->setValue(x);
            good = Params::toCore(in, qq->getValue()) == v && (int32_t)std::lround(qq->convertFrom0to1(x)) ==
                   (in.kind == KC_KIND_SELECT ? v - in.min : v);
        }
        static_cast<juce::AudioProcessorParameter*>(qq)->setValue(q->getDefaultValue());
        if (good) ok++;
        else { std::printf("params_layout: id %u (%s) kind / range / default / labels do not match\n", id, in.name); bad++; }
    }
    // the ones never exposed: the stored-only effect ids, P_SEQ_GEN (also no name, no chord: not ids)
    int hidden = 0;
    for (uint16_t id : {(uint16_t)P_FX_ON, (uint16_t)P_FXA_TYPE, (uint16_t)P_FXB_DIV, (uint16_t)P_SEQ_GEN})
        hidden += paramById(p, "p" + juce::String(id)) == nullptr;
    // in id order, nothing else
    int order = 1, prev = -1;
    for (auto* q : p.getParameters()) {
        auto* r = dynamic_cast<juce::RangedAudioParameter*>(q);
        const int id = r ? r->getParameterID().substring(1).getIntValue() : -1;
        order &= r && id > prev;
        prev = id;
    }
    std::printf("params_layout: %d exposed ids, %d parameters (%d match kind / range / default / labels, %d bad); "
                "effect ids and P_SEQ_GEN not parameters: %d of 4; in id order: %s\n", exposed,
                p.getParameters().size(), ok, bad, hidden, order ? "yes" : "NO");
    CHECK(exposed > 70 && ok == exposed && bad == 0 && p.getParameters().size() == exposed && hidden == 4 && order);
}

static void automation_to_core()
{
    const uint16_t knob = P_FLT_FREQ, sw = P_VCO1_SYNC, sel = P_LFO_SHAPE, bip = P_FLT_ENV_AMT;
    const int n = 300;                            // (host blocks not on the 256 grid)
    struct Change { int block; uint16_t id; int32_t v; };
    const std::vector<Change> changes = {{5, knob, 60}, {5, bip, 90}, {9, knob, 120}, {12, sw, 1}, {12, sel, 3},
                                         {15, knob, 30}, {15, bip, -70}};
    Processor p;
    p.setPlayConfigDetails(0, 2, 44100, n);
    p.prepareToPlay(44100, n);
    juce::AudioBuffer<float> buf(2, n);
    juce::MidiBuffer midi;
    std::vector<float> out;
    const int nb = 24;
    for (int b = 0; b < nb; b++) {
        midi.clear();
        if (b == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8)110), 0),
                    midi.addEvent(juce::MidiMessage::noteOn(1, 55, (juce::uint8)110), 0);
        for (auto& c : changes)                   // the host: before the block (as the wrappers deliver it)
            if (c.block == b) {
                auto* q = const_cast<juce::RangedAudioParameter*>(paramById(p, "p" + juce::String(c.id)));
                if (!q) { std::printf("automation_to_core: no parameter p%u\n", c.id); CHECK(q); return; }
                kc_param_info_t in;
                kc_param_info(c.id, &in);
                q->setValueNotifyingHost(Params::toNormalised(in, c.v));
            }
        p.processBlock(buf, midi);
        for (int i = 0; i < n; i++) out.push_back(buf.getSample(0, i)), out.push_back(buf.getSample(1, i));
    }
    // the reference: the core directly, each change at its block's first sample (split there)
    kc_t* k = kc_create();
    const int64_t total = (int64_t)nb * n;
    std::vector<int16_t> x((size_t)(2 * total), 0);
    int64_t done = 0;
    const uint8_t on1[3] = {0x90, 48, 110}, on2[3] = {0x90, 55, 110};
    kc_midi(k, on1, 3), kc_midi(k, on2, 3);
    while (done < total) {
        int64_t end = (done / KC_BLOCK + 1) * KC_BLOCK;
        for (auto& c : changes) {
            const int64_t at = (int64_t)c.block * n;
            if (at == done) kc_param_set(k, c.id, c.v);
            if (at > done && at < end) end = at;
        }
        if (end > total) end = total;
        kc_render_exact(k, &x[(size_t)(2 * done)], (uint32_t)(end - done));
        done = end;
    }
    long diff = 0;
    for (int64_t s = 0; s < total; s++)
        for (int c = 0; c < 2; c++) {
            const float want = s >= 256 ? (float)x[(size_t)(2 * (s - 256) + c)] * (1.0f / 32768.0f) : 0.0f;
            diff += out[(size_t)(2 * s + c)] != want;
        }
    const int32_t kv = kc_param_get(p.runner().core(), knob), bv = kc_param_get(p.runner().core(), bip),
                  sv = kc_param_get(p.runner().core(), sw), lv = kc_param_get(p.runner().core(), sel);
    kc_destroy(k);
    // a change the host made is not reported back to it (no echo, no automation written by us)
    const int told = p.params().syncToHost();
    std::printf("automation_to_core: 7 host changes before blocks 5, 9, 12, 15 (300-sample blocks): %ld samples differ "
                "from the core with each change at its block's first sample; core values %d %d %d %d (want 30 -70 1 3); "
                "reported back to the host: %d\n", diff, kv, bv, sv, lv, told);
    CHECK(diff == 0 && kv == 30 && bv == -70 && sv == 1 && lv == 3 && told == 0);
}

static void core_to_host()
{
    auto bank = makeBank();
    program_t p3;
    seq_t s3;
    makeProgram(3, p3, s3, "CORE TEST");
    bank->used[3] = 1, bank->program[3] = p3, bank->seq[3] = s3;
    Processor p;
    CHECK(p.setBank(bank.get()));
    p.setPlayConfigDetails(0, 2, 44100, 256);
    p.prepareToPlay(44100, 256);
    HostSpy spy;
    p.addListener(&spy);
    auto* flt = const_cast<juce::RangedAudioParameter*>(paramById(p, "p" + juce::String((int)P_FLT_FREQ)));
    if (!flt) { std::printf("core_to_host: no parameter p%d\n", (int)P_FLT_FREQ); CHECK(flt); p.removeListener(&spy); return; }
    kc_param_info_t fin;
    kc_param_info(P_FLT_FREQ, &fin);
    auto allMatch = [&] {
        int bad = 0;
        for (int i = 0; i < p.params().size(); i++) {
            const auto& e = p.params().entry(i);
            bad += Params::toCore(e.info, e.param->getValue()) != kc_param_get(p.runner().core(), e.id);
        }
        return bad;
    };
    // 1. a CC (the firmware's map: CC 74 = FILTER FREQ)
    juce::MidiBuffer m;
    m.addEvent(juce::MidiMessage::controllerEvent(1, 74, 20), 10);
    blocks(p, 3, 256, &m);
    const int32_t cc = kc_param_get(p.runner().core(), P_FLT_FREQ);
    const bool before = Params::toCore(fin, flt->getValue()) == fin.def;   // (not told yet: the message thread does)
    const int told1 = (p.syncHost(), spy.changed.load());
    const bool hostHasCc = Params::toCore(fin, flt->getValue()) == cc;
    // 2. the host echoes the value back (VST3: performEdit comes back to the processor) after the core moved on: the
    //    echo must not pull the core back
    m.clear();
    m.addEvent(juce::MidiMessage::controllerEvent(1, 74, 110), 0);
    blocks(p, 2, 256, &m);
    const int32_t cc2 = kc_param_get(p.runner().core(), P_FLT_FREQ);
    flt->setValueNotifyingHost(Params::toNormalised(fin, cc));   // (the echo of the first report)
    blocks(p, 2);
    const bool echoIgnored = kc_param_get(p.runner().core(), P_FLT_FREQ) == cc2;
    p.syncHost();
    const bool hostHasCc2 = Params::toCore(fin, flt->getValue()) == cc2;
    // our own report did not come back as a host change
    const bool noLoop = !p.params().hostPending();
    // a genuine host change after that still goes to the core
    flt->setValueNotifyingHost(Params::toNormalised(fin, 77));
    blocks(p, 2);
    const bool hostWins = kc_param_get(p.runner().core(), P_FLT_FREQ) == 77;
    // 3. a MIDI program change: every parameter follows, the program list too
    m.clear();
    m.addEvent(juce::MidiMessage::programChange(1, 3), 5);
    blocks(p, 3, 256, &m);
    const int programAfterPc = p.getCurrentProgram();
    p.syncHost();
    const int badPc = allMatch();
    program_t now;
    seq_t nowS;
    kc_program_get(p.runner().core(), nullptr, &now, &nowS, nullptr);
    const bool isBank3 = sameRecords(now, nowS, p3, s3);
    // 4. the host's program list: setCurrentProgram (message thread)
    p.setCurrentProgram(0);
    const int badSel = allMatch();
    const int programAfterSel = p.getCurrentProgram();
    p.setCurrentProgram(3);
    const int badSel3 = allMatch();
    const bool nameOk = p.getProgramName(3) == "003 CORE TEST" && p.getProgramName(0) == "000 INIT" &&
                        p.getNumPrograms() == 500;
    blocks(p, 3);
    p.syncHost();                                 // (what the core did after the load, if anything)
    blocks(p, 3);
    const bool noLoop2 = !p.params().hostPending() && p.params().syncToHost() == 0;
    p.removeListener(&spy);
    std::printf("core_to_host: CC 74 -> FILTER FREQ %d: host before sync %s, after %s (%d told); echo of the old "
                "value after the core moved on (%d): ignored %s, host then %s; no loop %s; host change after: %s. "
                "Program change 3: current %d, %d parameters differ, the bank's program %s. setCurrentProgram 0 / 3: "
                "%d / %d differ, current %d; names %s; no loop %s; gestures %d (want 0)\n",
                cc, before ? "default" : "MOVED", hostHasCc ? "follows" : "NOT", told1, cc2, echoIgnored ? "yes" : "NO",
                hostHasCc2 ? "follows" : "NOT", noLoop ? "yes" : "NO", hostWins ? "applied" : "NOT applied",
                programAfterPc, badPc, isBank3 ? "yes" : "NO", badSel, badSel3, programAfterSel,
                nameOk ? "ok" : (p.getProgramName(3) + " / " + p.getProgramName(0)).toRawUTF8(), noLoop2 ? "yes" : "NO",
                spy.gestures.load());
    CHECK(before && hostHasCc && told1 > 0 && echoIgnored && hostHasCc2 && noLoop && hostWins);
    CHECK(programAfterPc == 3 && badPc == 0 && isBank3 && badSel == 0 && badSel3 == 0 && programAfterSel == 0);
    CHECK(nameOk && noLoop2 && spy.gestures == 0);
}

struct Snap { int32_t num; program_t p; seq_t s; int edited; };
static Snap snapOf(Processor& p)
{
    Snap s{};
    kc_program_get(p.runner().core(), &s.num, &s.p, &s.s, &s.edited);   // (no audio running: this thread has it)
    return s;
}

static void state_roundtrip()
{
    auto bank = makeBank();
    for (int i : {7, 8}) {
        makeProgram(i, bank->program[i], bank->seq[i], i == 7 ? "SEVEN" : "EIGHT");
        bank->used[i] = 1;
    }
    int good = 0;
    for (int edited = 0; edited < 2; edited++) {
        Processor a;
        a.setBank(bank.get());
        a.setPlayConfigDetails(0, 2, 44100, 256);
        a.prepareToPlay(44100, 256);
        a.setCurrentProgram(edited ? 7 : 8);
        blocks(a, 2);
        if (edited) {                             // an edit (automation) and a different sequence step-entered
            auto* q = const_cast<juce::RangedAudioParameter*>(paramById(a, "p" + juce::String((int)P_FLT_RESO)));
            if (q) q->setValueNotifyingHost(0.25f);
            blocks(a, 2);
            Snap s = snapOf(a);
            const uint8_t nn[1] = {72}, vv[1] = {33};
            seq_rec_step(&s.s, nn, vv, 1);
            kc_program_set(a.runner().core(), s.num, &s.p, &s.s, 1);
            blocks(a, 2);
        }
        Snap sa = snapOf(a);
        juce::MemoryBlock st;
        a.getStateInformation(st);
        Processor b;                              // a new instance, the same bank (a set reopened)
        b.setBank(bank.get());
        b.setStateInformation(st.getData(), (int)st.getSize());
        Snap sb = snapOf(b);
        int badParams = 0;
        for (int i = 0; i < b.params().size(); i++) {
            const auto& e = b.params().entry(i);
            badParams += Params::toCore(e.info, e.param->getValue()) != kc_param_get(b.runner().core(), e.id);
        }
        juce::MemoryBlock st2;
        b.getStateInformation(st2);
        const bool ok = sa.num == sb.num && sameRecords(sa.p, sa.s, sb.p, sb.s) && sa.edited == edited &&
                        sb.edited == edited && b.getCurrentProgram() == sa.num && badParams == 0 && st == st2 &&
                        b.lastStateDiffered() == (edited != 0);
        std::printf("state_roundtrip: program %d, %s, sequence of %u steps: %zu bytes; restored: number %d, program + "
                    "sequence %s, edited %d, parameters differing %d, differs from bank %d, saved again identical %s\n",
                    sa.num, edited ? "edited" : "as loaded", sa.s.len, st.getSize(), sb.num,
                    sameRecords(sa.p, sa.s, sb.p, sb.s) ? "identical" : "DIFFERENT", sb.edited, badParams,
                    (int)b.lastStateDiffered(), st == st2 ? "yes" : "NO");
        good += ok;
    }
    CHECK(good == 2);
}

static void state_bank_changed()
{
    auto bank1 = makeBank(), bank2 = makeBank();
    makeProgram(5, bank1->program[5], bank1->seq[5], "FIRST");
    makeProgram(9, bank2->program[5], bank2->seq[5], "CHANGED");
    bank1->used[5] = bank2->used[5] = 1;
    Processor a;
    a.setBank(bank1.get());
    a.setCurrentProgram(5);
    juce::MemoryBlock st;
    a.getStateInformation(st);
    SetState dec;
    CHECK(StateCodec::decode(st.getData(), st.getSize(), dec) == StateCodec::kOk);
    Processor b;                                  // the bank changed since the set was saved
    b.setBank(bank2.get());
    b.setStateInformation(st.getData(), (int)st.getSize());
    Snap sb = snapOf(b);
    const bool storedWins = sb.num == 5 && sameRecords(sb.p, sb.s, bank1->program[5], bank1->seq[5]);
    Processor c;                                  // the same bank: no difference
    c.setBank(bank1.get());
    c.setStateInformation(st.getData(), (int)st.getSize());
    std::printf("state_bank_changed: saved program 5 \"FIRST\" (edited %d, differs %d); loaded with a bank whose 5 is "
                "\"CHANGED\": the stored copy %s, edited %d, differs from bank %d (now %d); with the same bank: differs %d\n",
                (int)dec.edited, (int)dec.differs, storedWins ? "wins" : "LOST", sb.edited, (int)b.lastStateDiffered(),
                (int)b.differsFromBank(), (int)c.lastStateDiffered());
    CHECK(!dec.edited && !dec.differs && storedWins && sb.edited == 0 && b.lastStateDiffered() && b.differsFromBank() &&
          !c.lastStateDiffered() && !c.differsFromBank());
}

static void state_versioned()
{
    program_t p;
    seq_t s;
    makeProgram(4, p, s, "VERSIONED");
    uint8_t rec[PROGRAM_PACKED_MAX], sq[SEQ_PACKED_MAX];
    const int nrec = program_pack(&p, rec, sizeof rec), nsq = seq_pack(&s, sq, sizeof sq);
    const uint8_t num12[4] = {12, 0, 0, 0};
    // 1. a later writer: version 9 (minReader 1), records this build does not know, an unknown flag bit
    std::vector<uint8_t> later;
    StateCodec::header(later, 9, 1);
    const uint8_t junk[17] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};
    StateCodec::put(later, "XTRA", junk, sizeof junk);
    StateCodec::put(later, "PNUM", num12, 4);
    const uint8_t flags = 0x01 | 0x80;
    StateCodec::put(later, "FLAG", &flags, 1);
    StateCodec::put(later, "ZZZZ", junk, 0);
    StateCodec::put(later, "PROG", rec, (uint32_t)nrec);
    StateCodec::put(later, "SEQ ", sq, (uint32_t)nsq);
    StateCodec::put(later, "MORE", junk, 5);
    Processor a;
    a.setStateInformation(later.data(), (int)later.size());
    Snap sa = snapOf(a);
    const bool laterOk = sa.num == 12 && sameRecords(sa.p, sa.s, p, s) && sa.edited == 1;
    // 2. an older state: a version-1 program record (Phase 1's values only), no sequence, no flags
    std::vector<uint8_t> v1rec;
    v1rec.push_back(1);
    v1rec.insert(v1rec.end(), p.name, p.name + PROGRAM_NAME_LEN);
    for (int i = 0; i < P_COUNT_PHASE1; i++) v1rec.push_back((uint8_t)p.v[i]), v1rec.push_back((uint8_t)((uint16_t)p.v[i] >> 8));
    v1rec.insert(v1rec.end(), p.chord, p.chord + 6);
    const uint16_t crc = program_crc16(v1rec.data(), (uint32_t)v1rec.size());
    v1rec.push_back((uint8_t)crc), v1rec.push_back((uint8_t)(crc >> 8));
    CHECK(v1rec.size() == program_packed_size(1));
    std::vector<uint8_t> older;
    StateCodec::header(older, 1, 1);
    const uint8_t num40[4] = {40, 0, 0, 0};
    StateCodec::put(older, "PNUM", num40, 4);
    StateCodec::put(older, "PROG", v1rec.data(), (uint32_t)v1rec.size());
    Processor b;
    b.setStateInformation(older.data(), (int)older.size());
    Snap sb = snapOf(b);
    bool olderOk = sb.num == 40 && sb.s.len == 0 && sb.edited == 0 && std::memcmp(sb.p.name, p.name, PROGRAM_NAME_LEN) == 0;
    for (int i = 0; i < P_COUNT_PHASE1; i++) olderOk = olderOk && sb.p.v[i] == p.v[i];
    kc_param_info_t bpm, arp;
    kc_param_info(P_BPM, &bpm), kc_param_info(P_ARP_ON, &arp);
    olderOk = olderOk && sb.p.v[P_BPM] == bpm.def && sb.p.v[P_ARP_ON] == arp.def;   // (what v1 lacks: the defaults)
    // 3. the plan-Task-3 build's state (it saved nothing): nothing changes
    Processor c;
    c.setCurrentProgram(33);
    c.setStateInformation(nullptr, 0);
    const bool emptyOk = snapOf(c).num == 33;
    // 4. refused, nothing changes: a format this build may not read, a cut state, a broken record, a bad number
    int refused = 0;
    auto tryBad = [&](std::vector<uint8_t> st, const char* what) {
        Processor d;
        d.setCurrentProgram(21);
        Snap before = snapOf(d);
        d.setStateInformation(st.data(), (int)st.size());
        Snap after = snapOf(d);
        const bool same = after.num == 21 && sameRecords(before.p, before.s, after.p, after.s);
        if (!same) std::printf("state_versioned: %s CHANGED the instance\n", what);
        refused += same;
    };
    std::vector<uint8_t> tooNew;
    StateCodec::header(tooNew, 3, 2);
    tooNew.insert(tooNew.end(), later.begin() + 8, later.end());
    tryBad(tooNew, "minReader 2");
    tryBad(std::vector<uint8_t>(later.begin(), later.end() - 3), "a cut state");
    std::vector<uint8_t> badCrc = later;
    for (size_t i = 8; i + 4 < badCrc.size(); i++)
        if (!std::memcmp(&badCrc[i], "PROG", 4)) { badCrc[i + 8 + 30] ^= 0x55; break; }
    tryBad(badCrc, "a broken program record");
    std::vector<uint8_t> badNum;
    StateCodec::header(badNum, 1, 1);
    const uint8_t num500[4] = {0xF4, 0x01, 0, 0};
    StateCodec::put(badNum, "PNUM", num500, 4);
    StateCodec::put(badNum, "PROG", rec, (uint32_t)nrec);
    tryBad(badNum, "program 500");
    tryBad({'X', 'X', 'X', 'X', 1, 0, 1, 0}, "another magic");
    std::printf("state_versioned: a later writer's state (version 9, 3 unknown records, an unknown flag bit) loads: %s; "
                "an older one (a version-1 program record, no sequence, no flags) loads: %s; the Task-3 build's empty "
                "state: nothing changes %s; refused with nothing changed: %d of 5\n",
                laterOk ? "yes" : "NO", olderOk ? "yes" : "NO", emptyOk ? "yes" : "NO", refused);
    CHECK(laterOk && olderOk && emptyOk && refused == 5);
}

// the set state when the instance does not answer (run() timed out: the audio thread holds it and serves nothing):
// the last good state (the last getStateInformation or setStateInformation that worked), never INIT
static void stuckState(Processor& p, FakeClock& clk, juce::MemoryBlock& out)
{
    CHECK(p.access().audioEnter());               // (an audio thread stuck mid-block: no call served, no idle)
    std::atomic<bool> done{false};
    std::thread t([&] { p.getStateInformation(out); done = true; });
    for (int i = 0; i < 2000 && !done; i++) {
        clk.advanceMs(100);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    t.join();
    p.access().audioExit();
}

static void state_timeout_keeps_last()
{
    auto bank = makeBank();
    for (int i : {7, 8}) {
        makeProgram(i, bank->program[i], bank->seq[i], i == 7 ? "SEVEN" : "EIGHT");
        bank->used[i] = 1;
    }
    Processor p;
    p.setBank(bank.get());
    p.setPlayConfigDetails(0, 2, 44100, 256);
    p.prepareToPlay(44100, 256);
    p.setCurrentProgram(7);
    blocks(p, 2);
    juce::MemoryBlock good;
    p.getStateInformation(good);
    FakeClock clk;
    p.access().setClock(&clk);
    juce::MemoryBlock bad1;
    stuckState(p, clk, bad1);
    SetState d1;
    const bool dec1 = StateCodec::decode(bad1.getData(), bad1.getSize(), d1) == StateCodec::kOk;
    // a state loaded (another instance's, program 8) is the last good one now
    Processor b;
    b.setBank(bank.get());
    b.setCurrentProgram(8);
    juce::MemoryBlock loaded;
    b.getStateInformation(loaded);
    p.access().setClock(nullptr);
    p.setStateInformation(loaded.getData(), (int)loaded.getSize());
    p.access().setClock(&clk);
    juce::MemoryBlock bad2;
    stuckState(p, clk, bad2);
    SetState d2;
    const bool dec2 = StateCodec::decode(bad2.getData(), bad2.getSize(), d2) == StateCodec::kOk;
    p.access().setClock(nullptr);
    std::printf("state_timeout_keeps_last: the instance not answering: saved state = the last good one %s (program %d, "
                "want 7); after loading program 8's state: %s (program %d, want 8)\n", bad1 == good ? "yes" : "NO",
                dec1 ? (int)d1.number : -1, bad2 == loaded ? "yes" : "NO", dec2 ? (int)d2.number : -1);
    CHECK(bad1 == good && dec1 && d1.number == 7 && bad2 == loaded && dec2 && d2.number == 8);
}

// the seam's idle time never reaches run()'s timeout (2000 ms): a host with huge blocks, or none at all, is still taken
// for idle in time (at most a quarter of the timeout), so a call never times out only because the threshold is too long
static void seam_idle_clamped()
{
    CoreAccess acc;
    acc.setIdle(44100, 1 << 20);                  // (blocks of 23.8 s: 3 periods would be 71 s)
    const int huge = acc.idleMs();
    FakeClock clk;
    acc.setClock(&clk);
    std::atomic<int> r{-1};
    std::thread t([&] { r = acc.run([](void*) {}, nullptr) ? 1 : 0; });
    for (int i = 0; i < 2000 && r < 0; i++) {
        clk.advanceMs(50);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    t.join();
    acc.setIdle(44100, 2048);                     // (a usual block: 3 periods, 140 ms)
    const int usual = acc.idleMs();
    acc.setIdle(44100, 64);                       // (a short one: kIdleMs)
    const int shortB = acc.idleMs();
    std::printf("seam_idle_clamped: idle for 2^20-sample blocks %d ms (want <= 500), a call with no audio %s; 2048: %d "
                "ms (want 140), 64: %d ms (want %d)\n", huge, r == 1 ? "ran" : "TIMED OUT", usual, shortB,
                CoreAccess::kIdleMs);
    CHECK(huge <= 500 && huge >= CoreAccess::kIdleMs && r == 1 && acc.ranHere() == 1 && usual == 140 &&
          shortB == CoreAccess::kIdleMs);
}

// prepareToPlay changes the instance (CoreRunner::prepare: kc_* calls, the rings reallocated): it takes it as a call
// through the seam does, so it never overlaps another thread's call (a library watcher's globals, run() on the asking
// thread after an idle). Here a call holds the instance on another thread: prepareToPlay waits until it is done
static void prepare_takes_instance()
{
    Processor p;
    p.setPlayConfigDetails(0, 2, 44100, 256);
    p.prepareToPlay(44100, 256);
    blocks(p, 2);
    FakeClock clk;
    p.access().setClock(&clk);
    struct Ctx { std::atomic<int> inside{0}; std::atomic<bool> release{false}; } c;
    std::thread caller([&] {
        p.access().run([](void* v) {
            auto* x = (Ctx*)v;
            x->inside = 1;
            while (!x->release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            x->inside = 0;
        }, &c);
    });
    for (int i = 0; i < 2000 && !c.inside; i++) {   // (no audio: the asking thread takes it after the idle time)
        clk.advanceMs(50);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const bool holding = c.inside == 1;
    std::atomic<bool> prepared{false};
    std::atomic<int> insideAtReturn{-1};
    std::thread preparer([&] {
        p.prepareToPlay(48000, 512);
        insideAtReturn = c.inside.load();
        prepared = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const bool early = prepared.load();
    c.release = true;
    caller.join();
    preparer.join();
    p.access().setClock(nullptr);
    juce::AudioBuffer<float> buf(2, 512);         // (and the instance is free again: blocks and calls go on)
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    p.processBlock(buf, midi);
    const uint64_t silent = p.silentBlocks();
    const bool runs = p.access().run([](void*) {}, nullptr);
    std::printf("prepare_takes_instance: a call holding the instance %s; prepareToPlay returned while it ran: %s "
                "(the call inside at its return: %d); afterwards a block %s, a call %s\n", holding ? "yes" : "NO",
                early ? "YES" : "no", insideAtReturn.load(), silent ? "SILENT" : "rendered", runs ? "ran" : "FAILED");
    CHECK(holding && !early && insideAtReturn == 0 && silent == 0 && runs);
}

// the real-time part of Task 4: automation storms and program loads in processBlock: no allocation, no lock
static void rt_params()
{
    auto bank = makeBank();
    for (int i = 0; i < 20; i++) {
        makeProgram(i + 1, bank->program[i], bank->seq[i], "RT");
        bank->used[i] = 1;
    }
    Processor p;
    FakeClock clock;                              // (the seam's idle rule: never met here, whatever the scheduling)
    p.access().setClock(&clock);
    p.setBank(bank.get());
    const int block = 128;
    p.setPlayConfigDetails(0, 2, 48000, block);
    p.prepareToPlay(48000, block);
    juce::AudioBuffer<float> buf(2, block);
    juce::MidiBuffer midi;
    midi.ensureSize(8192);
    std::vector<juce::AudioProcessorParameter*> ps;
    for (auto* q : p.getParameters()) ps.push_back(q);
    long allocs = 0, locks = 0;
    int served = 0, loads = 0;
    for (int b = 0; b < 600; b++) {
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, 48 + b % 24, (juce::uint8)100), b % block);
        if (b % 2) midi.addEvent(juce::MidiMessage::noteOff(1, 48 + (b - 1) % 24), 3);
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, (b * 5) % 128), 7);
        if (b % 30 == 11) midi.addEvent(juce::MidiMessage::programChange(1, b % 20), 40), loads++;
        std::thread asker;
        std::atomic<bool> release{false};
        if (b % 50 == 25) {                       // the host's program list, from another thread (served by us); the
            asker = std::thread([&] {             // asking thread's own work (the bank view, its mutex, thread exit)
                p.setCurrentProgram(b % 20);      // stays outside the counted part
                while (!release) std::this_thread::yield();
            });
            while (!p.access().posted()) std::this_thread::yield();
        }
        long a = alloc_hook_allocs(), l = alloc_hook_locks();
        alloc_hook_arm(1);
        for (size_t i = 0; i < ps.size(); i++)    // the storm: every parameter, every block (what the wrapper calls)
            ps[i]->setValue((float)((b * 37 + (int)i * 11) % 101) / 100.0f);
        p.processBlock(buf, midi);
        alloc_hook_arm(0);
        if (alloc_hook_allocs() - a || alloc_hook_locks() - l)
            std::printf("rt_params: block %d: %ld allocations, %ld locks%s\n", b, alloc_hook_allocs() - a,
                        alloc_hook_locks() - l, asker.joinable() ? " (a setCurrentProgram served)" : "");
        allocs += alloc_hook_allocs() - a, locks += alloc_hook_locks() - l;
        release = true;
        if (asker.joinable()) asker.join(), served++;
        if (b % 10 == 0) p.syncHost();            // (the message thread's part, outside the counted part)
    }
    std::printf("rt_params: 600 blocks at 48 kHz, %zu parameters set every block, %d MIDI program changes, %d "
                "setCurrentProgram calls served: %ld allocations, %ld locks in processBlock (ran on the asking thread: "
                "%llu)\n", ps.size(), loads, served, allocs, locks, (unsigned long long)p.access().ranHere());
    CHECK(allocs == 0 && locks == 0 && served == 12 && p.access().ranHere() == 0);
}

// ===============================================================================================================
// plan Task 5: the clock from the host's transport (a fake AudioPlayHead)
// ===============================================================================================================
// a host: blocks of `block` at `rate`, the position moved on after each block at its tempo (a loop's end wraps it, as
// Live does: the next block starts inside the loop); MIDI at absolute session samples; the output kept
struct Host {
    Processor& p;
    FakeHead head;
    double rate;
    int block;
    int64_t s = 0;
    std::vector<float> out;
    Host(Processor& pp, double r, int b) : p(pp), rate(r), block(b)
    {
        p.setPlayConfigDetails(0, 2, rate, block);
        p.prepareToPlay(rate, block);
        p.setPlayHead(&head);
    }
    void run(int64_t frames, const std::vector<Msg>& msgs = {}, std::function<void(int64_t)> before = nullptr)
    {
        juce::AudioBuffer<float> buf(2, block);
        juce::MidiBuffer midi;
        for (int64_t d = 0; d < frames; d += block) {
            const int n = (int)std::min<int64_t>(block, frames - d);
            if (before) before(s);
            buf.setSize(2, n, false, false, true);
            midi.clear();
            for (auto& m : msgs) if (m.at >= s && m.at < s + n) midi.addEvent(m.d, m.len, (int)(m.at - s));
            p.processBlock(buf, midi);
            for (int i = 0; i < n; i++) out.push_back(buf.getSample(0, i)), out.push_back(buf.getSample(1, i));
            s += n;
            if (head.playing) {
                head.ppq += n * head.bpm / 60.0 / rate;
                if (head.looping && head.le > head.ls && head.ppq >= head.le) head.ppq -= head.le - head.ls;
            }
        }
    }
    std::vector<kc_step_t> steps()                // (the test's thread: no audio thread runs meanwhile)
    {
        std::vector<kc_step_t> v(KC_LOG);
        v.resize(kc_clock_steps(p.runner().core(), v.data(), KC_LOG));
        return v;
    }
};
constexpr int kDiv16th = 6;                       // (P_CLK_DIV's 16TH: synth/clock.h kDiv16th)
// the program: a sequence of 16ths (len steps), note 40 + i on step i, or only `only` (>= 0) playing (the rest rests)
static void tpProgram(Processor& p, int len, int only = -1)
{
    program_t prog;
    seq_t q;
    int32_t num;
    kc_t* k = p.runner().core();
    kc_program_get(k, &num, &prog, nullptr, nullptr);
    seq_clear(&q);
    q.len = (uint8_t)len;
    for (int i = 0; i < len; i++)
        if (only < 0 || i == only) q.step[i].count = 1, q.step[i].notes[0] = (uint8_t)(40 + i), q.step[i].vels[0] = 100;
    prog.v[P_CLK_DIV] = kDiv16th;
    CHECK(kc_program_set(k, 0, &prog, &q, 0) == 0);
}
// internal frame of session sample time x (real): the first at or after it (CoreRunner's e(s) for a real time)
static int64_t eOf(double x, double rate) { return (int64_t)std::ceil(x * 44100.0 / rate - 1e-6); }

// play from bar 1 at session sample S0 (44.1 kHz blocks of 512, 48 kHz blocks of 1000): the step events at the internal
// frame of the session sample where the host is at each 16th (S0 + k x 5512.5 x rate / 44100), numbered 0, 1, ...;
// the sequence's notes 40, 41, ...; following the host while playing
static void transport_start_bar()
{
    for (auto [rate, block] : {std::pair<double, int>{44100, 512}, {48000, 1000}}) {
        Processor p;
        Host h(p, rate, block);
        tpProgram(p, 16);
        h.run(4 * block);
        kc_seq_play(p.runner().core(), 1);
        h.steps();
        const int64_t S0 = h.s;
        h.head.playing = true, h.head.ppq = 0.0;
        h.run(16 * 5513 * rate / 44100);
        auto st = h.steps();
        int bad = 0, n = 0;
        for (auto& e : st) {
            if (e.frame < (uint64_t)eOf((double)S0, rate)) continue;
            bad += (int64_t)e.frame != eOf(S0 + n * 5512.5 * rate / 44100.0, rate) || e.step != n;
            n++;
        }
        std::printf("transport_start_bar: %.0f Hz, blocks of %d, from sample %lld: %d steps, %d off; following %d\n", rate,
                    block, (long long)S0, n, bad, (int)p.followingHost());
        CHECK(n == 16 && bad == 0 && p.followingHost());
    }
}

// the alignment with the host's grid: a sequence step at the host's position p sounds exactly as a MIDI note the host
// places at p (the same samples out: the same internal frame, the same latency), at 44.1 and 48 kHz. The sequence: a
// note on its 2nd 16th only (60, velocity 100; its gate 2756 frames); the reference: SEQ off, MIDI note-on at the session
// sample where the host is at ppq 0.25 (+ 4 bars), its note-off where the gate ends (internal frame + 2756)
static void transport_aligned()
{
    for (auto [rate, block] : {std::pair<double, int>{44100, 512}, {48000, 480}, {48000, 1000}}) {
        const int64_t frames = (int64_t)(3.0 * rate);
        Processor a, b;
        Host ha(a, rate, block), hb(b, rate, block);
        tpProgram(a, 16, 1);
        tpProgram(b, 16, 1);
        {                                         // (step 1's note: 60)
            program_t prog;
            seq_t q;
            int32_t num;
            for (Processor* p : {&a, &b}) {
                kc_program_get(p->runner().core(), &num, &prog, &q, nullptr);
                q.step[1].notes[0] = 60;
                CHECK(kc_program_set(p->runner().core(), 0, &prog, &q, 0) == 0);
            }
        }
        ha.run(4 * block), hb.run(4 * block);
        const int64_t S0 = ha.s;
        kc_seq_play(a.runner().core(), 1);        // (with the start: its internal restart's step 0 is a rest)
        ha.head.playing = true, ha.head.ppq = 0.0;
        hb.head.playing = true, hb.head.ppq = 0.0;
        std::vector<Msg> msgs;
        for (int bar = 0; bar < 2; bar++) {       // (the reference: the clip's notes at ppq 0.25 and 4.25)
            const double x = S0 + (bar * 4.0 + 0.25) * 60.0 / 120.0 * rate;
            const int64_t on = (int64_t)std::ceil(x - 1e-6), f = eOf(x, rate) + 2756;
            int64_t off = (int64_t)std::floor(f * rate / 44100.0);
            while (eOf((double)off, rate) < f) off++;
            msgs.push_back({on, 3, {0x90, 60, 100}}), msgs.push_back({off, 3, {0x80, 60, 0}});
        }
        ha.run(frames - S0), hb.run(frames - S0, msgs);
        size_t diff = 0, first = 0;
        for (size_t i = 0; i < ha.out.size(); i++)
            if (ha.out[i] != hb.out[i]) { if (!diff) first = i / 2; diff++; }
        const int64_t onA = onset(ha.out, 1e-4f);
        std::printf("transport_aligned: %.0f Hz, blocks of %d: the sequence's step at ppq 0.25 sounds from sample %lld "
                    "(the host's ppq 0.25 at %.1f + latency %d); %zu samples differ from the clip's MIDI note (first %zu)\n",
                    rate, block, (long long)onA, S0 + 0.25 * 0.5 * rate, a.getLatencySamples(), diff, first);
        CHECK(diff == 0 && onA > S0);
    }
}

// a 1-bar loop [0, 4) as Live loops it (the position wrapped from the next block on; the loop points reported), blocks
// of 1000 at 48 kHz and 500 at 44.1 kHz (the wrap inside a block), a 12-step sequence: 3 passes, 16 steps each (one at
// each wrap, at the wrap's frame: the loop's start), steps 0..15 every pass, none doubled or skipped; the notes 40..51,
// 40..43 every pass
static void transport_loop()
{
    for (auto [rate, block] : {std::pair<double, int>{48000, 1000}, {44100, 500}}) {
        Processor p;
        Host h(p, rate, block);
        tpProgram(p, 12);
        h.run(2 * block);
        kc_seq_play(p.runner().core(), 1);        // (stopped: armed until the start)
        h.run(2 * block);
        h.steps();
        {
            kc_note_t drop[KC_LOG];
            kc_notes(p.runner().core(), drop, KC_LOG);
        }
        const int64_t S0 = h.s;
        h.head.playing = true, h.head.looping = true, h.head.ls = 0.0, h.head.le = 4.0;
        h.run((int64_t)(3 * 2.0 * rate) - 1);
        auto st = h.steps();
        int n = 0, bad = 0;
        for (auto& e : st) {
            if (!(e.kind & KC_EV_HOSTED)) continue;
            const int pass = n / 16, k = n % 16;
            const bool off = (int64_t)e.frame != eOf(S0 + (pass * 4.0 + k * 0.25) * 0.5 * rate, rate) || e.step != k;
            if (off && std::getenv("TP_DEBUG"))
                std::printf("  [%d] frame %llu step %d kind %02X want %lld\n", n, (unsigned long long)e.frame, e.step, e.kind,
                            (long long)eOf(S0 + (pass * 4.0 + k * 0.25) * 0.5 * rate, rate));
            bad += off;
            n++;
        }
        kc_note_t nt[KC_LOG];
        uint32_t m = kc_notes(p.runner().core(), nt, KC_LOG), ons = 0, order = 1;
        for (uint32_t i = 0; i < m; i++)
            if (nt[i].vel) order &= nt[i].note == 40 + (int)(ons % 16) % 12, ons++;
        std::printf("transport_loop: %.0f Hz, blocks of %d: %d steps in 3 passes (want 48), %d off; %u note-ons in order: "
                    "%s\n", rate, block, n, bad, ons, order ? "yes" : "no");
        CHECK(n == 48 && bad == 0 && ons >= 47 && order);
    }
}

// the loop at tempos that are not whole multiples of 1/64 BPM (the core's tempo resolution) and under a ramp, blocks of
// 2048 (the wrap far into a block): 1-bar loops, 48 passes each (a ramp: the tempo moving every block); the steps'
// numbers 0..15 every pass, none doubled or skipped at a wrap (the old timeline's loop-end step never fires before the
// wrap's jump), never two steps a frame or less apart
static void transport_loop_tempo()
{
    struct Case { double rate, bpm, ramp; };      // ramp: BPM added per block (bounded: a triangle)
    for (const Case c : {Case{48000, 100.4, 0}, Case{44100, 100.4, 0}, Case{48000, 77.7, 0}, Case{44100, 141.2, 0},
                         Case{44100, 97.3, 0}, Case{48000, 133.71, 0}, Case{44100, 100.4, 0.37},
                         Case{48000, 123.45, -0.29}}) {
        Processor p;
        Host h(p, c.rate, 2048);
        kc_param_set(p.runner().core(), P_CLK_DIV, kDiv16th);
        h.run(2048);
        h.steps();
        h.head.playing = true, h.head.looping = true, h.head.ls = 0.0, h.head.le = 4.0, h.head.bpm = c.bpm;
        int blk = 0, expect = 0, bad = 0, total = 0, close = 0;
        int64_t lastF = -100;
        for (int pass = 0; pass < 48; pass++) {
            const int64_t until = h.s + (int64_t)(4.0 * 60.0 / c.bpm * c.rate);
            h.run(until - h.s, {}, [&](int64_t) {
                if (c.ramp != 0) h.head.bpm = c.bpm + c.ramp * ((blk / 40) % 2 ? 40 - blk % 40 : blk % 40);
                blk++;
            });
            for (auto& e : h.steps()) {
                if (!(e.kind & KC_EV_HOSTED)) continue;
                if (e.step != expect && std::getenv("TP_DEBUG"))
                    std::printf("  frame %llu step %d kind %02X (want %d; last at %lld)\n", (unsigned long long)e.frame,
                                e.step, e.kind, expect, (long long)lastF);
                bad += e.step != expect;
                close += (int64_t)e.frame - lastF <= 1;
                lastF = (int64_t)e.frame, expect = (e.step + 1) % 16, total++;
            }
        }
        std::printf("transport_loop_tempo: %.0f Hz, %.2f BPM%s: %d steps (steady: want 768), %d out of order, %d at "
                    "once\n", c.rate, c.bpm, c.ramp != 0 ? " ramping" : "", total, bad, close);
        CHECK(bad == 0 && close == 0 && (c.ramp != 0 ? total > 600 : total >= 767 && total <= 769));
    }
}

// a locate while playing (the host's position jumps between blocks): to ppq 8.25 (the song's 16th 33 = the sequence's
// step 1, note 41) at the block's first sample's frame; backwards to 0.6 (inside the 16th 2's first half: resumed,
// note 42 at once)
static void transport_locate()
{
    Processor p;
    Host h(p, 48000, 1000);
    tpProgram(p, 16);
    h.run(2000);
    kc_seq_play(p.runner().core(), 1);
    h.head.playing = true;
    h.run(10000);
    h.steps();
    kc_note_t nt[KC_LOG];
    kc_notes(p.runner().core(), nt, KC_LOG);
    for (auto [to, step, note, kind] : {std::tuple<double, int, int, int>{8.25, 33, 41, KC_EV_JUMP}, {0.6, 2, 42, KC_EV_RESUME}}) {
        const int64_t J = h.s;
        h.head.ppq = to;
        h.run(3000);
        auto st = h.steps();
        uint32_t m = kc_notes(p.runner().core(), nt, KC_LOG), on = 0;
        while (on < m && !nt[on].vel) on++;
        std::printf("transport_locate: at %lld to ppq %.2f: step %d kind %02X at frame %llu (want %d at %lld), note %d\n",
                    (long long)J, to, st.empty() ? -99 : st[0].step, st.empty() ? 0 : st[0].kind,
                    (unsigned long long)(st.empty() ? 0 : st[0].frame), step, (long long)eOf((double)J, 48000),
                    on < m ? nt[on].note : -1);
        CHECK(!st.empty() && st[0].step == step && (st[0].kind & KC_EV_KIND) == kind &&
              (int64_t)st[0].frame == eOf((double)J, 48000) && on < m && nt[on].note == note);
    }
}

// tempo automation 100 -> 140 BPM over 4 s (linear in time; each block's tempo at its start, the position the exact
// integral): every 16th within 1 internal frame of the ideal (the first frame at or past the time the host's position
// reaches it), 48 kHz blocks of 1000 and 44.1 kHz blocks of 512
static double tpRamp(double t) { return t < 4.0 ? (100.0 * t + 5.0 * t * t) / 60.0 : 8.0 + 140.0 * (t - 4.0) / 60.0; }
static void transport_tempo_ramp()
{
    for (auto [rate, block] : {std::pair<double, int>{48000, 1000}, {44100, 512}}) {
        Processor p;
        Host h(p, rate, block);
        kc_param_set(p.runner().core(), P_CLK_DIV, kDiv16th);
        h.head.playing = true;
        const double r = rate;
        h.run((int64_t)(5.0 * rate), {}, [&h, r](int64_t s) {
            const double t = s / r;
            h.head.bpm = t < 4.0 ? 100.0 + 10.0 * t : 140.0, h.head.ppq = tpRamp(t);
        });
        auto st = h.steps();
        double worst = 0;
        int bad = 0;
        for (size_t i = 0; i < st.size(); i++) {
            double lo = 0, hi = 6;
            for (int it = 0; it < 80; it++) { double m = (lo + hi) / 2; if (tpRamp(m) * 4.0 < (double)i) lo = m; else hi = m; }
            worst = std::max(worst, std::fabs((double)st[i].frame - hi * 44100.0));
            if (std::getenv("TP_DEBUG")) std::printf("  %zu: %llu ideal %.3f\n", i, (unsigned long long)st[i].frame, hi * 44100.0);
            bad += st[i].step != (int)i;
        }
        std::printf("transport_tempo_ramp: %.0f Hz, blocks of %d: %zu 16ths, worst %.3f internal frames from the ideal "
                    "grid, %d numbered wrong\n", rate, block, st.size(), worst, bad);
        CHECK(st.size() == 42 && worst <= 1.0 && bad == 0);
    }
}

// stopped: the internal clock at P_BPM (the arp's, as the firmware's); P_BPM greyed while playing: a host change of it
// (automation) reaches the core (kc_param_get) but the steps stay on the host's 120; following while playing, not
// after the stop; after the stop the internal clock at the new P_BPM (90): steps 7350 frames apart; the sequencer stops
// with Live (no note while stopped, SEQ PLAY still on: armed, state 2) and Live's next play resumes it on the song's
// step (from ppq 2.75: the 16th 11, note 51)
static void transport_stop_internal()
{
    Processor p;
    Host h(p, 44100, 512);
    tpProgram(p, 16);
    h.run(1024);
    kc_seq_play(p.runner().core(), 1);
    h.head.playing = true;
    h.run(512 * 20);
    auto* q = const_cast<juce::RangedAudioParameter*>(paramById(p, "p" + juce::String(P_BPM)));
    kc_param_info_t in;
    kc_param_info(P_BPM, &in);
    CHECK(q != nullptr);
    if (q) q->setValueNotifyingHost(Params::toNormalised(in, 90));
    h.steps();
    const bool followed = p.followingHost();
    h.run(512 * 40);
    auto st = h.steps();
    int bad = 0;
    for (size_t i = 1; i < st.size(); i++) bad += st[i].frame - st[i - 1].frame != 5512 && st[i].frame - st[i - 1].frame != 5513;
    std::printf("bpm_param_greyed: P_BPM %d while playing: following %d, %zu steps, %d not 16ths of 120\n",
                (int)kc_param_get(p.runner().core(), P_BPM), (int)followed, st.size(), bad);
    CHECK(kc_param_get(p.runner().core(), P_BPM) == 90 && followed && st.size() >= 3 && bad == 0);
    kc_note_t nt[KC_LOG];
    kc_notes(p.runner().core(), nt, KC_LOG);      // (the notes played so far: dropped)
    const int64_t stopAt = h.s;
    h.head.playing = false;
    h.run(512 * 100);
    st = h.steps();
    int rel = 0, gaps = 0, n = 0;
    for (size_t i = 0; i < st.size(); i++) {
        if ((st[i].kind & KC_EV_KIND) == KC_EV_RELEASE) { rel++; continue; }
        if (n++ && st[i].frame - st[i - 1].frame != 7350) gaps++;
    }
    uint32_t m = kc_notes(p.runner().core(), nt, KC_LOG), ons = 0;
    for (uint32_t i = 0; i < m; i++) ons += nt[i].vel != 0 && (int64_t)nt[i].frame >= stopAt;
    std::printf("transport_stop_internal: stopped: following %d, %d release(s), %d steps on the internal clock, %d not "
                "7350 apart (90 BPM); the sequencer: %u note-ons, SEQ PLAY %d, state %d\n", (int)p.followingHost(), rel, n,
                gaps, ons, kc_seq_playing(p.runner().core()), kc_seq_state(p.runner().core()));
    CHECK(!p.followingHost() && rel == 1 && n >= 6 && gaps == 0 && ons == 0 && kc_seq_playing(p.runner().core()) &&
          kc_seq_state(p.runner().core()) == 2);
    h.head.playing = true, h.head.ppq = 2.75;
    h.run(1024);
    m = kc_notes(p.runner().core(), nt, KC_LOG);
    uint32_t first = 0;
    while (first < m && !nt[first].vel) first++;
    std::printf("transport_stop_internal: Live plays again from ppq 2.75: note %d (want 51), state %d\n",
                first < m ? nt[first].note : -1, kc_seq_state(p.runner().core()));
    CHECK(first < m && nt[first].note == 51 && kc_seq_state(p.runner().core()) == 1);
}

// P_BPM greyed: following while playing (the panel's flag), the host's tempo used, not P_BPM; stopped: P_BPM
static void bpm_param_greyed()
{
    Processor p;
    Host h(p, 44100, 512);
    kc_param_set(p.runner().core(), P_BPM, 60);
    h.run(512 * 4);
    const bool before = p.followingHost();
    h.head.playing = true, h.head.bpm = 150.0;
    h.run(512 * 100);
    auto st = h.steps();
    int bad = 0, n = 0;
    for (size_t i = 0; i < st.size(); i++) {
        if (!(st[i].kind & KC_EV_HOSTED)) continue;
        bad += (int64_t)st[i].frame != 2048 + (int64_t)std::ceil(n * 17640.0 - 1e-9);
        n++;
    }
    std::printf("bpm_param_greyed: stopped: following %d; playing at 150 (P_BPM 60): following %d, %d quarters, %d off "
                "17640 apart\n", (int)before, (int)p.followingHost(), n, bad);
    CHECK(!before && p.followingHost() && n == 3 && bad == 0);
}


// a host with no transport (no play head, or one that gives no position / no ppq): the clock is never given a host
// transport, so the sequencer is the firmware's (SEQ PLAY plays at once on the internal clock), never armed for ever
struct NoPosHead final : juce::AudioPlayHead {
    bool withInfo = false;                        // a position without ppq (else: no position at all)
    juce::Optional<PositionInfo> getPosition() const override
    {
        if (!withInfo) return {};
        PositionInfo i;
        i.setIsPlaying(false);
        i.setBpm(120.0);
        return i;
    }
};
static void transport_none()
{
    int good = 0;
    const char* what[3] = {"no play head", "no position", "a position without ppq"};
    for (int v = 0; v < 3; v++) {
        Processor p;
        NoPosHead head;
        head.withInfo = v == 2;
        p.setPlayConfigDetails(0, 2, 44100, 512);
        p.prepareToPlay(44100, 512);
        if (v) p.setPlayHead(&head);
        tpProgram(p, 16);
        blocks(p, 4, 512);
        p.seqPlay(true);
        blocks(p, 40, 512);
        kc_note_t nt[KC_LOG];
        const uint32_t m = kc_notes(p.runner().core(), nt, KC_LOG);
        uint32_t ons = 0;
        for (uint32_t i = 0; i < m; i++) ons += nt[i].vel != 0;
        const int state = p.seqState();
        std::printf("transport_none: %s: SEQ PLAY -> state %d (want 1, playing), %u note-ons, following %d\n", what[v],
                    state, ons, (int)p.followingHost());
        good += state == 1 && ons >= 2 && !p.followingHost();
    }
    CHECK(good == 3);
}

// a host that gave a position while playing, then stops giving one (no play head now): the core is told the host
// stopped, once (its notes released, the sequencer stops, armed), then nothing more; a host that never played and loses
// its position is told nothing
static void transport_vanishes()
{
    Processor p;
    Host h(p, 44100, 512);
    tpProgram(p, 16);
    h.run(1024);
    kc_seq_play(p.runner().core(), 1);
    h.head.playing = true;
    h.run(512 * 20);
    const bool followed = p.followingHost();
    kc_note_t nt[KC_LOG];
    kc_notes(p.runner().core(), nt, KC_LOG);      // (the notes played so far: dropped)
    h.steps();
    const int64_t goneAt = h.s;
    p.setPlayHead(nullptr);                       // (the position disappears while playing)
    h.run(512 * 100);
    auto st = h.steps();
    int rel = 0;
    for (auto& e : st) rel += (e.kind & KC_EV_KIND) == KC_EV_RELEASE;
    uint32_t m = kc_notes(p.runner().core(), nt, KC_LOG), ons = 0;
    for (uint32_t i = 0; i < m; i++) ons += nt[i].vel != 0 && (int64_t)nt[i].frame >= goneAt;
    const int state = kc_seq_state(p.runner().core());
    std::printf("transport_vanishes: playing with a position, following %d; the position gone: following %d, %d "
                "release(s), %u note-ons since, the sequencer state %d (want 2, armed)\n", (int)followed,
                (int)p.followingHost(), rel, ons, state);
    CHECK(followed && !p.followingHost() && rel == 1 && ons == 0 && state == 2);
}

// Follow-up (export dropout): an offline render (isNonRealtime) slower than real time, while calls arrive from another
// thread (the panel's 15 Hz poll) and another thread takes the instance (prepareToPlay's take()): the gaps between the
// blocks on the seam's clock are past the idle threshold, yet no call runs on the asking thread and no block is silent:
// the output is sample for sample the same as an undisturbed offline render
static void offline_no_silent()
{
    const int block = 2048, nBlocks = 24, nCalls = 14;
    const double rate = 44100;
    auto script = [](int b, juce::MidiBuffer& m) {
        m.clear();
        if (b == 1) m.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 100);
        if (b == 3) m.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)90), 50);
        if (b == 9) m.addEvent(juce::MidiMessage::noteOff(1, 60), 7);
        if (b == 12) m.addEvent(juce::MidiMessage::noteOff(1, 64), 1000);
        if (b == 15) m.addEvent(juce::MidiMessage::noteOn(1, 67, (juce::uint8)120), 0);
        if (b == 17) m.addEvent(juce::MidiMessage::noteOn(1, 71, (juce::uint8)70), 2047);
        if (b == 21) m.addEvent(juce::MidiMessage::noteOff(1, 67), 30), m.addEvent(juce::MidiMessage::noteOff(1, 71), 31);
    };
    auto render = [&](Processor& p, const std::function<void(int)>& before) {
        std::vector<float> out;
        juce::AudioBuffer<float> buf(2, block);
        juce::MidiBuffer midi;
        midi.ensureSize(1024);
        for (int b = 0; b < nBlocks; b++) {
            if (before) before(b);
            script(b, midi);
            p.processBlock(buf, midi);
            for (int i = 0; i < block; i++) out.push_back(buf.getSample(0, i)), out.push_back(buf.getSample(1, i));
        }
        return out;
    };
    Processor ref;
    ref.setNonRealtime(true);
    ref.setPlayConfigDetails(0, 2, rate, block);
    ref.prepareToPlay(rate, block);
    const auto want = render(ref, nullptr);
    Processor p;
    p.setNonRealtime(true);
    p.setPlayConfigDetails(0, 2, rate, block);
    p.prepareToPlay(rate, block);
    FakeClock clk;
    p.access().setClock(&clk);
    const int idle = p.access().idleMs();
    struct Ctx { kc_t* k; std::thread::id asker; std::atomic<int> here{0}, audio{0}; std::atomic<bool> holding{false}; } c;
    c.k = p.runner().core();
    std::atomic<int> served{0};
    std::thread asker([&] {
        c.asker = std::this_thread::get_id();
        for (int i = 0; i < nCalls; i++)
            served += p.access().run([](void* v) {
                auto* x = (Ctx*)v;
                int32_t num;
                kc_program_get(x->k, &num, nullptr, nullptr, nullptr);
                if (std::this_thread::get_id() == x->asker) {   // (on the asking thread: hold it while a block comes)
                    x->here++;
                    x->holding = true;
                    std::this_thread::sleep_for(std::chrono::milliseconds(30));
                    x->holding = false;
                } else
                    x->audio++;
            }, &c);
    });
    std::atomic<bool> taking{false};
    std::thread taker;
    const auto out = render(p, [&](int b) {
        if (b < nCalls) {                         // a call waits; the render is slow: a gap past the idle threshold
            for (int i = 0; i < 1000 && !p.access().posted(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            clk.advanceMs(idle + 50);
            for (int i = 0; i < 50 && !c.holding; i++) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else if (b >= 16 && b < 20) {           // another thread holds the instance (take) as the block comes
            if (taker.joinable()) taker.join();
            taking = false;
            taker = std::thread([&] {
                p.access().take();
                taking = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                p.access().give();
            });
            while (!taking) std::this_thread::yield();
        }
    });
    if (taker.joinable()) taker.join();
    asker.join();
    p.access().setClock(nullptr);
    size_t diff = 0;
    for (size_t i = 0; i < out.size() && i < want.size(); i++) diff += out[i] != want[i];
    const bool sound = rms(want.data(), want.size()) > 1e-3;
    std::printf("offline_no_silent: %d blocks of %d offline, %d calls each after %d ms of the seam's clock (idle %d ms) "
                "and 4 blocks while another thread holds the instance: %d served, %d on the asking thread, %d on the "
                "audio thread, %llu silent blocks; output vs an undisturbed render: %zu of %zu samples differ (sound %d)\n",
                nBlocks, block, nCalls, idle + 50, idle, served.load(), c.here.load(), c.audio.load(),
                (unsigned long long)p.silentBlocks(), diff, out.size(), (int)sound);
    CHECK(served == nCalls && c.here == 0 && c.audio == nCalls && p.silentBlocks() == 0 && out.size() == want.size() &&
          diff == 0 && sound && p.access().ranHere() == 0);
}

// Follow-up (export dropout): offline, run() never takes the instance on the asking thread: with no block coming it
// times out (false; the set state falls back to the last good one); a block serves it on the audio thread; back in
// real time, the idle rule acts again (the asking thread takes it)
static void offline_run_waits()
{
    Processor p;
    p.setPlayConfigDetails(0, 2, 44100, 256);
    p.prepareToPlay(44100, 256);
    p.setCurrentProgram(5);
    blocks(p, 2);
    juce::MemoryBlock good;
    p.getStateInformation(good);
    const uint64_t here0 = p.access().ranHere();  // (the calls above: real time, no audio -- on this thread)
    p.setNonRealtime(true);
    FakeClock clk;
    p.access().setClock(&clk);
    struct Ctx { std::thread::id asker; std::atomic<int> here{0}, other{0}; } c;
    auto fn = [](void* v) {
        auto* x = (Ctx*)v;
        if (std::this_thread::get_id() == x->asker) x->here++; else x->other++;
    };
    auto ask = [&](const std::function<void()>& meanwhile) {
        std::atomic<int> r{-1};
        std::thread t([&] { c.asker = std::this_thread::get_id(); r = p.access().run(fn, &c) ? 1 : 0; });
        meanwhile();
        for (int i = 0; i < 3000 && r < 0; i++) {
            clk.advanceMs(50);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        t.join();
        return r.load();
    };
    const int none = ask([] {});                  // offline, no block: times out
    const int hereNone = c.here, otherNone = c.other;
    juce::MemoryBlock saved;                      // (the set state, no block coming: the last good one)
    {
        std::atomic<bool> done{false};
        std::thread t([&] { p.getStateInformation(saved); done = true; });
        for (int i = 0; i < 3000 && !done; i++) {
            clk.advanceMs(50);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        t.join();
    }
    const int served = ask([&] {                  // offline, a block comes: served on the audio thread
        while (!p.access().posted()) std::this_thread::yield();
        clk.advanceMs(1000);                      // (a slow render: far past the idle threshold)
        blocks(p, 1);
    });
    const int hereServed = c.here, otherServed = c.other;
    p.setNonRealtime(false);
    const int rt = ask([] {});                    // real time again, no block: the asking thread takes it
    p.access().setClock(nullptr);
    std::printf("offline_run_waits: offline, no block: run() %d (want 0, timed out), on the asking thread %d; the set "
                "state the last good one %s; a block comes after 1000 ms: run() %d, on the audio thread %d, on the "
                "asking thread %d; real time again, no block: run() %d, on the asking thread %d (ranHere since offline %llu)\n", none,
                hereNone, saved == good ? "yes" : "NO", served, otherServed - otherNone, hereServed - hereNone, rt,
                c.here - hereServed, (unsigned long long)(p.access().ranHere() - here0));
    CHECK(none == 0 && hereNone == 0 && otherNone == 0 && saved == good && served == 1 && otherServed - otherNone == 1 &&
          hereServed == 0 && rt == 1 && c.here == 1 && p.access().ranHere() - here0 == 1);
}

// ===============================================================================================================
// plan Task 6: the shared library
// ===============================================================================================================
static std::string gTmp;                          // this run's temporary directory (main)

static std::string freshDir(const char* name)
{
    namespace fs = std::filesystem;
    fs::path d = fs::path(gTmp) / name;
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d.string();
}
static std::vector<uint8_t> readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static void writeFile(const std::string& path, const std::vector<uint8_t>& b)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write((const char*)b.data(), (std::streamsize)b.size());
}
static bool waitFor(const std::function<bool()>& ok, int ms = 3000)
{
    for (int i = 0; i < ms; i++) {
        if (ok()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return ok();
}
static int countFiles(const std::string& dir, const std::string& part)
{
    int n = 0;
    for (auto& e : std::filesystem::directory_iterator(dir))
        n += e.path().filename().string().find(part) != std::string::npos;
    return n;
}
static std::string firstFile(const std::string& dir, const std::string& part)
{
    for (auto& e : std::filesystem::directory_iterator(dir))
        if (e.path().filename().string().find(part) != std::string::npos) return e.path().string();
    return {};
}
// the slot as the library holds it equals p + s
static bool slotIs(const LibrarySnapshot& snap, int n, const program_t& p, const seq_t& s)
{
    return snap.bank.used[n] && ProgramBank::same(snap.bank.program[n], snap.bank.seq[n], p, s);
}
static bool sameBank(const LibrarySnapshot& a, const LibrarySnapshot& b)
{
    for (int n = 0; n < (int)KC_PROGRAMS; n++) {
        if (a.bank.used[n] != b.bank.used[n]) return false;
        if (a.bank.used[n] && !ProgramBank::same(a.bank.program[n], a.bank.seq[n], b.bank.program[n], b.bank.seq[n]))
            return false;
    }
    return std::memcmp(a.globals, b.globals, sizeof a.globals) == 0;
}
static void packSlot(Library::Data& d, int n, const program_t& p, const seq_t& s)
{
    uint8_t pb[PROGRAM_PACKED_MAX], sb[SEQ_PACKED_MAX];
    program_t q = p;
    program_set_seq_gen(&q, 0);
    const int np = program_pack(&q, pb, sizeof pb), ns = seq_pack(&s, sb, sizeof sb);
    d.prog[n].assign(pb, pb + (np > 0 ? np : 0));
    d.seq[n].assign(sb, sb + (ns > 0 ? ns : 0));
}

static void library_roundtrip()
{
    const std::string dir = freshDir("roundtrip"), path = dir + "/library.knobi";
    // 500 slots + the globals, as a file
    Library::Data d;
    std::vector<program_t> ps(KC_PROGRAMS);
    std::vector<seq_t> ss(KC_PROGRAMS);
    for (int n = 0; n < (int)KC_PROGRAMS; n++) {
        char name[21];
        std::snprintf(name, sizeof name, "SLOT %03d", n);
        makeProgram(n + 1, ps[(size_t)n], ss[(size_t)n], name);
        packSlot(d, n, ps[(size_t)n], ss[(size_t)n]);
    }
    const int gv[] = {-17, 5, 3, 2, 1};           // TUNE, TRANSPOSE, VEL CURVE, AT CURVE, STEREO
    for (int i = 0; i < 5; i++) d.globals[(uint8_t)Library::kGlobals[i]] = (int8_t)gv[i];
    const auto bytes = Library::encode(d);
    writeFile(path, bytes);
    Library lib(path, 0);
    auto snap = lib.snapshot();
    int bad = 0;
    for (int n = 0; n < (int)KC_PROGRAMS; n++) bad += !slotIs(*snap, n, ps[(size_t)n], ss[(size_t)n]);
    for (int i = 0; i < 5; i++) bad += snap->globals[Library::kGlobals[i]] != gv[i];
    Library::Data back;
    const bool again = Library::decode(bytes.data(), bytes.size(), back) == Library::kDecOk && Library::encode(back) == bytes;
    std::printf("library_roundtrip: 500 slots + 5 globals, %zu bytes: %d wrong, unreadable %d, bad slots %d, encoded "
                "again the same %d\n", bytes.size(), bad, (int)snap->unreadable, snap->badSlots, (int)again);
    CHECK(bad == 0 && !snap->unreadable && snap->badSlots == 0 && again && bytes.size() > 100000);

    // through the API, from no file (the directory made on the first write)
    const std::string dir2 = freshDir("roundtrip2") + "/sub/KNOBI-SIX", path2 = dir2 + "/library.knobi";
    Library a(path2, 0);
    const bool empty0 = !std::filesystem::exists(path2) && !a.snapshot()->unreadable;
    const auto w1 = a.write(7, ps[7], ss[7]), w2 = a.write(499, ps[499], ss[499]), w3 = a.setGlobal(G_TUNE, -50),
               w4 = a.write(0, ps[0], ss[0]), w5 = a.clear(0), w6 = a.write(500, ps[0], ss[0]),
               w7 = a.setGlobal(G_MIDI_CH, 3);
    Library b(path2, 0);
    auto sa = a.snapshot(), sb = b.snapshot();
    int used = 0;
    for (int n = 0; n < (int)KC_PROGRAMS; n++) used += sb->bank.used[n];
    std::printf("library_roundtrip: API: no file at first %d; writes %d %d %d %d %d (0 ok), slot 500 %d, MIDI CH %d "
                "(1 refused); another library reads: slots 7 %d, 499 %d, 0 empty %d, %d used, TUNE %d; the same as the "
                "writer's %d\n", (int)empty0, w1, w2, w3, w4, w5, w6, w7, (int)slotIs(*sb, 7, ps[7], ss[7]),
                (int)slotIs(*sb, 499, ps[499], ss[499]), (int)!sb->bank.used[0], used, sb->globals[G_TUNE],
                (int)sameBank(*sa, *sb));
    CHECK(empty0 && w1 == Library::kOk && w2 == Library::kOk && w3 == Library::kOk && w4 == Library::kOk &&
          w5 == Library::kOk && w6 == Library::kInvalid && w7 == Library::kInvalid && slotIs(*sb, 7, ps[7], ss[7]) &&
          slotIs(*sb, 499, ps[499], ss[499]) && !sb->bank.used[0] && used == 2 && sb->globals[G_TUNE] == -50 &&
          sameBank(*sa, *sb));

    // a processor on that library: program 7 is the library's
    Processor p;
    p.attachLibrary(std::make_shared<Library>(path2, 0));
    p.setCurrentProgram(7);
    program_t got;
    seq_t gs;
    int32_t num = -1;
    kc_program_get(p.runner().core(), &num, &got, &gs, nullptr);
    const bool loaded = num == 7 && ProgramBank::same(got, gs, ps[7], ss[7]) && p.userSlot(7) && !p.userSlot(8) &&
                        p.getProgramName(7) == "007 SLOT 007" && p.global(G_TUNE) == -50;
    std::printf("library_roundtrip: a processor loads program 7 from the library: %d (%s); default path %s\n",
                (int)loaded, p.getProgramName(7).toRawUTF8(), Library::defaultPath().c_str());
    CHECK(loaded);
}

static std::string gSelf;                         // this program's path (main: argv[0])
// library_atomic's writer (test_plugin --child-atomic PATH): WRITEs slot 2 ("NEW ONE") and is killed (SIGKILL, raised
// by the Library's test hook) between the temp file's fsync and the rename; exit status 3: it was not killed
static int childAtomic(const char* path)
{
    program_t pb;
    seq_t sb;
    makeProgram(12, pb, sb, "NEW ONE");
    Library::beforeRename = [] { raise(SIGKILL); };
    Library lib(path, 0);
    lib.write(2, pb, sb);
    return 3;                                     // (not reached)
}

static void library_atomic()
{
    const std::string dir = freshDir("atomic"), path = dir + "/library.knobi";
    program_t pa, pb;
    seq_t sa, sb;
    makeProgram(11, pa, sa, "OLD ONE");
    makeProgram(12, pb, sb, "NEW ONE");
    {
        Library lib(path, 0);
        CHECK(lib.write(1, pa, sa) == Library::kOk);
    }
    const auto before = readFile(path);
    // the writer killed between the fsync and the rename: this program started afresh (posix_spawn, childAtomic), not
    // a fork (other tests' threads -- a library's watcher -- may run in this process); its own test hook
    // (Library::beforeRename) raises SIGKILL at that point
    const std::string pathArg = path;
    char* args[] = {(char*)gSelf.c_str(), (char*)"--child-atomic", (char*)pathArg.c_str(), nullptr};
    pid_t child = -1;
    const int spawned = posix_spawn(&child, gSelf.c_str(), nullptr, nullptr, args, environ);
    int st = 0;
    if (spawned == 0) waitpid(child, &st, 0);
    const bool killed = spawned == 0 && WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL;
    const auto after = readFile(path);
    const int temps = countFiles(dir, ".tmp-");
    Library lib(path, 0);
    auto snap = lib.snapshot();
    const bool old = slotIs(*snap, 1, pa, sa) && !snap->bank.used[2] && !snap->unreadable;
    const auto w = lib.write(3, pb, sb);
    const int tempsAfter = countFiles(dir, ".tmp-");
    Library again(path, 0);
    const bool both = slotIs(*again.snapshot(), 1, pa, sa) && slotIs(*again.snapshot(), 3, pb, sb);
    std::printf("library_atomic: writer killed before its rename %d; the file the same bytes %d (%zu), %d stale temp "
                "file(s); read: the old bank %d; the next WRITE %d (0 ok) removes them (%d left), both slots %d\n",
                (int)killed, (int)(after == before), after.size(), temps, (int)old, w, tempsAfter, (int)both);
    CHECK(killed && after == before && !before.empty() && temps == 1 && old && w == Library::kOk && tempsAfter == 0 && both);
}

// library_two_writers' other process (test_plugin --child-write PATH FIRST): writes slots FIRST..FIRST+19 with the
// programs the parent expects (makeProgram(n + 3), "W nnn"); exit status: the writes that failed
static int childWrite(const char* path, int first)
{
    Library c(path, 0);
    int f = 0;
    for (int n = first; n < first + 20; n++) {
        program_t p;
        seq_t q;
        char name[21];
        std::snprintf(name, sizeof name, "W %03d", n);
        makeProgram(n + 3, p, q, name);
        f += c.write(n, p, q) != Library::kOk;
    }
    return f;
}

static void library_two_writers()
{
    const std::string dir = freshDir("two"), path = dir + "/library.knobi";
    Library a(path, 5), b(path, 5);
    std::vector<program_t> ps(KC_PROGRAMS);
    std::vector<seq_t> ss(KC_PROGRAMS);
    for (int n = 0; n < (int)KC_PROGRAMS; n++) {
        char name[21];
        std::snprintf(name, sizeof name, "W %03d", n);
        makeProgram(n + 3, ps[(size_t)n], ss[(size_t)n], name);
    }
    std::atomic<int> failed{0};                   // (both writer threads count here)
    for (int r = 0; r < 40; r++) {                // different slots at once
        std::atomic<int> go{0};
        std::thread t1([&] { go++; while (go.load() < 2) {} failed += a.write(2 * r, ps[(size_t)(2 * r)], ss[(size_t)(2 * r)]) != Library::kOk; });
        std::thread t2([&] { go++; while (go.load() < 2) {} failed += b.write(2 * r + 1, ps[(size_t)(2 * r + 1)], ss[(size_t)(2 * r + 1)]) != Library::kOk; });
        t1.join(), t2.join();
    }
    program_t x, y;
    seq_t xs, ys;
    makeProgram(91, x, xs, "SAME A");
    makeProgram(92, y, ys, "SAME B");
    {                                             // the same slot at once: one of them
        std::atomic<int> go{0};
        std::thread t1([&] { go++; while (go.load() < 2) {} failed += a.write(499, x, xs) != Library::kOk; });
        std::thread t2([&] { go++; while (go.load() < 2) {} failed += b.write(499, y, ys) != Library::kOk; });
        t1.join(), t2.join();
    }
    // two processes at once (flock across processes): the child writes 300..319, this process 320..339. The child is
    // this program started afresh (posix_spawn, childWrite), not a fork: a, b's watcher threads run here meanwhile
    const std::string slots = "300", pathArg = path;
    char* args[] = {(char*)gSelf.c_str(), (char*)"--child-write", (char*)pathArg.c_str(), (char*)slots.c_str(), nullptr};
    pid_t child = -1;
    const int spawned = posix_spawn(&child, gSelf.c_str(), nullptr, nullptr, args, environ);
    for (int n = 320; n < 340; n++) failed += a.write(n, ps[(size_t)n], ss[(size_t)n]) != Library::kOk;
    int st = 0;
    if (spawned == 0) waitpid(child, &st, 0);
    const bool childOk = spawned == 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0;
    // the file's content, read fresh; both libraries end with it (their watchers reload)
    Library f(path, 0);
    auto file = f.snapshot();
    const bool settled = waitFor([&] { return sameBank(*a.snapshot(), *file) && sameBank(*b.snapshot(), *file); });
    int missing = 0;
    for (int n = 0; n < 80; n++) missing += !slotIs(*file, n, ps[(size_t)n], ss[(size_t)n]);
    for (int n = 300; n < 340; n++) missing += !slotIs(*file, n, ps[(size_t)n], ss[(size_t)n]);
    const bool one = slotIs(*file, 499, x, xs) != slotIs(*file, 499, y, ys);
    std::printf("library_two_writers: %d write(s) failed, the child's %d; %d of 120 writes missing from the file; "
                "slot 499 written by both at once: one of them %d (%s); both libraries the file's content %d\n", failed.load(),
                (int)childOk, missing, (int)one, file->bank.program[499].name, (int)settled);
    CHECK(failed == 0 && childOk && missing == 0 && one && settled);
}

static void library_corrupt()
{
    const std::string dir = freshDir("corrupt"), path = dir + "/library.knobi";
    program_t pa, pb;
    seq_t sa, sb;
    makeProgram(21, pa, sa, "GOOD");
    makeProgram(22, pb, sb, "AFTER");
    {
        Library lib(path, 0);
        lib.write(5, pa, sa);
        lib.setGlobal(G_TRANSPOSE, 7);
    }
    const auto good = readFile(path);
    struct Case { const char* what; std::vector<uint8_t> bytes; };
    std::vector<Case> cases;
    auto flip = good;
    flip[good.size() / 2] ^= 0x40;
    cases.push_back({"a CRC error", flip});
    auto magic = good;
    magic[0] = 'X';
    cases.push_back({"a bad magic", magic});
    cases.push_back({"a cut file", std::vector<uint8_t>(good.begin(), good.begin() + (long)good.size() / 3)});
    auto newer = good;                            // minReader 2 (and the CRC made right: a newer writer's file)
    newer[6] = 2;
    {
        uint32_t c = 0xFFFFFFFFu;
        for (size_t i = 0; i + 4 < newer.size(); i++) {
            c ^= newer[i];
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
        c = ~c;
        for (int k = 0; k < 4; k++) newer[newer.size() - 4 + (size_t)k] = (uint8_t)(c >> (8 * k));
    }
    cases.push_back({"a newer format", newer});
    int bad = 0;
    for (auto& c : cases) {
        writeFile(path, c.bytes);
        Library lib(path, 2);
        auto snap = lib.snapshot();
        int used = 0;
        for (int n = 0; n < (int)KC_PROGRAMS; n++) used += snap->bank.used[n];
        std::this_thread::sleep_for(std::chrono::milliseconds(30));   // (the watcher runs: it writes nothing)
        Processor p;
        p.attachLibrary(std::make_shared<Library>(path, 0));
        const bool ok = snap->unreadable && used == 0 && snap->globals[G_TRANSPOSE] == GLOB_DEF[G_TRANSPOSE] &&
                        readFile(path) == c.bytes && p.libraryUnreadable() && !p.userSlot(5) &&
                        countFiles(dir, ".bad-") == 0;
        std::printf("library_corrupt: %s: unreadable %d, %d slots used, the file untouched %d, the processor's notice "
                    "%d\n", c.what, (int)snap->unreadable, used, (int)(readFile(path) == c.bytes), (int)p.libraryUnreadable());
        bad += !ok;
    }
    // a newer version's file: a WRITE is refused, nothing written, no backup (its slots would be lost to it)
    {
        writeFile(path, newer);
        Library lib(path, 0);
        const auto w = lib.write(6, pb, sb);
        Processor p;
        p.attachLibrary(std::make_shared<Library>(path, 0));
        const bool ok = w == Library::kNewer && readFile(path) == newer && countFiles(dir, ".bad-") == 0 &&
                        lib.snapshot()->newer && p.libraryNewer() && p.writeProgram(6) == Library::kNewer &&
                        readFile(path) == newer;
        std::printf("library_corrupt: a newer format: WRITE %d (3 refused), the file untouched %d, no backup %d, "
                    "notices %d %d\n", w, (int)(readFile(path) == newer), (int)(countFiles(dir, ".bad-") == 0),
                    (int)lib.snapshot()->newer, (int)p.libraryNewer());
        CHECK(ok);
    }
    // a WRITE on the unreadable file: kept as library.knobi.bad-<time> first, then a good file
    writeFile(path, flip);
    Library lib(path, 0);
    const auto w = lib.write(6, pb, sb);
    const std::string backup = firstFile(dir, "library.knobi.bad-");
    Library after(path, 0);
    auto snap = after.snapshot();
    int used = 0;
    for (int n = 0; n < (int)KC_PROGRAMS; n++) used += snap->bank.used[n];
    const bool kept = !backup.empty() && readFile(backup) == flip;
    std::printf("library_corrupt: a WRITE: %d (0 ok); backup %s the old bytes %d; the new file readable %d, %d slot used "
                "(6: %d), the writer's notice cleared %d\n", w, backup.c_str(), (int)kept, (int)!snap->unreadable, used,
                (int)slotIs(*snap, 6, pb, sb), (int)!lib.snapshot()->unreadable);
    CHECK(bad == 0 && w == Library::kOk && kept && !snap->unreadable && used == 1 && slotIs(*snap, 6, pb, sb) &&
          !lib.snapshot()->unreadable);

    // one slot the firmware would not store (its sequence record broken), one good: that slot factory, counted
    Library::Data d;
    packSlot(d, 7, pa, sa);
    d.seq[7] = {65};                              // (65 steps: more than SEQ_MAX_STEPS)
    packSlot(d, 8, pb, sb);
    writeFile(path, Library::encode(d));
    Library one(path, 0);
    auto s1 = one.snapshot();
    const bool slotBad = !s1->unreadable && s1->badSlots == 1 && !s1->bank.used[7] && slotIs(*s1, 8, pb, sb);
    one.write(9, pa, sa);
    Library::Data back;
    const auto bytes = readFile(path);
    const bool keptBad = Library::decode(bytes.data(), bytes.size(), back) == Library::kDecOk && back.seq[7] == d.seq[7] &&
                         back.prog[7] == d.prog[7] && !back.prog[9].empty();
    // SLOT records this version does not use (a number past the bank, no program record): kept byte for byte
    Library::Data odd;
    packSlot(odd, 8, pb, sb);
    const std::vector<uint8_t> r600 = {'S', 'L', 'O', 'T', 7, 0, 0, 0, 0x58, 0x02, 1, 0, 0xAB, 0, 0},
                               r0 = {'S', 'L', 'O', 'T', 6, 0, 0, 0, 3, 0, 0, 0, 0, 0};
    odd.unknown.insert(odd.unknown.end(), r600.begin(), r600.end());
    odd.unknown.insert(odd.unknown.end(), r0.begin(), r0.end());
    writeFile(path, Library::encode(odd));
    {
        Library l(path, 0);
        l.write(9, pa, sa);
    }
    Library::Data oddBack;
    const auto ob = readFile(path);
    const auto has = [&](const std::vector<uint8_t>& r) {
        return std::search(oddBack.unknown.begin(), oddBack.unknown.end(), r.begin(), r.end()) != oddBack.unknown.end();
    };
    const bool oddKept = Library::decode(ob.data(), ob.size(), oddBack) == Library::kDecOk && has(r600) && has(r0) &&
                         !oddBack.prog[8].empty() && !oddBack.prog[9].empty();
    std::printf("library_corrupt: SLOT 600 and a SLOT with no program record kept through a WRITE: %d\n", (int)oddKept);
    CHECK(oddKept);
    writeFile(path, Library::encode(d));
    Library one2(path, 0);
    one2.write(9, pa, sa);
    Processor p;
    p.attachLibrary(std::make_shared<Library>(path, 0));
    std::printf("library_corrupt: a slot with a broken sequence: readable %d, bad slots %d, slot 7 factory %d, slot 8 "
                "loaded %d; its bytes kept through a WRITE of slot 9 %d; the processor's count %d\n",
                (int)!s1->unreadable, s1->badSlots, (int)!s1->bank.used[7], (int)slotIs(*s1, 8, pb, sb), (int)keptBad,
                p.libraryBadSlots());
    CHECK(slotBad && keptBad && p.libraryBadSlots() == 1);
}

static void reload_off_audio_thread()
{
    const std::string dir = freshDir("reload"), path = dir + "/library.knobi";
    program_t px;
    seq_t sx;
    makeProgram(31, px, sx, "RELOADED");
    auto lib = std::make_shared<Library>(path, 2);
    Processor p;
    p.attachLibrary(lib);
    p.setPlayConfigDetails(0, 2, 44100, 256);
    p.prepareToPlay(44100, 256);
    const auto firstSnap = lib->snapshot();
    std::atomic<bool> stop{false}, sendPc{false};
    std::atomic<long> allocs{0}, locks{0}, blocksDone{0};
    std::thread::id audioId;
    std::thread audio([&] {
        audioId = std::this_thread::get_id();
        juce::AudioBuffer<float> buf(2, 256);
        juce::MidiBuffer midi, pc;
        pc.addEvent(juce::MidiMessage::programChange(1, 7), 0);
        midi.ensureSize(64);
        bool sent = false;
        for (int w = 0; w < 4; w++) p.processBlock(buf, midi);   // (a new thread's first calls: its thread-locals)
        while (!stop.load()) {
            midi.clear();
            const bool now = !sent && sendPc.load();
            const long a0 = alloc_hook_allocs(), l0 = alloc_hook_locks();
            alloc_hook_arm_thread(1);
            p.processBlock(buf, now ? pc : midi);
            alloc_hook_arm_thread(0);
            allocs += alloc_hook_allocs() - a0, locks += alloc_hook_locks() - l0;
            sent = sent || now;
            blocksDone++;
            std::this_thread::sleep_for(std::chrono::microseconds(300));
        }
    });
    waitFor([&] { return blocksDone.load() > 20; });
    {
        Library other(path, 0);                   // (another instance / process WRITEs)
        CHECK(other.write(7, px, sx) == Library::kOk);
    }
    const bool reloaded = waitFor([&] { return lib->snapshot()->bank.used[7] != 0; });
    const auto readerId = lib->lastReloadThread();
    // the processor got the new bank (the watcher's thread gave it): the host's list, then a MIDI program change
    const bool listed = waitFor([&] { return p.userSlot(7); });
    sendPc = true;
    const long b0 = blocksDone.load();
    waitFor([&] { return blocksDone.load() > b0 + 20; });
    program_t got;
    seq_t gs;
    int32_t num = -1;
    struct Ctx { kc_t* k; int32_t* num; program_t* p; seq_t* s; } c{p.runner().core(), &num, &got, &gs};
    p.access().run([](void* v) { auto* x = (Ctx*)v; kc_program_get(x->k, x->num, x->p, x->s, nullptr); }, &c);
    const bool played = num == 7 && ProgramBank::same(got, gs, px, sx);
    // the release: the old snapshot (only this test and the processor hold it) freed once the instance moved on
    std::weak_ptr<const LibrarySnapshot> old = firstSnap;
    const long firstUses = firstSnap.use_count();
    const_cast<std::shared_ptr<const LibrarySnapshot>&>(firstSnap).reset();
    p.collectBanks();
    const bool freed = old.expired() && p.heldBanks() == 1;
    stop = true;
    audio.join();
    std::printf("library_reload_off_audio_thread: reloaded %d on the watcher thread %d (not the audio thread %d, not "
                "this one %d); the processor's bank %d; program change 7 on the audio thread: %d (%s); audio thread "
                "%ld allocation(s), %ld lock(s) in %ld blocks; old snapshot held by %ld then freed %d (held %zu)\n",
                (int)reloaded, (int)(readerId != std::thread::id()), (int)(readerId != audioId),
                (int)(readerId != std::this_thread::get_id()), (int)listed, (int)played, got.name, allocs.load(),
                locks.load(), blocksDone.load(), firstUses, (int)freed, p.heldBanks());
    CHECK(reloaded && readerId != std::thread::id() && readerId != audioId && readerId != std::this_thread::get_id() &&
          listed && played && allocs.load() == 0 && locks.load() == 0 && freed);
}

static void library_snapshot_consistent()
{
    const std::string dir = freshDir("consistent"), path = dir + "/library.knobi";
    program_t p3, p7;
    seq_t s3, s7;
    makeProgram(41, p3, s3, "THREE");
    makeProgram(42, p7, s7, "SEVEN");
    auto lib = std::make_shared<Library>(path, 0);
    lib->write(3, p3, s3);
    lib->write(7, p7, s7);
    Processor p;
    p.attachLibrary(lib);
    p.setPlayConfigDetails(0, 2, 44100, 256);
    p.prepareToPlay(44100, 256);
    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer none;
    p.processBlock(buf, none);                    // (the engine takes kc_create's load: free again)
    p.setCurrentProgram(3);                       // (a load taken at once: the engine holds it until the next block)
    juce::MidiBuffer pc;
    pc.addEvent(juce::MidiMessage::programChange(1, 7), 0);
    p.processBlock(buf, pc);                      // (the program change finds the engine busy: deferred)
    int32_t num = -1;
    program_t edit;
    kc_program_get(p.runner().core(), &num, &edit, nullptr, nullptr);
    const int pending = kc_program_pending(p.runner().core());
    const bool window = pending && num == 7 && std::strcmp(edit.name, "THREE") == 0;   // (the core's window)
    // the set state now: number 7 with SEVEN
    juce::MemoryBlock mb;
    p.getStateInformation(mb);
    SetState st;
    const bool stateOk = StateCodec::decode(mb.getData(), mb.getSize(), st) == StateCodec::kOk && st.number == 7 &&
                         ProgramBank::same(st.program, st.seq, p7, s7) && !st.edited && !st.differs;
    // WRITE now (to its own number 7): the library's slot 7 is SEVEN, not THREE
    const auto w = p.writeProgram();
    auto snap = lib->snapshot();
    const bool writeOk = w == Library::kOk && slotIs(*snap, 7, p7, s7);
    std::printf("library_snapshot_consistent: the core's window %d (pending %d, number %d, edit buffer %s); set state "
                "number %d %s %d; WRITE %d: slot 7 %s %d\n", (int)window, pending, num, edit.name, st.number,
                st.program.name, (int)stateOk, w, snap->bank.program[7].name, (int)writeOk);
    CHECK(window && stateOk && writeOk);
}

static void library_write_restore()
{
    const std::string dir = freshDir("write"), path = dir + "/library.knobi";
    Processor a, b;
    a.attachLibrary(std::make_shared<Library>(path, 2));
    b.attachLibrary(std::make_shared<Library>(path, 2));
    program_t pe;
    seq_t se;
    makeProgram(51, pe, se, "EDITED");
    kc_program_set(a.runner().core(), 12, &pe, &se, 1);   // (an edited program in play, number 12)
    int ed = -1;
    const auto w = a.writeProgram();
    int32_t num = -1;
    kc_program_get(a.runner().core(), &num, nullptr, nullptr, &ed);
    const bool written = num == 12 && ed == 0;
    const bool inFile = slotIs(*Library(path, 0).snapshot(), 12, pe, se);
    const bool seen = waitFor([&] { return b.userSlot(12); });
    program_t bp;
    seq_t bs;
    const bool bHas = b.bank().program(12, &bp, &bs) && ProgramBank::same(bp, bs, pe, se);
    HostSpy spy;                                  // (the host told the program names changed: syncHost)
    b.addListener(&spy);
    b.syncHost();
    const int info = spy.infoChanged.load();
    b.removeListener(&spy);
    std::printf("library_write_restore: the other processor tells its host the names changed: %d\n", info);
    CHECK(info == 1);
    std::printf("library_write_restore: WRITE %d (0 ok): in the file %d, the other processor %d %d; after: number %d, "
                "edited %d\n", w, (int)inFile, (int)seen, (int)bHas, num, ed);
    // WRITE to another slot: the instance plays that number
    const auto w2 = a.writeProgram(250);
    kc_program_get(a.runner().core(), &num, nullptr, nullptr, &ed);
    const bool moved = w2 == Library::kOk && num == 250 && ed == 0 && slotIs(*Library(path, 0).snapshot(), 250, pe, se);
    // RESTORE FACTORY on 250: the factory program in the edit buffer, edited; nothing stored
    program_t fp, cur;
    seq_t fs, cs;
    a.bank().factory(250, &fp, &fs);
    const bool r = a.restoreFactory();
    kc_program_get(a.runner().core(), &num, &cur, &cs, &ed);
    const bool restored = r && num == 250 && ed == 1 && ProgramBank::same(cur, cs, fp, fs) &&
                          slotIs(*Library(path, 0).snapshot(), 250, pe, se);
    // WRITE after it: the slot emptied (factory); the other processor follows
    const auto w3 = a.writeProgram();
    const bool emptied = w3 == Library::kOk && !Library(path, 0).snapshot()->bank.used[250] && !a.userSlot(250) &&
                         waitFor([&] { return !b.userSlot(250); });
    // RESTORE FACTORY, then an edit, then WRITE: the edit is stored (not emptied)
    a.setCurrentProgram(12);
    a.restoreFactory();
    kc_param_set(a.runner().core(), P_FLT_FREQ, 3);
    const auto w4 = a.writeProgram();
    auto s4 = Library(path, 0).snapshot();
    const bool editKept = w4 == Library::kOk && s4->bank.used[12] && s4->bank.program[12].v[P_FLT_FREQ] == 3;
    const bool browser = Processor::bankOf(437) == 4 && Processor::tensOf(437) == 3 && Processor::onesOf(437) == 7 &&
                         Processor::programOf(4, 3, 7) == 437 && Processor::programOf(0, 0, 9) == 9;
    std::printf("library_write_restore: WRITE to 250 %d; RESTORE FACTORY %d (edited, nothing stored); WRITE after it "
                "empties the slot %d (the other follows); restore + edit + WRITE stores the edit %d; browser %d\n",
                (int)moved, (int)restored, (int)emptied, (int)editKept, (int)browser);
    CHECK(w == Library::kOk && written && inFile && seen && bHas && moved && restored && emptied && editKept && browser);
    kc_program_get(a.runner().core(), &num, nullptr, nullptr, &ed);
    CHECK(num == 12 && ed == 0);
}

static void library_globals()
{
    const std::string dir = freshDir("globals"), path = dir + "/library.knobi";
    Processor a, b;
    a.attachLibrary(std::make_shared<Library>(path, 2));
    b.attachLibrary(std::make_shared<Library>(path, 2));
    kc_global_set(b.runner().core(), G_MIDI_CH, 5);   // (a stray channel: the library's snapshot puts omni back)
    const auto r1 = a.setGlobal(G_TUNE, 25), r2 = a.setGlobal(G_STEREO, 1), r3 = a.setGlobal(G_VEL_CURVE, 7),
               r4 = a.setGlobal(G_MIDI_CH, 4), r5 = a.setGlobal(G_TUNE, 51);
    const bool here = a.global(G_TUNE) == 25 && a.global(G_STEREO) == 1 && a.global(G_VEL_CURVE) == 7;
    const bool there = waitFor([&] {
        return b.global(G_TUNE) == 25 && b.global(G_STEREO) == 1 && b.global(G_VEL_CURVE) == 7;
    });
    auto f = Library(path, 0).snapshot();
    const bool file = f->globals[G_TUNE] == 25 && f->globals[G_STEREO] == 1 && f->globals[G_VEL_CURVE] == 7;
    std::printf("library_globals: set %d %d %d (0 ok), MIDI CH %d / TUNE 51 %d (1 refused); this processor %d, the other "
                "%d, the file %d; MIDI CH omni in both %d %d\n", r1, r2, r3, r4, r5, (int)here, (int)there, (int)file,
                a.global(G_MIDI_CH), b.global(G_MIDI_CH));
    CHECK(r1 == Library::kOk && r2 == Library::kOk && r3 == Library::kOk && r4 == Library::kInvalid &&
          r5 == Library::kInvalid && here && there && file && a.global(G_MIDI_CH) == 0 && b.global(G_MIDI_CH) == 0);
}


// WRITE as the keyboard's: no load. A sequence playing with Live: the output with a WRITE in the middle equals the
// output without it, sample for sample; then number / edited / differs right. An edit arriving while the file is
// written stays (and leaves the program edited)
static void library_write_plays_on()
{
    const std::string da = freshDir("plays_a"), db = freshDir("plays_b");
    Processor a, b;
    a.attachLibrary(std::make_shared<Library>(da + "/library.knobi", 0));
    b.attachLibrary(std::make_shared<Library>(db + "/library.knobi", 0));
    program_t pr;
    seq_t sq;
    makeProgram(61, pr, sq, "PLAYING");
    pr.v[P_ARP_ON] = 0;
    Host ha(a, 44100, 512), hb(b, 44100, 512);
    for (Processor* p : {&a, &b}) kc_program_set(p->runner().core(), 20, &pr, &sq, 0);
    ha.run(512 * 4), hb.run(512 * 4);
    for (Processor* p : {&a, &b}) kc_param_set(p->runner().core(), P_FLT_FREQ, 40);   // (edited)
    for (Processor* p : {&a, &b}) kc_seq_play(p->runner().core(), 1);
    ha.head.playing = hb.head.playing = true;
    ha.run(512 * 60), hb.run(512 * 60);
    const bool editedBefore = a.edited();
    kc_note_t nt[KC_LOG];
    kc_notes(a.runner().core(), nt, KC_LOG);
    const auto w = a.writeProgram(33);
    const int32_t numAfter = a.getCurrentProgram();
    const bool flags = w == Library::kOk && numAfter == 33 && !a.edited() && !a.differsFromBank() && a.userSlot(33) &&
                       kc_seq_state(a.runner().core()) == 1;
    ha.run(512 * 120), hb.run(512 * 120);
    long diff = 0;
    for (size_t i = 0; i < ha.out.size() && i < hb.out.size(); i++) diff += ha.out[i] != hb.out[i];
    const uint32_t m = kc_notes(a.runner().core(), nt, KC_LOG);
    uint32_t ons = 0;
    for (uint32_t i = 0; i < m; i++) ons += nt[i].vel != 0;
    std::printf("library_write_plays_on: edited before %d; WRITE to 33 while the sequence plays: %d (0 ok), number %d, "
                "edited %d, differs %d, user slot %d, sequencer state %d; output vs no WRITE: %ld samples differ of %zu; "
                "%u note-ons after\n", (int)editedBefore, w, numAfter, (int)a.edited(), (int)a.differsFromBank(),
                (int)a.userSlot(33), kc_seq_state(a.runner().core()), diff, ha.out.size(), ons);
    CHECK(editedBefore && flags && diff == 0 && ha.out.size() == hb.out.size() && ons > 0 && kc_seq_state(a.runner().core()) == 1);

    // an edit (a CC's value) arriving after the snapshot, before the file is written: stays, the program edited
    Processor::testHooks.afterSnapshot = [](Processor& p) { kc_param_set(p.runner().core(), P_FLT_FREQ, 99); };
    kc_param_set(a.runner().core(), P_FLT_FREQ, 41);
    const auto w2 = a.writeProgram();
    Processor::testHooks.afterSnapshot = nullptr;
    auto snap = Library(da + "/library.knobi", 0).snapshot();
    const bool kept = w2 == Library::kOk && snap->bank.program[33].v[P_FLT_FREQ] == 41 &&
                      kc_param_get(a.runner().core(), P_FLT_FREQ) == 99 && a.edited() && a.getCurrentProgram() == 33 &&
                      a.differsFromBank();
    std::printf("library_write_plays_on: an edit during the WRITE: stored %d (41), in play %d (99), edited %d, differs "
                "%d\n", snap->bank.program[33].v[P_FLT_FREQ], kc_param_get(a.runner().core(), P_FLT_FREQ),
                (int)a.edited(), (int)a.differsFromBank());
    CHECK(kept);

    // a load between the snapshot and the mark (fix round 2): never renamed to the written slot. A MIDI program change
    // the audio thread applies meanwhile, then a host program load (kc_program_set) meanwhile: kStoredOnly, the
    // instance keeps that load's number and content (not edited), the file has what the snapshot read
    Processor::testHooks.afterSnapshot = [](Processor& p) {
        juce::AudioBuffer<float> buf(2, 512);
        juce::MidiBuffer pc;
        pc.addEvent(juce::MidiMessage::programChange(1, 5), 0);
        p.processBlock(buf, pc);
        juce::MidiBuffer none;
        p.processBlock(buf, none), p.processBlock(buf, none);
    };
    kc_param_set(a.runner().core(), P_FLT_FREQ, 42);
    const auto w3 = a.writeProgram(34);
    const int n3 = a.getCurrentProgram();
    const bool e3 = a.edited();
    auto s3 = Library(da + "/library.knobi", 0).snapshot();
    Processor::testHooks.afterSnapshot = [](Processor& p) { p.setCurrentProgram(7); };
    const auto w4 = a.writeProgram(35);
    Processor::testHooks.afterSnapshot = nullptr;
    const int n4 = a.getCurrentProgram();
    const bool e4 = a.edited();
    auto s4 = Library(da + "/library.knobi", 0).snapshot();
    std::printf("library_write_plays_on: a MIDI program change during the WRITE: %d (4 stored only), number %d (5), "
                "edited %d, slot 34 stored %d (42); a host program load during it: %d, number %d (7), edited %d, slot "
                "35 stored %d\n", w3, n3, (int)e3, s3->bank.program[34].v[P_FLT_FREQ], w4, n4, (int)e4,
                (int)s4->bank.used[35]);
    CHECK(w3 == Library::kStoredOnly && n3 == 5 && !e3 && s3->bank.used[34] && s3->bank.program[34].v[P_FLT_FREQ] == 42 &&
          w4 == Library::kStoredOnly && n4 == 7 && !e4 && s4->bank.used[35]);
}

// two snapshots' listeners at once (fix round 2): the older one's globals, applied last, must not win. Snapshot 100
// taken, and before its globals are applied snapshot 101 is taken and applied (the hook): TUNE stays 101's
static std::shared_ptr<const LibrarySnapshot> gLater;
static void library_globals_order()
{
    const std::string dir = freshDir("globals_order");
    Processor p;
    p.attachLibrary(std::make_shared<Library>(dir + "/library.knobi", 0));
    auto base = *Library(dir + "/library.knobi", 0).snapshot();
    auto older = std::make_shared<LibrarySnapshot>(base), later = std::make_shared<LibrarySnapshot>(base);
    older->generation = 100, older->globals[G_TUNE] = 10, older->globals[G_STEREO] = 1;
    later->generation = 101, later->globals[G_TUNE] = 20, later->globals[G_STEREO] = 0;
    gLater = later;
    Processor::testHooks.beforeGlobals = [](Processor& q, uint64_t gen) {
        if (gen == 100) q.deliverLibrarySnapshot(gLater);   // (101 taken and applied while 100 waits)
    };
    p.deliverLibrarySnapshot(older);
    Processor::testHooks.beforeGlobals = nullptr;
    const int tune = p.global(G_TUNE), stereo = p.global(G_STEREO);
    auto next = std::make_shared<LibrarySnapshot>(base);
    next->generation = 102, next->globals[G_TUNE] = 30;
    p.deliverLibrarySnapshot(next);
    p.deliverLibrarySnapshot(older);              // (an old one late: dropped)
    const int tune2 = p.global(G_TUNE);
    gLater.reset();
    std::printf("library_globals_order: 100 then 101 inside it: TUNE %d (20), STEREO %d (0); then 102 and a late 100: "
                "TUNE %d (30)\n", tune, stereo, tune2);
    CHECK(tune == 20 && stereo == 0 && tune2 == 30);
}

// a library path that is a symbolic link stays one: the file it names is written; listeners are told with the
// file's lock released
static void library_symlink()
{
    namespace fs = std::filesystem;
    const std::string real = freshDir("link_real"), links = freshDir("link_here");
    const std::string target = real + "/library.knobi", link = links + "/library.knobi";
    std::error_code ec;
    fs::create_symlink(target, link, ec);         // (the target does not exist yet)
    program_t p1, p2;
    seq_t s1, s2;
    makeProgram(71, p1, s1, "VIA LINK");
    makeProgram(72, p2, s2, "AGAIN");
    Library lib(link, 0);
    std::atomic<int> lockFree{-1};
    lib.addListener([&](const std::shared_ptr<const LibrarySnapshot>&) {
        const int fd = ::open((target + ".lock").c_str(), O_RDWR);
        lockFree = fd >= 0 && ::flock(fd, LOCK_EX | LOCK_NB) == 0;
        if (fd >= 0) ::flock(fd, LOCK_UN), ::close(fd);
    });
    const auto w1 = lib.write(1, p1, s1), w2 = lib.write(2, p2, s2);
    auto t = Library(target, 0).snapshot();
    const bool ok = w1 == Library::kOk && w2 == Library::kOk && fs::is_symlink(fs::symlink_status(link)) &&
                    fs::is_regular_file(fs::symlink_status(target)) && slotIs(*t, 1, p1, s1) && slotIs(*t, 2, p2, s2) &&
                    slotIs(*Library(link, 0).snapshot(), 2, p2, s2) && countFiles(links, ".lock") == 0;
    std::printf("library_symlink: writes %d %d through a link: still a link %d, the target a file with both slots %d; "
                "listener ran with the lock free %d\n", w1, w2, (int)fs::is_symlink(fs::symlink_status(link)),
                (int)(slotIs(*t, 1, p1, s1) && slotIs(*t, 2, p2, s2)), lockFree.load());
    CHECK(ok && lockFree.load() == 1);
}

static void factory_gating()
{
    ProgramBank bank;
    program_t init, p;
    seq_t s;
    program_init(&init);
    int notInit = 0, unread = 0;
    for (int n = 0; n < (int)KC_PROGRAMS; n++) {
        if (!bank.factory(n, &p, &s)) { unread++; continue; }
        seq_t empty;
        seq_clear(&empty);
        notInit += !ProgramBank::same(p, s, init, empty);
    }
    const std::string name = JucePlugin_Name, au = std::filesystem::path(KNOBI_AU_BUNDLE).filename().string(),
                      vst3 = std::filesystem::path(KNOBI_VST3_BUNDLE).filename().string();
#if KNOBI_PLUGIN_LOCAL
    const bool ok = name == "KNOBI-SIX-local" && au == "KNOBI-SIX-local.component" && vst3 == "KNOBI-SIX-local.vst3" &&
                    notInit > 0;
    const char* build = "KNOBI_FACTORY_LOCAL=ON";
#else
    const bool ok = name == "KNOBI-SIX" && au == "KNOBI-SIX.component" && vst3 == "KNOBI-SIX.vst3" && notInit == 0;
    const char* build = "default";
#endif
    std::printf("factory_gating: %s build: %s (%s, %s); factory programs not the init program: %d of 500 (%d unread)\n",
                build, name.c_str(), au.c_str(), vst3.c_str(), notInit, unread);
    CHECK(ok && unread == 0);
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc == 4 && std::strcmp(argv[1], "--child-write") == 0) return childWrite(argv[2], std::atoi(argv[3]));
    if (argc == 3 && std::strcmp(argv[1], "--child-atomic") == 0) return childAtomic(argv[2]);
    gSelf = std::filesystem::absolute(argv[0]).string();
    juce::ScopedJuceInitialiser_GUI juce;           // (JUCE's statics; no window is opened)
    Processor::startHostSyncTimer = false;              // (no JUCE timer thread in this process: the tests call syncHost)
    // every library file in a temporary directory, the processors' default one too: never ~/Music
    const std::string realDefault = Library::defaultPath();
    gTmp = (std::filesystem::temp_directory_path() / ("knobi-six-tests-" + std::to_string((long)getpid()))).string();
    std::filesystem::create_directories(gTmp);
    Library::setDefaultPath(gTmp + "/default/library.knobi");
    const bool defaultOk = realDefault.size() > 30 &&
                           realDefault.compare(realDefault.size() - 30, 30, "/Music/KNOBI-SIX/library.knobi") == 0;
    if (!defaultOk) std::printf("FAIL the default library path %s\n", realDefault.c_str()), fails++;
    std::map<std::string, std::function<void()>> tests = {
        {"render_44k_bitexact", render_44k_bitexact}, {"render_48k", render_48k},
        {"midi_sample_accurate", midi_sample_accurate}, {"rt_no_alloc", rt_no_alloc}, {"seam", seam},
        {"silent_block", silent_block}, {"prepare_keeps_pending", prepare_keeps_pending}, {"events_ordered", events_ordered},
        {"params_layout", params_layout}, {"automation_to_core", automation_to_core}, {"core_to_host", core_to_host},
        {"state_roundtrip", state_roundtrip}, {"state_bank_changed", state_bank_changed},
        {"state_versioned", state_versioned}, {"rt_params", rt_params},
        {"state_timeout_keeps_last", state_timeout_keeps_last}, {"seam_idle_clamped", seam_idle_clamped},
        {"prepare_takes_instance", prepare_takes_instance}, {"transport_none", transport_none},
        {"transport_vanishes", transport_vanishes}, {"offline_no_silent", offline_no_silent},
        {"offline_run_waits", offline_run_waits},
        {"transport_start_bar", transport_start_bar}, {"transport_aligned", transport_aligned},
        {"transport_loop", transport_loop}, {"transport_loop_tempo", transport_loop_tempo},
        {"transport_locate", transport_locate},
        {"transport_tempo_ramp", transport_tempo_ramp}, {"transport_stop_internal", transport_stop_internal},
        {"bpm_param_greyed", bpm_param_greyed},
        {"library_roundtrip", library_roundtrip}, {"library_atomic", library_atomic},
        {"library_two_writers", library_two_writers}, {"library_corrupt", library_corrupt},
        {"reload_off_audio_thread", reload_off_audio_thread}, {"library_snapshot_consistent", library_snapshot_consistent},
        {"library_write_restore", library_write_restore}, {"library_globals", library_globals},
        {"factory_gating", factory_gating}, {"library_write_plays_on", library_write_plays_on},
        {"library_symlink", library_symlink}, {"library_globals_order", library_globals_order},
    };
    if (argc > 1) {
        auto t = tests.find(argv[1]);
        if (t == tests.end()) { std::printf("no test %s\n", argv[1]); return 2; }
        t->second();
    } else
        for (auto& t : tests) t.second();
    std::error_code ec;
    std::filesystem::remove_all(gTmp, ec);
    if (fails) { std::printf("test_plugin: %d FAILED\n", fails); return 1; }
    std::printf("test_plugin: passed\n");
    return 0;
}
