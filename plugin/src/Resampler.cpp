// SPDX-License-Identifier: GPL-3.0-only
#include "Resampler.h"
#include <cmath>

namespace knobi {

static double besselI0(double x)                  // the Kaiser window's I0 (series)
{
    double sum = 1.0, term = 1.0, y = x * x / 4.0;
    for (int k = 1; k < 64; k++) {
        term *= y / ((double)k * (double)k);
        sum += term;
        if (term < sum * 1e-17) break;
    }
    return sum;
}

static uint32_t gcd(uint32_t a, uint32_t b)
{
    while (b) { uint32_t t = a % b; a = b; b = t; }
    return a;
}

bool Resampler::prepare(uint32_t inRate, uint32_t outRate)
{
    if (!inRate || !outRate) return false;
    in_ = inRate, out_ = outRate;
    uint32_t l = outRate / gcd(inRate, outRate);
    phases_ = l <= kMaxPhases ? l : kMaxPhases;
    // the pass band to 0.93 of the lower Nyquist (20.5 kHz at 44.1 in); Kaiser beta 10 (stop band ~ -100 dB), its
    // transition ~ +-1.5 kHz around the cut-off: flat to ~19 kHz, the input's images above 22.05 kHz removed
    const double nyq = 0.5 * (double)(inRate < outRate ? inRate : outRate);
    cutoff_ = 0.93 * nyq;
    const double fc = cutoff_ / (double)inRate;    // cycles per input frame
    const double beta = 10.0, i0b = besselI0(beta);
    table_.assign((size_t)phases_ * kTaps, 0.0f);
    if (bypass()) return true;
    for (uint32_t p = 0; p < phases_; p++) {
        double f = (double)p / (double)phases_, sum = 0.0, h[kTaps];
        for (int k = 0; k < kTaps; k++) {
            double x = f + (double)(kHalf - 1 - k);  // input time - frame (in frames)
            double u = x / (double)kHalf, w = u * u < 1.0 ? besselI0(beta * std::sqrt(1.0 - u * u)) / i0b : 0.0;
            double s = x == 0.0 ? 2.0 * fc : std::sin(2.0 * M_PI * fc * x) / (M_PI * x);
            h[k] = s * w;
            sum += h[k];
        }
        for (int k = 0; k < kTaps; k++) table_[(size_t)p * kTaps + k] = (float)(h[k] / sum);   // (DC gain 1, each phase)
    }
    return true;
}

void Resampler::timeOf(int64_t m, int64_t& q, uint32_t& p) const
{
    int64_t num = m * (int64_t)in_, o = (int64_t)out_;
    q = num >= 0 ? num / o : -((-num + o - 1) / o);
    int64_t rem = num - q * o;                    // 0 .. out - 1
    uint64_t ph = ((uint64_t)rem * phases_ * 2u + (uint64_t)o) / (2u * (uint64_t)o);   // (exact when phases = l)
    if (ph >= phases_) ph = 0, q++;
    p = (uint32_t)ph;
}

} // namespace knobi
