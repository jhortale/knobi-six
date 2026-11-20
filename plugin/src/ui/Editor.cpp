// SPDX-License-Identifier: GPL-3.0-only
#include "Editor.h"
#include <cstdlib>

namespace knobi::ui {

namespace {
// the panel's short labels (the mockups'), by parameter id; the full name is the tooltip
const char* shortLabel(uint16_t id)
{
    switch (id) {
    case P_VCO1_FREQ: case P_VCO2_FREQ: case P_FLT_FREQ: case P_LFO_FREQ: return "FREQUENCY";
    case P_VCO1_SHAPE: case P_VCO2_SHAPE: case P_LFO_SHAPE: return "SHAPE";
    case P_VCO1_PW: case P_VCO2_PW: return "PULSE WIDTH";
    case P_VCO1_SYNC: return "SYNC";
    case P_VCO2_FINE: return "FINE";
    case P_VCO2_LOWFREQ: return "LOW FREQ";
    case P_VCO2_KEYBOARD: return "KEYBOARD";
    case P_MIX_VCO1: case P_LFO_D_VCO1: case P_XM_D_VCO1: case P_AT_D_VCO1: return "VCO 1";
    case P_MIX_VCO2: case P_LFO_D_VCO2: case P_XM_VCO2: case P_AT_D_VCO2: return "VCO 2";
    case P_MIX_SUB: return "SUB OCTAVE";
    case P_MIX_NOISE: return "NOISE";
    case P_FLT_RESO: return "RESONANCE";
    case P_FLT_MODE: return "LP-NOTCH-HP";
    case P_FLT_BP: return "BP";
    case P_FLT_TRACK: return "KEY TRACK";
    case P_FLT_ENV_AMT: case P_LENV_AMT: case P_LFO_AMT: case P_AT_AMT: return "AMOUNT";
    case P_FLT_VEL: case P_LENV_VEL: return "VELOCITY";
    case P_FENV_A: case P_LENV_A: return "ATTACK";
    case P_FENV_D: case P_LENV_D: return "DECAY";
    case P_FENV_S: case P_LENV_S: return "SUSTAIN";
    case P_FENV_R: case P_LENV_R: return "RELEASE";
    case P_LFO_SYNC: return "LFO SYNC";
    case P_LFO_D_PW: return "PW";
    case P_LFO_D_AMP: case P_AT_D_AMP: return "AMP";
    case P_LFO_D_FFREQ: case P_XM_D_FFREQ: case P_AT_D_FFREQ: return "F FREQ";
    case P_LFO_D_FMODE: case P_XM_D_FMODE: case P_AT_D_FMODE: return "F MODE";
    case P_XM_FENV: return "FILTER ENV";
    case P_XM_D_SHAPE1: return "SHAPE 1";
    case P_XM_D_PW1: return "PW 1";
    case P_XM_D_BP: return "NORM-BP";
    case P_AT_D_LFOAMT: return "LFO AMT";
    case P_DETUNE: return "DETUNE";
    case P_PAN_SPREAD: return "PAN SPREAD";
    case P_BEND_RANGE: return "BEND RANGE";
    case P_KEY_MODE: return "KEY MODE";
    case P_PRG_VOL: return "PROGRAM VOL";
    case P_PORTA_ON: return "PORTAMENTO";
    case P_PORTA_MODE: return "PORTA MODE";
    case P_PORTA_RATE: return "PORTA RATE";
    case P_UNISON_ON: return "UNISON";
    case P_UNISON_VOICES: return "VOICES";
    case P_DISTORTION: return "DISTORTION";
    case P_BPM: return "BPM";
    case P_CLK_DIV: return "DIVISION";
    case P_ARP_ON: return "ON / OFF";
    case P_ARP_MODE: return "MODE";
    case P_ARP_RANGE: return "OCTAVES";
    case P_ARP_HOLD: return "HOLD";
    default: return "";
    }
}

constexpr int kKnobH = 66, kSwitchH = 40, kRow = 80, kTop = 28;   // a knob cell, a switch cell, a row, a title
} // namespace

// ---- the sequencer grid's place
void GridSlot::paint(juce::Graphics& g)
{
    g.setColour(col::slot);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.0f);
    const float cw = (float)getWidth() / 16.0f, rh = (float)getHeight() / 4.0f;
    g.setColour(juce::Colour(0xff24262b));
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 16; c++)
            g.fillRoundedRectangle(juce::Rectangle<float>((float)c * cw, (float)r * rh, cw, rh).reduced(3.0f), 2.0f);
    g.setColour(col::dim);
    g.setFont(font(11.0f));
    g.drawText("64-STEP GRID", getLocalBounds(), juce::Justification::centred, false);
}

