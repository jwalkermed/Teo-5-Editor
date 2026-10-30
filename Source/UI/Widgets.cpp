// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "Widgets.h"

namespace teo5
{

// ---- Knob -----------------------------------------------------------------------------------

Knob::Knob (KnobStyle style)
    : juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox)
{
    getProperties().set ("knobStyle", (int) style);
    setRotaryParameters (juce::MathConstants<float>::pi * 7.0f / 6.0f,   // -150 deg
                         juce::MathConstants<float>::pi * 17.0f / 6.0f,  // +150 deg  -> 300 deg travel
                         true);
    setPopupDisplayEnabled (true, true, nullptr, 1500);
    setScrollWheelEnabled (true);
    setVelocityBasedMode (false);
    setMouseDragSensitivity ((int) pixelsForFullRange);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onContextMenu)
            onContextMenu(); // right-click is a context menu, not an edit
        return;
    }
    if (onTouched) onTouched();
    juce::Slider::mouseDown (e); // starts the host gesture; rotary-drag knobs don't jump on click
    dragValue = getValue();
    lastDragY = e.position.y;
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    // Relative drag (instead of juce::Slider's) so Shift can change the speed mid-drag without a jump.
    if (! isEnabled() || e.mods.isPopupMenu())
        return;
    const double range = getMaximum() - getMinimum();
    const double step = getInterval() > 0.0 ? getInterval() : range / 1000.0;
    const double perPixel = e.mods.isShiftDown() ? juce::jmax (step / 4.0, range / 2000.0) // fine
                                                 : range / pixelsForFullRange;
    dragValue = juce::jlimit (getMinimum(), getMaximum(), dragValue + (double) (lastDragY - e.position.y) * perPixel);
    lastDragY = e.position.y;
    setValue (dragValue, juce::sendNotificationSync);
    if (onTouched) onTouched();
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // Reversed from JUCE's default, as requested: scrolling down turns the knob up.
    // Shift: fine steps (JUCE still moves at least one value per wheel event). With Shift held macOS turns a
    // mouse wheel's vertical scroll into horizontal (deltaX), which JUCE reads with the opposite sign, so
    // deltaX is not reversed then: the knob keeps turning the same way as without Shift.
    const bool fine = e.mods.isShiftDown();
    auto reversed = w;
    reversed.deltaX = w.deltaX * (fine ? 0.1f : -1.0f);
    reversed.deltaY = w.deltaY * (fine ? -0.1f : -1.0f);
    juce::Slider::mouseWheelMove (e, reversed);
    if (onTouched) onTouched();
}

void Knob::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        juce::Slider::mouseUp (e);
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    if (! resetValue)
    {
        juce::Slider::mouseDoubleClick (e);
        return;
    }
    if (auto v = resetValue())
    {
        setValue (*v, juce::sendNotificationSync);
        if (onTouched) onTouched();
    }
}

// ---- PanelButton ----------------------------------------------------------------------------

void PanelButton::paintButton (juce::Graphics& g, bool, bool down)
{
    const bool lit = litWhen ? litWhen() : getToggleState();
    // Leave room for the bloom inside the component bounds.
    TEO5LookAndFeel::drawPanelButton (g, getLocalBounds().toFloat().reduced (bloomPad), lit, down);
}

// ---- SevenSegment ---------------------------------------------------------------------------

