// SPDX-License-Identifier: GPL-3.0-only
#include "Processor.h"
#include <cstring>
#include "ui/Editor.h"

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
    // the shared library (Library.h): its bank and globals now, its later snapshots as they come
    attachLibrary(Library::shared(Library::defaultPath()), true);
    if (startHostSyncTimer.load()) startTimerHz(30);  // the core's changes to the host (syncHost), the message thread
}

Processor::~Processor()
{
    stopTimer();
    if (lib_ && listener_) lib_->removeListener(listener_);   // (waits for a call in progress: none after this)
}

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
    seqPlaying_.store(kc_seq_playing(k) != 0, std::memory_order_release);   // (the panel's SEQ PLAY and state)
    seqState_.store(kc_seq_state(k), std::memory_order_release);
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
    collectBanks();                               // (snapshots the instance has moved on from: freed here)
    params_.syncToHost();
    // a program load the host did not ask for (MIDI program change): AU hosts refresh the preset shown
    // (kAudioUnitProperty_PresentPreset). Not on VST3: JUCE reports it there as a gesture on its "Program" parameter
    // (an automation write), which this plugin never sends for a core change
    if (programMoved_.exchange(false) && wrapperType == wrapperType_AudioUnit)
        updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withProgramChanged(true));
    // the bank changed (a WRITE, another instance's, the file reloaded): the program list's names. AU: no "factory
    // presets changed" notification exists; programChanged makes hosts re-read the present preset; the list is
    // re-read when the host asks for it again. VST3: no way to tell the host in JUCE 8.0.15 -- parameterInfoChanged
    // reaches restartComponent(kParamTitlesChanged) only when a parameter's own title / units / steps changed, never
    // for program names, and programChanged would be written as a gesture on JUCE's "Program" parameter (not sent).
    // parameterInfoChanged is still sent (harmless; listeners in the process see it). plugin/README.md: limitation
    if (namesMoved_.exchange(false)) {
        auto d = juce::AudioProcessorListener::ChangeDetails().withParameterInfoChanged(true);
        if (wrapperType == wrapperType_AudioUnit) d = d.withProgramChanged(true);
        updateHostDisplay(d);
    }
}

// ---- programs
void Processor::setCurrentProgram(int index)
{
    if (index < 0 || index >= (int)KC_PROGRAMS || !runner_.ok()) return;
    restored_.store(-1);
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
    {
        std::lock_guard<std::mutex> b(bankLock_);
        if (!bank_.setBank(bank)) return false;
        if (runner_.ok()) kc_bank_set(runner_.core(), bank);   // (any thread, any time: knobi_core.h)
        if (held_) retired_.push_back(std::move(held_));      // (a caller's bank now: the snapshot released later)
        collectLocked();
    }
    bankGen_.fetch_add(1, std::memory_order_acq_rel);
    std::lock_guard<std::mutex> g(namesLock_);
    namesValid_ = false;
    return true;
}

// ---- the library (plan Task 6; Library.h)
void Processor::attachLibrary(std::shared_ptr<Library> lib) { attachLibrary(std::move(lib), false); }

void Processor::attachLibrary(std::shared_ptr<Library> lib, bool constructing)
{
    if (lib_ && listener_) lib_->removeListener(listener_);
    listener_ = 0;
    {
        std::lock_guard<std::mutex> b(bankLock_);
        libGen_ = 0;                              // (another library: its generations count from its own start)
    }
    {
        std::lock_guard<std::mutex> g(globalsLock_);
        globalsGen_ = 0;
    }
    lib_ = std::move(lib);
    if (!lib_) return;
    listener_ = lib_->addListener([this](const std::shared_ptr<const LibrarySnapshot>& s) { onLibrary(s, false); });
    onLibrary(lib_->snapshot(), constructing);    // (a newer one given meanwhile by the listener wins: generation)
}

void Processor::deliverLibrarySnapshot(const std::shared_ptr<const LibrarySnapshot>& s) { onLibrary(s, false); }

void Processor::onLibrary(const std::shared_ptr<const LibrarySnapshot>& s, bool constructing)
{
    if (!s || !takeSnapshot(s)) return;
    if (testHooks.beforeGlobals) testHooks.beforeGlobals(*this, s->generation);
    applyGlobals(*s, constructing);
}

