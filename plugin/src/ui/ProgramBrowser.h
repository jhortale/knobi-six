// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin panel: PROGRAM, the program browser (spec: bank / tens / ones as the keyboard, WRITE, RESTORE
// FACTORY). Everything goes through the Processor's API (Processor.h: setCurrentProgram, writeProgram, restoreFactory,
// programInfo, libraryUnreadable / libraryNewer); nothing here touches the core.
//
// Choosing a program, the OB-6 way: two digit presses = tens then ones in the bank shown (the display shows "4-"
// after the first); BANK then a digit 0..4 = the same tens / ones in that bank; ◀ ▶ step by one. The program loads
// at once (the edit buffer is lost, as on the keyboard: WRITE first).
// WRITE, as the hardware: WRITE arms (the pad lit, the number blinks: the slot to write, the program in play's by
// default); BANK / digits / ◀ ▶ now choose the slot without loading anything; WRITE again stores the program in play +
// its sequence into that slot; CANCEL leaves. When the library file could not be read (libraryUnreadable, not a newer
// version's file), the first WRITE asks first (a confirmation box in the panel): the unreadable file is kept as
// library.knobi.bad-<date-time> beside it, then a new library is written. The result is shown on the message line:
//   kOk          "PROGRAM 123 STORED"
//   kStoredOnly  "STORED IN 123; THE INSTANCE MOVED ON TO PROGRAM 45"
//   kNewer       "LIBRARY FROM A NEWER VERSION - NOT WRITTEN"
//   kIoError     "WRITE FAILED - NOTHING CHANGED"
// RESTORE FACTORY: the factory program of the number in play (+ its sequence) into the edit buffer, edited; nothing is
// stored until WRITE (after which that slot is the factory program again).
#pragma once
#include "../Processor.h"
#include "Widgets.h"

namespace knobi::ui {

class ProgramBrowser final : public juce::Component {
public:
    explicit ProgramBrowser(Processor& p);
    // the editor's timer: the program in play, edited; the name and "differs from bank" (a snapshot: when the number,
    // the edited flag or the bank changed, else once a second; force: now); the blinking while WRITE is armed
    void refresh(bool force = false);
    void paint(juce::Graphics& g) override;
    void resized() override;

    enum class Mode { Normal, WriteArmed };
    Mode mode() const { return mode_; }
    juce::String bankText() const;                // the BANK display ("2"; "-" waiting for the bank digit)
    juce::String programText() const;             // the PROGRAM display: tens + ones ("37"; "3-" after one digit)
    const juce::String& name() const { return name_; }
    bool editedLit() const { return info_.edited; }
    bool differsLit() const { return info_.differs; }
    bool userLit() const { return info_.user; }
    const juce::String& message() const { return message_; }
    int selected() const { return sel_; }         // the number shown (the program in play, or WRITE's slot)
    bool confirming() const { return confirmBox_.isVisible(); }

    Pad& digit(int d) { return *digits_[(size_t)d]; }
    Pad& bankPad() { return bank_; }
    Pad& prevPad() { return prev_; }
    Pad& nextPad() { return next_; }
    Pad& writePad() { return write_; }
    Pad& restorePad() { return restore_; }
    Pad& cancelPad() { return cancel_; }
    Pad& confirmYes() { return yes_; }
    Pad& confirmNo() { return no_; }

    static juce::String resultText(Library::Result r, int slot, int nowPlaying);

private:
    void onDigit(int d);
    void onBank();
    void step(int by);
    void choose(int n);
    void onWrite();
    void commit(int n);
    void doWrite(int n, bool overUnreadable);
    void onRestore();
    void setMessage(const juce::String& m, bool error = false);
    void updatePads();

    Processor& p_;
    Mode mode_ = Mode::Normal;
    enum class Entry { None, Ones, Bank } entry_ = Entry::None;
    int sel_ = 0, tens_ = 0, confirmSlot_ = -1;
    Processor::ProgramInfo info_;
    juce::String name_, message_;
    bool messageError_ = false, blinkOn_ = true;
    int lastNumber_ = -1, ticks_ = 0;
    bool lastEdited_ = false;
    uint32_t lastGen_ = 0;
    std::array<std::unique_ptr<Pad>, 10> digits_;
    Pad bank_{"BANK", true}, prev_{"<", true}, next_{">", true}, write_{"WRITE", true},
        restore_{"RESTORE FACTORY", true}, cancel_{"CANCEL", true}, yes_{"WRITE ANYWAY", true}, no_{"CANCEL", true};
    struct ConfirmBox final : juce::Component {
        juce::String text;
        void paint(juce::Graphics& g) override;
    } confirmBox_;
};

} // namespace knobi::ui
