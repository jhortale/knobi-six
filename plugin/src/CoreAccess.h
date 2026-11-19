// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin: the threading seam around one core instance. The core's calls on one instance must never overlap
// (knobi_core.h), and even its "read-only" getters write (kc_bind). The audio thread owns the instance while it is in
// processBlock; any other thread (the message thread: state get / set, later the panel's edits) asks for a call on it:
//   run(fn, ctx): posts fn; the audio thread runs it at the start of its next block, before that block's events (so
//   it acts at the block's first sample, in order with everything else the audio thread does). If no block comes (the
//   host is genuinely not processing: not prepared, stopped, offline), the asking thread takes the instance itself
//   once the audio thread has been idle for idleMs() -- max(kIdleMs, 3 block periods), set from prepareToPlay
//   (setIdle), so a host with long blocks (2048 at 44.1 kHz: 46 ms apart) is never taken for idle between its
//   callbacks -- and runs fn there. Either way fn runs once, alone with the instance.
// The audio side is wait-free (atomics only): audioEnter / audioServe / audioExit never block, allocate or lock.
// audioEnter fails only while another thread is running fn itself (the host was idle for idleMs() and resumed during
// fn): Processor then outputs that block faded to silence, keeps its MIDI for the next block and keeps the timeline
// (Processor.cpp, test silent_block). fn must itself be real-time safe (it may run on the audio thread): copies only.
// The asking side takes a mutex (several non-audio threads queue up there), never the audio thread.
// Time: the asking side's idle and timeout measures read a Clock (production: the monotonic clock, polled every
// 500 us; tests: a clock they advance themselves, setClock, so the idle rule is exercised without real sleeps). The
// audio side reads no clock.
#pragma once
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <thread>

namespace knobi {

class CoreAccess {
public:
    using Fn = void (*)(void* ctx);
    struct Clock {                                // the asking side's time
        virtual ~Clock() = default;
        virtual int64_t nowUs() = 0;              // monotonic, microseconds
        virtual void pause() = 0;                 // between two looks at the request
    };
    struct SteadyClock final : Clock {            // production
        int64_t nowUs() override
        {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        void pause() override { std::this_thread::sleep_for(std::chrono::microseconds(500)); }
    };
    // (tests) the clock run() reads; nullptr: the steady clock. Set it while no run() is waiting
    void setClock(Clock* c) { clock_ = c ? c : &steady_; }
    static constexpr int kIdleMs = 20;            // the least idle time taken as "the host is not processing"

    // the host's block period (prepareToPlay): idle = no block for max(kIdleMs, 3 block periods)
    void setIdle(double sampleRate, int maxBlock)
    {
        int ms = sampleRate > 0 && maxBlock > 0 ? (int)std::ceil(3000.0 * maxBlock / sampleRate) : kIdleMs;
        idleMs_.store(ms > kIdleMs ? ms : kIdleMs, std::memory_order_relaxed);
    }
    int idleMs() const { return idleMs_.load(std::memory_order_relaxed); }

    // ---- the audio thread, around each block
    bool audioEnter()                             // false: another thread holds the instance (output silence)
    {
        int idle = kFree;
        return owner_.compare_exchange_strong(idle, kAudio, std::memory_order_acquire);
    }
    void audioServe()                             // after audioEnter: a posted call, if any, here
    {
        int posted = kPosted;
        if (req_.load(std::memory_order_acquire) == kPosted &&
            req_.compare_exchange_strong(posted, kTaken, std::memory_order_acq_rel)) {
            fn_(ctx_);
            req_.store(kDone, std::memory_order_release);
        }
    }
    void audioExit()
    {
        blocks_.fetch_add(1, std::memory_order_relaxed);
        owner_.store(kFree, std::memory_order_release);
    }

    // ---- any other thread: fn(ctx) run once, alone with the instance; false: timed out, fn not run
    bool run(Fn fn, void* ctx, int timeoutMs = 2000)
    {
        std::lock_guard<std::mutex> side(side_);
        fn_ = fn, ctx_ = ctx;
        req_.store(kPosted, std::memory_order_release);
        const int64_t t0 = clock_->nowUs();
        int64_t seen = t0;
        uint64_t last = blocks_.load(std::memory_order_relaxed);
        for (;;) {
            int r = req_.load(std::memory_order_acquire);
            if (r == kDone) { req_.store(kIdle, std::memory_order_relaxed); return true; }
            const int64_t now = clock_->nowUs();
            uint64_t b = blocks_.load(std::memory_order_relaxed);
            if (b != last) last = b, seen = now;
            if (r == kPosted && now - seen >= (int64_t)idleMs() * 1000) {   // the host is not processing
                int idle = kFree;
                if (owner_.compare_exchange_strong(idle, kOther, std::memory_order_acquire)) {
                    int posted = kPosted;
                    bool mine = req_.compare_exchange_strong(posted, kTaken, std::memory_order_acq_rel);
                    if (mine) fn(ctx);
                    owner_.store(kFree, std::memory_order_release);
                    if (mine) { req_.store(kIdle, std::memory_order_relaxed); ranHere_++; return true; }
                }
            }
            if (r == kPosted && now - t0 >= (int64_t)timeoutMs * 1000) {
                int posted = kPosted;
                if (req_.compare_exchange_strong(posted, kIdle, std::memory_order_acq_rel)) return false;
            }
            clock_->pause();
        }
    }

    uint64_t ranHere() const { return ranHere_; } // (tests) calls the asking thread ran itself
    bool posted() const { return req_.load(std::memory_order_acquire) == kPosted; }   // (tests) a call waits

private:
    enum { kFree, kAudio, kOther };               // owner_
    enum { kIdle, kPosted, kTaken, kDone };       // req_
    std::atomic<int> owner_{kFree}, req_{kIdle};
    std::atomic<uint64_t> blocks_{0};
    std::atomic<int> idleMs_{kIdleMs};
    Fn fn_ = nullptr;
    void* ctx_ = nullptr;
    std::mutex side_;
    uint64_t ranHere_ = 0;
    SteadyClock steady_;
    Clock* clock_ = &steady_;
};

} // namespace knobi
