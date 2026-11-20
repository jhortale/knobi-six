// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin panel: the sequencer's 64-step grid (plan Task 8; spec "Sequencer on the panel"): the program's
// sequence shown and edited with the mouse, through the same edit buffer as the keyboard's step entry (Processor::seqSet
// -> kc_seq_set: the program is edited, WRITE stores it with the program; the player takes the change at its next
// block, no note left sounding).
//
// What it shows (16 steps a row, 4 rows): LENGTH (a 7-segment value with - / +), each step's notes by name (up to 6,
// brighter = louder, a velocity bar under them), a rest as a dash, a tie as "TIE" with a bar joined to the step before
// (its notes, the copy it carries, dimmed), the steps past the length as empty frames, the step playing framed red (from
// Processor::seqStep: an atomic the audio thread publishes), the step being edited framed blue; while recording the
// next step to record framed red and, before the first entry, the sequence shown empty (it is replaced at the first
// entry, as on the keyboard); a message line (what the last edit did, or why it was refused); XP (the transpose
// playing) when not 0.
//
// The mouse (the design): click a step inside the length to open the STEP editor over the grid:
//   - a two-octave piano (< > move it an octave, notes 12..108 only): click a key to add that note to the step, click
//     a lit key to remove it (6 notes at most: a 7th is refused, "6 NOTES AT MOST"); removing the last note makes the
//     step a rest;
//   - each note of the step as a column: its name, a velocity bar (drag up / down, or click a height: 1..127, sent when
//     the mouse is let go), X removes it;
//   - REST makes the step a rest; TIE ties it to the step before (it then carries a copy of that step's notes: synth/
//     seq.h), refused on step 1 ("NO TIE ON STEP 1") and after a rest ("NO TIE AFTER A REST"), TIE on a tied step unties
//     it (it keeps the notes, played anew); < > the step before / after; CLOSE.
//   - a tied step's notes are the copy: changing them (a key, X) unties it first; its velocities are the step before's
//     (a drag is refused, "TIED: SET IT ON STEP n").
//   Every edit keeps the sequence one the firmware stores (seqedit::fixTies: a tie that comes to follow a rest is
//   untied, keeping its notes; a tie after a changed step carries the new copy), so kc_seq_set never refuses one.
// LENGTH: - / + (1..64; new steps are rests, steps cut off are gone), or drag the number up / down (sent when let go).
// Steps past the length are not editable (raise the length first). While recording the grid is the step entry's
// (clicks say so); while a sequence plays, edits are heard from the next step.
//
// Cost: the grid polls on the panel's 15 Hz timer: Processor::seqVersion / seqStep / seqRecState / seqXp (atomics);
// the sequence is read through access() only when seqVersion moved, so nothing changing means no access() call. Edits
// go through access() (one call each; a drag: one call when let go).
#pragma once
#include "../Processor.h"
#include "Widgets.h"