bool Processor::takeSnapshot(const std::shared_ptr<const LibrarySnapshot>& s)
{
    {
        std::lock_guard<std::mutex> b(bankLock_);
        if (s->generation <= libGen_) return false;   // (older than the one taken: snapshots only move forward)
        // ProgramBank first: once its setBank returns its scratch instance never reads the old bank (it sets its bank
        // under its mutex at every read); then the playing instance (taken at its next call)
        if (!bank_.setBank(&s->bank)) return false;   // (never: the library gives only banks the firmware would store)
        if (runner_.ok()) kc_bank_set(runner_.core(), &s->bank);
        if (held_) retired_.push_back(std::move(held_));
        held_ = s;
        libGen_ = s->generation;
        collectLocked();
    }
    namesMoved_.store(true);                      // (the host's program list: syncHost tells it)
    std::lock_guard<std::mutex> g(namesLock_);
    namesValid_ = false;
    return true;
}

void Processor::applyGlobals(const LibrarySnapshot& s, bool constructing)
{
    struct Ctx { kc_t* k; int8_t v[G_COUNT]; } c{runner_.core(), {}};
    if (!c.k) return;
    std::memcpy(c.v, s.globals, sizeof c.v);
    auto fn = [](void* v) {                      // (copies only: it may run on the audio thread)
        auto* x = (Ctx*)v;
        for (int g : Library::kGlobals) kc_global_set(x->k, g, x->v[g]);
        kc_global_set(x->k, G_MIDI_CH, 0);        // (omni in the plugin: Live routes the MIDI)
    };
    // two snapshots' listeners may run at once (a reload and a WRITE): the globals only move forward -- an older
    // generation than the one applied is dropped, the check and the apply under one lock (never the audio thread's)
    std::lock_guard<std::mutex> g(globalsLock_);
    if (s.generation <= globalsGen_) return;
    if (constructing || tlInProcess) fn(&c);      // (the constructor: no thread has the instance yet)
    else if (!access_.run(fn, &c)) return;        // (not applied: a later snapshot may still)
    globalsGen_ = s.generation;
}

void Processor::collectLocked()                  // (bankLock_ held; never the audio thread)
{
    if (retired_.empty()) return;
    // kc_bank_in_use asked after the replacing kc_bank_set: a bank it does not name is never read again
    const kc_bank_t* inUse = runner_.ok() ? kc_bank_in_use(runner_.core()) : nullptr;
    for (size_t i = retired_.size(); i-- > 0;)
        if (&retired_[i]->bank != inUse) retired_.erase(retired_.begin() + (long)i);
}

void Processor::collectBanks()
{
    std::lock_guard<std::mutex> b(bankLock_);
    collectLocked();
}

size_t Processor::heldBanks()
{
    std::lock_guard<std::mutex> b(bankLock_);
    return (held_ ? 1u : 0u) + retired_.size();
}

bool Processor::libraryUnreadable()
{
    std::lock_guard<std::mutex> b(bankLock_);
    return held_ && held_->unreadable;
}

bool Processor::libraryNewer()
{
    std::lock_guard<std::mutex> b(bankLock_);
    return held_ && held_->newer;
}

int Processor::libraryBadSlots()
{
    std::lock_guard<std::mutex> b(bankLock_);
    return held_ ? held_->badSlots : 0;
}

bool Processor::userSlot(int n)
{
    if (n < 0 || n >= (int)KC_PROGRAMS) return false;
    std::lock_guard<std::mutex> b(bankLock_);
    const kc_bank_t* bank = bank_.bank();         // (set only under bankLock_: alive while it is held)
    return bank && bank->used[n];
}

Library::Result Processor::setGlobal(int g, int v)
{
    if (!lib_ || tlInProcess) return Library::kIoError;   // (file I/O: never the audio thread)
    return lib_->setGlobal(g, v);                 // (this instance follows through its listener)
}

