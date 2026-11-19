// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin: the AudioProcessor. One core instance per plugin instance (CoreRunner), driven on the audio thread;
// MIDI at its sample offsets; 44.1 kHz inside, resampled to the session's rate; the latency reported to the host.
// Other threads reach the instance only through access() (CoreAccess.h).
// Plan Task 4: the parameters (Params.h: layout, host <-> core sync, no loop), the host's program list (500 programs,
// following MIDI program change + bank select: the core's), the bank as the plugin sees it (ProgramBank.h: the seam
// plan Task 6's library plugs into, setBank) and the set state (StateCodec.h; through access(), never kc_* directly).
#pragma once
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "CoreAccess.h"
#include "CoreRunner.h"
#include "Params.h"
#include "ProgramBank.h"
#include "StateCodec.h"

namespace knobi {

class Processor : public juce::AudioProcessor, private juce::Timer {
public:
    static constexpr int kMaxEvents = 2048;      // MIDI messages a block (beyond: the rest of that block dropped)
    static constexpr int kPending = 256;         // MIDI kept from silent blocks (beyond: all notes off at the replay)
    static constexpr int kFade = 64;             // samples: a silent block fades out, the next block fades in

    Processor();
    ~Processor() override;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // the host's program list: the bank's 500 programs ("000 INIT" .. "499 ..."); the current one follows MIDI program
    // change + bank select too (the core's). setCurrentProgram loads it as a program change does (the edit is lost,
    // as on the keyboard: WRITE is the library's, plan Task 6)
    int getNumPrograms() override { return (int)KC_PROGRAMS; }
    int getCurrentProgram() override { return program_.load(std::memory_order_acquire); }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    // the set state (StateCodec.h): the program number, the program + sequence in play, edited, differs from bank.
    // Loading it: the stored copy wins over the bank's program (spec "Set state"); the parameters follow
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    Params& params() { return params_; }
    // the core's changes to the host (Params.h), the message thread: its timer calls it; tests call it
    void syncHost();
    // the 30 Hz timer that calls syncHost: started by the constructor unless startHostSyncTimer is false (process-wide). The tests set it
    // false before making any processor: JUCE's timer thread (made by the first timer, alive after stopTimer) posts
    // to the message queue -- it allocates and locks on its own thread, which the tests' process-wide counter counts
    static inline std::atomic<bool> startHostSyncTimer{true};
    void hostSyncTimer(bool on) { if (on) startTimerHz(30); else stopTimer(); }
    // the user layer of the bank (plan Task 6's library; nullptr: none): to the playing instance and the bank view. The
    // caller keeps it alive while it may be in use (knobi_core.h kc_bank_set / kc_bank_in_use). false: refused
    bool setBank(const kc_bank_t* bank);
    ProgramBank& bank() { return bank_; }
    bool edited() const { return edited_.load(std::memory_order_acquire); }   // the program in play changed since loaded
    // the program in play is not the bank's program of its number (computed now: the message thread)
    bool differsFromBank();
    bool lastStateDiffered() const { return stateDiffers_.load(); }   // (the panel, Task 7) as of the last set state load

    CoreAccess& access() { return access_; }
    CoreRunner& runner() { return runner_; }     // (the audio thread, or inside access().run)
    uint64_t midiDropped() const { return midiDropped_; }
    uint64_t silentBlocks() const { return silentBlocks_; }   // blocks output silent (CoreAccess.h)
    // the clock follows the host's transport (Live playing): P_BPM is not used, the panel shows it greyed (plan Task
    // 7). Any thread; as of the last block
    bool followingHost() const { return following_.load(std::memory_order_acquire); }

private:
    void timerCallback() override { syncHost(); }
    void scanCore();                              // the core's values -> Params (alone with the core)
    void applyHostChanges();                      // Params -> the core at the block's first sample (the audio thread)
    struct Snapshot { int32_t number; program_t program; seq_t seq; int edited; };
    void snapshot(Snapshot& s);                   // the program in play (through access())

    CoreRunner runner_;
    CoreAccess access_;
    Params params_{*this};
    ProgramBank bank_;
    std::array<int32_t, Params::kLimit> lastCore_{};   // the core's values as last scanned (alone with the core)
    std::atomic<int> program_{0};
    std::atomic<bool> programMoved_{false}, edited_{false}, stateDiffers_{false}, following_{false};
    std::array<juce::String, KC_PROGRAMS> names_;    // (the message thread; filled on first use, cleared by setBank)
    bool namesValid_ = false;
    std::mutex namesLock_;
    std::array<MidiEvent, kMaxEvents> events_{};
    uint64_t midiDropped_ = 0, silentBlocks_ = 0;
    // the silent path (CoreAccess.h): this processor's own, never CoreRunner's (a call may be using the instance)
    std::array<MidiEvent, kPending + 16> pending_{};
    uint32_t pendingN_ = 0;
    bool pendingOverflow_ = false, fadeIn_ = false;
    uint64_t skipped_ = 0;                       // session samples output silent, not yet rendered
    float lastL_ = 0.0f, lastR_ = 0.0f;          // the last sample output

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};

} // namespace knobi
