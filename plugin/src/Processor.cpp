// SPDX-License-Identifier: GPL-3.0-only
#include "Processor.h"

namespace knobi {

namespace {
thread_local bool tlInProcess = false;          // inside processBlock on this thread (it owns the instance)
}

Processor::Processor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    runner_.prepare(44100.0, 512);               // (until the host prepares: the latency of 44.1 kHz)
    setLatencySamples(runner_.latency());
    lastCore_.fill(Params::kNone);
    if (runner_.ok()) scanCore();                // (no thread has the instance yet: the init program's values)
    params_.discardToHost();                      // (they are the defaults, as the parameters were made)
    if (startHostSyncTimer.load()) startTimerHz(30);  // the core's changes to the host (syncHost), the message thread
}

Processor::~Processor() { stopTimer(); }

// ---- parameters (Params.h)
void Processor::scanCore()
{
    kc_t* k = runner_.core();
    if (!k) return;
    for (int i = 0; i < params_.size(); i++) {
        const uint16_t id = params_.entry(i).id;
        if (runner_.paramPending(id)) continue;   // (a host value on its way: the core's is not the news)
        const int32_t v = kc_param_get(k, id);
        if (v != lastCore_[id]) lastCore_[id] = v, params_.coreChanged(id, v);
    }
    int32_t num = 0;
    int ed = 0;
    kc_program_get(k, &num, nullptr, nullptr, &ed);
    if (program_.exchange(num, std::memory_order_acq_rel) != num) programMoved_.store(true);
    edited_.store(ed != 0, std::memory_order_release);
}

void Processor::applyHostChanges()
{
    params_.takeHostChanges([this](uint16_t id, int32_t v) {
        lastCore_[id] = v;                        // (the core gets the host's value: not reported back)
        runner_.paramAt(0, id, v);
    });
}

void Processor::syncHost()
{
    params_.syncToHost();
    // a program load the host did not ask for (MIDI program change): AU hosts refresh the preset shown
    // (kAudioUnitProperty_PresentPreset). Not on VST3: JUCE reports it there as a gesture on its "Program" parameter
    // (an automation write), which this plugin never sends for a core change
    if (programMoved_.exchange(false) && wrapperType == wrapperType_AudioUnit)
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withProgramChanged(true));
}

// ---- programs
void Processor::setCurrentProgram(int index)
{
    if (index < 0 || index >= (int)KC_PROGRAMS || !runner_.ok()) return;
    // the bank's program + sequence, as a program change loads it (ProgramBank: the same bank, the firmware's loading),
    // read here (not the audio thread) and handed to the core as a load (kc_program_set, not edited): the core reports
    // the new program at once (kc_program_get), even while the engine still takes an earlier load
    struct Ctx { Processor* p; int n; bool have; program_t prog; seq_t seq; } c{this, index, false, {}, {}};
    if (!tlInProcess) c.have = bank_.program(index, &c.prog, &c.seq);
    auto fn = [](void* v) {                      // (copies only: it may run on the audio thread)
        auto* x = (Ctx*)v;
        if (!x->have || kc_program_set(x->p->runner_.core(), x->n, &x->prog, &x->seq, 0))
            kc_program_select(x->p->runner_.core(), x->n);   // (in a host's processBlock: as a program change)
        x->p->scanCore();
        x->p->programMoved_.store(false);         // (the host asked for it)
    };
    if (tlInProcess) fn(&c);                      // (a host calling it from its processBlock: the instance is ours)
    else access_.run(fn, &c);
    if (juce::MessageManager::existsAndIsCurrentThread()) params_.syncToHost();
}

const juce::String Processor::getProgramName(int index)
{
    if (index < 0 || index >= (int)KC_PROGRAMS) return {};
    std::lock_guard<std::mutex> g(namesLock_);
    if (!namesValid_) {
        for (int i = 0; i < (int)KC_PROGRAMS; i++)
            names_[(size_t)i] = juce::String(i).paddedLeft('0', 3) + " " + juce::String(bank_.name(i));
        namesValid_ = true;
    }
    return names_[(size_t)index];
}

bool Processor::setBank(const kc_bank_t* bank)
{
    if (!bank_.setBank(bank)) return false;
    if (runner_.ok()) kc_bank_set(runner_.core(), bank);   // (any thread, any time: knobi_core.h)
    std::lock_guard<std::mutex> g(namesLock_);
    namesValid_ = false;
    return true;
}

// ---- state (StateCodec.h): the message thread, through access()
void Processor::snapshot(Snapshot& s)
{
    struct Ctx { kc_t* k; Snapshot* s; } c{runner_.core(), &s};
    s.number = 0, s.edited = 0;
    program_init(&s.program);
    seq_clear(&s.seq);
    if (!c.k) return;
    auto fn = [](void* v) {                      // (copies only: it may run on the audio thread)
        auto* x = (Ctx*)v;
        kc_program_get(x->k, &x->s->number, &x->s->program, &x->s->seq, &x->s->edited);
    };
    if (tlInProcess) fn(&c);
    else access_.run(fn, &c);
}

bool Processor::differsFromBank()
{
    Snapshot s;
    snapshot(s);
    program_t p;
    seq_t q;
    if (!bank_.program(s.number, &p, &q)) return false;
    return !ProgramBank::same(s.program, s.seq, p, q);
}

