// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Skin.h"

namespace teo5
{

// The panel's named colours. They follow the skin (see Skin.h): read them when painting, not once.
namespace Palette
{
    inline SkinColour panel      { 0xff2a2b2e, Skin::Area::Face };
    inline SkinColour panelDark  { 0xff1d1e21, Skin::Area::Face };
    inline SkinColour pinstripe  { 0xff2f7fd0, Skin::Area::Panel };
    inline SkinColour silkscreen { 0xffe6e6e6, Skin::Area::Text };
    inline SkinColour cheek      { 0xff0b0b0c, Skin::Area::Panel };
    inline SkinColour ledOn      { 0xffff4a1c, Skin::Area::Panel };
    inline SkinColour ledOff     { 0xff3e0c08, Skin::Area::Panel };
    inline SkinColour buttonRed  { 0xffff2a14, Skin::Area::Panel };
    inline SkinColour segmentOn  { 0xffff2216, Skin::Area::Panel };
    inline SkinColour segmentOff { 0xff2c0907, Skin::Area::Panel };
}

enum class KnobStyle
{
    Standard,
    Cutoff
};

class TEO5LookAndFeel : public juce::LookAndFeel_V4
{
public:
    TEO5LookAndFeel();
    ~TEO5LookAndFeel() override;

    static juce::Font panelFont (float height);
    static juce::Font oledFont (float height, bool bold = false);

    // Shared renderers, also used directly by the panel and widgets.
    static void drawKnob (juce::Graphics&, juce::Rectangle<float> bounds, float angleRadians, KnobStyle);
    static void drawPanelButton (juce::Graphics&, juce::Rectangle<float> bounds, bool lit, bool pressed);
    static void drawLed (juce::Graphics&, juce::Point<float> centre, float radius, bool lit);

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    // Value bubble shown while dragging a knob.
    juce::Font getSliderPopupFont (juce::Slider&) override { return panelFont (11.0f); }
    void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip,
                     const juce::Rectangle<float>& body) override;

private:
    static juce::Typeface::Ptr michroma();
};

} // namespace teo5