namespace knobi::ui {

// the edit operations on a sequence (pure: no processor), each leaving a sequence seq_unpack accepts
namespace seqedit {
struct Result {
    bool changed = false;                         // the sequence changed
    juce::String why;                             // refused: the reason shown (empty: done)
};
juce::String noteName(int note);                  // 60 = "C4" (the firmware's names: middle C is C4; Live calls it C3)
bool valid(const seq_t& s);                       // the rules of seq_unpack (synth/seq.h)
void fixTies(seq_t& s);                           // ties after a rest untied, a tie's notes the copy of the step before
Result toggleNote(seq_t& s, int step, int note, int vel = 100);   // add (max 6) / remove; a tie: untied first
Result removeNote(seq_t& s, int step, int index);
Result setVelocity(seq_t& s, int step, int index, int vel);      // 1..127; refused on a tie
Result makeRest(seq_t& s, int step);
Result toggleTie(seq_t& s, int step);             // tie (refused at step 1 / after a rest) or untie
Result setLength(seq_t& s, int len);              // 1..64
} // namespace seqedit

class SeqGrid;

// the STEP editor (over the grid while a step is chosen)
class StepEditor final : public juce::Component {
public:
    static constexpr int kNoteW = 46, kNotesX = 8, kBarTop = 50, kBarH = 96;
    static constexpr int kPianoX = 312, kPianoY = 34, kWhiteW = 27, kWhiteH = 108, kBlackH = 66;
    explicit StepEditor(SeqGrid& g);
    void showStep(int step);
    int step() const { return step_; }
    int octave() const { return base_; }          // the piano's lowest C (12 .. 84)
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    // the geometry (tests click there)
    juce::Rectangle<int> keyBounds(int note) const;   // empty: not on the piano shown
    juce::Rectangle<int> barBounds(int index) const;  // a note's velocity bar
    juce::Rectangle<int> removeBounds(int index) const;
    int noteAt(juce::Point<int> p) const;             // the piano key there, -1 none
    Pad& restPad() { return rest_; }
    Pad& tiePad() { return tie_; }
    Pad& closePad() { return close_; }
    Pad& prevPad() { return prev_; }
    Pad& nextPad() { return next_; }
    Pad& octDownPad() { return octDown_; }
    Pad& octUpPad() { return octUp_; }
    int dragVelocity() const { return dragVel_; }     // the velocity shown while a bar is dragged (-1: none)
private:
    static bool black(int note) { const int c = note % 12; return c == 1 || c == 3 || c == 6 || c == 8 || c == 10; }
    int velAt(int y) const;
    SeqGrid& g_;
    int step_ = 0, base_ = 48;
    int dragIndex_ = -1, dragVel_ = -1;
    Pad rest_{"REST", true}, tie_{"TIE", true}, close_{"CLOSE", true}, prev_{"<", true}, next_{">", true},
        octDown_{"<", true}, octUp_{">", true};
};

class SeqGrid final : public juce::Component {
public:
    static constexpr int kCols = 16, kRows = 4, kHeader = 22;
    explicit SeqGrid(Processor& p);
    // the panel's timer: the playing step, the recording state, the sequence (read only when its version moved)
    void poll();
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // what a step shows (tests)
    struct Cell {
        bool past = false, rest = false, tie = false, playing = false, selected = false, next = false;
        int count = 0;
        juce::String text;                        // the note names, space separated
    };
    Cell cell(int i) const;
    juce::Rectangle<int> cellBounds(int i) const;
    juce::Rectangle<int> lengthBounds() const;    // the LENGTH number (drag)
    const seq_t& shown() const { return shownSeq_; }   // as drawn (recording, before the first entry: empty)
    const seq_t& sequence() const { return seq_; }     // the edit buffer's, as last read
    int length() const { return shownSeq_.len; }
    const juce::String& message() const { return msg_; }
    bool recording() const { return (rec_ & KC_REC_ON) != 0; }
    StepEditor& editor() { return editor_; }
    bool editing() const { return editor_.isVisible(); }
    Pad& lengthDown() { return lenDown_; }
    Pad& lengthUp() { return lenUp_; }
    uint64_t fetches() const { return fetches_; }  // (tests) sequence reads through access()

    // an edit: op on a copy of the sequence, sent if it changed (kc_seq_set); false: refused or not sent (message)
    bool edit(const std::function<seqedit::Result(seq_t&)>& op);
    void openStep(int step);                      // (the mouse: a click on a step)
    void say(const juce::String& m);              // the message line
private:
    friend class StepEditor;
    void fetch();
    void setLengthNow(int len);
    Processor& p_;
    seq_t seq_{}, shownSeq_{};
    bool have_ = false;
    uint32_t ver_ = 0;
    int step_ = -1, rec_ = 0, xp_ = 0;
    int dragLen_ = -1, dragFrom_ = 0, dragY_ = 0;
    uint64_t fetches_ = 0;
    juce::String msg_;
    Pad lenDown_{"-", true}, lenUp_{"+", true};
    StepEditor editor_;
};

} // namespace knobi::ui
