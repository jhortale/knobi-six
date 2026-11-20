// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin: the AudioProcessor. One core instance per plugin instance (CoreRunner), driven on the audio thread;
// MIDI at its sample offsets; 44.1 kHz inside, resampled to the session's rate; the latency reported to the host.
// Other threads reach the instance only through access() (CoreAccess.h).
// Plan Task 4: the parameters (Params.h: layout, host <-> core sync, no loop), the host's program list (500 programs,
// following MIDI program change + bank select: the core's), the bank as the plugin sees it (ProgramBank.h: the seam
// plan Task 6's library plugs into, setBank) and the set state (StateCodec.h; through access(), never kc_* directly).
// Plan Task 6: the shared library (Library.h: the bank's user layer and the globals from ~/Music/KNOBI-SIX/library.knobi,
// shared by every instance; WRITE, RESTORE FACTORY, the program browser's numbers; the bank release protocol below).
#pragma once
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "CoreAccess.h"
#include "CoreRunner.h"
#include "Library.h"
#include "Params.h"
#include "ProgramBank.h"
#include "StateCodec.h"

namespace knobi {

class Processor;
struct ProcessorTestHooks {                       // (tests only: Processor::testHooks)
    void (*afterSnapshot)(Processor&) = nullptr;
    void (*beforeGlobals)(Processor&, uint64_t generation) = nullptr;
};

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
    // an offline render (export, freeze: the host's isNonRealtime) never outputs a silent block: processBlock waits for
    // the instance instead, and the seam never takes it on an asking thread meanwhile (CoreAccess::setOffline)
    void setNonRealtime(bool isNonRealtime) noexcept override;
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
    // Loading it: the stored copy wins over the bank's program (spec "Set state"); the parameters follow. Saving it
    // when the instance does not answer (access().run timed out): the last state saved or loaded that worked, as it
    // was (never INIT in place of the set's sound; INIT only if there is none yet)
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

    // ---- the shared library (plan Task 6; Library.h). The constructor attaches Library::shared(Library::defaultPath()):
    // its bank (user layer) and globals, and every later snapshot (another instance's WRITE, the file changed). The
    // program browser (the panel, Task 7): number = bank * 100 + tens * 10 + ones (bank 0..4), as the keyboard
    void attachLibrary(std::shared_ptr<Library> lib);   // (the tests: another file; nullptr: none)
    Library* library() { return lib_.get(); }
    // WRITE: the program in play + its sequence into slot n (-1: its own number); the instance's program is then n, not
    // edited, with no load (as the keyboard's WRITE: the sound and the sequencer play on; kc_program_mark_stored; an
    // edit made while the file is written stays an edit). Library::kStoredOnly: the file has it but the instance was
    // not marked (a load came meanwhile -- a MIDI program change, a set state: it keeps that program's number -- or
    // the instance did not answer): the panel says so. The content stored is always the content
    // of the number reported (a program change still waiting for the engine: that program as the bank gives it,
    // kc_program_pending). After restoreFactory on n and no change since, WRITE n empties the slot (factory)
    Library::Result writeProgram(int n = -1);
    // RESTORE FACTORY: the factory program of the number in play + its sequence into the edit buffer, marked edited;
    // nothing stored until WRITE (the slot keeps what it holds). false: no instance
    bool restoreFactory();
    Library::Result setGlobal(int g, int v);    // one of Library::kGlobals: into the file, every instance follows
    int32_t global(int g);                       // the instance's value now
    void globalsNow(int32_t out[G_COUNT]);       // every global's value now, in one call through access()
    // the file could not be read: the factory layer. The panel (Task 7) shows the notice and asks before the first
    // WRITE over it (a WRITE keeps the bad file as library.knobi.bad-<time> first)
    bool libraryUnreadable();
    bool libraryNewer();                         // (unreadable) a newer version's file: WRITE refused (Library::kNewer)
    int libraryBadSlots();                       // slots the firmware would not store: their factory program (notice)
    bool userSlot(int n);                        // slot n holds a WRITE (else: the factory program)
    static int bankOf(int n) { return n / 100; }
    static int tensOf(int n) { return n / 10 % 10; }
    static int onesOf(int n) { return n % 10; }
    static int programOf(int bank, int tens, int ones) { return bank * 100 + tens * 10 + ones; }
    // the bank release (the message thread; syncHost calls it): snapshots replaced and no longer read are freed
    void collectBanks();
    size_t heldBanks();                          // (tests) snapshots held: the current one + those not yet released
    // (tests only; nullptr in the plugin) afterSnapshot: writeProgram, after its snapshot, before the file is written;
    // beforeGlobals: a library snapshot taken, its globals not applied yet
    static inline ProcessorTestHooks testHooks{};
    void deliverLibrarySnapshot(const std::shared_ptr<const LibrarySnapshot>& s);   // (tests) as the library's listener

