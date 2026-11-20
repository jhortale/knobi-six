// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin panel: the controls (knob, switch pad, LED select list, 7-segment value) and what they are bound
// to (a Binding: a host parameter, or one of the library's globals).
//
// Parameter binding (ParamBinding), both ways, with no callback from any other thread into the UI: the editor's
// timer polls each control (Control::poll: the parameter's value, an atomic read) and shows a changed value; a user
// edit is a host change with a gesture (beginChangeGesture, setValueNotifyingHost, endChangeGesture; a drag: one
// gesture, its steps inside it). JUCE's own ParameterAttachment is not used on purpose: it listens to the parameter,
// and the AU / VST3 wrappers notify parameter listeners on the audio thread when automation plays, so its
// triggerAsyncUpdate would post a message (a lock, maybe an allocation) from the audio thread. Polling keeps the audio
// thread free of the UI (spec: no allocation or lock on the audio thread; Processor.h).
#pragma once
#include <functional>
#include <memory>
#include "../Params.h"
#include "Style.h"

namespace knobi::ui {

enum class Kind { Knob, Switch, Select };

struct Binding {
    virtual ~Binding() = default;
    Kind kind = Kind::Knob;
    int32_t min = 0, max = 1, def = 0;
    bool bipolar = false;
    juce::StringArray labels;                     // a select: labels[v - min]
    juce::String name;                            // the full name (tooltips, the value popup)
    virtual int paramId() const { return -1; }    // the host parameter's core id, -1: not a parameter
    virtual int32_t read() = 0;                   // the value now
    virtual void begin() {}
    virtual void set(int32_t v) = 0;
    virtual void end() {}
    juce::String text(int32_t v) const
    {
        if (kind == Kind::Select && v >= min && v - min < labels.size()) return labels[v - min];
        if (kind == Kind::Switch) return v ? "ON" : "OFF";
        return juce::String(v);
    }
};

class ParamBinding final : public Binding {
public:
    explicit ParamBinding(const Params::Entry& e);
    int paramId() const override { return e_.id; }
    int32_t read() override { return Params::toCore(e_.info, e_.param->getValue()); }
    void begin() override { e_.param->beginChangeGesture(); }
    void set(int32_t v) override { e_.param->setValueNotifyingHost(Params::toNormalised(e_.info, v)); }
    void end() override { e_.param->endChangeGesture(); }
private:
    const Params::Entry& e_;
};

// a binding to plain functions (the globals view)
class FnBinding final : public Binding {
public:
    std::function<int32_t()> get;
    std::function<void(int32_t)> put;             // at a gesture's end (or a click): the last value set
    int32_t read() override { return pending_ ? last_ : get(); }
    void begin() override { pending_ = true; }
    void set(int32_t v) override { last_ = v, pending_ = true; }
    void end() override { pending_ = false; put(last_); }
private:
    bool pending_ = false;
    int32_t last_ = 0;
};

// a control on the panel: one binding, a short label under it. Tests find the parameter controls with paramId()
class Control : public juce::Component, public juce::SettableTooltipClient {
public:
    Control(std::unique_ptr<Binding> b, juce::String label);
    Binding& binding() { return *b_; }
    int paramId() const { return b_->paramId(); }
    const juce::String& label() const { return label_; }
    int32_t shown() const { return shown_; }
    // the binding's value -> the view (unless the user is dragging it); true: the view changed
    bool poll();
    // the user's edit as the mouse makes it (tests: the same path as a click / drag ending at v)
    virtual void simulateUser(int32_t v) = 0;
    void setDimmed(bool d) { if (dimmed_ != d) dimmed_ = d, repaint(); }
protected:
    void userSet(int32_t v);                       // one complete gesture
    virtual void show(int32_t v) = 0;
    void paintLabel(juce::Graphics& g, juce::Rectangle<int> r);
    std::unique_ptr<Binding> b_;
    juce::String label_;
    int32_t shown_ = INT32_MIN;
    bool dragging_ = false, dimmed_ = false;
};

class Knob final : public Control, private juce::Slider::Listener {
public:
    static constexpr int kSize = 40;              // the knob's diameter (base pixels)
    Knob(std::unique_ptr<Binding> b, juce::String label);
    ~Knob() override;
    void resized() override;
    void paint(juce::Graphics& g) override;
    void simulateUser(int32_t v) override { slider_.setValue((double)v, juce::sendNotificationSync); }
    juce::Slider& slider() { return slider_; }
private:
    struct Look;
    juce::SharedResourcePointer<Look> look_;      // (declared before slider_: outlives its use)
    void show(int32_t v) override { slider_.setValue((double)v, juce::dontSendNotification); }
    void sliderValueChanged(juce::Slider*) override;
    void sliderDragStarted(juce::Slider*) override;
    void sliderDragEnded(juce::Slider*) override;
    juce::Slider slider_;
};

// a switch: a red pad, lit when on (click toggles)
class Switch final : public Control {
public:
    Switch(std::unique_ptr<Binding> b, juce::String label);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override
    {
        if (shown_ == INT32_MIN) poll();          // (not shown yet: toggle the parameter's own value)
        userSet(shown_ ? 0 : 1);
    }
    void simulateUser(int32_t v) override
    {
        if (shown_ == INT32_MIN) poll();
        if ((v != 0) != (shown_ != 0)) userSet(v ? 1 : 0);
    }
    juce::Rectangle<float> padArea() const;
private:
    void show(int32_t) override { repaint(); }
};

// a select: its choices as a list of red LEDs with their labels (columns x rows), click one to choose it
class Select final : public Control {
public:
    Select(std::unique_ptr<Binding> b, juce::String label, int columns = 1);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void simulateUser(int32_t v) override { userSet(juce::jlimit(b_->min, b_->max, v)); }
    juce::Rectangle<float> itemArea(int i) const;
    int columns() const { return cols_; }
private:
    void show(int32_t) override { repaint(); }
    int cols_;
};

// a value on a red 7-segment display, dragged up / down to change it (BPM); greyed shows another value (Live's tempo)
class SegValue final : public Control {
public:
    SegValue(std::unique_ptr<Binding> b, juce::String label, int digits);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent&) override { if (!greyed_) userSet(b_->def); }
    void simulateUser(int32_t v) override { userSet(juce::jlimit(b_->min, b_->max, v)); }
    // greyed: the parameter is not in use; other: what is shown instead (e.g. Live's tempo), "": the parameter's
    void setGreyed(bool g, const juce::String& other);
    bool greyed() const { return greyed_; }
    juce::String displayText() const;
private:
    void show(int32_t) override { repaint(); }
    int digits_;
    bool greyed_ = false;
    juce::String other_;
    int32_t dragFrom_ = 0;
};

// a momentary / toggle panel button that is not a parameter (WRITE, BANK, digits, SEQ PLAY ...): a red pad with a
// label; lit() is drawn lit; onPress runs on a click (the mouse pressed), onRelease (if set) when it is let go (SEQ REC:
// held while the sequence plays + a note = a transpose)
class Pad final : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit Pad(juce::String label, bool labelInside = false);
    std::function<void()> onPress, onRelease;
    void setLit(bool l) { if (lit_ != l) lit_ = l, repaint(); }
    bool lit() const { return lit_; }
    bool isDown() const { return down_; }         // pressed, not let go yet
    void press() { down_ = isEnabled(); if (down_ && onPress) onPress(); }   // (tests: as a click)
    void paint(juce::Graphics& g) override;
    void release() { if (down_ && onRelease) onRelease(); down_ = false; }   // (tests: the mouse let go)
    void mouseDown(const juce::MouseEvent&) override { press(); }
    void mouseUp(const juce::MouseEvent&) override { release(); }
    const juce::String& label() const { return label_; }
private:
    juce::String label_;
    bool inside_, lit_ = false, down_ = false;
};

} // namespace knobi::ui