// ---- SEQUENCER
SequencerSection::SequencerSection(Processor& p) : p_(p)
{
    play_.onPress = [this] {
        p_.seqPlay(!p_.seqPlaying());
        refresh();
    };
    play_.setTooltip("SEQ PLAY: the program's sequence plays with Live's transport (waits for Live's play)");
    rec_.setEnabled(false);
    rec_.setTooltip("SEQ REC: step entry comes with the sequencer grid");
    addAndMakeVisible(play_);
    addAndMakeVisible(rec_);
    addAndMakeVisible(grid_);
}

juce::String SequencerSection::stateText() const
{
    return state_ == 1 ? "PLAYING" : state_ == 2 ? "WAITING FOR LIVE" : "OFF";
}

void SequencerSection::refresh()
{
    const int st = p_.seqState();
    play_.setLit(p_.seqPlaying());
    if (st != state_) state_ = st, repaint();
}

void SequencerSection::resized()
{
    play_.setBounds(8, kTop, 96, 24);
    rec_.setBounds(112, kTop, 96, 24);
    grid_.setBounds((getWidth() - kGridW) / 2, kTop + 34, kGridW, kGridH);
}

void SequencerSection::paint(juce::Graphics& g)
{
    drawSectionTitle(g, getLocalBounds(), "SEQUENCER");
    drawLed(g, {230.0f, (float)kTop + 12.0f}, 8.0f, state_ == 1);
    g.setFont(font(12.0f));
    g.setColour(state_ ? col::text : col::dim);
    g.drawText(stateText(), juce::Rectangle<int>(242, kTop, 300, 24), juce::Justification::centredLeft, false);
}

// ---- GLOBALS
juce::String GlobalsView::displayPath(const std::string& path)
{
    const char* home = std::getenv("HOME");
    const std::string h = home ? home : "";
    if (!h.empty() && path.compare(0, h.size(), h) == 0 && path.size() > h.size() && path[h.size()] == '/')
        return "~" + juce::String(path.substr(h.size()));
    return juce::String(path);
}

juce::String GlobalsView::hint()
{
    return "AU RECOMMENDED IN LIVE (THE VST3'S LIMITS: SEE PLUGIN/README.MD)";
}

GlobalsView::GlobalsView(Processor& p) : p_(p)
{
    struct G { int g; const char* label; Kind kind; int cols; } gs[] = {
        {G_TUNE, "MASTER TUNE", Kind::Knob, 1}, {G_TRANSPOSE, "TRANSPOSE", Kind::Knob, 1},
        {G_VEL_CURVE, "VELOCITY CURVE", Kind::Select, 4}, {G_AT_CURVE, "AFTERTOUCH CURVE", Kind::Select, 3},
        {G_STEREO, "OUTPUT", Kind::Select, 1}};
    for (auto& x : gs) {
        auto b = std::make_unique<FnBinding>();
        b->kind = x.kind, b->min = GLOB_MIN[x.g], b->max = GLOB_MAX[x.g], b->def = GLOB_DEF[x.g];
        b->bipolar = b->min < 0;
        b->name = x.label;
        if (x.g == G_STEREO) b->labels = {"STEREO", "MONO"};
        else if (x.kind == Kind::Select)
            for (int v = b->min; v <= b->max; v++) b->labels.add(juce::String(v));   // (as the keyboard shows them)
        const int g = x.g;
        b->get = [this, g] { auto it = cache_.find(g); return it == cache_.end() ? (int32_t)GLOB_DEF[g] : it->second; };
        b->put = [this, g](int32_t v) {
            cache_[g] = v;
            p_.setGlobal(g, v);                   // (the library file: every instance follows)
        };
        std::unique_ptr<Control> c;
        if (x.kind == Kind::Knob) {
            auto k = std::make_unique<Knob>(std::move(b), x.label);
            k->slider().setPopupDisplayEnabled(true, true, this);
            c = std::move(k);
        } else c = std::make_unique<Select>(std::move(b), x.label, x.cols);
        controls_[x.g] = c.get();
        addAndMakeVisible(*c);
        owned_.push_back(std::move(c));
    }
    close_.onPress = [this] { setVisible(false); };
    addAndMakeVisible(close_);
}

