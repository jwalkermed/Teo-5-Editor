// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "TEO5LookAndFeel.h"
#include "BinaryData.h"

namespace teo5
{

TEO5LookAndFeel::TEO5LookAndFeel()
{
    setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xf0101012));
    setColour (juce::BubbleComponent::outlineColourId, Palette::pinstripe);
    setColour (juce::TooltipWindow::textColourId, Palette::silkscreen);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1a1b1d));
    setColour (juce::PopupMenu::textColourId, Palette::silkscreen);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::pinstripe.withAlpha (0.6f));
}

juce::Typeface::Ptr TEO5LookAndFeel::michroma()
{
    static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor (BinaryData::MichromaRegular_ttf,
                                                                             BinaryData::MichromaRegular_ttfSize);
    return tf;
}

juce::Font TEO5LookAndFeel::panelFont (float height)
{
    return juce::Font (juce::FontOptions (michroma()).withHeight (height));
}

juce::Font TEO5LookAndFeel::oledFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions ("Arial Narrow", height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Typeface::Ptr TEO5LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return michroma();
    return LookAndFeel_V4::getTypefaceForFont (f);
}

void TEO5LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider& s)
{
    const auto style = (KnobStyle) (int) s.getProperties().getWithDefault ("knobStyle", 0);
    drawKnob (g, juce::Rectangle<int> (x, y, w, h).toFloat(), startAngle + pos * (endAngle - startAngle), style);
}

void TEO5LookAndFeel::drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float angle, KnobStyle style)
{
    using namespace juce;
    auto area = bounds.withSizeKeepingCentre (jmin (bounds.getWidth(), bounds.getHeight()),
                                              jmin (bounds.getWidth(), bounds.getHeight()));
    const auto c = area.getCentre();
    const float r = area.getWidth() * 0.5f;

    // Soft drop shadow, light coming from the top.
    {
        const float sr = r * 1.02f;
        ColourGradient sh (Colour (0x90000000), c.x, c.y + r * 0.18f, Colour (0x00000000), c.x, c.y + r * 0.18f + sr, true);
        g.setGradientFill (sh);
        g.fillEllipse (c.x - sr, c.y + r * 0.18f - sr, sr * 2.0f, sr * 2.0f);
    }

    auto rotated = [&] (float radius, float a) { return c.getPointOnCircumference (radius, a); };

    if (style == KnobStyle::Cutoff)
    {
        // Gray ridged outer skirt.
        const float skirt = r * 0.98f;
        ColourGradient sg (Colour (0xff9a9ba0), c.x, c.y - skirt, Colour (0xff3f4044), c.x, c.y + skirt, false);
        sg.addColour (0.45, Colour (0xff6b6c71));
        g.setGradientFill (sg);
        g.fillEllipse (c.x - skirt, c.y - skirt, skirt * 2, skirt * 2);

        const int ridges = 72;
        for (int i = 0; i < ridges; ++i)
        {
            const float a = angle + MathConstants<float>::twoPi * (float) i / (float) ridges;
            const float shade = 0.5f + 0.5f * std::cos (a); // brighter at top
            g.setColour (juce::Colours::black.withAlpha (0.22f + 0.12f * (1.0f - shade)));
            g.drawLine (Line<float> (rotated (skirt * 0.70f, a), rotated (skirt * 0.98f, a)), 1.0f);
        }
        g.setColour (Colour (0x60000000));
        g.drawEllipse (c.x - skirt, c.y - skirt, skirt * 2, skirt * 2, 1.2f);

        // Black cap with a step.
        const float cap = r * 0.66f;
        ColourGradient cg (Colour (0xff3a3b3e), c.x - cap * 0.4f, c.y - cap * 0.8f, Colour (0xff070708), c.x + cap * 0.3f, c.y + cap, true);
        g.setGradientFill (cg);
        g.fillEllipse (c.x - cap, c.y - cap, cap * 2, cap * 2);
        const float top = cap * 0.62f;
        ColourGradient tg (Colour (0xff2a2b2e), c.x, c.y - top, Colour (0xff0c0c0d), c.x, c.y + top, false);
        g.setGradientFill (tg);
        g.fillEllipse (c.x - top, c.y - top, top * 2, top * 2);
        g.setColour (Colour (0x30ffffff));
        g.drawEllipse (c.x - top, c.y - top, top * 2, top * 2, 0.8f);

        // White triangle pointer on the cap rim.
        Path tri;
        tri.addTriangle (rotated (cap * 0.93f, angle), rotated (cap * 0.70f, angle - 0.17f), rotated (cap * 0.70f, angle + 0.17f));
        g.setColour (Colour (0xfff4f4f4));
        g.fillPath (tri);
        return;
    }

    // ---- Standard fluted knob ----
    const float body = r * 0.92f;
    {
        ColourGradient bg (Colour (0xff2b2c2f), c.x - body * 0.5f, c.y - body, Colour (0xff050506), c.x + body * 0.4f, c.y + body, false);
        g.setGradientFill (bg);
        g.fillEllipse (c.x - body, c.y - body, body * 2, body * 2);
    }

    // Knurled skirt: alternating facets around the rim, lit from the top.
    const int flutes = 28;
    for (int i = 0; i < flutes; ++i)
    {
        const float a0 = angle + MathConstants<float>::twoPi * (float) i / (float) flutes;
        const float a1 = a0 + MathConstants<float>::twoPi / (float) flutes * 0.5f;
        const float light = 0.5f + 0.5f * std::cos (a0);
        Path facet;
        facet.addPieSegment (c.x - body, c.y - body, body * 2, body * 2, a0, a1, 0.78f);
        g.setColour (Colour::greyLevel (0.08f + 0.17f * light).withAlpha (0.85f));
        g.fillPath (facet);
    }

    // Top face with a soft highlight.
    const float face = body * 0.76f;
    {
        ColourGradient fg (Colour (0xff36373a), c.x - face * 0.25f, c.y - face * 0.9f, Colour (0xff0e0e10), c.x + face * 0.2f, c.y + face, false);
        g.setGradientFill (fg);
        g.fillEllipse (c.x - face, c.y - face, face * 2, face * 2);

        ColourGradient hl (Colour (0x38ffffff), c.x, c.y - face, Colour (0x00ffffff), c.x, c.y, false);
        g.setGradientFill (hl);
        g.fillEllipse (c.x - face * 0.8f, c.y - face * 0.98f, face * 1.6f, face * 1.2f);
        g.setColour (Colour (0x50000000));
        g.drawEllipse (c.x - face, c.y - face, face * 2, face * 2, 1.0f);
    }

    // White pointer line, centre to edge.
    g.setColour (Colour (0xfff2f2f2));
    g.drawLine (Line<float> (rotated (body * 0.30f, angle), rotated (body * 0.98f, angle)), jmax (1.6f, r * 0.085f));
}

