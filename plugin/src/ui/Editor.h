// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin: the window, the OB-6-style panel (plan Task 7; spec "A full OB-6-style panel"), after the approved
// mockups docs/design/standalone-ui/panel-style-v2.html and sections-v2.html. Every section at once, in the panel's
// order (VCO 1, VCO 2, MIXER, FILTER, ENVELOPES / LFO, X-MOD, AFTERTOUCH, MISC / PROGRAM, EFFECTS, ARPEGGIATOR,
// CLOCK, SEQUENCER), wood cheeks, the KNOBI-SIX wordmark; GLOBALS opens a small settings view over the panel.
//
// Every parameter the processor exposes has exactly one control (Widgets.h: knob, switch pad, LED select, the BPM
// display), bound both ways by polling (Widgets.h: no callback from the audio thread into the UI). The panel never
// calls the core: parameters, and the Processor's API (program browser, SEQ PLAY, transport state, globals).
//
// Size: the panel is laid out once at its base size (kWidth x kHeight: 100 %, fits a 1440-wide screen) and drawn
// scaled (an affine transform), so 100 .. 200 % keeps the layout exactly; the window keeps the aspect ratio. The
// scale is kept by the processor while the instance lives.
//
// The sequencer (plan Task 8): SEQ PLAY, SEQ REC (step entry: Live's MIDI notes recorded; REST / TIE / BACK; held while
// the sequence plays + a note: a transpose) and the 64-step grid with mouse editing (SeqGrid.h).
#pragma once
#include <map>
#include "../Processor.h"
#include "ProgramBrowser.h"
#include "SeqGrid.h"
#include "Widgets.h"

namespace knobi::ui {

class SequencerSection final : public juce::Component {
public:
    static constexpr int kGridW = 728, kGridH = 208;
    explicit SequencerSection(Processor& p);
    ~SequencerSection() override;                 // SEQ REC still held: the hold dropped (no tap, no transpose)
    void visibilityChanged() override;            // (hidden while held: the same)
    void refresh();
    void paint(juce::Graphics& g) override;
    void resized() override;
    Pad& playPad() { return play_; }
    Pad& recPad() { return rec_; }
    Pad& restPad() { return rest_; }
    Pad& tiePad() { return tie_; }
    Pad& backPad() { return back_; }
    SeqGrid& grid() { return grid_; }
    juce::String stateText() const;               // "OFF", "PLAYING", "WAITING FOR LIVE", "RECORDING"
private:
    void recResult(int rc, const char* what);
    void dropHold();     // REST / TIE / BACK refused: why, on the grid's message line
    Processor& p_;
    Pad play_{"SEQ PLAY", true}, rec_{"SEQ REC", true}, rest_{"REST", true}, tie_{"TIE", true}, back_{"BACK", true};
    SeqGrid grid_;
    int state_ = 0;
    bool rec_on_ = false;
};

// GLOBALS: the library's globals (every instance follows), shown over the panel
class GlobalsView final : public juce::Component {
public:
    explicit GlobalsView(Processor& p);
    void open();                                  // reads the globals now, shows the view
    void refresh();                               // (the editor's timer, while open) the bank changed: read again
    void paint(juce::Graphics& g) override;
    void resized() override;
    Control& control(int g) { return *controls_.at(g); }
    Pad& closePad() { return close_; }
    static juce::String hint();                   // "AU recommended in Live ..."
    static juce::String displayPath(const std::string& path);   // the home folder as "~"
private:
    void readAll();
    Processor& p_;
    std::map<int, int32_t> cache_;
    std::map<int, Control*> controls_;
    std::vector<std::unique_ptr<Control>> owned_;
    Pad close_{"CLOSE", true};
    uint32_t gen_ = 0;
};

class Panel final : public juce::Component {
public:
    static constexpr int kWidth = 1280, kHeight = 760;
    explicit Panel(Processor& p);
    void poll(bool force = false);                // everything shown from the processor / the parameters
    void paint(juce::Graphics& g) override;
    void resized() override;

    const std::vector<Control*>& controls() const { return controls_; }
    Control* controlFor(int paramId) const;
    ProgramBrowser& browser() { return browser_; }
    SequencerSection& sequencer() { return seq_; }
    GlobalsView& globals() { return globals_; }
    Pad& globalsPad() { return globalsPad_; }
    SegValue& bpm() { return *bpm_; }
    juce::String notice() const { return notice_; }
    // the section boxes (base pixels), by title
    const std::vector<std::pair<juce::String, juce::Rectangle<int>>>& sections() const { return sections_; }

private:
    Control& add(uint16_t id, int x, int y, int w = 64, int h = 64, int columns = 1);
    void layout();
    Processor& p_;
    std::vector<std::unique_ptr<Control>> owned_;
    std::vector<Control*> controls_;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> sections_;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> sideLabels_;
    SegValue* bpm_ = nullptr;
    ProgramBrowser browser_;
    SequencerSection seq_;
    GlobalsView globals_;
    Pad globalsPad_{"GLOBALS", true};
    juce::String notice_;
};

class Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    static inline std::atomic<bool> startPollTimer{true};   // (tests: false, they call poll themselves)
    explicit Editor(Processor& p);
    ~Editor() override;
    void resized() override;
    void paint(juce::Graphics& g) override { g.fillAll(col::frame); }
    float scale() const { return scale_; }
    Panel& panel() { return panel_; }
    void poll(bool force = false) { panel_.poll(force); }
private:
    void timerCallback() override { panel_.poll(); }
    Processor& p_;
    Panel panel_;
    float scale_ = 1.0f;
};

} // namespace knobi::ui
