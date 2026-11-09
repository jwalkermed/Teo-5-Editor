// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "Skin.h"
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
    g.setColour (skinned (0xff120303, Skin::Area::Panel));
    g.fillRoundedRectangle (b, 2.0f);
    g.setColour (skinned (0xff000000, Skin::Area::Panel));
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
    showingGraph = false;
    repaint();
    startTimer (juce::jlimit (1500, 4000, 800 + 60 * value.length())); // longer messages stay up long enough to read
}

void OledDisplay::showGraph (Graph kind, const juce::String& focusId, const juce::String& section,
                             const juce::String& name, const juce::String& value, const juce::String& stored)
{
    graph = kind;
    graphFocus = focusId;
    graphSection = section;
    paramName = name;
    paramValue = value;
    paramStored = stored;
    showingParam = false;
    showingGraph = true;
    graphHeld = juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown(); // a drag holds it; wheel/synth don't
    graphTouched = juce::Time::getMillisecondCounterHiRes();
    graphAlpha = 1.0f;
    repaint();
    startTimerHz (30); // follows the values and runs the fade
}

void OledDisplay::timerCallback()
{
    if (showingGraph)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (graphHeld && juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
            graphTouched = now;
        else
            graphHeld = false;
        const double t = now - graphTouched - graphHoldMs;
        const float a = t <= 0.0 ? 1.0f : (float) juce::jmax (0.0, 1.0 - t / graphFadeMs);
        graphAlpha = a * a * (3.0f - 2.0f * a); // smoothstep
        if (a <= 0.0f)
        {
            showingGraph = false;
            stopTimer();
        }
        repaint();
        return;
    }
    stopTimer();
    showingParam = false;
    repaint();
}

void OledDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (! showingParam && ! showingGraph && ! e.mods.isPopupMenu() && starArea.expanded (3.0f).contains (e.position) && onFavouriteClicked)
        onFavouriteClicked();
}

juce::String OledDisplay::getTooltip()
{
    if (! showingParam && ! showingGraph && starArea.expanded (3.0f).contains (getMouseXYRelative().toFloat()))
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
    if (! showingParam && ! showingGraph && (favourite ? favourite() : -1) != shownFavourite)
        repaint();
}

void OledDisplay::paint (juce::Graphics& g)
{
    using namespace juce;
    auto b = getLocalBounds().toFloat();

    // Bezel + glass.
    g.setColour (skinned (0xff050505, Skin::Area::Panel));
    g.fillRect (b);
    g.setColour (skinned (0xff3a3b3e, Skin::Area::Panel));
    g.drawRect (b, 1.0f);

    auto screen = b.reduced (5.0f, 4.0f);
    const auto white = skinned (0xfff0f4f8, Skin::Area::Panel);
    const float h = screen.getHeight();

    if (showingParam && ! showingGraph)
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

    paintHome (g, screen, white);
    if (showingGraph && graphAlpha > 0.0f)
    {
        // The graph page over the home page, fading out at the end.
        g.beginTransparencyLayer (graphAlpha);
        g.setColour (skinned (0xff050505, Skin::Area::Panel));
        g.fillRect (b.reduced (1.0f));
        paintGraph (g, screen, white);
        g.endTransparencyLayer();
    }
}

void OledDisplay::paintHome (juce::Graphics& g, juce::Rectangle<float> screen, juce::Colour white)
{
    using namespace juce;
    const float h = screen.getHeight();
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
    g.setColour (fav == 1 ? skinned (0xffffc040, Skin::Area::Panel) : white.withAlpha (fav == 0 ? 0.6f : 0.25f));
    g.setFont (FontOptions (h * 0.17f));
    g.drawText (String::fromUTF8 (fav == 1 ? "\xe2\x98\x85" : "\xe2\x98\x86"), starArea, Justification::centredLeft);

    auto nameArea = screen.withTrimmedLeft (starArea.getWidth()).withTrimmedRight (starArea.getWidth() * 0.5f);
    if (home.edited)
    {
        // EDITED in red under the name; the name moves up to make room.
        auto line = nameArea.removeFromBottom (nameArea.getHeight() * 0.34f);
        g.setColour (skinned (0xffff3b2f, Skin::Area::Panel));
        g.setFont (TEO5LookAndFeel::oledFont (h * 0.095f, true).withExtraKerningFactor (0.12f));
        g.drawText ("EDITED", line.translated (0.0f, -h * 0.02f), Justification::centred, false);
    }
    g.setColour (white);
    g.setFont (TEO5LookAndFeel::oledFont (h * 0.17f, true));
    g.drawFittedText (home.name, nameArea.toNearestInt(), Justification::centred, 1, 0.8f);

    auto bt = bottom.removeFromTop (bottom.getHeight() * 0.52f);
    g.setFont (mid);
    g.drawText (home.arpMode, bt, Justification::centredLeft);
    g.drawText (home.effect, bt, Justification::centredRight);
    g.setFont (small);
    g.drawText ("ARP", bottom, Justification::centredLeft);
    g.drawText ("EFFECT", bottom, Justification::centredRight);
}