int32_t Processor::global(int g)
{
    struct Ctx { kc_t* k; int g; int32_t v; } c{runner_.core(), g, 0};
    if (!c.k) return 0;
    auto fn = [](void* v) { auto* x = (Ctx*)v; x->v = kc_global_get(x->k, x->g); };
    if (tlInProcess) fn(&c);
    else access_.run(fn, &c);
    return c.v;
}

void Processor::globalsNow(int32_t out[G_COUNT])
{
    struct Ctx { kc_t* k; int32_t* out; } c{runner_.core(), out};
    for (int g = 0; g < G_COUNT; g++) out[g] = GLOB_DEF[g];
    if (!c.k) return;
    auto fn = [](void* v) {
        auto* x = (Ctx*)v;
        for (int g = 0; g < G_COUNT; g++) x->out[g] = kc_global_get(x->k, g);
    };
    if (tlInProcess) fn(&c);
    else access_.run(fn, &c);
}

void Processor::loadAs(int n, const program_t& p, const seq_t& s, int edited)
{
    struct Ctx { Processor* p; int n; const program_t* prog; const seq_t* seq; int edited; } c{this, n, &p, &s, edited};
    auto fn = [](void* v) {                      // (copies only: it may run on the audio thread)
        auto* x = (Ctx*)v;
        kc_program_set(x->p->runner_.core(), x->n, x->prog, x->seq, x->edited);
        x->p->scanCore();
        x->p->programMoved_.store(false);         // (asked for here)
    };
    if (tlInProcess) fn(&c);
    else access_.run(fn, &c);
    if (juce::MessageManager::existsAndIsCurrentThread()) params_.syncToHost();
}

Library::Result Processor::writeProgram(int n)
{
    if (!lib_ || !runner_.ok() || tlInProcess) return Library::kIoError;   // (file I/O: never the audio thread)
    Snapshot s;
    if (!snapshot(s)) return Library::kIoError;   // (number and content agree: snapshot(); no answer: nothing written)
    if (n < 0) n = s.number;
    if (n < 0 || n >= (int)KC_PROGRAMS) return Library::kInvalid;
    // RESTORE FACTORY then WRITE on the same number, unchanged since: the slot emptied (the factory program again)
    program_t fp;
    seq_t fs;
    const bool factory = restored_.load() == n && s.number == n && bank_.factory(n, &fp, &fs) &&
                         ProgramBank::same(s.program, s.seq, fp, fs);
    if (testHooks.afterSnapshot) testHooks.afterSnapshot(*this);   // (tests: something arriving while the file is written)
    const Library::Result r = factory ? lib_->clear(n) : lib_->write(n, s.program, s.seq);
    if (r != Library::kOk) return r;
    restored_.store(-1);
    // as the keyboard's WRITE: the edit buffer is now program n, no load (the sound, the sequencer, the arp play on);
    // "edited" clears unless the edit buffer changed since the snapshot (an edit made meanwhile stays an edit). Not
    // marked when a load came after the snapshot (a MIDI program change, a set state: kc_program_loads -- that load
    // is the program in play, it keeps its number) or one is on its way: kStoredOnly
    struct Ctx { Processor* p; int n; const Snapshot* s; int rc; } c{this, n, &s, -3};
    auto fn = [](void* v) {                      // (copies only: it may run on the audio thread)
        auto* x = (Ctx*)v;
        x->rc = kc_program_mark_stored(x->p->runner_.core(), x->n, &x->s->program, &x->s->seq, x->s->loads);
        x->p->scanCore();
        x->p->programMoved_.store(false);         // (asked for here)
    };
    const bool ran = access_.run(fn, &c);
    if (juce::MessageManager::existsAndIsCurrentThread()) params_.syncToHost();
    // (a program change still on its way when the snapshot was taken, written to its own number: that load is
    // exactly what was stored, unedited -- nothing to mark)
    const bool loadIsIt = ran && c.rc == -1 && s.pending && n == s.number;
    return ran && (c.rc >= 0 || loadIsIt) ? Library::kOk : Library::kStoredOnly;
}

bool Processor::restoreFactory()
{
    if (!runner_.ok() || tlInProcess) return false;
    Snapshot s;
    if (!snapshot(s)) return false;
    program_t fp;
    seq_t fs;
    if (!bank_.factory(s.number, &fp, &fs)) return false;
    loadAs(s.number, fp, fs, 1);                  // (edited: nothing stored until WRITE)
    restored_.store(s.number);
    return true;
}