void TEO5LookAndFeel::drawPanelButton (juce::Graphics& g, juce::Rectangle<float> b, bool lit, bool pressed)
{
    using namespace juce;
    if (pressed)
        b = b.reduced (0.6f);

    if (lit) // red bloom spilling onto the panel: stacked rounded layers that fade and round off outwards
    {
        constexpr int layers = 10;
        for (int i = layers; i >= 1; --i)
        {
            const float grow = (float) i;
            g.setColour (Palette::buttonRed.withAlpha (0.045f));
            g.fillRoundedRectangle (b.expanded (grow), 3.0f + grow);
        }
    }

    // Outer bezel.
    ColourGradient bez (Colour (0xff4a4b4f), b.getX(), b.getY(), Colour (0xff101112), b.getX(), b.getBottom(), false);
    g.setGradientFill (bez);
    g.fillRoundedRectangle (b, 2.5f);
    g.setColour (Colour (0xff050506));
    g.drawRoundedRectangle (b, 2.5f, 1.0f);

    auto face = b.reduced (b.getWidth() * 0.16f);
    if (lit)
    {
        ColourGradient rg (Colour (0xffffd2b0), face.getCentreX(), face.getCentreY(),
                           Colour (0xffb00c06), face.getX(), face.getY(), true);
        rg.addColour (0.25, Colour (0xffff4a2a));
        rg.addColour (0.65, Colour (0xffe01a10));
        g.setGradientFill (rg);
        g.fillRoundedRectangle (face, 1.5f);
        // Diffuser texture.
        g.setColour (Colour (0x30ffffff));
        for (float yy = face.getY() + 2.0f; yy < face.getBottom() - 1.0f; yy += 2.0f)
            g.drawHorizontalLine ((int) yy, face.getX() + 1.0f, face.getRight() - 1.0f);
    }
    else
    {
        ColourGradient fg (Colour (0xff1a1b1d), face.getX(), face.getY(), Colour (0xff2c2d30), face.getX(), face.getBottom(), false);
        g.setGradientFill (fg);
        g.fillRoundedRectangle (face, 1.5f);
        g.setColour (Colour (0x18ffffff));
        for (float yy = face.getY() + 2.0f; yy < face.getBottom() - 1.0f; yy += 2.0f)
            g.drawHorizontalLine ((int) yy, face.getX() + 1.0f, face.getRight() - 1.0f);
    }
    g.setColour (Colour (0x80000000));
    g.drawRoundedRectangle (face, 1.5f, 0.8f);
}

void TEO5LookAndFeel::drawLed (juce::Graphics& g, juce::Point<float> c, float r, bool lit)
{
    using namespace juce;
    if (lit)
    {
        // Round glow that fades out smoothly (the LED's component leaves room for all of it, so it isn't clipped).
        const float gr = r * 3.6f;
        ColourGradient glow (Palette::ledOn.withAlpha (0.5f), c.x, c.y, Palette::ledOn.withAlpha (0.0f), c.x + gr, c.y, true);
        glow.addColour (0.35, Palette::ledOn.withAlpha (0.22f));
        glow.addColour (0.7, Palette::ledOn.withAlpha (0.05f));
        g.setGradientFill (glow);
        g.fillEllipse (c.x - gr, c.y - gr, gr * 2.0f, gr * 2.0f);
        ColourGradient body (Colour (0xffffe0b0), c.x - r * 0.2f, c.y - r * 0.3f, Palette::ledOn, c.x + r, c.y + r, true);
        g.setGradientFill (body);
    }
    else
    {
        ColourGradient body (Colour (0xff6a1610), c.x - r * 0.3f, c.y - r * 0.4f, Palette::ledOff.darker (0.6f), c.x + r, c.y + r, true);
        g.setGradientFill (body);
    }
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    g.setColour (Colour (0xa0000000));
    g.drawEllipse (c.x - r, c.y - r, r * 2, r * 2, 0.8f);
}

void TEO5LookAndFeel::drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&,
                                  const juce::Rectangle<float>& body)
{
    g.setColour (findColour (juce::BubbleComponent::backgroundColourId));
    g.fillRoundedRectangle (body, 3.0f);
    g.setColour (findColour (juce::BubbleComponent::outlineColourId));
    g.drawRoundedRectangle (body, 3.0f, 1.0f);
}

} // namespace teo5