void GlobalsView::readAll()
{
    for (int g : Library::kGlobals) cache_[g] = p_.global(g);
    gen_ = p_.bankGeneration();
    for (auto& c : owned_) c->poll();
}

void GlobalsView::open()
{
    readAll();
    setVisible(true);
    toFront(false);
}

void GlobalsView::refresh()
{
    if (!isVisible()) return;
    if (p_.bankGeneration() != gen_) readAll();   // (another instance, the file changed)
    for (auto& c : owned_) c->poll();
}

void GlobalsView::resized()
{
    const auto box = getLocalBounds().withSizeKeepingCentre(600, 300);
    const int x = box.getX() + 30, y = box.getY() + 60;
    controls_.at(G_TUNE)->setBounds(x, y, 80, kKnobH);
    controls_.at(G_TRANSPOSE)->setBounds(x + 90, y, 80, kKnobH);
    controls_.at(G_VEL_CURVE)->setBounds(x + 190, y, 120, 44);
    controls_.at(G_AT_CURVE)->setBounds(x + 330, y, 100, 44);
    controls_.at(G_STEREO)->setBounds(x + 450, y, 80, 44);
    close_.setBounds(box.getRight() - 100, box.getBottom() - 40, 80, 24);
}

void GlobalsView::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xb0000000));
    const auto box = getLocalBounds().withSizeKeepingCentre(600, 300);
    g.setColour(col::panel);
    g.fillRoundedRectangle(box.toFloat(), 4.0f);
    g.setColour(col::rule);
    g.drawRoundedRectangle(box.toFloat(), 4.0f, 2.0f);
    drawSectionTitle(g, box.reduced(20, 14), "GLOBALS");
    g.setFont(font(9.5f));
    g.setColour(col::sub);
    juce::String lib = p_.library() ? displayPath(p_.library()->path()) : juce::String("(no library)");
    g.drawFittedText("KEPT IN THE LIBRARY FILE, SHARED BY EVERY KNOBI-SIX INSTANCE: " + lib +
                         "\nMIDI CHANNEL: OMNI. TUNE IN CENTS, TRANSPOSE IN SEMITONES.",
                     box.reduced(30, 0).withTrimmedTop(150).withHeight(44), juce::Justification::centredLeft, 3, 0.8f);
    g.setColour(col::dim);
    g.drawText(hint(), box.reduced(30, 0).withTrimmedTop(250).withHeight(24), juce::Justification::centredLeft, true);
}

// ---- the panel
Panel::Panel(Processor& p) : p_(p), browser_(p), seq_(p), globals_(p)
{
    setSize(kWidth, kHeight);
    globalsPad_.onPress = [this] { globals_.open(); };
    globalsPad_.setTooltip("Master tune, transpose, velocity / aftertouch curves, stereo / mono");
    addAndMakeVisible(browser_);
    addAndMakeVisible(seq_);
    addAndMakeVisible(globalsPad_);
    layout();
    addChildComponent(globals_);
    globals_.setBounds(getLocalBounds());
    poll(true);
}

