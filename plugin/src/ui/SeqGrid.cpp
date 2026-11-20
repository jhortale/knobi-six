// SPDX-License-Identifier: GPL-3.0-only
#include "SeqGrid.h"
#include <cstring>

namespace knobi::ui {

// ---- the edit operations (synth/seq.h's model and rules)
namespace seqedit {

juce::String noteName(int note)
{
    static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (note < 0 || note > 127) return "?";
    return juce::String(names[note % 12]) + juce::String(note / 12 - 1);
}

bool valid(const seq_t& s)
{
    if (s.len > SEQ_MAX_STEPS) return false;
    for (int i = 0; i < s.len; i++) {
        const seq_step_t& st = s.step[i];
        if (st.count > SEQ_MAX_NOTES || st.tie > 1) return false;
        if (st.tie && (st.count == 0 || i == 0 || s.step[i - 1].count == 0)) return false;
        for (int k = 0; k < st.count; k++)
            if (st.notes[k] < SEQ_NOTE_MIN || st.notes[k] > SEQ_NOTE_MAX || st.vels[k] > 127) return false;
    }
    return true;
}

void fixTies(seq_t& s)
{
    for (int i = 0; i < s.len; i++) {
        seq_step_t& st = s.step[i];
        if (!st.tie) continue;
        if (i == 0 || s.step[i - 1].count == 0 || st.count == 0) { st.tie = 0; continue; }   // (untied: its notes kept)
        const seq_step_t& prev = s.step[i - 1];
        std::memcpy(st.notes, prev.notes, sizeof st.notes);   // (the copy of the step before: seq.h)
        std::memcpy(st.vels, prev.vels, sizeof st.vels);
        st.count = prev.count;
    }
    for (int i = s.len; i < (int)SEQ_MAX_STEPS; i++) std::memset(&s.step[i], 0, sizeof s.step[i]);
}

static bool inside(const seq_t& s, int step) { return step >= 0 && step < s.len; }

static void compact(seq_step_t& st)               // (unused slots zero)
{
    for (int k = st.count; k < (int)SEQ_MAX_NOTES; k++) st.notes[k] = st.vels[k] = 0;
    if (!st.count) st.tie = 0;
}

Result toggleNote(seq_t& s, int step, int note, int vel)
{
    Result r;
    if (!inside(s, step)) return r;
    if (note < SEQ_NOTE_MIN || note > SEQ_NOTE_MAX) { r.why = "NOTES 12-108 ONLY (C0-C8)"; return r; }
    seq_step_t& st = s.step[step];
    int at = -1;
    for (int k = 0; k < st.count; k++) if (st.notes[k] == note) at = k;
    if (at < 0 && st.count >= SEQ_MAX_NOTES) { r.why = "6 NOTES AT MOST"; return r; }
    st.tie = 0;                                   // (a tie's notes are a copy: changed, it is a step of its own)
    if (at >= 0) {
        for (int k = at; k + 1 < st.count; k++) st.notes[k] = st.notes[k + 1], st.vels[k] = st.vels[k + 1];
        st.count--;
    } else {
        st.notes[st.count] = (uint8_t)note, st.vels[st.count] = (uint8_t)juce::jlimit(1, 127, vel);
        st.count++;
    }
    compact(st);
    fixTies(s);
    r.changed = true;
    return r;
}

Result removeNote(seq_t& s, int step, int index)
{
    if (!inside(s, step) || index < 0 || index >= s.step[step].count) return {};
    return toggleNote(s, step, s.step[step].notes[index]);
}

Result setVelocity(seq_t& s, int step, int index, int vel)
{
    Result r;
    if (!inside(s, step) || index < 0 || index >= s.step[step].count) return r;
    if (s.step[step].tie) { r.why = "TIED: SET IT ON STEP " + juce::String(step); return r; }
    const uint8_t v = (uint8_t)juce::jlimit(1, 127, vel);
    if (s.step[step].vels[index] == v) return r;
    s.step[step].vels[index] = v;
    fixTies(s);
    r.changed = true;
    return r;
}

Result makeRest(seq_t& s, int step)
{
    Result r;
    if (!inside(s, step) || s.step[step].count == 0) return r;
    std::memset(&s.step[step], 0, sizeof s.step[step]);
    fixTies(s);                                   // (a tie after it: untied, its notes kept)
    r.changed = true;
    return r;
}

Result toggleTie(seq_t& s, int step)
{
    Result r;
    if (!inside(s, step)) return r;
    seq_step_t& st = s.step[step];
    if (st.tie) {                                 // untie: the notes it carries, played anew
        st.tie = 0;
        r.changed = true;
        return r;
    }
    if (step == 0) { r.why = "NO TIE ON STEP 1"; return r; }
    if (s.step[step - 1].count == 0) { r.why = "NO TIE AFTER A REST"; return r; }
    st.tie = 1, st.count = s.step[step - 1].count;
    fixTies(s);
    r.changed = true;
    return r;
}

Result setLength(seq_t& s, int len)
{
    Result r;
    len = juce::jlimit(1, (int)SEQ_MAX_STEPS, len);
    if (len == s.len) return r;
    for (int i = len; i < (int)SEQ_MAX_STEPS; i++) std::memset(&s.step[i], 0, sizeof s.step[i]);   // (new: rests)
    s.len = (uint8_t)len;
    fixTies(s);
    r.changed = true;
    return r;
}

} // namespace seqedit

// ---- the STEP editor
StepEditor::StepEditor(SeqGrid& g) : g_(g)
{
    for (Pad* p : {&rest_, &tie_, &close_, &prev_, &next_, &octDown_, &octUp_}) addAndMakeVisible(*p);
    rest_.onPress = [this] { g_.edit([this](seq_t& s) { return seqedit::makeRest(s, step_); }); };
    tie_.onPress = [this] { g_.edit([this](seq_t& s) { return seqedit::toggleTie(s, step_); }); };
    close_.onPress = [this] { setVisible(false); g_.repaint(); };
    prev_.onPress = [this] { if (step_ > 0) g_.openStep(step_ - 1); };
    next_.onPress = [this] { if (step_ + 1 < g_.length()) g_.openStep(step_ + 1); };
    octDown_.onPress = [this] { if (base_ > 12) base_ -= 12, repaint(); };
    octUp_.onPress = [this] { if (base_ < 84) base_ += 12, repaint(); };
    rest_.setTooltip("REST: the step plays nothing");
    tie_.setTooltip("TIE: the step before held on through this one (not on step 1, not after a rest); again: untie");
    setVisible(false);
}

void StepEditor::showStep(int step)
{
    step_ = step;
    const seq_step_t& st = g_.seq_.step[step];
    if (st.count) {                               // (the piano where the step's notes are)
        int lo = 127;
        for (int k = 0; k < st.count; k++) lo = juce::jmin(lo, (int)st.notes[k]);
        if (lo < base_ || lo > base_ + 24) base_ = juce::jlimit(12, 84, lo / 12 * 12);
    }
    setVisible(true);
    repaint();
}

void StepEditor::resized()
{
    prev_.setBounds(150, 4, 24, 22), next_.setBounds(176, 4, 24, 22);
    rest_.setBounds(212, 4, 56, 22), tie_.setBounds(272, 4, 56, 22);
    close_.setBounds(getWidth() - 68, 4, 62, 22);
    octDown_.setBounds(kPianoX, kPianoY + kWhiteH + 8, 24, 20), octUp_.setBounds(kPianoX + 132, kPianoY + kWhiteH + 8, 24, 20);
}

juce::Rectangle<int> StepEditor::keyBounds(int note) const
{
    if (note < base_ || note > base_ + 24) return {};
    int w = 0;
    for (int n = base_; n < note; n++) w += !black(n);
    const int x = kPianoX + w * kWhiteW;
    if (black(note)) return {x - 9, kPianoY, 18, kBlackH};
    return {x, kPianoY, kWhiteW, kWhiteH};
}

int StepEditor::noteAt(juce::Point<int> p) const
{
    for (int n = base_; n <= base_ + 24; n++) if (black(n) && keyBounds(n).contains(p)) return n;
    for (int n = base_; n <= base_ + 24; n++) if (!black(n) && keyBounds(n).contains(p)) return n;
    return -1;
}

juce::Rectangle<int> StepEditor::barBounds(int index) const
{
    return {kNotesX + index * kNoteW + 12, kBarTop, kNoteW - 24, kBarH};
}

juce::Rectangle<int> StepEditor::removeBounds(int index) const
{
    return {kNotesX + index * kNoteW + 10, kBarTop + kBarH + 18, kNoteW - 20, 16};
}

int StepEditor::velAt(int y) const
{
    return juce::jlimit(1, 127, juce::roundToInt((double)(kBarTop + kBarH - y) * 127.0 / (double)kBarH));
}

void StepEditor::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    const seq_step_t& st = g_.seq_.step[step_];
    if (const int note = noteAt(p); note >= 0) {
        g_.edit([this, note](seq_t& s) { return seqedit::toggleNote(s, step_, note); });
        return;
    }
    for (int k = 0; k < st.count; k++) {
        if (removeBounds(k).contains(p)) {
            g_.edit([this, k](seq_t& s) { return seqedit::removeNote(s, step_, k); });
            return;
        }
        if (barBounds(k).expanded(4, 6).contains(p)) {
            if (st.tie) { g_.say("TIED: SET IT ON STEP " + juce::String(step_)); return; }
            dragIndex_ = k, dragVel_ = velAt(p.y);
            repaint();
            return;
        }
    }
}