// ---- state (StateCodec.h): the message thread, through access()
bool Processor::snapshot(Snapshot& s)
{
    struct Ctx { kc_t* k; Snapshot* s; int pending; } c{runner_.core(), &s, 0};
    s.number = 0, s.edited = 0, s.loads = 0, s.pending = false;
    program_init(&s.program);
    seq_clear(&s.seq);
    if (!c.k) return false;
    auto fn = [](void* v) {                      // (copies only: it may run on the audio thread)
        auto* x = (Ctx*)v;
        kc_program_get(x->k, &x->s->number, &x->s->program, &x->s->seq, &x->s->edited);
        x->pending = kc_program_pending(x->k);
        x->s->loads = kc_program_loads(x->k);     // (with the content: kc_program_mark_stored's "a load since?")
    };
    if (tlInProcess) fn(&c);
    else if (!access_.run(fn, &c)) return false;
    // a program change the engine has not taken yet: the core reports its number with the previous edit buffer
    // (kc_program_pending); what that load gives is program number as the bank holds it, unedited -- number and
    // content always agree here (WRITE, the set state)
    program_t p;
    seq_t q;
    s.pending = c.pending != 0;
    if (c.pending && !tlInProcess && bank_.program(s.number, &p, &q)) s.program = p, s.seq = q, s.edited = 0;
    return true;
}

void Processor::seqPlay(bool on)
{
    struct Ctx { Processor* p; int on; } c{this, on ? 1 : 0};
    if (!runner_.ok()) return;
    auto fn = [](void* v) {                      // (it may run on the audio thread)
        auto* x = (Ctx*)v;
        kc_seq_play(x->p->runner_.core(), x->on);
        x->p->scanCore();
    };
    if (tlInProcess) fn(&c);
    else access_.run(fn, &c);
}

bool Processor::programInfo(ProgramInfo& out)
{
    Snapshot s;
    if (!snapshot(s)) return false;
    char name[PROGRAM_NAME_LEN + 1];
    std::memcpy(name, s.program.name, PROGRAM_NAME_LEN);
    name[PROGRAM_NAME_LEN] = 0;
    out.number = s.number, out.name = name, out.edited = s.edited != 0;
    while (!out.name.empty() && out.name.back() == ' ') out.name.pop_back();
    program_t p;
    seq_t q;
    out.differs = bank_.program(s.number, &p, &q) && !ProgramBank::same(s.program, s.seq, p, q);
    out.user = userSlot(s.number);
    return true;
}

bool Processor::differsFromBank()
{
    Snapshot s;
    if (!snapshot(s)) return false;
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
    restored_.store(-1);
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
    Transport tr;                                 // the host's transport at this block's first sample (plan Task 5)
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition()) {
            const auto ppq = pos->getPpqPosition();
            tr.playing = pos->getIsPlaying() && ppq.hasValue();   // (no position: as stopped)
            if (ppq) tr.ppq = *ppq;
            if (const auto bpm = pos->getBpm()) tr.bpm = *bpm, hostBpm_.store(*bpm, std::memory_order_release);
            if (const auto loop = pos->getLoopPoints(); loop && pos->getIsLooping())
                tr.looping = true, tr.loopStart = loop->ppqStart, tr.loopEnd = loop->ppqEnd;
        }
    runner_.transport(tr, (uint32_t)n);
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    if (R)
        runner_.process(L, R, (uint32_t)n, events_.data(), nev);
    else {                                       // (a mono bus never offered; kept harmless)
        buffer.clear();
    }
    for (int c = 2; c < buffer.getNumChannels(); c++) buffer.clear(c, 0, n);
    scanCore();                                   // CC, program change: the host is told later (syncHost)
    following_.store(runner_.following(), std::memory_order_release);   // (P_BPM greyed: the panel)
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

// ---- the panel (plan Task 7; src/ui/Editor.h)
juce::AudioProcessorEditor* Processor::createEditor() { return new ui::Editor(*this); }

} // namespace knobi

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new knobi::Processor(); }
