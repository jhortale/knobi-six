// SPDX-License-Identifier: GPL-3.0-only
#include "ProgramBrowser.h"
#include <tuple>

namespace knobi::ui {

namespace {
juce::String num3(int n) { return juce::String(n).paddedLeft('0', 3); }
}

ProgramBrowser::ProgramBrowser(Processor& p) : p_(p)
{
    for (int d = 0; d < 10; d++) {
        digits_[(size_t)d] = std::make_unique<Pad>(juce::String(d), true);
        digits_[(size_t)d]->onPress = [this, d] { onDigit(d); };
        addAndMakeVisible(*digits_[(size_t)d]);
    }
    bank_.onPress = [this] { onBank(); };
    prev_.onPress = [this] { step(-1); };
    next_.onPress = [this] { step(1); };
    write_.onPress = [this] { onWrite(); };
    restore_.onPress = [this] { onRestore(); };
    cancel_.onPress = [this] {
        mode_ = Mode::Normal, entry_ = Entry::None;
        setMessage("WRITE CANCELLED");
        refresh(true);
    };
    bank_.setTooltip("BANK, then a digit 0-4: the same program number in that bank");
    write_.setTooltip("WRITE: press, choose the slot (BANK / digits / < >), press WRITE again");
    restore_.setTooltip("The factory program of this number into the edit buffer (nothing stored until WRITE)");
    for (Pad* x : {&bank_, &prev_, &next_, &write_, &restore_}) addAndMakeVisible(*x);
    addChildComponent(cancel_);
    yes_.onPress = [this] {
        const int n = confirmSlot_;
        confirmBox_.setVisible(false);
        doWrite(n, true);
    };
    no_.onPress = [this] {
        confirmBox_.setVisible(false);
        mode_ = Mode::Normal, entry_ = Entry::None;
        setMessage("NOTHING WRITTEN");
        refresh(true);
    };
    confirmBox_.addAndMakeVisible(yes_);
    confirmBox_.addAndMakeVisible(no_);
    addChildComponent(confirmBox_);
    refresh(true);
}

juce::String ProgramBrowser::resultText(Library::Result r, int slot, int nowPlaying)
{
    switch (r) {
    case Library::kOk: return "PROGRAM " + num3(slot) + " STORED";
    case Library::kStoredOnly:
        return "STORED IN " + num3(slot) + "; THE INSTANCE MOVED ON TO PROGRAM " + num3(nowPlaying);
    case Library::kNewer: return "LIBRARY FROM A NEWER VERSION - NOT WRITTEN";
    case Library::kInvalid: return "WRITE REFUSED - NOT A PROGRAM THE KEYBOARD WOULD STORE";
    case Library::kIoError:
    default: return "WRITE FAILED - NOTHING CHANGED";
    }
}

void ProgramBrowser::setMessage(const juce::String& m, bool error)
{
    message_ = m, messageError_ = error;
    repaint();
}

juce::String ProgramBrowser::bankText() const
{
    if (entry_ == Entry::Bank) return "-";
    return juce::String(Processor::bankOf(sel_));
}

juce::String ProgramBrowser::programText() const
{
    if (entry_ == Entry::Ones) return juce::String(tens_) + "-";
    return juce::String(Processor::tensOf(sel_)) + juce::String(Processor::onesOf(sel_));
}

void ProgramBrowser::refresh(bool force, bool paramChanged)
{
    const int number = p_.getCurrentProgram();
    const bool edited = p_.edited();
    const uint32_t gen = p_.bankGeneration();
    paramPending_ = paramPending_ || paramChanged;
    if (ticks_ < kParamTicks) ++ticks_;
    const auto before = std::make_tuple(info_.number, info_.edited, info_.differs, info_.user, name_, sel_, blinkOn_);
    const bool moved = number != lastNumber_ || edited != lastEdited_ || gen != lastGen_;
    if (force || moved || (paramPending_ && ticks_ >= kParamTicks)) {
        Processor::ProgramInfo info;
        if (p_.programInfo(info)) {
            info_ = info;
            name_ = juce::String(info.name);
        }
        lastNumber_ = number, lastEdited_ = edited, lastGen_ = gen, ticks_ = 0, paramPending_ = false;
        snapshots_++;
    }
    info_.edited = edited;
    if (mode_ == Mode::Normal && entry_ == Entry::None) sel_ = number;
    blinkOn_ = mode_ != Mode::WriteArmed || (juce::Time::getMillisecondCounter() / 300) % 2 == 0;
    updatePads();
    if (force || before != std::make_tuple(info_.number, info_.edited, info_.differs, info_.user, name_, sel_, blinkOn_))
        repaint();
}

void ProgramBrowser::updatePads()
{
    write_.setLit(mode_ == Mode::WriteArmed);
    bank_.setLit(entry_ == Entry::Bank);
    cancel_.setVisible(mode_ == Mode::WriteArmed);
    restore_.setEnabled(mode_ == Mode::Normal);
}

void ProgramBrowser::choose(int n)
{
    n = (n % (int)KC_PROGRAMS + (int)KC_PROGRAMS) % (int)KC_PROGRAMS;
    entry_ = Entry::None;
    sel_ = n;
    if (mode_ == Mode::Normal) {
        p_.setCurrentProgram(n);                  // (a load, as a program change: the edit buffer is lost)
        message_.clear();
        refresh(true);
    } else {
        setMessage("WRITE TO " + num3(n) + "? PRESS WRITE AGAIN (CANCEL: NOTHING WRITTEN)");
        repaint();
    }
}

void ProgramBrowser::onDigit(int d)
{
    if (confirming()) return;
    if (entry_ == Entry::Bank) {
        if (d > (int)KC_PROGRAMS / 100 - 1) { setMessage("BANKS 0-4"); return; }
        choose(Processor::programOf(d, Processor::tensOf(sel_), Processor::onesOf(sel_)));
        return;
    }
    if (entry_ == Entry::Ones) {
        choose(Processor::programOf(Processor::bankOf(sel_), tens_, d));
        return;
    }
    entry_ = Entry::Ones, tens_ = d;
    updatePads();
    repaint();
}

void ProgramBrowser::onBank()
{
    if (confirming()) return;
    entry_ = entry_ == Entry::Bank ? Entry::None : Entry::Bank;
    updatePads();
    repaint();
}

void ProgramBrowser::step(int by)
{
    if (confirming()) return;
    choose(sel_ + by);
}

void ProgramBrowser::onWrite()
{
    if (confirming()) return;
    if (mode_ == Mode::Normal) {
        mode_ = Mode::WriteArmed, entry_ = Entry::None;
        sel_ = p_.getCurrentProgram();
        setMessage("WRITE: CHOOSE THE SLOT (BANK / 0-9 / < >), THEN WRITE AGAIN");
        updatePads();
        return;
    }
    entry_ = Entry::None;
    commit(sel_);
}

void ProgramBrowser::commit(int n)
{
    if (p_.libraryUnreadable() && !p_.libraryNewer()) {   // (a newer version's file: refused anyway, kNewer)
        confirmSlot_ = n;
        confirmBox_.text = "THE LIBRARY FILE CANNOT BE READ (THE FACTORY PROGRAMS ARE IN USE). WRITING TO " + num3(n) +
                           " FIRST KEEPS THE UNREADABLE FILE AS A BACKUP BESIDE IT (LIBRARY.KNOBI.BAD-<DATE-TIME>), "
                           "THEN WRITES A NEW LIBRARY WITH ONLY THIS PROGRAM IN IT. WRITE ANYWAY?";
        confirmBox_.setVisible(true);
        confirmBox_.toFront(false);
        return;
    }
    doWrite(n, false);
}

void ProgramBrowser::doWrite(int n, bool overUnreadable)
{
    const Library::Result r = p_.writeProgram(n);
    mode_ = Mode::Normal, entry_ = Entry::None;
    juce::String m = resultText(r, n, p_.getCurrentProgram());
    if (r == Library::kOk && overUnreadable) m << "; THE OLD FILE KEPT AS LIBRARY.KNOBI.BAD-<DATE-TIME>";
    setMessage(m, r != Library::kOk);
    refresh(true);
}

void ProgramBrowser::onRestore()
{
    if (confirming() || mode_ != Mode::Normal) return;
    const int n = p_.getCurrentProgram();
    if (p_.restoreFactory())
        setMessage("FACTORY PROGRAM " + num3(n) + " IN THE EDIT BUFFER - WRITE TO STORE IT");
    else
        setMessage("RESTORE FACTORY FAILED - NOTHING CHANGED", true);
    refresh(true);
}

void ProgramBrowser::resized()
{
    int x = 0;
    for (int d = 0; d < 10; d++, x += 31) digits_[(size_t)d]->setBounds(x, 88, 29, 22);
    bank_.setBounds(x + 6, 88, 56, 22);
    prev_.setBounds(x + 66, 88, 30, 22);
    next_.setBounds(x + 98, 88, 30, 22);
    write_.setBounds(0, 114, 80, 22);
    restore_.setBounds(86, 114, 140, 22);
    cancel_.setBounds(232, 114, 80, 22);
    confirmBox_.setBounds(getLocalBounds());
    const auto cb = confirmBox_.getLocalBounds();
    yes_.setBounds(cb.getWidth() / 2 - 150, cb.getHeight() - 32, 140, 24);
    no_.setBounds(cb.getWidth() / 2 + 10, cb.getHeight() - 32, 110, 24);
}

void ProgramBrowser::ConfirmBox::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xf0101114));
    g.setColour(col::btnOn);
    g.drawRect(getLocalBounds(), 2);
    g.setColour(col::text);
    g.setFont(font(11.0f));
    g.drawFittedText(text, getLocalBounds().reduced(12, 10).withTrimmedBottom(32), juce::Justification::centred, 6,
                     0.9f);
}