void StepEditor::mouseDrag(const juce::MouseEvent& e)
{
    if (dragIndex_ < 0) return;
    dragVel_ = velAt(e.getPosition().y);
    repaint();
}

void StepEditor::mouseUp(const juce::MouseEvent&)
{
    if (dragIndex_ < 0) return;
    const int k = dragIndex_, v = dragVel_;
    dragIndex_ = -1, dragVel_ = -1;
    g_.edit([this, k, v](seq_t& s) { return seqedit::setVelocity(s, step_, k, v); });
    repaint();
}

void StepEditor::paint(juce::Graphics& g)
{
    const seq_t& s = g_.seq_;
    const seq_step_t& st = s.step[step_];
    g.setColour(juce::Colour(0xff101114));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    g.setColour(col::rule);
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 4.0f, 1.0f);
    g.setColour(col::text);
    g.setFont(font(13.0f));
    g.drawText("STEP " + juce::String(step_ + 1), 8, 4, 70, 22, juce::Justification::centredLeft, false);
    g.setFont(font(9.5f));
    g.setColour(col::sub);
    const juce::String kind = st.tie ? "TIED TO STEP " + juce::String(step_)
                              : st.count ? juce::String(st.count) + (st.count == 1 ? " NOTE" : " NOTES") : "REST";
    g.drawText(kind, 70, 4, 80, 22, juce::Justification::centredLeft, true);
    tie_.setLit(st.tie != 0);
    rest_.setLit(st.count == 0);
    // the notes: name, velocity bar, value, X
    for (int k = 0; k < (int)SEQ_MAX_NOTES; k++) {
        const int x = kNotesX + k * kNoteW;
        const auto bar = barBounds(k);
        g.setColour(juce::Colour(0xff1a1b1f));
        g.fillRoundedRectangle(bar.toFloat(), 2.0f);
        if (k >= st.count) continue;
        const int v = k == dragIndex_ ? dragVel_ : st.vels[k];
        g.setColour(st.tie ? col::dim : col::text);
        g.setFont(font(10.5f));
        g.drawText(seqedit::noteName(st.notes[k]), x, kBarTop - 18, kNoteW, 16, juce::Justification::centred, false);
        const float h = (float)bar.getHeight() * (float)v / 127.0f;
        g.setColour(st.tie ? col::ledOff : col::segOn);
        g.fillRoundedRectangle(bar.toFloat().withTop((float)bar.getBottom() - h), 2.0f);
        g.setColour(col::sub);
        g.setFont(font(9.0f));
        g.drawText(juce::String(v), x, bar.getBottom() + 2, kNoteW, 14, juce::Justification::centred, false);
        const auto rm = removeBounds(k).toFloat();
        drawPad(g, rm, false, true);
        g.setColour(col::text);
        g.drawText("X", rm.toNearestInt(), juce::Justification::centred, false);
    }
    g.setColour(col::dim);
    g.setFont(font(8.5f));
    g.drawText("VELOCITY: DRAG THE BARS", kNotesX, getHeight() - 14, 280, 12, juce::Justification::centredLeft, false);
    // the piano: white keys, then black; the step's notes lit
    auto lit = [&st](int n) {
        for (int k = 0; k < st.count; k++) if (st.notes[k] == n) return true;
        return false;
    };
    for (int pass = 0; pass < 2; pass++)
        for (int n = base_; n <= base_ + 24; n++) {
            if (black(n) != (pass == 1)) continue;
            const auto r = keyBounds(n).toFloat().reduced(pass ? 0.0f : 0.5f);
            const bool on = lit(n), usable = n >= SEQ_NOTE_MIN && n <= SEQ_NOTE_MAX;
            g.setColour(on ? col::btnOn : !usable ? juce::Colour(0xff55575c) : pass ? juce::Colour(0xff111113)
                                                                                   : juce::Colour(0xffe8e8e4));
            g.fillRoundedRectangle(r, 2.0f);
            g.setColour(juce::Colour(0xff000000));
            g.drawRoundedRectangle(r, 2.0f, 1.0f);
            if (!pass && n % 12 == 0) {
                g.setColour(on ? juce::Colours::black : juce::Colour(0xff6a6c72));
                g.setFont(font(8.0f));
                g.drawText(seqedit::noteName(n), r.toNearestInt().removeFromBottom(14), juce::Justification::centred,
                           false);
            }
        }
    g.setColour(col::sub);
    g.setFont(font(9.5f));
    g.drawText(seqedit::noteName(base_) + " - " + seqedit::noteName(base_ + 24), kPianoX + 28, kPianoY + kWhiteH + 8, 100,
               20, juce::Justification::centred, false);
    g.setColour(col::dim);
    g.setFont(font(8.5f));
    g.drawText("CLICK A KEY: ADD / REMOVE (6 NOTES AT MOST)", kPianoX + 164, kPianoY + kWhiteH + 8, 250, 20,
               juce::Justification::centredLeft, false);
}

