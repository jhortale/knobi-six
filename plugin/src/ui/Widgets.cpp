// SPDX-License-Identifier: GPL-3.0-only
#include "Widgets.h"

namespace knobi::ui {

ParamBinding::ParamBinding(const Params::Entry& e) : e_(e)
{
    const auto& in = e.info;
    kind = in.kind == KC_KIND_SWITCH ? Kind::Switch : in.kind == KC_KIND_SELECT && in.labels ? Kind::Select : Kind::Knob;
    min = in.min, max = in.max, def = in.def, bipolar = in.bipolar != 0;
    name = in.name;
    if (kind == Kind::Select)
        for (int32_t v = in.min; v <= in.max; v++) labels.add(in.labels[v - in.min]);
}

// ---- Control
Control::Control(std::unique_ptr<Binding> b, juce::String label) : b_(std::move(b)), label_(std::move(label))
{
    setName(b_->name);
}

bool Control::poll()
{
    if (dragging_) return false;
    const int32_t v = b_->read();
    if (v == shown_) return false;
    shown_ = v;
    show(v);
    return true;
}

void Control::userSet(int32_t v)
{
    b_->begin();
    b_->set(v);
    b_->end();
    shown_ = v;
    show(v);
}

void Control::paintLabel(juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour(dimmed_ ? col::dim : col::text);
    g.setFont(font(9.5f));
    g.drawFittedText(label_, r, juce::Justification::centredTop, 2, 0.8f);
}

// ---- Knob
namespace {
class KnobLook final : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1,
                          juce::Slider& s) override
    {
        const float d = (float)juce::jmin(w, h) - 4.0f;
        const auto r = juce::Rectangle<float>(d, d).withCentre({(float)x + (float)w * 0.5f, (float)y + (float)h * 0.5f});
        const bool bipolar = (bool)s.getProperties()["bipolar"];
        // the scale: a faint arc, a tick at the centre of a bipolar knob
        juce::Path arc;
        arc.addCentredArc(r.getCentreX(), r.getCentreY(), d * 0.5f + 1.5f, d * 0.5f + 1.5f, 0.0f, a0, a1, true);
        g.setColour(juce::Colour(0x30ffffff));
        g.strokePath(arc, juce::PathStrokeType(1.0f));
        if (bipolar) {
            g.setColour(juce::Colour(0x90ffffff));
            g.fillRect(juce::Rectangle<float>(1.5f, 3.0f).withCentre({r.getCentreX(), r.getY() - 2.0f}));
        }
        g.setColour(juce::Colour(0xb0000000));
        g.fillEllipse(r.translated(0.0f, 2.0f).expanded(0.5f));   // (the shadow)
        juce::ColourGradient grad(col::knobHi, r.getX() + d * 0.38f, r.getY() + d * 0.32f, col::knobLo,
                                  r.getX() + d * 0.38f + d * 0.65f, r.getY() + d * 0.32f, true);
        g.setGradientFill(grad);
        g.fillEllipse(r);
        const float ang = a0 + pos * (a1 - a0);
        const float rr = d * 0.5f - d * 0.17f;
        const juce::Point<float> dot(r.getCentreX() + rr * std::sin(ang), r.getCentreY() - rr * std::cos(ang));
        g.setColour(s.isEnabled() ? juce::Colours::white : juce::Colour(0xff808080));
        g.fillEllipse(juce::Rectangle<float>(d * 0.12f, d * 0.12f).withCentre(dot));
    }
    void drawBubble(juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&,
                    const juce::Rectangle<float>& body) override
    {
        g.setColour(col::segBg);
        g.fillRoundedRectangle(body, 3.0f);
        g.setColour(col::segOn);
        g.drawRoundedRectangle(body, 3.0f, 1.0f);
    }
    juce::Font getSliderPopupFont(juce::Slider&) override { return font(12.0f); }
    int getSliderPopupPlacement(juce::Slider&) override { return juce::BubbleComponent::above; }
};
} // namespace

juce::LookAndFeel& Knob::lookAndFeel()
{
    static KnobLook look;
    return look;
}

Knob::Knob(std::unique_ptr<Binding> b, juce::String label) : Control(std::move(b), std::move(label))
{
    slider_.setLookAndFeel(&lookAndFeel());
    slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider_.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    slider_.setRange((double)b_->min, (double)b_->max, 1.0);
    slider_.setMouseDragSensitivity(220);
    slider_.setDoubleClickReturnValue(true, (double)b_->def);
    slider_.setColour(juce::Slider::textBoxTextColourId, col::segOn);
    slider_.getProperties().set("bipolar", b_->bipolar);
    Binding* bp = b_.get();
    slider_.textFromValueFunction = [bp](double v) { return bp->name + "  " + bp->text((int32_t)std::lround(v)); };
    slider_.setTooltip(b_->name);
    slider_.addListener(this);
    addAndMakeVisible(slider_);
}

Knob::~Knob() { slider_.setLookAndFeel(nullptr); }

void Knob::resized()
{
    const int d = kSize + 4;
    slider_.setBounds((getWidth() - d) / 2, 0, d, d);
}

void Knob::paint(juce::Graphics& g) { paintLabel(g, getLocalBounds().withTrimmedTop(kSize + 6)); }

void Knob::sliderValueChanged(juce::Slider*)
{
    const int32_t v = (int32_t)std::lround(slider_.getValue());
    if (dragging_) { b_->set(v), shown_ = v; return; }
    if (v != shown_) userSet(v);                  // (a double click, the wheel, a test: one complete gesture)
}

