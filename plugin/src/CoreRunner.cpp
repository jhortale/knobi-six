// SPDX-License-Identifier: GPL-3.0-only
#include "CoreRunner.h"
#include <algorithm>
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
    // events still waiting for their frame (a note-off up to a block ahead): to the core now, in order, not lost (the
    // core keeps sounding across a re-prepare; they act at its next block)
    if (core_)
        for (; qr_ != qw_; qr_++) {
            const Ev& e = queue_[qr_ % kQueue];
            if (e.kind == kMidi) kc_midi(core_, e.data, e.len);
            else kc_param_set(core_, e.id, e.value);
            applied(e);
        }
    std::memset(paramQueued_, 0, sizeof paramQueued_);
    session_ = 0, front_ = 0, qr_ = qw_ = 0;
    last_.valid = false, wrapped_ = false;
    return true;
}

int64_t CoreRunner::internalAt(int64_t s) const   // ceil(s x 44100 / rate)
{
    if (rs_.bypass()) return s;
    int64_t num = s * (int64_t)KC_RATE, o = (int64_t)rate_;
    return num >= 0 ? (num + o - 1) / o : -((-num) / o);
}

// in frame order, stable (an event goes after those at its frame or earlier): renderTo reads only the head, so
// parameter changes queued before a block (paramAt) and that block's MIDI interleave by offset
void CoreRunner::push(const Ev& e)
{
    if (qw_ - qr_ >= kQueue) { dropped_++; return; }
    uint32_t i = qw_;
    while (i != qr_ && queue_[(i - 1u) % kQueue].at > e.at) {
        queue_[i % kQueue] = queue_[(i - 1u) % kQueue];
        i--;
    }
    queue_[i % kQueue] = e;
    qw_++;
    if (e.kind == kParam && e.id < KC_PARAM_ID_LIMIT) paramQueued_[e.id]++;
}

void CoreRunner::applied(const Ev& e)
{
    if (e.kind == kParam && e.id < KC_PARAM_ID_LIMIT && paramQueued_[e.id]) paramQueued_[e.id]--;
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
            applied(e);
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

void CoreRunner::skip(uint32_t n, const MidiEvent* ev, uint32_t nev)
{
    if (!core_) return;
    for (uint32_t i = 0; i < nev; i++) queueMidi(ev[i], 0);   // (at the stretch's first sample, in order)
    while (n) {                                   // (the stretch rendered, its output dropped: the timeline kept)
        uint32_t part = n < kScratch ? n : kScratch;
        if (part > maxPart_) part = maxPart_;
        processPart(scratchL_, scratchR_, part);
        n -= part;
    }
}

// CoreRunner.h "The host's transport". The core's clock takes it at internal frames (kc_clock_host: frame_offset from
// front_, the next frame the core renders: no FIFO here). e(S) >= front_ always (front_ is a block start at or before it)
void CoreRunner::transport(const Transport& t, uint32_t n)
{
    if (!core_) return;
    const int64_t S = session_, e0 = internalAt(S);
    if (!t.playing) {
        last_.valid = false, wrapped_ = false;
        kc_clock_host(core_, 0, 120.0, 0.0, (uint32_t)(e0 - front_));
        return;
    }
    const double rate = (double)rate_;
    double ppq = t.ppq;
    // the last block's wrap came (by its predicted tempo) a little before the host's own: the host is still just
    // before the loop's end here (within kWrapLate). It is the same instant after the wrap: the position as wrapped
    // (just before the loop's start: the clock's own position within its tolerance, no jump, no step again)
    if (wrapped_ && t.looping && t.loopEnd > t.loopStart && ppq < t.loopEnd && ppq >= t.loopEnd - kWrapLate)
        ppq -= t.loopEnd - t.loopStart;
    wrapped_ = false;
    double bpm = t.bpm > 0.0 && t.bpm < 1e4 ? t.bpm : 120.0;
    // a tempo ramp (automation): the host gives the tempo at the block's start only. The tempo over the last block (its
    // positions' difference) is half a block's change behind the tempo at this one's start when the tempo moves
    // steadily: the core runs this block at the tempo predicted for its middle (bpm + that change x n / the last
    // block's n), so the position at its end is the host's to second order. Only a small change (<= kRampMax BPM a
    // block, after a continuous block): a jump in tempo or position is taken as it is (the next block's position
    // corrects any error)
    const double given = bpm;
    if (last_.valid && S > last_.s) {
        const double secs = (double)(S - last_.s) / rate, avg = (ppq - last_.ppq) / secs * 60.0, d = bpm - avg;
        if (std::fabs(avg - last_.bpm) <= kRampMax && std::fabs(d) <= kRampMax)
            bpm += d * (double)n / (double)(S - last_.s);
    }
    last_ = {true, S, ppq, given};
    // the core's tempo exactly (kc_clock_host: 1..999 BPM in steps of 1/64, rounded), so the wrap's frame below is
    // where the core's own clock reaches the loop's end
    bpm = std::round(std::min(999.0, std::max(1.0, bpm)) * 64.0) / 64.0;
    const double qps = bpm / 60.0;               // (quarters a second)
    // the position at e(S): the host's at S, moved on by e(S)'s time after S (0 at 44.1 kHz)
    kc_clock_host(core_, 1, bpm, ppq + ((double)e0 / KC_RATE - (double)S / rate) * qps, (uint32_t)(e0 - front_));
    if (t.looping && t.loopEnd > t.loopStart && ppq < t.loopEnd && ppq + n / rate * qps > t.loopEnd) {
        const double sw = (double)S + (t.loopEnd - ppq) / qps * rate;   // the wrap: its session time (samples)
        // its frame: the first at or after it, and never after the frame where the clock reaches the loop's end on
        // its own: the clock runs from this block's anchor at the same (quantised) tempo, its position rounded to
        // 1 / CLOCK_U of a tick there (< 1e-4 frame), so within kWrapEps a frame earlier is the same instant; the old
        // timeline's step at the loop's end is never fired before the wrap (a doubled step)
        const int64_t ew = (int64_t)std::ceil(sw * KC_RATE / rate - kWrapEps);
        if (ew < internalAt(S + (int64_t)n)) {    // (in this block: the next one's anchor has it otherwise)
            last_.valid = false, wrapped_ = true; // (the next block's position: after the wrap)
            kc_clock_host(core_, 1, bpm, t.loopStart + ((double)ew / KC_RATE - sw / rate) * qps, (uint32_t)(ew - front_));
        }
    }
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
