// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin: the host parameters (spec "Parameters", plan Task 4). Every id the core exposes (kc_param_info: the
// firmware's live ids -- Phase 1's, P_DISTORTION, P_BPM, P_CLK_DIV, P_ARP_*; not the stored-only effect ids, P_SEQ_GEN,
// the chord or the name) is one JUCE parameter, in id order, with the stable ID "p<id>" (version hint 1) and the
// firmware's name:
//   switch -> AudioParameterBool (0 / 1)
//   select -> AudioParameterChoice (the firmware's labels; index = value - min)
//   knob   -> AudioParameterInt min..max, integer steps (a bipolar knob -max..max: its centre 0 is the normalised 0.5)
// The default is the firmware's. Parameter IDs never change (Live finds automation in a saved set by them).
//
// Sync, host <-> core, real-time safe (atomics only; any thread may call the host side):
//   host -> core  A host change (automation, Live's device view, a host's own MIDI mapping) arrives as setValue on any
//                 thread (the AU / VST3 wrapper; JUCE 8 hands over one value a parameter a block, the queue's last
//                 point, before processBlock): hostChanged() stores the value and marks the id. processBlock (the
//                 audio thread) takes the marked ids (takeHostChanges) and gives each to the core at the block's first
//                 sample (CoreRunner::paramAt(0, ...): the right sample for what JUCE delivers).
//   core -> host  The core changes values itself (a MIDI CC through the firmware's CC map, a MIDI program change, a
//                 program load from the host's program list or the set state). processBlock reads the core's values
//                 after rendering (Processor::scanCore) and marks the changed ids here (coreChanged); the message
//                 thread (Processor's timer, 30 Hz, and right after a program load it asked for) tells the host
//                 (syncToHost): setValueNotifyingHost for the changed ids only, with no begin / endChangeGesture.
//   no loop       1. syncToHost's own setValue is not a host change (a thread-local guard: hostChanged ignores it).
//                 2. The host's echo of a value we reported (VST3: the host sends a performEdit back to the processor)
//                    is not given to the core when it equals the last value reported for that id (notified_): the
//                    core may have moved on since (a CC sweep), an echo must not pull it back. A host value that
//                    differs is a real change and goes to the core (and clears notified_).
//                 3. A host change applied to the core is not reported back (Processor: the id is left out of the
//                    scan while its paramAt waits, and its last core value is the host's).
#pragma once
#include <array>
#include <atomic>
#include <climits>
#include <juce_audio_processors/juce_audio_processors.h>
#include "CoreC.h"

namespace knobi {

class Params {
public:
    static constexpr int kLimit = (int)KC_PARAM_ID_LIMIT;
    static constexpr int32_t kNone = INT32_MIN;

    struct Entry {
        uint16_t id;
        kc_param_info_t info;
        juce::RangedAudioParameter* param;        // (owned by the processor)
    };

    explicit Params(juce::AudioProcessor& owner); // adds the parameters to owner (its constructor, message thread)

    static juce::String paramID(uint16_t id) { return "p" + juce::String(id); }
    static int32_t toCore(const kc_param_info_t& in, float normalised);   // the parameter's value as the core's
    static float toNormalised(const kc_param_info_t& in, int32_t v);      // the core's value as the parameter's

    int size() const { return n_; }
    const Entry& entry(int i) const { return entries_[(size_t)i]; }
    const Entry* byId(uint16_t id) const { return id < kLimit && index_[id] >= 0 ? &entries_[(size_t)index_[id]] : nullptr; }
    int32_t hostValue(uint16_t id) const;         // the parameter's current value, as the core's

    // ---- host side (any thread): a parameter's setValue
    void hostChanged(uint16_t id, int32_t v);

    // ---- the audio thread (or a call alone with the core: CoreAccess)
    template <typename F> void takeHostChanges(F&& apply)   // apply(id, value) for each id the host changed
    {
        for (int w = 0; w < 2; w++) {
            uint64_t m = hostDirty_[w].exchange(0, std::memory_order_acq_rel);
            while (m) {
                const int b = __builtin_ctzll(m);
                m &= m - 1;
                const uint16_t id = (uint16_t)(w * 64 + b);
                const int32_t v = hostVal_[id].load(std::memory_order_acquire);
                if (v == notified_[id].load(std::memory_order_acquire)) continue;   // (our own report echoed back)
                notified_[id].store(kNone, std::memory_order_release);
                coreVal_[id].store(v, std::memory_order_release);   // (the core takes it: nothing to report back)
                toHost_[w].fetch_and(~(1ull << b), std::memory_order_acq_rel);
                apply(id, v);
            }
        }
    }
    void dropHostChanges() { hostDirty_[0].store(0), hostDirty_[1].store(0); }   // (a set state load: theirs is older)
    void coreChanged(uint16_t id, int32_t v);    // the core's value of id changed (not by the host)
    void discardToHost() { toHost_[0].store(0), toHost_[1].store(0); }   // (the constructor: the host has the defaults)
    bool hostPending() const { return (hostDirty_[0].load() | hostDirty_[1].load()) != 0; }   // (tests)

    // ---- the message thread: the core's changes to the host, no gesture; returns the parameters told
    int syncToHost();

private:
    friend struct ParamHook;
    std::vector<Entry> entries_;
    std::array<int, kLimit> index_;
    int n_ = 0;
    std::array<std::atomic<int32_t>, kLimit> hostVal_, coreVal_, notified_;
    std::atomic<uint64_t> hostDirty_[2], toHost_[2];
};

} // namespace knobi
