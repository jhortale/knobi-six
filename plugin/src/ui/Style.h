// SPDX-License-Identifier: GPL-3.0-only
// KNOBI-SIX plugin panel: the look of the approved mockups (docs/design/standalone-ui/panel-style-v2.html,
// sections-v2.html): charcoal panel, white bold section titles over blue rules, black knobs with a white dot, small
// white labels, red 7-segment displays, red LEDs and lit buttons, wood cheeks.
//
// Font: the mockups name "Arial Rounded MT Bold". It is a macOS system font (/System/Library/Fonts/Supplemental):
// the panel asks the system for it by name at run time and embeds nothing (no font file in this repository or in
// the bundles, so no licence question); where it is missing JUCE falls back to the system's sans-serif bold. The
// 7-segment displays are drawn as vector segments (no font at all), as the mockups' red displays look.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace knobi::ui {

namespace col {
inline const juce::Colour panel{0xff1d1e22}, frame{0xff0c0c0c}, text{0xfff4f4f4}, dim{0xff9aa0aa}, sub{0xffcfd3db},
    rule{0xff2f6be0}, knobHi{0xff4a4b50}, knobLo{0xff141416}, ledOff{0xff3c0f0b}, ledOn{0xffff3524},
    btnOff{0xff2a0a07}, btnOn{0xffff4a2a}, segBg{0xff120606}, segOn{0xffff2a1a}, segGhost{0xff2a0c09},
    woodDark{0xff5a2a14}, woodLight{0xff8a4a26}, slot{0xff16171a};
}

inline juce::Font font(float h, bool heavy = true)
{
    juce::Font f(juce::FontOptions(heavy ? "Arial Rounded MT Bold" : "Helvetica Neue", h,
                                   heavy ? juce::Font::plain : juce::Font::bold));
    return f;
}

// the 7-segment text (digits, '-', ' ', a few letters) in area, italic like the mockups; ghost: the unlit segments
void drawSegments(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, juce::Colour on,
                  bool ghost = true);

// the panel's frame pieces
void drawWood(juce::Graphics& g, juce::Rectangle<float> r);
void drawSectionTitle(juce::Graphics& g, juce::Rectangle<int> sectionBounds, const juce::String& title);
void drawLed(juce::Graphics& g, juce::Point<float> c, float d, bool on);
void drawPad(juce::Graphics& g, juce::Rectangle<float> r, bool on, bool enabled = true);

} // namespace knobi::ui
