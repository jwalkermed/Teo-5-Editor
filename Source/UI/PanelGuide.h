// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace teo5
{

// The front panel guide: opened by the ? at the panel's top left, it covers the panel with three columns
// explaining the panel's controls, right-click modulation and shortcuts. Laid out for a 1700-px-wide editor;
// the editor scales it with a transform. The text is `panelGuide` in PanelGuide.cpp (keep it up to date).
class PanelGuide : public juce::Component
{
public:
    PanelGuide();
    ~PanelGuide() override;

    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void scrollToTop();

    // The ? that opens it, drawn like the sequencer's (lit while the guide is open).
    struct HelpButton : juce::Button
    {
        HelpButton() : juce::Button ("?") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

private:
    class Column;
    juce::TextButton close { "CLOSE" };
    juce::OwnedArray<juce::Viewport> viewports;
    juce::OwnedArray<Column> columns;
};

} // namespace teo5