// ---- Graph pages ----------------------------------------------------------------------------
// The scales below (envelope times, cutoff frequency, Q) are estimates for drawing; the TEO-5's own curves
// haven't been measured. The header shows the parameter's value as everywhere else in the editor.

void OledDisplay::paintGraph (juce::Graphics& g, juce::Rectangle<float> screen, juce::Colour white)
{
    using namespace juce;
    const float h = screen.getHeight();
    auto head = screen.removeFromTop (h * 0.28f);
    auto row1 = head.removeFromTop (head.getHeight() * 0.42f);
    g.setColour (white.withAlpha (0.6f));
    g.setFont (TEO5LookAndFeel::oledFont (h * 0.095f));
    g.drawText (graphSection, row1, Justification::centredLeft, false);
    if (paramStored.isNotEmpty() && paramStored != paramValue)
        g.drawText ("(" + paramStored + ")", row1, Justification::centredRight, false);
    g.setColour (white);
    g.setFont (TEO5LookAndFeel::oledFont (h * 0.135f, true));
    g.drawText (paramName, head, Justification::centredLeft, false);
    g.drawText (paramValue, head, Justification::centredRight, false);
    g.setColour (white.withAlpha (0.35f));
    g.fillRect (screen.getX(), screen.getY(), screen.getWidth(), 1.0f);
    screen.removeFromTop (h * 0.05f);

    if (graph == Graph::Filter)
        paintFilter (g, screen, white);
    else
        paintEnvelope (g, screen, white, graph == Graph::Envelope1 ? 1 : 2);
}

