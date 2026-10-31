// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#pragma once

#include <optional>
#include <utility>

#include <juce_gui_basics/juce_gui_basics.h>
#include "TEO5LookAndFeel.h"

namespace teo5
{

// Rotary knob: 300 degree travel, vertical drag, shift-drag for fine, double-click to default,
// mouse wheel, value bubble while dragging.
class Knob : public juce::Slider
{
public:
    explicit Knob (KnobStyle style = KnobStyle::Standard);

    std::function<void()> onTouched; // fired on mouse-down / wheel, used by the OLED parameter page
    std::function<void()> onContextMenu; // right-click (never an edit)
    // Double-click sets the knob to this value (the program's stored value); empty = no change.
    std::function<std::optional<double>()> resetValue;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Vertical drag: the full range in about this many pixels; holding Shift (checked on every move)
    // switches to fine steps of about one value per few pixels.
    static constexpr double pixelsForFullRange = 220.0;

private:
    double dragValue = 0.0;
    float lastDragY = 0.0f;
};

// Square panel push button; glows red from inside when lit.
class PanelButton : public juce::Button
{
public:
    PanelButton() : juce::Button ({}) {}

    // Margin around the button face for its bloom; clicks only count on the face.
    static constexpr float bloomPad = 12.0f;
    bool hitTest (int x, int y) override { return getLocalBounds().toFloat().reduced (bloomPad).contains ((float) x, (float) y); }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    // When set, the lit state comes from this instead of the toggle state
    // (e.g. buttons that cycle a value but stay unlit).
    std::function<bool()> litWhen;

    // Called only for a mouse click. onClick also fires when a parameter attachment changes the toggle
    // state (e.g. the TEO-5 sends a new value), which must not pop up the OLED.
    std::function<void()> onUserClick;

    std::function<void()> onContextMenu; // right-click

    // Right-click is the panel's context menu: it never presses a panel button.
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
            juce::Button::mouseDown (e);
        else if (onContextMenu)
            onContextMenu();
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Button::mouseDrag (e); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            return;
        userGesture = true;
        clickedByUser = false;
        juce::Button::mouseUp (e);
        userGesture = false;
        // Only now: clicked() runs before the parameter attachment has stored the new value.
        if (std::exchange (clickedByUser, false) && onUserClick)
            onUserClick();
    }

protected:
    void clicked() override
    {
        if (userGesture)
            clickedByUser = true;
    }

private:
    bool userGesture = false, clickedByUser = false;
};

class Led : public juce::Component
{
public:
    Led() { setInterceptsMouseClicks (false, false); }
    void setLit (bool shouldBeLit)
    {
        if (lit != shouldBeLit) { lit = shouldBeLit; repaint(); }
    }
    bool isLit() const { return lit; }
    // The component is larger than the LED so its glow fits without being clipped.
    static constexpr float dotRadius = 4.4f, componentSize = 36.0f;
    void paint (juce::Graphics& g) override
    {
        TEO5LookAndFeel::drawLed (g, getLocalBounds().toFloat().getCentre(), dotRadius, lit);
    }

private:
    bool lit = false;
};

// Three-digit red 7-segment display with faint unlit segments.
class SevenSegment : public juce::Component
{
public:
    void setText (const juce::String& t) { text = t; repaint(); }
    void paint (juce::Graphics&) override;

private:
    juce::String text { "101" };
};

// 128x64-style white-on-black OLED.
class OledDisplay : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    struct HomePage
    {
        juce::String bpm { "120" }, clkDiv { "1/16" }, name { "It's an Oberheim" };
        juce::String bpmLabel { "BPM" }; // "EXT BPM" while the TEO-5 follows external clock
        juce::String arpMode { "Up+Down" }, effect { "Chorus" };
        bool edited = false; // the sound differs from the stored program: EDITED in red under the name
    };

    void setHome (const HomePage& h) { home = h; repaint(); }
    const HomePage& getHome() const { return home; }

    // Temporarily show a parameter name/value, then revert to home after ~1.5 s. `stored` (the value in the
    // program as loaded) is shown smaller in parentheses after the value, if given.
    void showParameter (const juce::String& name, const juce::String& value, const juce::String& stored = {});

    void paint (juce::Graphics&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }

    std::function<void()> onDoubleClick; // rename the current program

    // Favourite star left of the program name on the home page. favourite(): 1 = favourite, 0 = not,
    // -1 = the program isn't in the library (star drawn dim). Clicking the star calls onFavouriteClicked.
    std::function<int()> favourite;
    std::function<void()> onFavouriteClicked;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getTooltip() override; // the star has its own tooltip
    void refreshFavourite();   // repaint if the favourite state changed (called on the panel's timer)

private:
    void timerCallback() override;

    HomePage home;
    juce::String paramName, paramValue, paramStored;
    int shownFavourite = -2;
    juce::Rectangle<float> starArea; // where the star was drawn (for clicks)
    bool showingParam = false;
};

} // namespace teo5