void Processor::getStateInformation(juce::MemoryBlock& dest)
{
    Snapshot s;
    snapshot(s);
    SetState st;
    st.number = s.number, st.program = s.program, st.seq = s.seq, st.edited = s.edited != 0;
    program_t p;
    seq_t q;
    st.differs = bank_.program(s.number, &p, &q) && !ProgramBank::same(s.program, s.seq, p, q);
    const auto bytes = StateCodec::encode(st);
    dest.replaceAll(bytes.data(), bytes.size());
}

void Processor::setStateInformation(const void* data, int size)
{
    SetState st;
    if (size <= 0 || StateCodec::decode(data, (size_t)size, st) != StateCodec::kOk || !runner_.ok())
        return;                                   // (no state, a broken one or a newer format: nothing changes)
    // differs from the bank as it is now (the bank may have changed since the set was saved); no bank answer: as saved
    program_t p;
    seq_t q;
    const bool differs = bank_.program(st.number, &p, &q) ? !ProgramBank::same(st.program, st.seq, p, q) : st.differs;
    struct Ctx { Processor* p; SetState* st; int rc; } c{this, &st, -1};
    auto fn = [](void* v) {                      // (copies only: it may run on the audio thread)
        auto* x = (Ctx*)v;
        x->p->params_.dropHostChanges();          // (host changes made before the load are older than it)
        x->rc = kc_program_set(x->p->runner_.core(), x->st->number, &x->st->program, &x->st->seq, x->st->edited ? 1 : 0);
        x->p->scanCore();
        x->p->programMoved_.store(false);
    };
    if (tlInProcess) fn(&c);
    else if (!access_.run(fn, &c)) return;
    if (c.rc != 0) return;
    stateDiffers_.store(differs);
    // the parameters show the loaded program now (the wrappers are restoring state: JUCE's AU and VST3 wrappers send
    // no automation for this and re-read every value afterwards -- VST3 setComponentState: kParamValuesChanged)
    if (!tlInProcess) params_.syncToHost();
}

bool Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.inputBuses.isEmpty() && layouts.outputBuses.size() == 1 &&
           layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void Processor::prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)
{
    runner_.prepare(sampleRate, maximumExpectedSamplesPerBlock);   // (allocates: never on the audio thread)
    access_.setIdle(sampleRate, maximumExpectedSamplesPerBlock);
    setLatencySamples(runner_.latency());
    skipped_ = 0, fadeIn_ = false, lastL_ = lastR_ = 0.0f;   // (a new timeline; kept MIDI still goes in, next block)
}

void Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0) return;
    const juce::ScopedValueSetter<bool> inProcess(tlInProcess, true);
    if (!access_.audioEnter()) {                 // another thread holds the instance (CoreAccess.h: the host resumed
        silentBlocks_++;                         // during a call it ran itself after a real idle): this block silent,
        for (const auto meta : midi) {           // its MIDI kept for the next block (in order; overflow: all notes off)
            const int len = meta.numBytes;
            if (len < 1 || len > 3) continue;
            if (pendingN_ == (uint32_t)kPending) { pendingOverflow_ = true; continue; }
            MidiEvent& e = pending_[pendingN_++];
            e.offset = 0, e.len = (uint8_t)len;
            e.data[0] = meta.data[0], e.data[1] = len > 1 ? meta.data[1] : 0, e.data[2] = len > 2 ? meta.data[2] : 0;
        }
        midi.clear();
        skipped_ += (uint64_t)n;                 // (rendered later, output dropped: the timeline stays the host's)
        for (int c = 0; c < buffer.getNumChannels(); c++) {   // no hard cut: the last output faded out over kFade
            float* o = buffer.getWritePointer(c);
            const float v = c == 0 ? lastL_ : c == 1 ? lastR_ : 0.0f;
            for (int i = 0; i < n; i++) o[i] = i < kFade ? v * (float)(kFade - 1 - i) / (float)kFade : 0.0f;
        }
        lastL_ = lastR_ = 0.0f;
        fadeIn_ = true;
        return;
    }
    access_.audioServe();                        // a call posted by another thread: at this block's first sample
    if (skipped_ || pendingN_ || pendingOverflow_) {   // after silent blocks: their frames, their MIDI at their start
        if (pendingOverflow_)
            for (uint8_t ch = 0; ch < 16; ch++) {
                MidiEvent& e = pending_[pendingN_++];
                e.offset = 0, e.len = 3, e.data[0] = (uint8_t)(0xB0u | ch), e.data[1] = 123, e.data[2] = 0;
            }
        uint64_t left = skipped_;
        while (left) {                           // (in parts: skip takes a uint32_t)
            uint32_t part = left > 0x40000000u ? 0x40000000u : (uint32_t)left;
            runner_.skip(part, pending_.data(), left == skipped_ ? pendingN_ : 0u);
            left -= part;
        }
        if (!skipped_) runner_.skip(0, pending_.data(), pendingN_);
        skipped_ = 0, pendingN_ = 0, pendingOverflow_ = false;
    }
    applyHostChanges();                          // automation etc.: at this block's first sample, before its MIDI
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
    scanCore();                                   // CC, program change: the host is told later (syncHost)
    if (R) {
        if (fadeIn_) {                           // (after a silent block: in over kFade, no hard step)
            for (int i = 0; i < n && i < kFade; i++) {
                const float g = (float)(i + 1) / (float)kFade;
                L[i] *= g, R[i] *= g;
            }
            fadeIn_ = false;
        }
        lastL_ = L[n - 1], lastR_ = R[n - 1];
    }
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
