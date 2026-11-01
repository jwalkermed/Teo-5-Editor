// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "SkinsDrawer.h"
#include "Skin.h"
#include "TEO5LookAndFeel.h"

namespace teo5
{
namespace
{
constexpr int headerH = 40, rowH = 82, rowGap = 6, thumbW = 190, thumbH = 62, footH = 50;
}

// The skins, one row each, then the note; sits in the drawer's viewport.
class SkinsDrawer::List : public juce::Component
{
public:
    std::vector<juce::Image> thumbnails;

    int preferredHeight() const { return Skin::count() * (rowH + rowGap) + footH; }

    juce::Rectangle<int> rowBounds (int index) const { return { 0, index * (rowH + rowGap), getWidth(), rowH }; }

    int rowAt (juce::Point<int> p) const
    {
        for (int i = 0; i < Skin::count(); ++i)
            if (rowBounds (i).contains (p))
                return i;
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        const auto accent = juce::Colour (Palette::pinstripe);
        for (int i = 0; i < Skin::count(); ++i)
        {
            auto r = rowBounds (i);
            const bool on = i == Skin::current();
            if (on || i == hover)
            {
                g.setColour (on ? accent : accent.withAlpha (0.45f));
                g.drawRoundedRectangle (r.toFloat().reduced (0.75f), 5.0f, on ? 2.0f : 1.0f);
            }
            auto inner = r.reduced (6);
            auto pic = inner.removeFromLeft (thumbW).withSizeKeepingCentre (thumbW, thumbH);
            if (juce::isPositiveAndBelow (i, (int) thumbnails.size()) && thumbnails[(size_t) i].isValid())
                g.drawImage (thumbnails[(size_t) i], pic.toFloat(), juce::RectanglePlacement::stretchToFit);
            g.setColour (juce::Colours::black);
            g.drawRect (pic, 1);
            inner.removeFromLeft (10);

            const auto info = Skin::info (i);
            auto name = inner.removeFromTop (inner.getHeight() / 2).withTrimmedTop (4);
            g.setColour (Palette::silkscreen);
            g.setFont (TEO5LookAndFeel::panelFont (10.5f));
            g.drawText (juce::String (info.name).toUpperCase(), name, juce::Justification::bottomLeft, true);
            if (on)
            {
                g.setColour (accent);
                g.setFont (TEO5LookAndFeel::panelFont (8.0f));
                g.drawText ("IN USE", name, juce::Justification::bottomRight, false);
            }
            g.setColour (Palette::silkscreen.withAlpha (0.75f));
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText (info.description, inner.withTrimmedTop (2), juce::Justification::topLeft, 3, 1.0f);
        }

        auto foot = juce::Rectangle<int> (0, Skin::count() * (rowH + rowGap) + 4, getWidth(), footH - 4);
        g.setColour (accent.withAlpha (0.35f));
        g.fillRect (foot.removeFromTop (1));
        g.setColour (Palette::silkscreen.withAlpha (0.7f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText ("Your choice is saved for every TEO-5 Editor on this Mac, in every project.", foot.withTrimmedTop (6),
                          juce::Justification::topLeft, 2, 1.0f);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (const int i = rowAt (e.getPosition()); i >= 0 && ! e.mods.isPopupMenu() && e.mouseWasClicked())
            Skin::select (i); // the editor re-colours itself; the pictures stay as they are
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int i = rowAt (e.getPosition());
        setMouseCursor (i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        if (i != hover)
        {
            hover = i;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hover = -1;
        repaint();
    }

private:
    int hover = -1;
};

SkinsDrawer::SkinsDrawer() : list (std::make_unique<List>())
{
    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (true, false); // vertical only, shown when the list doesn't fit
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

SkinsDrawer::~SkinsDrawer() = default;

void SkinsDrawer::visibilityChanged()
{
    // Pictures of the panel in each skin, made when the drawer opens (they match the panel as it is now).
    if (isVisible() && renderPanel)
    {
        list->thumbnails.clear();
        for (int i = 0; i < Skin::count(); ++i)
            list->thumbnails.push_back (renderPanel (i));
        list->repaint();
    }
}

void SkinsDrawer::paint (juce::Graphics& g)
{
    g.fillAll (skinned (0xf0181a1c));
    g.setColour (Palette::pinstripe);
    g.fillRect (0, 0, 2, getHeight());
    g.fillRect (12, 34, getWidth() - 24, 1);

    g.setColour (Palette::silkscreen);
    g.setFont (TEO5LookAndFeel::panelFont (14.0f));
    g.drawText ("SKINS", juce::Rectangle<int> (16, 8, 200, 24), juce::Justification::centredLeft);
    g.setColour (skinned (0xff8d939c));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Click to use", juce::Rectangle<int> (getWidth() - 130, 8, 116, 24), juce::Justification::centredRight);
}

void SkinsDrawer::resized()
{
    auto area = getLocalBounds().withTrimmedTop (headerH + 4).reduced (14, 0).withTrimmedBottom (8);
    viewport.setBounds (area);
    // Room for the scroll bar on the right so it never covers the text.
    list->setSize (area.getWidth() - (list->preferredHeight() > area.getHeight() ? 12 : 0), list->preferredHeight());
}

} // namespace teo5
