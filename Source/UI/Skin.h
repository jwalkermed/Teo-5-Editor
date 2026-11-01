// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace teo5
{

// Skins: the editor is designed in one set of colours (charcoal panel, blue pinstripes, red-orange LEDs). Every colour
// in the panel and drawers goes through skinned(), which maps it onto the current skin: neutrals onto the skin's
// panel or drawer shades, red-orange onto its LED colour, blue onto its accent. Skin 0 is the design itself.
// The choice is one setting for every editor on this Mac (~/Library/TEO5Editor/skin.txt; TEO5_SKIN overrides it).
namespace Skin
{
    // Where a colour is used: the panel's hardware (knobs, buttons, cheeks), the panel face, a drawer, or lettering.
    enum class Area { Panel, Face, Drawer, Text };

    struct Info { juce::String name, description; };
    int count();          // built-in skins first, then the user's custom ones
    int builtInCount();
    bool isCustom (int index);
    Info info (int index);
    int current();
    // Switch skins: updates every SkinColour, the registered look-and-feels, and every open editor (they listen to
    // changes()); saves the choice unless temporary (thumbnails render each skin briefly and switch back).
    void select (int index, bool temporary = false);
    juce::ChangeBroadcaster& changes();
    void reapply (bool notify); // after the current skin's colours changed
    void rememberChoice();      // save the skin in use as the choice for every editor

    // Custom skins: six colours chosen in the skin editor; everything else follows from them. Saved in
    // ~/Library/TEO5Editor/custom-skins.json (TEO5_CUSTOM_SKINS_FILE for tools and tests).
    struct Custom
    {
        juce::String name;
        juce::Colour panel, lettering, pinstripes, leds, drawers;
        bool lightDrawers = false;
    };
    Custom coloursOf (int index);                     // any skin's six colours (a new skin starts from these)
    int addCustom (const Custom&, bool save = true);  // returns its index
    void updateCustom (int index, const Custom&, bool save = true); // live while editing (save = false)
    void removeCustom (int index);
    bool exportCustom (int index, const juce::File&);
    std::optional<Custom> readCustom (const juce::File&); // a skin file (import)
    double contrast (juce::Colour, juce::Colour);     // WCAG contrast ratio, 1 to 21

    juce::Colour map (juce::uint32 argb, Area);

    // Look-and-feels whose colour scheme follows the skin (their own extra colours: afterChange).
    void registerLookAndFeel (juce::LookAndFeel_V4&, std::function<void()> afterChange = nullptr);
    void unregisterLookAndFeel (juce::LookAndFeel_V4&);
    // Re-colour the colours set on components (setColour) under `root` from the previous skin to the current one.
    void recolourComponents (juce::Component& root, int fromSkin);
}

inline juce::Colour skinned (juce::uint32 argb, Skin::Area area = Skin::Area::Drawer) { return Skin::map (argb, area); }

// A named colour that follows the skin: use it like a juce::Colour; it is refreshed whenever the skin changes.
class SkinColour
{
public:
    SkinColour (juce::uint32 argb, Skin::Area);
    SkinColour (const SkinColour&);
    ~SkinColour();
    void refresh();

    operator const juce::Colour&() const noexcept { return colour; }
    juce::Colour withAlpha (float a) const noexcept { return colour.withAlpha (a); }
    juce::Colour withMultipliedAlpha (float a) const noexcept { return colour.withMultipliedAlpha (a); }
    juce::Colour brighter (float amount = 0.4f) const noexcept { return colour.brighter (amount); }
    juce::Colour darker (float amount = 0.4f) const noexcept { return colour.darker (amount); }
    juce::Colour interpolatedWith (juce::Colour other, float p) const noexcept { return colour.interpolatedWith (other, p); }
    juce::uint32 getARGB() const noexcept { return colour.getARGB(); }

private:
    juce::Colour colour;
    juce::uint32 original;
    Skin::Area area;
};

} // namespace teo5
