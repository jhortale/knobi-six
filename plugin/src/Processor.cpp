// SPDX-License-Identifier: GPL-3.0-only
#include "Processor.h"

namespace knobi {

Processor::Processor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    runner_.prepare(44100.0, 512);               // (until the host prepares: the latency of 44.1 kHz)
    setLatencySamples(runner_.latency());
}

Processor::~Processor() = default;

bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.inputBuses.isEmpty() && layouts.outputBuses.size() == 1 &&
           layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void Processor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    runner_.prepare(sampleRate, maximumExpectedSamplesPerBlock);   // (allocates: never on the audio thread)
    setLatencySamples(runner_.latency());
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0) return;
    if (!access_.audioEnter()) {                 // (another thread holds the instance: CoreAccess.h)
        buffer.clear();
        midi.clear();
        return;
    }
    access_.audioServe();                        // a call posted by another thread: at this block's first sample
    uint32_t nev = 0;
    for (const auto meta : midi) {
        const int len = meta.numBytes;
        if (len < 1 || len > 3 || meta.samplePosition < 0) continue;   // (SysEx and the like: not for the core)
        if (nev == (uint32_t)kMaxEvents) { midiDropped_++; continue; }
        MidiEvent& e = events_[nev++];
        e.offset = (uint32_t)juce::jmin(meta.samplePosition, n - 1);
        e.len = (uint8_t)len;
        e.data[0] = meta.data[0];
        e.data[1] = len > 1 ? meta.data[1] : 0;
        e.data[2] = len > 2 ? meta.data[2] : 0;
    }
    midi.clear();                                // (no MIDI out: spec, first version)
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    if (R)
        runner_.process(L, R, (uint32_t)n, events_.data(), nev);
    else {                                       // (a mono bus never offered; kept harmless)
        buffer.clear();
    }
    for (int c = 2; c < buffer.getNumChannels(); c++) buffer.clear(c, 0, n);
    access_.audioExit();
}

// ---- a placeholder window until the panel (plan Task 7)
namespace {
class PlaceholderEditor : public juce::AudioProcessorEditor {
public:
    explicit PlaceholderEditor(Processor& p) : juce::AudioProcessorEditor(p) { setSize(480, 160); }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1b1b1b));
        g.setColour(juce::Colour(0xffe8e2d4));
        g.setFont(juce::FontOptions(28.0f, juce::Font::bold));
        g.drawText(JucePlugin_Name, getLocalBounds().removeFromTop(90), juce::Justification::centred);
        g.setFont(juce::FontOptions(14.0f));
        g.drawText("the KNOBI-SIX firmware's engine - play it from MIDI; the panel comes later",
                   getLocalBounds().removeFromBottom(70), juce::Justification::centred);
    }
};
} // namespace

juce::AudioProcessorEditor* Processor::createEditor() { return new PlaceholderEditor(*this); }

} // namespace knobi

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new knobi::Processor(); }