// ---- the grid
SeqGrid::SeqGrid(Processor& p) : p_(p), editor_(*this)
{
    setComponentID("seqGrid");
    addAndMakeVisible(lenDown_);
    addAndMakeVisible(lenUp_);
    addChildComponent(editor_);
    lenDown_.onPress = [this] { setLengthNow(seq_.len - 1); };
    lenUp_.onPress = [this] { setLengthNow(seq_.len + 1); };
    lenDown_.setTooltip("LENGTH - 1");
    lenUp_.setTooltip("LENGTH + 1 (a rest added)");
}

void SeqGrid::resized()
{
    lenDown_.setBounds(52, 1, 22, 20);
    lenUp_.setBounds(114, 1, 22, 20);
    editor_.setBounds(0, kHeader, getWidth(), getHeight() - kHeader);
}

juce::Rectangle<int> SeqGrid::lengthBounds() const { return {76, 1, 36, 20}; }

juce::Rectangle<int> SeqGrid::cellBounds(int i) const
{
    const int r = i / kCols, c = i % kCols, w = getWidth(), h = getHeight() - kHeader;
    const int x0 = c * w / kCols, x1 = (c + 1) * w / kCols, y0 = kHeader + r * h / kRows, y1 = kHeader + (r + 1) * h / kRows;
    return {x0, y0, x1 - x0, y1 - y0};
}