void Knob::sliderDragStarted(juce::Slider*) { dragging_ = true, b_->begin(); }

void Knob::sliderDragEnded(juce::Slider*)
{
    dragging_ = false;
    b_->end();
}

// ---- Switch
Switch::Switch(std::unique_ptr<Binding> b, juce::String label) : Control(std::move(b), std::move(label))
{
    setTooltip(b_->name);
}

juce::Rectangle<float> Switch::padArea() const
{
    return juce::Rectangle<float>(24.0f, 12.0f).withCentre({(float)getWidth() * 0.5f, 10.0f});
}

void Switch::paint(juce::Graphics& g)
{
    drawPad(g, padArea(), shown_ > 0, isEnabled());
    paintLabel(g, getLocalBounds().withTrimmedTop(21));
}

// ---- Select
Select::Select(std::unique_ptr<Binding> b, juce::String label, int columns)
    : Control(std::move(b), std::move(label)), cols_(juce::jmax(1, columns))
{
    setTooltip(b_->name);
}

juce::Rectangle<float> Select::itemArea(int i) const
{
    const int n = b_->max - b_->min + 1, rows = (n + cols_ - 1) / cols_;
    const float labelH = label_.isEmpty() ? 0.0f : 14.0f;
    const float rowH = juce::jmin(13.0f, ((float)getHeight() - labelH) / (float)rows);
    const float colW = (float)getWidth() / (float)cols_;
    const int c = i / rows, r = i % rows;
    return {colW * (float)c, rowH * (float)r, colW, rowH};
}

void Select::paint(juce::Graphics& g)
{
    const int n = b_->max - b_->min + 1;
    g.setFont(font(8.5f));
    float bottom = 0.0f;
    for (int i = 0; i < n; i++) {
        const auto a = itemArea(i);
        bottom = juce::jmax(bottom, a.getBottom());
        const bool on = shown_ - b_->min == i;
        drawLed(g, {a.getX() + 6.0f, a.getCentreY()}, 6.0f, on);
        g.setColour(on ? col::text : col::sub.withAlpha(0.85f));
        g.drawText(b_->labels[i], a.withTrimmedLeft(13.0f), juce::Justification::centredLeft, false);
    }
    paintLabel(g, getLocalBounds().withTrimmedTop((int)bottom + 3));
}

void Select::mouseDown(const juce::MouseEvent& e)
{
    const int n = b_->max - b_->min + 1;
    for (int i = 0; i < n; i++)
        if (itemArea(i).contains(e.position)) { userSet(b_->min + i); return; }
}

// ---- SegValue
SegValue::SegValue(std::unique_ptr<Binding> b, juce::String label, int digits)
    : Control(std::move(b), std::move(label)), digits_(digits)
{
    setTooltip(b_->name + " (drag up / down)");
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
}

void SegValue::setGreyed(bool g, const juce::String& other)
{
    if (greyed_ == g && other_ == other) return;
    greyed_ = g, other_ = other;
    repaint();
}

juce::String SegValue::displayText() const
{
    if (greyed_ && other_.isNotEmpty()) return other_;
    return shown_ == INT32_MIN ? juce::String() : juce::String(shown_).paddedLeft(' ', digits_);
}

void SegValue::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().removeFromTop((float)getHeight() - 14.0f).reduced(2.0f, 0.0f);
    g.setColour(col::segBg);
    g.fillRoundedRectangle(r, 2.0f);
    g.setColour(juce::Colours::black);
    g.drawRoundedRectangle(r, 2.0f, 1.0f);
    drawSegments(g, r.reduced(6.0f, 5.0f), displayText(), greyed_ ? col::segOn.withSaturation(0.15f).withAlpha(0.6f) : col::segOn);
    paintLabel(g, getLocalBounds().withTrimmedTop(getHeight() - 12));
}

void SegValue::mouseDown(const juce::MouseEvent&)
{
    if (greyed_) return;
    dragging_ = true, dragFrom_ = shown_;
    b_->begin();
}

void SegValue::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging_) return;
    const int32_t v = juce::jlimit(b_->min, b_->max, dragFrom_ - e.getDistanceFromDragStartY() / 3);
    if (v != shown_) b_->set(v), shown_ = v, repaint();
}

void SegValue::mouseUp(const juce::MouseEvent&)
{
    if (!dragging_) return;
    dragging_ = false;
    b_->end();
}

// ---- Pad
Pad::Pad(juce::String label, bool labelInside) : label_(std::move(label)), inside_(labelInside) {}

void Pad::paint(juce::Graphics& g)
{
    const bool en = isEnabled();
    if (inside_) {
        const auto r = getLocalBounds().toFloat().reduced(2.0f);
        drawPad(g, r, lit_, en);
        g.setColour(lit_ && en ? juce::Colours::black : en ? col::text : col::dim);
        g.setFont(font(juce::jmin(11.0f, r.getHeight() * 0.62f)));
        g.drawFittedText(label_, r.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        return;
    }
    drawPad(g, juce::Rectangle<float>(26.0f, 13.0f).withCentre({(float)getWidth() * 0.5f, 10.0f}), lit_, en);
    g.setColour(en ? col::text : col::dim);
    g.setFont(font(9.5f));
    g.drawFittedText(label_, getLocalBounds().withTrimmedTop(21), juce::Justification::centredTop, 2, 0.8f);
}

} // namespace knobi::ui
