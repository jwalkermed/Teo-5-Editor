#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace teo5
{

namespace Palette
{
    const juce::Colour panel        { 0xff2a2b2e };
    const juce::Colour panelDark    { 0xff1d1e21 };
    const juce::Colour pinstripe    { 0xff2f7fd0 };
    const juce::Colour silkscreen   { 0xffe6e6e6 };
    const juce::Colour cheek        { 0xff0b0b0c };
    const juce::Colour ledOn        { 0xffff4a1c };
    const juce::Colour ledOff       { 0xff3e0c08 };
    const juce::Colour buttonRed    { 0xffff2a14 };
    const juce::Colour segmentOn    { 0xffff2216 };
    const juce::Colour segmentOff   { 0xff2c0907 };
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