void SeqGrid::fetch()
{
    const uint32_t v = p_.seqVersion();
    seq_t t;
    fetches_++;
    if (!p_.seqGet(t)) return;
    seq_ = t, ver_ = v, have_ = true;
}

void SeqGrid::poll()
{
    bool dirty = false;
    if (!have_ || p_.seqVersion() != ver_) {      // (the only access(): the sequence changed)
        fetch();
        dirty = true;
        if (editing() && editor_.step() >= seq_.len) editor_.setVisible(false);
        else if (editing()) editor_.repaint();
    }
    const int st = p_.seqStep(), rec = p_.seqRecState(), xp = p_.seqXp();
    if (st != step_) step_ = st, dirty = true;
    if (xp != xp_) xp_ = xp, dirty = true;
    if (rec != rec_) {
        rec_ = rec, dirty = true;
        if ((rec & KC_REC_ON) && editing()) editor_.setVisible(false);   // (the step entry's now)
    }
    if (dirty) {
        if (rec_ & KC_REC_FRESH) seq_clear(&shownSeq_);
        else shownSeq_ = seq_;
        const bool editable = !(rec_ & KC_REC_ON);
        lenDown_.setEnabled(editable), lenUp_.setEnabled(editable);
        repaint();
    }
}

void SeqGrid::say(const juce::String& m)
{
    if (m != msg_) msg_ = m, repaint(0, 0, getWidth(), kHeader);
}

