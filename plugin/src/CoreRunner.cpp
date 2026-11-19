// SPDX-License-Identifier: GPL-3.0-only
#include "CoreRunner.h"
#include <cmath>
#include <cstring>

namespace knobi {

CoreRunner::CoreRunner() : core_(kc_create()) {}
CoreRunner::~CoreRunner() { if (core_) kc_destroy(core_); }

bool CoreRunner::prepare(double sampleRate, int maxBlock)
{
    long r = std::lround(sampleRate);
    if (r < 8000 || r > 768000) r = 44100;      // (a rate no host gives: as 44.1 kHz)
    rate_ = (uint32_t)r;
    if (!rs_.prepare(KC_RATE, rate_)) return false;
    maxPart_ = maxBlock > 0 ? (uint32_t)maxBlock : 512u;
    if (maxPart_ > 65536u) maxPart_ = 65536u;
    if (rs_.bypass())
        latency_ = (int)KC_BLOCK;
    else                                          // (CoreRunner.h: the block ahead, the kernel's half, one frame)
        latency_ = (int)(((uint64_t)(KC_BLOCK + 1u + Resampler::kHalf) * rate_ + KC_RATE - 1u) / KC_RATE);
    // the ring: every frame from the oldest the resampler reads to the newest rendered, with room to spare
    double perOut = (double)KC_RATE / (double)rate_;
    uint64_t span = (uint64_t)std::ceil((double)(maxPart_ + (uint32_t)latency_) * perOut) + 2u * Resampler::kTaps
                    + 4u * KC_BLOCK;
    uint32_t size = 1024;
    while (size < span) size <<= 1;
    ringL_.assign(size, 0.0f);
    ringR_.assign(size, 0.0f);
    mask_ = size - 1u;
    session_ = 0, front_ = 0, qr_ = qw_ = 0;
    return true;
}

int64_t CoreRunner::internalAt(int64_t s) const   // ceil(s x 44100 / rate)
{
    if (rs_.bypass()) return s;
    int64_t num = s * (int64_t)KC_RATE, o = (int64_t)rate_;
    return num >= 0 ? (num + o - 1) / o : -((-num) / o);
}

void CoreRunner::push(const Ev& e)
{
    if (qw_ - qr_ >= kQueue) { dropped_++; return; }
    queue_[qw_ % kQueue] = e;
    qw_++;
}

void CoreRunner::paramAt(uint32_t offset, uint16_t id, int32_t value)
{
    Ev e{};
    e.at = internalAt(session_ + (int64_t)offset), e.kind = kParam, e.id = id, e.value = value;
    if (e.at < front_) e.at = front_;            // (never before what is rendered)
    push(e);
}

// the core up to internal frame target (a multiple of KC_BLOCK): the firmware's blocks, split at each event's frame
void CoreRunner::renderTo(int64_t target)
{
    while (front_ < target) {
        while (qr_ != qw_ && queue_[qr_ % kQueue].at <= front_) {
            const Ev& e = queue_[qr_ % kQueue];
            if (e.kind == kMidi) kc_midi(core_, e.data, e.len);
            else kc_param_set(core_, e.id, e.value);
            qr_++;
        }
        int64_t end = (front_ / KC_BLOCK + 1) * KC_BLOCK;
        if (qr_ != qw_ && queue_[qr_ % kQueue].at < end) end = queue_[qr_ % kQueue].at;
        uint32_t n = (uint32_t)(end - front_);
        kc_render_exact(core_, tmp_, n);
        for (uint32_t i = 0; i < n; i++) {
            uint32_t j = (uint32_t)((uint64_t)(front_ + i) & mask_);
            ringL_[j] = (float)tmp_[2u * i] * (1.0f / 32768.0f);
            ringR_[j] = (float)tmp_[2u * i + 1u] * (1.0f / 32768.0f);
        }
        front_ = end;
    }
}

void CoreRunner::queueMidi(const MidiEvent& m, uint32_t offset)
{
    if (m.len < 1 || m.len > 3) return;
    Ev e{};
    e.at = internalAt(session_ + (int64_t)offset);
    if (e.at < front_) e.at = front_;            // (cannot happen: CoreRunner.h; kept safe)
    e.kind = kMidi, e.len = m.len;
    std::memcpy(e.data, m.data, 3);
    push(e);
}

void CoreRunner::processPart(float* L, float* R, uint32_t n)
{
    const int64_t end = session_ + (int64_t)n;
    const int64_t eEnd = rs_.bypass() ? end : (end * (int64_t)KC_RATE) / (int64_t)rate_;   // floor
    renderTo(eEnd / KC_BLOCK * KC_BLOCK);
    const int64_t d = latency_;
    if (rs_.bypass()) {
        for (uint32_t i = 0; i < n; i++) {
            int64_t t = session_ + (int64_t)i - d;
            uint32_t j = (uint32_t)((uint64_t)t & mask_);
            L[i] = t >= 0 ? ringL_[j] : 0.0f;
            R[i] = t >= 0 ? ringR_[j] : 0.0f;
        }
    } else {
        for (uint32_t i = 0; i < n; i++) {
            int64_t m = session_ + (int64_t)i - d, q;
            uint32_t p;
            if (m < 0) { L[i] = R[i] = 0.0f; continue; }
            rs_.timeOf(m, q, p);
            L[i] = rs_.at(ringL_.data(), mask_, q, p);
            R[i] = rs_.at(ringR_.data(), mask_, q, p);
        }
    }
    session_ = end;
}

void CoreRunner::process(float* L, float* R, uint32_t n, const MidiEvent* ev, uint32_t nev)
{
    if (!core_) { std::memset(L, 0, n * sizeof *L); std::memset(R, 0, n * sizeof *R); return; }
    uint32_t done = 0, e = 0;
    while (done < n) {                            // (a block longer than prepared: in parts, its events with them)
        uint32_t part = n - done < maxPart_ ? n - done : maxPart_;
        for (; e < nev && ev[e].offset < done + part; e++) queueMidi(ev[e], ev[e].offset - done);
        processPart(L + done, R + done, part);
        done += part;
    }
}

} // namespace knobi