Control& Panel::add(uint16_t id, int x, int y, int w, int h, int columns)
{
    const Params::Entry* e = p_.params().byId(id);
    jassert(e != nullptr);
    auto b = std::make_unique<ParamBinding>(*e);
    std::unique_ptr<Control> c;
    if (id == P_BPM) {
        auto s = std::make_unique<SegValue>(std::move(b), shortLabel(id), 3);
        bpm_ = s.get();
        c = std::move(s);
    } else if (b->kind == Kind::Switch)
        c = std::make_unique<Switch>(std::move(b), shortLabel(id));
    else if (b->kind == Kind::Select)
        c = std::make_unique<Select>(std::move(b), shortLabel(id), columns);
    else {
        auto k = std::make_unique<Knob>(std::move(b), shortLabel(id));
        k->slider().setPopupDisplayEnabled(true, true, this);
        c = std::move(k);
    }
    c->setComponentID(Params::paramID(id));
    c->setBounds(x, y, w, h);
    addAndMakeVisible(*c);
    controls_.push_back(c.get());
    owned_.push_back(std::move(c));
    return *controls_.back();
}

Control* Panel::controlFor(int paramId) const
{
    for (auto* c : controls_)
        if (c->paramId() == paramId) return c;
    return nullptr;
}

void Panel::layout()
{
    auto section = [this](const char* t, int x, int y, int w, int h) { sections_.push_back({t, {x, y, w, h}}); };
    const int K = 64;                              // a knob column
    // ---- row 1: VCO 1, VCO 2, MIXER, FILTER, ENVELOPES
    int y = 58, t = y + kTop;
    section("VCO 1", 24, y, 140, 196);
    add(P_VCO1_FREQ, 28, t, K, kKnobH), add(P_VCO1_SYNC, 96, t + 12, K, kSwitchH);
    add(P_VCO1_SHAPE, 28, t + kRow, K, kKnobH), add(P_VCO1_PW, 96, t + kRow, K, kKnobH);
    section("VCO 2", 176, y, 206, 196);
    add(P_VCO2_FREQ, 180, t, K, kKnobH), add(P_VCO2_FINE, 247, t, K, kKnobH), add(P_VCO2_LOWFREQ, 314, t + 12, K, kSwitchH);
    add(P_VCO2_SHAPE, 180, t + kRow, K, kKnobH), add(P_VCO2_PW, 247, t + kRow, K, kKnobH);
    add(P_VCO2_KEYBOARD, 314, t + kRow + 12, K, kSwitchH);
    section("MIXER", 394, y, 140, 196);
    add(P_MIX_VCO1, 398, t, K, kKnobH), add(P_MIX_VCO2, 466, t, K, kKnobH);
    add(P_MIX_SUB, 398, t + kRow, K, kKnobH), add(P_MIX_NOISE, 466, t + kRow, K, kKnobH);
    section("FILTER", 546, y, 224, 196);
    add(P_FLT_FREQ, 550, t, 70, kKnobH), add(P_FLT_RESO, 623, t, 70, kKnobH), add(P_FLT_MODE, 696, t, 70, kKnobH);
    add(P_FLT_BP, 550, t + kRow + 8, 70, kSwitchH), add(P_FLT_TRACK, 628, t + kRow + 12, 138, 30, 3);
    section("ENVELOPES", 782, y, 474, 196);
    sideLabels_.push_back({"FILTER", {784, t, 16, kKnobH}});
    sideLabels_.push_back({"LOUDNESS", {784, t + kRow, 16, kKnobH}});
    const uint16_t fenv[] = {P_FENV_A, P_FENV_D, P_FENV_S, P_FENV_R, P_FLT_ENV_AMT};
    const uint16_t lenv[] = {P_LENV_A, P_LENV_D, P_LENV_S, P_LENV_R, P_LENV_AMT};
    for (int i = 0; i < 5; i++) add(fenv[i], 806 + i * 70, t, 70, kKnobH), add(lenv[i], 806 + i * 70, t + kRow, 70, kKnobH);
    add(P_FLT_VEL, 1166, t + 12, 80, kSwitchH), add(P_LENV_VEL, 1166, t + kRow + 12, 80, kSwitchH);

    // ---- row 2: LFO, X-MOD, AFTERTOUCH, MISC
    y = 266, t = y + kTop;
    section("LFO", 24, y, 330, 192);
    add(P_LFO_FREQ, 28, t, K, kKnobH), add(P_LFO_SYNC, 28, t + kRow + 8, K, kSwitchH);
    add(P_LFO_SHAPE, 98, t + 4, 66, 82);
    add(P_LFO_AMT, 168, t, K, kKnobH), add(P_LFO_D_PW, 168, t + kRow - 6, 72, 68);
    add(P_LFO_D_VCO1, 244, t, 54, kSwitchH), add(P_LFO_D_VCO2, 298, t, 54, kSwitchH);
    add(P_LFO_D_AMP, 244, t + 48, 54, kSwitchH), add(P_LFO_D_FFREQ, 298, t + 48, 54, kSwitchH);
    add(P_LFO_D_FMODE, 244, t + 96, 54, kSwitchH);
    section("X-MOD", 366, y, 250, 192);
    add(P_XM_FENV, 370, t, 68, kKnobH), add(P_XM_VCO2, 440, t, 68, kKnobH);
    add(P_XM_D_VCO1, 510, t, 52, kSwitchH), add(P_XM_D_SHAPE1, 562, t, 52, kSwitchH);
    add(P_XM_D_PW1, 510, t + 48, 52, kSwitchH), add(P_XM_D_FFREQ, 562, t + 48, 52, kSwitchH);
    add(P_XM_D_FMODE, 510, t + 96, 52, kSwitchH), add(P_XM_D_BP, 562, t + 96, 52, kSwitchH);
    section("AFTERTOUCH", 628, y, 230, 192);
    add(P_AT_AMT, 632, t, 70, kKnobH);
    add(P_AT_D_VCO1, 704, t, 50, kSwitchH), add(P_AT_D_VCO2, 754, t, 50, kSwitchH), add(P_AT_D_LFOAMT, 804, t, 50, kSwitchH);
    add(P_AT_D_AMP, 704, t + 48, 50, kSwitchH), add(P_AT_D_FFREQ, 754, t + 48, 50, kSwitchH);
    add(P_AT_D_FMODE, 804, t + 48, 50, kSwitchH);
    section("MISC", 870, y, 386, 192);
    add(P_DETUNE, 872, t, K, kKnobH), add(P_PAN_SPREAD, 936, t, K, kKnobH), add(P_BEND_RANGE, 1000, t, K, kKnobH);
    add(P_PRG_VOL, 1064, t, K, kKnobH), add(P_KEY_MODE, 1134, t + 2, 120, 56, 2);
    add(P_PORTA_ON, 872, t + kRow + 8, K, kSwitchH), add(P_PORTA_RATE, 936, t + kRow, K, kKnobH);
    add(P_PORTA_MODE, 1004, t + kRow + 4, 120, 44, 2);
    add(P_UNISON_ON, 1128, t + kRow + 8, 56, kSwitchH), add(P_UNISON_VOICES, 1186, t + kRow + 2, 70, 70, 2);

    // ---- row 3: PROGRAM over EFFECTS, ARPEGGIATOR, CLOCK; the SEQUENCER beside them
    browser_.setBounds(24, 470, 476, 160);
    sections_.push_back({"PROGRAM", browser_.getBounds()});
    y = 640, t = y + kTop;
    section("EFFECTS", 24, y, 80, 110);
    add(P_DISTORTION, 32, t, K, kKnobH);
    section("ARPEGGIATOR", 116, y, 200, 110);
    add(P_ARP_ON, 118, t + 4, 50, kSwitchH), add(P_ARP_HOLD, 168, t + 4, 50, kSwitchH);
    add(P_ARP_MODE, 222, t, 58, 80), add(P_ARP_RANGE, 280, t, 36, 54);
    section("CLOCK", 328, y, 172, 110);
    add(P_BPM, 330, t + 2, 66, 60), add(P_CLK_DIV, 400, t, 100, 80, 2);
    seq_.setBounds(512, 470, 744, 280);
    sections_.push_back({"SEQUENCER", seq_.getBounds()});
    globalsPad_.setBounds(1170, 14, 80, 24);
}

