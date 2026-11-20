// SPDX-License-Identifier: GPL-3.0-only
#include "Style.h"

namespace knobi::ui {

namespace {
// segments a b c d e f g (bit 0 .. 6): a top, b top right, c bottom right, d bottom, e bottom left, f top left, g middle
int segMask(juce::juce_wchar c)
{
    switch (c) {
    case '0': case 'O': return 0x3f;
    case '1': case 'I': return 0x06;
    case '2': return 0x5b;
    case '3': return 0x4f;
    case '4': return 0x66;
    case '5': case 'S': case 's': return 0x6d;
    case '6': return 0x7d;
    case '7': return 0x07;
    case '8': return 0x7f;
    case '9': return 0x6f;
    case '-': return 0x40;
    case '_': return 0x08;
    case 'A': case 'a': return 0x77;
    case 'B': case 'b': return 0x7c;
    case 'C': return 0x39;
    case 'c': return 0x58;
    case 'D': case 'd': return 0x5e;
    case 'E': case 'e': return 0x79;
    case 'F': case 'f': return 0x71;
    case 'H': case 'h': return 0x76;
    case 'L': case 'l': return 0x38;
    case 'N': case 'n': return 0x54;
    case 'o': return 0x5c;
    case 'P': case 'p': return 0x73;
    case 'R': case 'r': return 0x50;
    case 'T': case 't': return 0x78;
    case 'U': case 'u': return 0x3e;
    case 'Y': case 'y': return 0x6e;
    default: return 0;
    }
}

juce::Path segment(float x0, float y0, float x1, float y1, float t)   // a segment from (x0, y0) to (x1, y1), thickness t
{
    juce::Path p;
    const bool horizontal = std::abs(y1 - y0) < std::abs(x1 - x0);
    const float h = t * 0.5f, in = t * 0.15f;
    if (horizontal) {
        x0 += in, x1 -= in;
        p.startNewSubPath(x0, y0);
        p.lineTo(x0 + h, y0 - h), p.lineTo(x1 - h, y0 - h), p.lineTo(x1, y0), p.lineTo(x1 - h, y0 + h),
            p.lineTo(x0 + h, y0 + h);
    } else {
        y0 += in, y1 -= in;
        p.startNewSubPath(x0, y0);
        p.lineTo(x0 + h, y0 + h), p.lineTo(x0 + h, y1 - h), p.lineTo(x0, y1), p.lineTo(x0 - h, y1 - h),
            p.lineTo(x0 - h, y0 + h);
    }
    p.closeSubPath();
    return p;
}
} // namespace

void drawSegments(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, juce::Colour on, bool ghost)
{
    const int n = text.length();
    if (n == 0) return;
    float h = area.getHeight(), w = h * 0.5f, gap = h * 0.16f;
    const float total = (float)n * w + (float)(n - 1) * gap;
    if (total > area.getWidth()) {
        const float k = area.getWidth() / total;
        h *= k, w *= k, gap *= k;
    }
    const float t = h * 0.12f, shear = 0.1f;
    float x = area.getCentreX() - ((float)n * w + (float)(n - 1) * gap) * 0.5f;
    const float y = area.getCentreY() - h * 0.5f;
    juce::Path lit, off;
    for (int i = 0; i < n; i++, x += w + gap) {
        const int m = segMask(text[i]);
        const float l = x + t * 0.5f, r = x + w - t * 0.5f, top = y + t * 0.5f, mid = y + h * 0.5f, bot = y + h - t * 0.5f;
        const juce::Path seg[7] = {segment(l, top, r, top, t), segment(r, top, r, mid, t), segment(r, mid, r, bot, t),
                                   segment(l, bot, r, bot, t), segment(l, mid, l, bot, t), segment(l, top, l, mid, t),
                                   segment(l, mid, r, mid, t)};
        for (int s = 0; s < 7; s++) (m >> s & 1 ? lit : off).addPath(seg[s]);
    }
    const auto italic = juce::AffineTransform::shear(-shear, 0.0f).translated(shear * area.getCentreY(), 0.0f);
    lit.applyTransform(italic), off.applyTransform(italic);
    if (ghost) g.setColour(col::segGhost), g.fillPath(off);
    g.setColour(on.withAlpha(0.28f));
    g.strokePath(lit, juce::PathStrokeType(t * 0.9f, juce::PathStrokeType::curved));   // (the glow)
    g.setColour(on);
    g.fillPath(lit);
}

void drawWood(juce::Graphics& g, juce::Rectangle<float> r)
{
    juce::ColourGradient grad(col::woodDark, r.getX(), 0.0f, col::woodDark, r.getRight(), 0.0f, false);
    grad.addColour(0.5, col::woodLight);
    g.setGradientFill(grad);
    g.fillRect(r);
    g.setColour(juce::Colour(0x22000000));       // a little grain
    for (int i = 0; i < 6; i++) {
        juce::Path p;
        const float x = r.getX() + r.getWidth() * (0.15f + 0.14f * (float)i);
        p.startNewSubPath(x, r.getY());
        for (float yy = r.getY(); yy <= r.getBottom(); yy += 20.0f)
            p.lineTo(x + std::sin(yy * 0.013f + (float)i) * r.getWidth() * 0.06f, yy);
        g.strokePath(p, juce::PathStrokeType(0.8f));
    }
}

void drawSectionTitle(juce::Graphics& g, juce::Rectangle<int> b, const juce::String& title)
{
    g.setColour(col::text);
    g.setFont(font(13.0f));
    g.drawText(title, b.getX(), b.getY(), b.getWidth(), 16, juce::Justification::centred, false);
    g.setColour(col::rule);
    g.fillRect(b.getX(), b.getY() + 18, b.getWidth(), 2);
}

void drawLed(juce::Graphics& g, juce::Point<float> c, float d, bool on)
{
    const auto r = juce::Rectangle<float>(d, d).withCentre(c);
    if (on) {
        g.setColour(col::ledOn.withAlpha(0.3f));
        g.fillEllipse(r.expanded(d * 0.5f));
    }
    g.setColour(on ? col::ledOn : col::ledOff);
    g.fillEllipse(r);
}

void drawPad(juce::Graphics& g, juce::Rectangle<float> r, bool on, bool enabled)
{
    if (on && enabled) {
        g.setColour(col::btnOn.withAlpha(0.3f));
        g.fillRoundedRectangle(r.expanded(3.0f), 4.0f);
    }
    g.setColour(on && enabled ? col::btnOn : col::btnOff.withMultipliedBrightness(enabled ? 1.0f : 0.6f));
    g.fillRoundedRectangle(r, 2.0f);
    g.setColour(juce::Colours::black);
    g.drawRoundedRectangle(r, 2.0f, 1.0f);
}

} // namespace knobi::ui
