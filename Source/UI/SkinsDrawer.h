// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace teo5
{

// Right-side drawer listing the skins, each with a small picture of the front panel in it; a click switches the
// whole editor at once. The choice is saved for every editor on this Mac (see Skin.h). The list scrolls when the
// editor is too small to show it all.
class SkinsDrawer : public juce::Component
{
public:
    SkinsDrawer();
    ~SkinsDrawer() override;

    // Draws the front panel in skin `index` (the editor provides it; the pictures are made when the drawer opens).
    std::function<juce::Image (int index)> renderPanel;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    class List;
    std::unique_ptr<List> list;
    juce::Viewport viewport;
};

} // namespace teo5