void OledDisplay::paintEnvelope (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour white, int env)
{
    using namespace juce;
    const String prefix = "env" + String (env);
    auto v = [&] (const char* s) { return liveValue ? liveValue (prefix + s) : 0; };
    const float h = area.getHeight();
    auto letters = area.removeFromBottom (h * 0.2f);
    area = area.reduced (2.0f, 1.0f);

    auto width = [] (int raw) { return std::log10 (1.0 + std::pow (10000.0, raw / 255.0) / 2.0) + 0.05; }; // 1 ms .. 10 s
    const double wa = width (v ("Attack")), wd = width (v ("Decay")), wr = width (v ("Release")), ws = 0.9;
    const double k = area.getWidth() / (wa + wd + ws + wr);
    const float x0 = area.getX(), xa = x0 + (float) (wa * k), xd = xa + (float) (wd * k), xs = xd + (float) (ws * k),
                xr = area.getRight();
    const float sus = (float) v ("Sustain") / 127.0f;
    const float amount = env == 1 ? ((float) v ("Amt") - 127.0f) / 127.0f : (float) v ("Amt") / 254.0f;
    const bool inverted = amount < 0.0f;
    const float base = inverted ? area.getY() : area.getBottom();
    const float peak = inverted ? area.getY() + area.getHeight() * -amount : area.getBottom() - area.getHeight() * amount;
    auto y = [&] (float level) { return base + (peak - base) * level; };

    const String seg = graphFocus.fromFirstOccurrenceOf (prefix, false, false); // Attack, Decay, Sustain, Release, Amt
    auto bright = [&] (const String& s) { return seg == "Amt" || seg == s; };

    // Full-scale guide and baseline.
    g.setColour (white.withAlpha (0.22f));
    const float guide = inverted ? area.getBottom() : area.getY();
    for (float x = area.getX(); x < area.getRight(); x += 4.0f)
        g.fillRect (x, guide, 1.5f, 1.0f);
    g.setColour (white.withAlpha (0.35f));
    g.fillRect (area.getX(), base, area.getWidth(), 1.0f);
    g.setColour (white.withAlpha (0.16f));
    for (float x : { xa, xd, xs })
        g.fillRect (x, area.getY(), 1.0f, area.getHeight());

    // Curve: analogue-style attack, exponential decay and release.
    const int n = 32;
    const float e3 = std::exp (-3.0f), e4 = std::exp (-4.0f);
    Path parts[4], fill;
    fill.startNewSubPath (x0, base);
    auto add = [&] (int part, float x, float yy, bool start)
    {
        if (start) parts[part].startNewSubPath (x, yy); else parts[part].lineTo (x, yy);
        fill.lineTo (x, yy);
    };
    for (int i = 0; i <= n; ++i)
    {
        const float t = (float) i / n;
        add (0, x0 + (xa - x0) * t, y ((1.0f - std::exp (-3.0f * t)) / (1.0f - e3)), i == 0);
    }
    for (int i = 0; i <= n; ++i)
    {
        const float t = (float) i / n, d = (std::exp (-4.0f * t) - e4) / (1.0f - e4);
        add (1, xa + (xd - xa) * t, y (sus + (1.0f - sus) * d), i == 0);
    }
    add (2, xd, y (sus), true);
    add (2, xs, y (sus), false);
    for (int i = 0; i <= n; ++i)
    {
        const float t = (float) i / n, d = (std::exp (-4.0f * t) - e4) / (1.0f - e4);
        add (3, xs + (xr - xs) * t, y (sus * d), i == 0);
    }
    fill.lineTo (xr, base);
    fill.closeSubPath();
    g.setColour (white.withAlpha (0.13f));
    g.fillPath (fill);

    const char* names[] = { "Attack", "Decay", "Sustain", "Release" };
    for (int p = 0; p < 4; ++p)
    {
        g.setColour (bright (names[p]) ? white : white.withAlpha (0.5f));
        g.strokePath (parts[p], PathStrokeType (bright (names[p]) ? 1.7f : 1.0f));
    }
    if (seg == "Sustain")
    {
        g.setColour (white.withAlpha (0.5f));
        for (float x = area.getX(); x < area.getRight(); x += 4.0f)
            g.fillRect (x, y (sus), 2.0f, 1.0f);
    }
    if (inverted)
    {
        g.setColour (white.withAlpha (0.6f));
        g.setFont (TEO5LookAndFeel::oledFont (h * 0.11f));
        g.drawText ("INVERTED", area.reduced (1.0f).withHeight (h * 0.16f).translated (0.0f, area.getHeight() - h * 0.18f),
                    Justification::centredRight, false);
    }

    // A D S R under their parts.
    const std::pair<const char*, float> labels[] = { { "A", (x0 + xa) / 2 }, { "D", (xa + xd) / 2 },
                                                      { "S", (xd + xs) / 2 }, { "R", (xs + xr) / 2 } };
    for (int p = 0; p < 4; ++p)
    {
        const bool on = bright (names[p]);
        g.setColour (on ? white : white.withAlpha (0.45f));
        g.setFont (TEO5LookAndFeel::oledFont (letters.getHeight() * 0.8f, on));
        g.drawText (labels[p].first, Rectangle<float> (labels[p].second - 8.0f, letters.getY(), 16.0f, letters.getHeight()),
                    Justification::centred, false);
    }
}