    CoreAccess& access() { return access_; }
    CoreRunner& runner() { return runner_; }     // (the audio thread, or inside access().run)
    uint64_t midiDropped() const { return midiDropped_; }
    uint64_t silentBlocks() const { return silentBlocks_; }   // blocks output silent (CoreAccess.h)
    // the clock follows the host's transport (Live playing): P_BPM is not used, the panel shows it greyed (plan Task
    // 7). Any thread; as of the last block
    bool followingHost() const { return following_.load(std::memory_order_acquire); }
    // the host's tempo as of the last block (the panel shows it in place of P_BPM while followingHost())
    double hostBpm() const { return hostBpm_.load(std::memory_order_acquire); }

    // ---- the panel (plan Task 7; src/ui). It never calls the core: these (and the parameters) are its only way in
    // SEQ PLAY, as the panel's button (kc_seq_play: recording ends first); through access(). The state as of the last
    // block (or this call): seqPlaying() the button's state, seqState() 0 stopped / 1 playing / 2 armed (waiting for
    // Live's play)
    void seqPlay(bool on);
    bool seqPlaying() const { return seqPlaying_.load(std::memory_order_acquire); }
    int seqState() const { return seqState_.load(std::memory_order_acquire); }
    // the program browser's line: the program in play (number, name, edited) and whether it differs from the bank's
    // program of its number; one snapshot through access() (the message thread; it may wait for the next block, or
    // the seam's idle time with no audio running: call it sparingly). false: the instance did not answer
    struct ProgramInfo { int number = 0; std::string name; bool edited = false, differs = false, user = false; };
    bool programInfo(ProgramInfo& out);
    uint32_t bankGeneration() const { return bankGen_.load(std::memory_order_acquire); }   // +1 each bank change

    // ---- the sequencer grid and the step entry (plan Task 8; ui/SeqGrid.h). Through access() (the message thread; it
    // may wait for the next block): seqGet the edit buffer's sequence; seqSet a sequence as an edit of it (kc_seq_set: 0
    // set, edited; 1 the same, nothing done; -1 refused: not a sequence the firmware would store; -2 refused: recording;
    // -3 the instance did not answer). The step entry, the keyboard's rules (knobi_core.h): seqRecButton SEQ REC pressed
    // / let go (while playing: tap = stop and record, held + a Live MIDI note = transpose); seqRecRest / Tie / Back the
    // panel's REST, TIE, BACK (1 changed, 0 not recording, -1 refused, -3 no answer). While recording, Live's MIDI notes
    // are recorded (the core's MIDI in)
    // seqSet with expect >= 0: refused (-4, nothing set) when the sequence's version is no longer expect (it changed
    // since the caller read it: a program change, a recorded step); checked in the same call as the set.
    // seqRecButton(-1): the hold dropped with no action (the panel closed while SEQ REC was held)
    bool seqGet(seq_t& out);
    int seqSet(const seq_t& s, int64_t expect = -1);
    void seqRecButton(int down);
    int seqRecRest();
    int seqRecTie();
    int seqRecBack();
    // published by the audio thread after every block (and by the calls above): atomics, no access(). seqVersion moves
    // when the sequence may have changed (an edit, a recorded step, a load, a WRITE); seqStep the step playing (-1 none);
    // seqRecState knobi_core.h kc_seq_rec_state (KC_REC_*); seqXp the transpose playing
    uint32_t seqVersion() const { return seqVer_.load(std::memory_order_acquire); }
    int seqStep() const { return seqStep_.load(std::memory_order_acquire); }
    int seqRecState() const { return seqRec_.load(std::memory_order_acquire); }
    int seqXp() const { return seqXp_.load(std::memory_order_acquire); }
    // the editor's size (1.0 .. 2.0 of its default), kept while the instance lives (not in the set state)
    std::atomic<float> editorScale{1.0f};

private:
    void timerCallback() override { syncHost(); }
    void scanCore();                              // the core's values -> Params (alone with the core)
    void applyHostChanges();                      // Params -> the core at the block's first sample (the audio thread)
    struct Snapshot { int32_t number; program_t program; seq_t seq; int edited; uint32_t loads; bool pending; };
    bool snapshot(Snapshot& s);                   // the program in play (through access()): number + content agree;
                                                  // false: the instance did not answer (s: init)
    void attachLibrary(std::shared_ptr<Library> lib, bool constructing);
    void onLibrary(const std::shared_ptr<const LibrarySnapshot>& s, bool constructing);   // (not the audio thread)
    bool takeSnapshot(const std::shared_ptr<const LibrarySnapshot>& s);  // its bank -> ProgramBank + the core
    void applyGlobals(const LibrarySnapshot& s, bool constructing);      // Library::kGlobals + MIDI CH omni
    void collectLocked();
    void loadAs(int n, const program_t& p, const seq_t& s, int edited);  // a load through access()