bool SeqGrid::edit(const std::function<seqedit::Result(seq_t&)>& op)
{
    if (recording()) { say("RECORDING: THE STEP ENTRY HAS THE SEQUENCE (SEQ REC ENDS IT)"); return false; }
    if (!have_ || p_.seqVersion() != ver_) fetch();   // (changed meanwhile: a load, a recorded step)
    seq_t t = seq_;
    const auto r = op(t);
    if (r.why.isNotEmpty()) { say(r.why); return false; }
    if (!r.changed) return false;
    jassert(seqedit::valid(t));
    const int rc = p_.seqSet(t);
    if (rc == 0 || rc == 1) {
        seq_ = t, ver_ = p_.seqVersion(), have_ = true;
        say({});
    } else {
        say(rc == -2 ? "RECORDING: THE STEP ENTRY HAS THE SEQUENCE (SEQ REC ENDS IT)"
            : rc == -1 ? "REFUSED: NOT A SEQUENCE THE SYNTH STORES" : "NO ANSWER FROM THE ENGINE: TRY AGAIN");
        fetch();
    }
    shownSeq_ = seq_;
    if (editing() && editor_.step() >= seq_.len) editor_.setVisible(false);
    editor_.repaint();
    repaint();
    return rc == 0 || rc == 1;
}

void SeqGrid::setLengthNow(int len)
{
    edit([len](seq_t& s) { return seqedit::setLength(s, len); });
}

void SeqGrid::openStep(int step)
{
    if (recording()) { say("RECORDING: THE STEP ENTRY HAS THE SEQUENCE (SEQ REC ENDS IT)"); return; }
    if (step < 0 || step >= seq_.len) {
        say("STEP " + juce::String(step + 1) + " IS PAST THE END: RAISE THE LENGTH");
        return;
    }
    say({});
    editor_.showStep(step);
    repaint();
}

void SeqGrid::mouseDown(const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (lengthBounds().contains(p)) {
        if (recording()) { say("RECORDING: THE STEP ENTRY HAS THE SEQUENCE (SEQ REC ENDS IT)"); return; }
        dragLen_ = dragFrom_ = juce::jmax(1, (int)seq_.len), dragY_ = p.y;
        return;
    }
    for (int i = 0; i < (int)SEQ_MAX_STEPS; i++)
        if (cellBounds(i).contains(p)) { openStep(i); return; }
}

void SeqGrid::mouseDrag(const juce::MouseEvent& e)
{
    if (dragLen_ < 0) return;
    const int len = juce::jlimit(1, (int)SEQ_MAX_STEPS, dragFrom_ + (dragY_ - e.getPosition().y) / 4);
    if (len != dragLen_) dragLen_ = len, repaint(0, 0, getWidth(), kHeader);
}

void SeqGrid::mouseUp(const juce::MouseEvent&)
{
    if (dragLen_ < 0) return;
    const int len = dragLen_;
    dragLen_ = -1;
    setLengthNow(len);
}

SeqGrid::Cell SeqGrid::cell(int i) const
{
    Cell c;
    const seq_t& s = shownSeq_;
    c.past = i >= s.len;
    c.playing = !recording() && i == step_ && !c.past;
    c.selected = editing() && editor_.step() == i;
    c.next = recording() && i == s.len;
    if (c.past) return c;
    const seq_step_t& st = s.step[i];
    c.count = st.count, c.rest = st.count == 0, c.tie = st.tie != 0;
    for (int k = 0; k < st.count; k++) c.text << (k ? " " : "") << seqedit::noteName(st.notes[k]);
    return c;
}