void SevenSegment::paint (juce::Graphics& g)
{
    using namespace juce;
    auto b = getLocalBounds().toFloat();
    g.setColour (Colour (0xff120303));
    g.fillRoundedRectangle (b, 2.0f);
    g.setColour (Colour (0xff000000));
    g.drawRoundedRectangle (b, 2.0f, 1.0f);

    // Segment masks for 0-9 and '-' / blank: bits a b c d e f g.
    auto mask = [] (juce_wchar ch) -> int
    {
        static const int digits[] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
        if (ch >= '0' && ch <= '9') return digits[ch - '0'];
        if (ch == '-') return 0x40;
        // Banks 10-16 show as A-G. B and D can't be drawn in capitals (they'd read as 8 and 0), so they use the
        // usual seven-segment b and d; G lights a, c, d, e, f.
        switch (CharacterFunctions::toUpperCase (ch))
        {
            case 'A': return 0x77;
            case 'B': return 0x7c;
            case 'C': return 0x39;
            case 'D': return 0x5e;
            case 'E': return 0x79;
            case 'F': return 0x71;
            case 'G': return 0x3d;
            default:  return 0;
        }
    };

    auto inner = b.reduced (b.getHeight() * 0.18f, b.getHeight() * 0.12f);
    const float cellW = inner.getWidth() / 3.0f;
    const float slant = cellW * 0.10f;

    for (int i = 0; i < 3; ++i)
    {
        const juce_wchar ch = i < text.length() ? text[i] : ' ';
        const int m = mask (ch);
        auto cell = Rectangle<float> (inner.getX() + cellW * (float) i, inner.getY(), cellW, inner.getHeight()).reduced (cellW * 0.16f, 0);
        const float w = cell.getWidth(), h = cell.getHeight();
        const float t = jmin (w * 0.2f, h * 0.13f); // segment thickness
        const float gap = t * 0.25f;                  // dark gap where segments meet
        const float x0 = cell.getX(), y0 = cell.getY();

        // Hexagonal segment between two joint points (ax,ay) and (bx,by).
        auto seg = [&] (float ax, float ay, float bx, float by)
        {
            // Skew for the classic italic look: x shifts with height.
            auto sk = [&] (float x, float y) { return Point<float> (x0 + x + slant * (1.0f - y / h), y0 + y); };
            const float ht = t * 0.5f;
            Path p;
            if (std::abs (by - ay) < 0.01f)
            {
                const float l = ax + gap, r = bx - gap;
                p.startNewSubPath (sk (l, ay));
                p.lineTo (sk (l + ht, ay - ht)); p.lineTo (sk (r - ht, ay - ht));
                p.lineTo (sk (r, ay));
                p.lineTo (sk (r - ht, ay + ht)); p.lineTo (sk (l + ht, ay + ht));
            }
            else
            {
                const float tp = ay + gap, bt = by - gap;
                p.startNewSubPath (sk (ax, tp));
                p.lineTo (sk (ax + ht, tp + ht)); p.lineTo (sk (ax + ht, bt - ht));
                p.lineTo (sk (ax, bt));
                p.lineTo (sk (ax - ht, bt - ht)); p.lineTo (sk (ax - ht, tp + ht));
            }
            p.closeSubPath();
            return p;
        };

        const float hx = t * 0.5f, rx = w - t * 0.5f, my = h * 0.5f;
        Path segs[7] = {
            seg (hx, t * 0.5f, rx, t * 0.5f),          // a
            seg (rx, t * 0.5f, rx, my),                // b
            seg (rx, my, rx, h - t * 0.5f),            // c
            seg (hx, h - t * 0.5f, rx, h - t * 0.5f),  // d
            seg (hx, my, hx, h - t * 0.5f),            // e
            seg (hx, t * 0.5f, hx, my),                // f
            seg (hx, my, rx, my)                       // g
        };

        for (int s = 0; s < 7; ++s)
        {
            const bool on = (m >> s) & 1;
            if (on)
            {
                g.setColour (Palette::segmentOn.withAlpha (0.25f));
                g.strokePath (segs[s], PathStrokeType (t * 0.9f));
                g.setColour (Palette::segmentOn);
            }
            else
            {
                g.setColour (Palette::segmentOff);
            }
            g.fillPath (segs[s]);
        }
    }
}

// ---- OledDisplay ----------------------------------------------------------------------------

void OledDisplay::showParameter (const juce::String& name, const juce::String& value, const juce::String& stored)
{
    paramName = name;
    paramValue = value;
    paramStored = stored;
    showingParam = true;
    repaint();
    startTimer (juce::jlimit (1500, 4000, 800 + 60 * value.length())); // longer messages stay up long enough to read
}

void OledDisplay::timerCallback()
{
    stopTimer();
    showingParam = false;
    repaint();
}

void OledDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (! showingParam && ! e.mods.isPopupMenu() && starArea.expanded (3.0f).contains (e.position) && onFavouriteClicked)
        onFavouriteClicked();
}

juce::String OledDisplay::getTooltip()
{
    if (! showingParam && starArea.expanded (3.0f).contains (getMouseXYRelative().toFloat()))
    {
        const int fav = favourite ? favourite() : -1;
        return fav == 1 ? "Remove from favourites (also in the library)"
             : fav == 0 ? "Add to favourites (also in the library)"
                        : "Favourites: fetch this program's bank in the library first";
    }
    return juce::SettableTooltipClient::getTooltip();
}

void OledDisplay::refreshFavourite()
{
    if (! showingParam && (favourite ? favourite() : -1) != shownFavourite)
        repaint();
}

