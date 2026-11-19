// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin: the AudioProcessor. One core instance per plugin instance (CoreRunner), driven on the audio thread;
// MIDI at its sample offsets; 44.1 kHz inside, resampled to the session's rate; the latency reported to the host.
// Other threads reach the instance only through access() (CoreAccess.h). Parameters, state and programs: plan Task 4.
#pragma once
#include <array>
#include <juce_audio_processors/juce_audio_processors.h>
#include "CoreAccess.h"
#include "CoreRunner.h"

namespace knobi {

class Processor : public juce::AudioProcessor {
public:
    static constexpr int kMaxEvents = 2048;      // MIDI messages a block (beyond: the rest of that block dropped)

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

    int getNumPrograms() override { return 1; }  // (the 500-program bank: plan Task 4 / 6)
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "INIT"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}   // (plan Task 4, through access())
    void setStateInformation(const void*, int) override {}

    CoreAccess& access() { return access_; }
    CoreRunner& runner() { return runner_; }     // (the audio thread, or inside access().run)
    uint64_t midiDropped() const { return midiDropped_; }

private:
    CoreRunner runner_;
    CoreAccess access_;
    std::array<MidiEvent, kMaxEvents> events_{};
    uint64_t midiDropped_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};

} // namespace knobi