void Panel::resized() {}

void Panel::poll(bool force)
{
    for (auto* c : controls_) c->poll();
    const bool following = p_.followingHost();
    bpm_->setGreyed(following, following ? juce::String(juce::roundToInt(p_.hostBpm())) : juce::String());
    browser_.refresh(force);
    seq_.refresh();
    globals_.refresh();
    juce::String n;
    if (p_.libraryNewer()) n = "LIBRARY FROM A NEWER VERSION: FACTORY PROGRAMS IN USE, WRITE REFUSED";
    else if (p_.libraryUnreadable()) n = "LIBRARY UNREADABLE: FACTORY PROGRAMS IN USE (A WRITE KEEPS A BACKUP FIRST)";
    else if (const int bad = p_.libraryBadSlots()) n = juce::String(bad) + " LIBRARY SLOTS UNREADABLE: FACTORY PROGRAMS";
    if (n != notice_) notice_ = n, repaint(0, 0, kWidth, 56);
}

void Panel::paint(juce::Graphics& g)
{
    g.fillAll(col::panel);
    drawWood(g, {0.0f, 0.0f, 16.0f, (float)kHeight});
    drawWood(g, {(float)kWidth - 16.0f, 0.0f, 16.0f, (float)kHeight});
    // the header: the wordmark (outlined, as the mockup's), the notice line
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText(font(30.0f), "KNOBI-SIX", 30.0f, 38.0f);
        juce::Path word;
        ga.createPath(word);
        g.setColour(col::text);
        g.strokePath(word, juce::PathStrokeType(1.4f));
        g.setColour(col::sub);
        g.setFont(font(9.0f));
        g.drawText("POLYPHONIC SYNTHESIZER  -  6 VOICES  -  2 VCO", 210, 22, 300, 14, juce::Justification::centredLeft,
                   false);
        g.setColour(col::rule);
        g.fillRect(24, 48, kWidth - 48, 2);
        if (notice_.isNotEmpty()) {
            g.setColour(col::ledOn);
            g.setFont(font(10.5f));
            g.drawText(notice_, 520, 18, 640, 18, juce::Justification::centredRight, true);
        }
    }
    for (auto& [title, r] : sections_)
        if (title != "PROGRAM" && title != "SEQUENCER") drawSectionTitle(g, r, title);
    g.setFont(font(8.5f));
    g.setColour(col::rule);
    for (auto& [text, r] : sideLabels_) {           // ENVELOPES' row names, vertical as in the mockup
        juce::Graphics::ScopedSaveState s(g);
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, (float)r.getCentreX(),
                                                       (float)r.getCentreY()));
        g.drawText(text, r.withSizeKeepingCentre(r.getHeight(), r.getWidth()), juce::Justification::centred, false);
    }
}

// ---- the window
Editor::Editor(Processor& p) : juce::AudioProcessorEditor(p), p_(p), panel_(p)
{
    const float s = juce::jlimit(1.0f, 2.0f, p.editorScale.load());   // (before the limits: they resize to 100 %)
    addAndMakeVisible(panel_);
    setResizable(true, true);
    setResizeLimits(Panel::kWidth, Panel::kHeight, Panel::kWidth * 2, Panel::kHeight * 2);
    getConstrainer()->setFixedAspectRatio((double)Panel::kWidth / (double)Panel::kHeight);
    setSize(juce::roundToInt(Panel::kWidth * s), juce::roundToInt(Panel::kHeight * s));
    if (startPollTimer.load()) startTimerHz(15);
}

Editor::~Editor() { stopTimer(); }

void Editor::resized()
{
    scale_ = (float)getWidth() / (float)Panel::kWidth;
    panel_.setTransform(juce::AffineTransform::scale(scale_));
    panel_.setBounds(0, 0, Panel::kWidth, Panel::kHeight);
    p_.editorScale.store(scale_);
}

} // namespace knobi::ui
