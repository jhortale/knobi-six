// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin: one core instance (firmware/src/core/knobi_core.h) driven at the session's rate. No JUCE.
//
// Time. The core renders at 44.1 kHz in the firmware's 256-frame blocks. Session sample s is at internal (44.1 kHz)
// time s x 44100 / rate; an event at session sample s acts at internal frame e(s) = ceil(s x 44100 / rate) (at
// 44.1 kHz: s). After a host block ending at session sample S, the core has rendered every whole block before
// floor(e_end / 256) x 256 (e_end = floor(S x 44100 / rate)): all events before there are known, so the blocks are the
// firmware's (256 frames, on its grid) split only at event frames (kc_render_exact), whatever the host's block size.
// The output is that stream delayed by latency() session samples, exactly:
//   44.1 kHz: y[s] = x[s - 256]                         latency 256 (5.8 ms; the block the core may be ahead)
//   else:     y[s] = x(t), t = (s - D) x 44100 / rate    D = ceil((KC_BLOCK + 1 + Resampler::kHalf) x rate / 44100)
//             (x(t) by the resampler, centred, no delay of its own): 48 kHz 332, 88.2 kHz 610, 96 kHz 664 (6.9 ms)
// so the output does not depend on the host's block sizes, and an event at s sounds at s + latency() (+ less than one
// internal frame at other rates, the 44.1 kHz grid).
//
// Real time: prepare() allocates (not on the audio thread); process() allocates nothing, takes no lock, does no I/O.
// Threads: process() and every use of core() on one thread at a time (plugin/src/CoreAccess.h: the seam).
#pragma once
#include <cstdint>
#include <vector>
#include "Resampler.h"
#include "core/knobi_core.h"

namespace knobi {

struct MidiEvent {                                // a host block's MIDI message at its sample offset
    uint32_t offset;
    uint8_t len;                                  // 1..3 (others are not given to the core)
    uint8_t data[3];
};

class CoreRunner {
public:
    static constexpr uint32_t kQueue = 4096;      // events waiting for their frame (beyond: dropped, counted)

    CoreRunner();
    ~CoreRunner();
    CoreRunner(const CoreRunner&) = delete;
    CoreRunner& operator=(const CoreRunner&) = delete;

    bool ok() const { return core_ != nullptr; }
    kc_t* core() { return core_; }

    // the session's rate (Hz, rounded to an integer) and the most frames a process() call will be given (more is
    // taken in parts). Resets the timeline (the sound's state stays: the core is not recreated). Allocates
    bool prepare(double sampleRate, int maxBlock);
    int latency() const { return latency_; }      // session samples (exact: render_44k_bitexact, render_48k)
    uint32_t rate() const { return rate_; }

    // n frames into L and R (floats, -1..1); ev: this block's MIDI, by offset (0..n-1, in order)
    void process(float* L, float* R, uint32_t n, const MidiEvent* ev, uint32_t nev);
    // a parameter change at a sample offset of the coming process() block (plan Task 4's automation; same timing as MIDI)
    void paramAt(uint32_t offset, uint16_t id, int32_t value);

    uint64_t dropped() const { return dropped_; } // events the queue had no room for
    int64_t rendered() const { return front_; }   // internal frames rendered so far (tests)

private:
    struct Ev { int64_t at; uint8_t kind, len; uint8_t data[3]; uint16_t id; int32_t value; };
    enum { kMidi, kParam };
    void push(const Ev& e);
    int64_t internalAt(int64_t s) const;          // e(s)
    void renderTo(int64_t target);
    void queueMidi(const MidiEvent& m, uint32_t offset);   // offset: in the part about to be processed
    void processPart(float* L, float* R, uint32_t n);

    kc_t* core_ = nullptr;
    Resampler rs_;
    uint32_t rate_ = 44100, maxPart_ = 512;
    int latency_ = 256;
    int64_t session_ = 0, front_ = 0;            // session samples done, internal frames rendered
    std::vector<float> ringL_, ringR_;            // the internal stream (frame j at [j & mask])
    uint32_t mask_ = 0;
    Ev queue_[kQueue];
    uint32_t qr_ = 0, qw_ = 0;
    uint64_t dropped_ = 0;
    int16_t tmp_[2 * KC_BLOCK];
};

} // namespace knobi
