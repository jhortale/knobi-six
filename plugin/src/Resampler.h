// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin: the resampler from the core's 44.1 kHz to the session's rate (48 / 88.2 / 96 kHz, any integer rate).
// A windowed-sinc interpolator (Kaiser, 96 taps at 44.1 kHz) read at exact rational input times: output sample m sits
// at input time m x in / out, split into a whole frame q and one of P phases (P = out / gcd(in, out) when that is at
// most kMaxPhases: exact; else kMaxPhases, the nearest). The kernel is centred (no delay of its own): the caller decides
// the latency (CoreRunner: an exact integer number of output samples). No JUCE; prepare allocates, the rest does not.
#pragma once
#include <cstdint>
#include <vector>

namespace knobi {

class Resampler {
public:
    static constexpr int kHalf = 48;             // taps on each side of the input time (96 in all)
    static constexpr int kTaps = 2 * kHalf;
    static constexpr uint32_t kMaxPhases = 1024;

    // the table for in -> out (Hz); false: a rate it does not take (0). Allocates (not on the audio thread)
    bool prepare(uint32_t inRate, uint32_t outRate);
    bool bypass() const { return in_ == out_; }  // the same rate: no filter, the samples as they are
    uint32_t inRate() const { return in_; }
    uint32_t outRate() const { return out_; }
    uint32_t phases() const { return phases_; }
    double cutoffHz() const { return cutoff_; }

    // output sample m (m >= 0) is at input time q + p / phases()
    void timeOf(int64_t m, int64_t& q, uint32_t& p) const;
    // the value at input time q + p / phases() from ring[] (frame j at ring[j & mask], frames before the first: 0):
    // frames q - kHalf + 1 .. q + kHalf
    float at(const float* ring, uint32_t mask, int64_t q, uint32_t p) const
    {
        const float* h = &table_[(size_t)p * kTaps];
        int64_t j = q - kHalf + 1;
        float acc = 0.0f;
        for (int k = 0; k < kTaps; k++) acc += h[k] * ring[(uint64_t)(j + k) & mask];
        return acc;
    }

private:
    uint32_t in_ = 0, out_ = 0, phases_ = 1;
    double cutoff_ = 0.0;
    std::vector<float> table_;
};

} // namespace knobi
