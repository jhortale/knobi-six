// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin: the threading seam around one core instance. The core's calls on one instance must never overlap
// (knobi_core.h), and even its "read-only" getters write (kc_bind). The audio thread owns the instance while it is in
// processBlock; any other thread (the message thread: state get / set, later the panel's edits) asks for a call on it:
//   run(fn, ctx): posts fn; the audio thread runs it at the start of its next block, before that block's events (so
//   it acts at the block's first sample, in order with everything else the audio thread does). If no block comes (the
//   host is not processing: not prepared, stopped, offline), the asking thread takes the instance itself once the
//   audio thread has been idle for kIdleMs, and runs fn there. Either way fn runs once, alone with the instance.
// The audio side is wait-free (atomics only): audioEnter / audioServe / audioExit never block, allocate or lock.
// audioEnter fails only while another thread is running fn itself (the audio thread was idle for kIdleMs and then a
// block came): that block is silent. fn must itself be real-time safe (it may run on the audio thread): copies only.
// The asking side takes a mutex (several non-audio threads queue up there), never the audio thread.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

namespace knobi {

class CoreAccess {
public:
    using Fn = void (*)(void* ctx);
    static constexpr int kIdleMs = 20;            // no block for this long: the host is not processing

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
        auto t0 = std::chrono::steady_clock::now(), seen = t0;
        uint64_t last = blocks_.load(std::memory_order_relaxed);
        for (;;) {
            int r = req_.load(std::memory_order_acquire);
            if (r == kDone) { req_.store(kIdle, std::memory_order_relaxed); return true; }
            auto now = std::chrono::steady_clock::now();
            uint64_t b = blocks_.load(std::memory_order_relaxed);
            if (b != last) last = b, seen = now;
            if (r == kPosted && now - seen >= std::chrono::milliseconds(kIdleMs)) {   // the host is not processing
                int idle = kFree;
                if (owner_.compare_exchange_strong(idle, kOther, std::memory_order_acquire)) {
                    int posted = kPosted;
                    bool mine = req_.compare_exchange_strong(posted, kTaken, std::memory_order_acq_rel);
                    if (mine) fn(ctx);
                    owner_.store(kFree, std::memory_order_release);
                    if (mine) { req_.store(kIdle, std::memory_order_relaxed); ranHere_++; return true; }
                }
            }
            if (r == kPosted && now - t0 >= std::chrono::milliseconds(timeoutMs)) {
                int posted = kPosted;
                if (req_.compare_exchange_strong(posted, kIdle, std::memory_order_acq_rel)) return false;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }

    uint64_t ranHere() const { return ranHere_; } // (tests) calls the asking thread ran itself

private:
    enum { kFree, kAudio, kOther };               // owner_
    enum { kIdle, kPosted, kTaken, kDone };       // req_
    std::atomic<int> owner_{kFree}, req_{kIdle};
    std::atomic<uint64_t> blocks_{0};
    Fn fn_ = nullptr;
    void* ctx_ = nullptr;
    std::mutex side_;
    uint64_t ranHere_ = 0;
};

} // namespace knobi