    // The bank release protocol (knobi_core.h kc_bank_set / kc_bank_in_use; ProgramBank.h). Two readers hold a bank:
    // the playing instance (kc_bank_set: taken at its next call, any thread) and ProgramBank's scratch instance (only
    // under ProgramBank's mutex, which sets its bank at every read: once ProgramBank::setBank has returned it never
    // reads the old bank again). A snapshot replaced goes to retired_; collectBanks frees a retired one once
    // kc_bank_in_use (asked after the replacing set) names another bank -- the instance has moved on and never reads
    // it again. The audio thread never holds a shared_ptr, so it never frees: frees happen in collectBanks (the
    // replacing thread, the message thread's timer) or at destruction (after the instance is destroyed: these are
    // declared before runner_ and bank_, so destroyed after them). bankLock_ is never taken by the audio thread.
    std::mutex bankLock_;
    std::shared_ptr<const LibrarySnapshot> held_;                 // the snapshot the instance is given (or none)
    std::vector<std::shared_ptr<const LibrarySnapshot>> retired_;
    uint64_t libGen_ = 0;                         // (bankLock_) the library snapshot taken last
    std::mutex globalsLock_;                      // (never the audio thread) the globals applied in generation order
    uint64_t globalsGen_ = 0;                     // (globalsLock_) the snapshot whose globals were applied last
    std::shared_ptr<Library> lib_;
    int listener_ = 0;
    std::atomic<bool> namesMoved_{false};         // the bank changed: syncHost tells the host (program names)
    std::atomic<int> restored_{-1};               // restoreFactory's number (WRITE then empties the slot), -1 none
    std::mutex stateLock_;                        // (never the audio thread's processBlock)
    std::vector<uint8_t> lastState_;              // the last set state saved or loaded that worked (stateLock_)

    CoreRunner runner_;
    CoreAccess access_;
    Params params_{*this};
    ProgramBank bank_;
    std::array<int32_t, Params::kLimit> lastCore_{};   // the core's values as last scanned (alone with the core)
    std::atomic<int> program_{0};
    std::atomic<bool> programMoved_{false}, edited_{false}, stateDiffers_{false}, following_{false};
    std::atomic<bool> seqPlaying_{false};
    std::atomic<int> seqState_{0};
    std::atomic<uint32_t> seqVer_{0};
    std::atomic<int> seqStep_{-1}, seqRec_{0}, seqXp_{0};
    int seqCall(int (*op)(kc_t*));                // a step-entry call through access(): its result, -3 no answer
    std::atomic<double> hostBpm_{120.0};
    std::atomic<uint32_t> bankGen_{0};
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
    bool hostRolling_ = false;                   // the last block had the host's position, playing (the audio thread)
    float lastL_ = 0.0f, lastR_ = 0.0f;          // the last sample output

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};

} // namespace knobi