void ProgramBrowser::paint(juce::Graphics& g)
{
    drawSectionTitle(g, getLocalBounds(), "PROGRAM");
    const bool show = blinkOn_ || mode_ != Mode::WriteArmed;
    // BANK and PROGRAM: red displays (as the panel's)
    const juce::Rectangle<float> bankR(0.0f, 28.0f, 44.0f, 44.0f), progR(52.0f, 28.0f, 74.0f, 44.0f);
    for (auto r : {bankR, progR}) {
        g.setColour(col::segBg);
        g.fillRoundedRectangle(r, 2.0f);
        g.setColour(juce::Colours::black);
        g.drawRoundedRectangle(r, 2.0f, 1.0f);
    }
    drawSegments(g, bankR.reduced(8.0f, 7.0f), show ? bankText() : " ", col::segOn);
    drawSegments(g, progR.reduced(8.0f, 7.0f), show ? programText() : "  ", col::segOn);
    g.setColour(col::text);
    g.setFont(font(9.5f));
    g.drawText("BANK", bankR.translated(0.0f, 46.0f).withHeight(12.0f), juce::Justification::centred, false);
    g.drawText("PROGRAM", progR.translated(0.0f, 46.0f).withHeight(12.0f), juce::Justification::centred, false);
    // the name, the state LEDs
    const float nx = 138.0f;
    g.setFont(font(19.0f));
    g.drawText(name_.toUpperCase(), juce::Rectangle<float>(nx, 26.0f, (float)getWidth() - nx, 24.0f),
               juce::Justification::centredLeft, true);
    g.setFont(font(9.0f));
    struct L { const char* t; bool on; } leds[] = {{"EDITED", info_.edited}, {"DIFFERS FROM BANK", info_.differs},
                                                    {info_.user ? "USER SLOT" : "FACTORY", info_.user}};
    float lx = nx;
    for (auto& l : leds) {
        drawLed(g, {lx + 4.0f, 62.0f}, 7.0f, l.on);
        const float w = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), l.t) + 8.0f;
        g.setColour(l.on ? col::text : col::dim);
        g.drawText(l.t, juce::Rectangle<float>(lx + 11.0f, 56.0f, w, 12.0f), juce::Justification::centredLeft, false);
        lx += w + 22.0f;
    }
    // the message line
    g.setColour(messageError_ ? col::ledOn : col::sub);
    g.setFont(font(9.5f));
    g.drawFittedText(message_, juce::Rectangle<int>(0, 140, getWidth(), 20), juce::Justification::centredLeft, 2, 0.8f);
}

} // namespace knobi::ui