void SeqGrid::paint(juce::Graphics& g)
{
    // the header: LENGTH, the message, XP
    g.setColour(col::sub);
    g.setFont(font(9.5f));
    g.drawText("LENGTH", 0, 1, 50, 20, juce::Justification::centredLeft, false);
    const auto lb = lengthBounds().toFloat();
    g.setColour(col::segBg);
    g.fillRoundedRectangle(lb, 2.0f);
    const int len = dragLen_ >= 0 ? dragLen_ : shownSeq_.len;
    drawSegments(g, lb.reduced(4.0f, 3.0f), juce::String(len).paddedLeft(' ', 2), col::segOn);
    if (msg_.isNotEmpty()) {
        g.setColour(col::ledOn);
        g.setFont(font(10.0f));
        g.drawText(msg_, 146, 1, getWidth() - 146 - 70, 20, juce::Justification::centredLeft, true);
    } else if (recording()) {
        g.setColour(col::sub);
        g.setFont(font(9.5f));
        const int held = KC_REC_HELD(rec_);
        g.drawText("STEP ENTRY: NOTES FROM LIVE" + (held ? juce::String(" - ") + juce::String(held) + " HELD" : juce::String()),
                   146, 1, getWidth() - 146 - 70, 20, juce::Justification::centredLeft, true);
    } else {
        g.setColour(col::dim);
        g.setFont(font(9.0f));
        g.drawText("CLICK A STEP TO EDIT IT", 146, 1, 300, 20, juce::Justification::centredLeft, false);
    }
    if (xp_) {
        g.setColour(col::sub);
        g.setFont(font(9.5f));
        g.drawText("XP " + juce::String(xp_ > 0 ? "+" : "") + juce::String(xp_), getWidth() - 66, 1, 66, 20,
                   juce::Justification::centredRight, false);
    }
    // the steps
    for (int i = 0; i < (int)SEQ_MAX_STEPS; i++) {
        const Cell c = cell(i);
        const auto r = cellBounds(i).toFloat().reduced(2.0f);
        g.setColour(c.past ? col::slot : c.playing ? juce::Colour(0xff3a2420) : juce::Colour(0xff26282d));
        g.fillRoundedRectangle(r, 2.5f);
        if (c.past) {
            g.setColour(juce::Colour(0xff2a2c31));
            g.drawRoundedRectangle(r, 2.5f, 1.0f);
        }
        g.setColour(c.past ? juce::Colour(0xff3c3f45) : col::dim);
        g.setFont(font(7.5f));
        g.drawText(juce::String(i + 1), r.toNearestInt().reduced(3, 1), juce::Justification::topLeft, false);
        if (!c.past) {
            const seq_step_t& st = shownSeq_.step[i];
            if (c.rest) {
                g.setColour(col::dim);
                g.fillRect(r.getCentreX() - 7.0f, r.getCentreY(), 14.0f, 2.0f);
            } else {
                if (c.tie) {                      // a bar joined to the step before
                    g.setColour(col::rule);
                    g.fillRect(r.getX() - 3.0f, r.getY() + 3.0f, r.getWidth() * 0.6f, 3.0f);
                    g.setFont(font(7.0f));
                    g.drawText("TIE", r.toNearestInt().reduced(3, 1), juce::Justification::topRight, false);
                }
                int vmax = 0;
                g.setFont(font(8.0f, false));
                for (int k = 0; k < st.count; k++) {
                    const float a = c.tie ? 0.45f : 0.45f + 0.55f * (float)st.vels[k] / 127.0f;
                    g.setColour(col::text.withAlpha(a));
                    const float x = r.getX() + 2.0f + (float)(k / 3) * (r.getWidth() - 4.0f) * 0.5f;
                    const float y = r.getY() + 11.0f + (float)(k % 3) * 9.5f;
                    g.drawText(seqedit::noteName(st.notes[k]), juce::Rectangle<float>(x, y, (r.getWidth() - 4.0f) * 0.5f, 9.5f),
                               juce::Justification::centred, false);
                    vmax = juce::jmax(vmax, (int)st.vels[k]);
                }
                g.setColour(c.tie ? col::ledOff : col::segOn.withAlpha(0.8f));
                g.fillRect(r.getX() + 3.0f, r.getBottom() - 4.0f, (r.getWidth() - 6.0f) * (float)vmax / 127.0f, 2.0f);
            }
        }
        if (c.playing || c.next) {
            g.setColour(col::ledOn);
            g.drawRoundedRectangle(r, 2.5f, c.playing ? 2.0f : 1.2f);
        } else if (c.selected) {
            g.setColour(col::rule);
            g.drawRoundedRectangle(r, 2.5f, 2.0f);
        }
    }
}

} // namespace knobi::ui