void OledDisplay::paint (juce::Graphics& g)
{
    using namespace juce;
    auto b = getLocalBounds().toFloat();

    // Bezel + glass.
    g.setColour (Colour (0xff050505));
    g.fillRect (b);
    g.setColour (Colour (0xff3a3b3e));
    g.drawRect (b, 1.0f);

    auto screen = b.reduced (5.0f, 4.0f);
    const auto white = Colour (0xfff0f4f8);
    const float h = screen.getHeight();

    if (showingParam)
    {
        g.setColour (white);
        g.setFont (TEO5LookAndFeel::oledFont (h * 0.15f));
        g.drawText (paramName, screen.removeFromTop (h * 0.32f), Justification::centred);

        if (paramStored.isNotEmpty())
        {
            // Value, then the stored value in parentheses, smaller and dimmer, centred together on one line.
            const auto stored = "(" + paramStored + ")";
            float size = h * 0.30f;
            auto valueFont = TEO5LookAndFeel::oledFont (size, true);
            auto storedFont = TEO5LookAndFeel::oledFont (size * 0.6f);
            auto widths = [&] { return GlyphArrangement::getStringWidth (valueFont, paramValue) + size * 0.25f
                                     + GlyphArrangement::getStringWidth (storedFont, stored); };
            while (size > h * 0.12f && widths() > screen.getWidth())
            {
                size -= 1.0f;
                valueFont = TEO5LookAndFeel::oledFont (size, true);
                storedFont = TEO5LookAndFeel::oledFont (size * 0.6f);
            }
            auto line = screen.withSizeKeepingCentre (juce::jmin (widths(), screen.getWidth()), screen.getHeight());
            const float valueW = GlyphArrangement::getStringWidth (valueFont, paramValue);
            g.setFont (valueFont);
            g.drawText (paramValue, line.removeFromLeft (valueW), Justification::centredLeft, true);
            line.removeFromLeft (size * 0.25f);
            g.setColour (white.withAlpha (0.6f));
            g.setFont (storedFont);
            // Centred on the same horizontal line as the value.
            g.drawText (stored, line, Justification::centredLeft, true);
            return;
        }

        // Shrink long values to fit on one line, down to a readable size; past that, wrap onto up to three lines.
        float size = h * 0.30f;
        auto font = TEO5LookAndFeel::oledFont (size, true);
        while (size > h * 0.14f && GlyphArrangement::getStringWidth (font, paramValue) > screen.getWidth())
            font = TEO5LookAndFeel::oledFont (size -= 1.0f, true);
        g.setFont (font);
        g.drawFittedText (paramValue, screen.toNearestInt(), Justification::centred, 3, 1.0f);
        return;
    }

    g.setColour (white);
    auto top = screen.removeFromTop (h * 0.26f);
    auto bottom = screen.removeFromBottom (h * 0.26f);

    auto small = TEO5LookAndFeel::oledFont (h * 0.105f);
    auto mid = TEO5LookAndFeel::oledFont (h * 0.125f);

    auto tl = top.removeFromTop (top.getHeight() * 0.48f);
    g.setFont (small);
    g.drawText (home.bpmLabel, tl, Justification::centredLeft);
    g.drawText ("CLK DIVIDE", tl, Justification::centredRight);
    g.setFont (mid);
    g.drawText (home.bpm, top, Justification::centredLeft);
    g.drawText (home.clkDiv, top, Justification::centredRight);

    g.setColour (white.withAlpha (0.55f));
    g.drawHorizontalLine ((int) screen.getY(), screen.getX(), screen.getRight());
    g.drawHorizontalLine ((int) screen.getBottom() - 1, screen.getX(), screen.getRight());

    // Favourite star at the left of the name line; the name is centred in the space to its right.
    const int fav = favourite ? favourite() : -1;
    shownFavourite = fav;
    starArea = screen.withWidth (h * 0.17f + 4.0f);
    g.setColour (fav == 1 ? Colour (0xffffc040) : white.withAlpha (fav == 0 ? 0.6f : 0.25f));
    g.setFont (FontOptions (h * 0.17f));
    g.drawText (String::fromUTF8 (fav == 1 ? "\xe2\x98\x85" : "\xe2\x98\x86"), starArea, Justification::centredLeft);

    g.setColour (white);
    g.setFont (TEO5LookAndFeel::oledFont (h * 0.17f, true));
    g.drawFittedText (home.name, screen.withTrimmedLeft (starArea.getWidth()).withTrimmedRight (starArea.getWidth() * 0.5f).toNearestInt(),
                      Justification::centred, 1, 0.8f);

    auto bt = bottom.removeFromTop (bottom.getHeight() * 0.52f);
    g.setFont (mid);
    g.drawText (home.arpMode, bt, Justification::centredLeft);
    g.drawText (home.effect, bt, Justification::centredRight);
    g.setFont (small);
    g.drawText ("ARP", bottom, Justification::centredLeft);
    g.drawText ("EFFECT", bottom, Justification::centredRight);
}

} // namespace teo5
