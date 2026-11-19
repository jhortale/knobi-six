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
//                         served from another thread (CoreAccess): zero allocations, zero locks
//   seam                  CoreAccess: with no audio running, run() takes the instance itself; with an audio thread
//                         rendering, run() is served by it between blocks; never two at once (the processor's too)
// Fix round 1:
//   silent_block          blocks of 2048 at 44.1 kHz (46 ms apart, paced like a host): run() calls are served by the
//                         audio thread (the idle threshold follows the block period: 3 blocks), no block is silent;
//                         then a collision forced (a call running on the asking thread after a real idle while the
//                         host resumes): that block is faded out from the last output (no hard cut), its note-off
//                         and note-on replayed at the start of the next block that enters (60 released, 67 sounds:
//                         a control without the 67 is silent by then), no note stuck, the timeline kept
//   prepare_keeps_pending a note-off still waiting for its frame when the host re-prepares (rate / block change) is
//                         given to the core, not lost: no stuck note
//   events_ordered        parameter changes (paramAt, queued before the block) and MIDI at offsets in between act in
//                         offset order: the same samples as the core driven directly in that order
// rt_no_alloc (and silent_block, for the silent path and its replay) covers processBlock as this file calls it (the
// processor's own code, CoreRunner, the core, CoreAccess's audio side): allocations (malloc family, typed and zone allocators, operator new) and pthread
// mutexes / rwlocks / os_unfair_lock. Not covered: what the plugin wrappers (JUCE's AU / VST3 glue) do around
// processBlock in a real host, condition variables, semaphores and dispatch calls (not counted).
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "Processor.h"
#include "Resampler.h"
#include "alloc_hook.h"

using namespace knobi;

static int fails;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

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
        const int block = 256;
        p.setPlayConfigDetails(0, 2, rate, block);
        p.prepareToPlay(rate, block);
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
        std::atomic<bool> posted{false}, served{false};
        std::thread asker;
        long allocs = 0, locks = 0;
        for (int b = 0; b < 400; b++) {
            midi.clear();
            midi.addEvents(blocks[(size_t)b], 0, -1, 0);   // (outside the counted part)
            int n = b == 200 ? block * 3 : block;
            buf.setSize(2, n, false, false, true);
            if (b == 100) {                           // (the asker's thread made outside the counted part)
                asker = std::thread([&] { posted = true; served = p.access().run(fn, &snap); });
                while (!posted) std::this_thread::yield();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            long a = alloc_hook_allocs(), l = alloc_hook_locks();
            alloc_hook_arm(1);
            p.processBlock(buf, midi);
            alloc_hook_arm(0);
            allocs += alloc_hook_allocs() - a, locks += alloc_hook_locks() - l;
            if (b == 100) { asker.join(); }
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
    // an audio thread rendering: served by it, never at the same time as its block
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
    // 1. a host with long blocks, paced in real time; calls from another thread meanwhile: all served by the audio thread
    std::atomic<bool> stop{false};
    std::atomic<int> served{0};
    struct Snap { kc_t* k; int32_t num; } snap{p.runner().core(), -1};
    auto get = [](void* c) { auto* s = (Snap*)c; kc_program_get(s->k, &s->num, nullptr, nullptr, nullptr); };
    std::thread asker([&] { for (int i = 0; i < 6; i++) served += p.access().run(get, &snap); stop = true; });
    int blocks = 0;
    while (!stop && blocks < 200) { host({}); blocks++; std::this_thread::sleep_for(period); }
    asker.join();
    uint64_t here1 = p.access().ranHere(), silent1 = p.silentBlocks();
    std::printf("silent_block: blocks of %d at %.0f Hz paced %lld ms: %d / 6 calls served, %llu run on the asking "
                "thread, %llu silent blocks\n", block, rate, (long long)(period.count() / 1000), served.load(),
                (unsigned long long)here1, (unsigned long long)silent1);
    CHECK(served == 6 && here1 == 0 && silent1 == 0);
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
    while (!holding) std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
    CHECK(silentAllocs == 0 && silentLocks == 0 && silent2 - silent1 == 1 && std::fabs(last) > 1e-4f && jump < std::fabs(last) * 0.1f + 1e-4f && tailSilent &&
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

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI juce;           // (JUCE's statics; no window is opened)
    std::map<std::string, std::function<void()>> tests = {
        {"render_44k_bitexact", render_44k_bitexact}, {"render_48k", render_48k},
        {"midi_sample_accurate", midi_sample_accurate}, {"rt_no_alloc", rt_no_alloc}, {"seam", seam},
        {"silent_block", silent_block}, {"prepare_keeps_pending", prepare_keeps_pending}, {"events_ordered", events_ordered},
    };
    if (argc > 1) {
        auto t = tests.find(argv[1]);
        if (t == tests.end()) { std::printf("no test %s\n", argv[1]); return 2; }
        t->second();
    } else
        for (auto& t : tests) t.second();
    if (fails) { std::printf("test_plugin: %d FAILED\n", fails); return 1; }
    std::printf("test_plugin: passed\n");
    return 0;
}