void OledDisplay::paintFilter (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour white)
{
    using namespace juce;
    auto v = [&] (const char* id) { return liveValue ? liveValue (id) : 0; };
    const float h = area.getHeight();
    auto stateRow = area.removeFromBottom (h * 0.2f);
    auto freqRow = area.removeFromBottom (h * 0.17f);
    area = area.reduced (2.0f, 1.0f);

    const double fc = 20.0 * std::pow (1000.0, v ("cutoff") / 1023.0);      // 20 Hz .. 20 kHz
    const double q = 0.55 + std::pow (v ("resonance") / 255.0, 2.0) * 18.0;
    const double s = v ("filterState") / 511.0;                              // LP > notch > HP
    const bool bp = v ("filterBP") != 0;
    const double lpMix = s < 0.5 ? 1.0 : 1.0 - (s - 0.5) * 2.0, hpMix = s < 0.5 ? s * 2.0 : 1.0;
    const float dbTop = 24.0f, dbBottom = -36.0f;
    auto fx = [&] (double f) { return area.getX() + area.getWidth() * (float) (std::log10 (f / 20.0) / 3.0); };
    auto dy = [&] (double db)
    {
        return area.getY() + area.getHeight() * (dbTop - jlimit (dbBottom, dbTop, (float) db)) / (dbTop - dbBottom);
    };

    // Grid: 100 Hz, 1 kHz, 10 kHz and 0 dB.
    g.setFont (TEO5LookAndFeel::oledFont (freqRow.getHeight() * 0.85f));
    for (auto [f, label] : { std::pair<double, const char*> { 100.0, "100" }, { 1000.0, "1k" }, { 10000.0, "10k" } })
    {
        g.setColour (white.withAlpha (0.14f));
        g.fillRect (fx (f), area.getY(), 1.0f, area.getHeight());
        g.setColour (white.withAlpha (0.45f));
        g.drawText (label, Rectangle<float> (fx (f) - 12.0f, freqRow.getY(), 24.0f, freqRow.getHeight()), Justification::centred, false);
    }
    g.setColour (white.withAlpha (0.3f));
    for (float x = area.getX(); x < area.getRight(); x += 4.0f)
        g.fillRect (x, dy (0.0), 1.5f, 1.0f);

    // Response of a state-variable filter: LP = 1/D, HP = -x^2/D, BP = (jx/Q)/D with D = 1 - x^2 + jx/Q.
    Path curve, fill;
    fill.startNewSubPath (area.getX(), area.getBottom());
    const int n = 120;
    for (int i = 0; i <= n; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, (double) i / n), x = f / fc;
        const double re = 1.0 - x * x, im = x / q, den = re * re + im * im;
        const double nr = bp ? 0.0 : lpMix - hpMix * x * x, ni = bp ? x / q : 0.0;
        const double mr = (nr * re + ni * im) / den, mi = (ni * re - nr * im) / den;
        const float px = fx (f), py = dy (20.0 * std::log10 (jmax (1.0e-6, std::hypot (mr, mi))));
        if (i == 0) curve.startNewSubPath (px, py); else curve.lineTo (px, py);
        fill.lineTo (px, py);
    }
    fill.lineTo (area.getRight(), area.getBottom());
    fill.closeSubPath();
    g.setColour (white.withAlpha (0.13f));
    g.fillPath (fill);
    g.setColour (white);
    g.strokePath (curve, PathStrokeType (graphFocus == "cutoff" ? 1.1f : 1.6f));

    // Cutoff marker.
    const float cx = fx (fc);
    const bool onCutoff = graphFocus == "cutoff";
    g.setColour (onCutoff ? white : white.withAlpha (0.45f));
    g.fillRect (cx - (onCutoff ? 0.7f : 0.4f), area.getY(), onCutoff ? 1.4f : 0.8f, area.getHeight());
    Path tri;
    tri.addTriangle (cx - 3.0f, area.getY() - 3.0f, cx + 3.0f, area.getY() - 3.0f, cx, area.getY() + 1.0f);
    g.fillPath (tri);

    // State: LP --- (notch) --- HP, or BAND-PASS.
    const float sy = stateRow.getCentreY(), sl = stateRow.getX() + 22.0f, sr = stateRow.getRight() - 22.0f;
    g.setFont (TEO5LookAndFeel::oledFont (stateRow.getHeight() * 0.75f, true));
    g.setColour (white.withAlpha (bp ? 0.3f : 0.75f));
    g.drawText ("LP", Rectangle<float> (stateRow.getX(), stateRow.getY(), 18.0f, stateRow.getHeight()), Justification::centredRight, false);
    g.drawText ("HP", Rectangle<float> (sr + 4.0f, stateRow.getY(), 18.0f, stateRow.getHeight()), Justification::centredLeft, false);
    g.setColour (white.withAlpha (bp ? 0.2f : 0.4f));
    g.fillRect (sl, sy, sr - sl, 1.0f);
    g.fillRect ((sl + sr) / 2.0f, sy - 2.5f, 1.0f, 6.0f);
    if (bp)
    {
        g.setColour (white.withAlpha (0.9f));
        g.drawText ("BAND-PASS", Rectangle<float> (sl, stateRow.getY(), sr - sl, stateRow.getHeight()), Justification::centred, false);
    }
    else
    {
        const float r = graphFocus == "filterState" ? 3.0f : 2.2f;
        g.setColour (graphFocus == "filterState" ? white : white.withAlpha (0.7f));
        g.fillEllipse (sl + (sr - sl) * (float) s - r, sy + 0.5f - r, r * 2.0f, r * 2.0f);
    }
}

} // namespace teo5
