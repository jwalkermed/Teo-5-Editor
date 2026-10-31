// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/PanelComponent.h"
#include "UI/MidiSettingsDrawer.h"
#include "UI/SettingsDrawer.h"
#include "UI/LibraryDrawer.h"
#include "UI/ModMatrixDrawer.h"
#include "UI/SequencerDrawer.h"
#include "UI/PanelGuide.h"

class TEO5Editor : public juce::AudioProcessorEditor
{
public:
    explicit TEO5Editor (TEO5Processor&);
    ~TEO5Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    // With no drawer open: Left/Right step through patches, Up/Down through banks.
    bool keyPressed (const juce::KeyPress&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // Default editor width; height follows the photo's aspect ratio.
    static constexpr int defaultWidth = 1700;

    void setDrawerOpen (bool);
    void setGlobalOpen (bool);
    void setProgramOpen (bool);
    void setLibraryOpen (bool);
    void setModMatrixOpen (bool);
    void showModulationMenu (const juce::String& paramId);
    void addModulation (int source, int destination); // first free slot, then the mod matrix opens on it
    void setSequencerOpen (bool);
    void setGuideOpen (bool); // the front panel guide (the ? at the top left)
    void handlePanelAction (const juce::String& action);

private:
    // Narrow tab on an end cheek that opens a drawer.
    struct DrawerTab : juce::Component, juce::SettableTooltipClient
    {
        explicit DrawerTab (juce::String t) : text (std::move (t)) {}
        juce::String text;
        std::function<void()> onClick;
        bool open = false;
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override { if (onClick) onClick(); }
    };

    bool anyDrawerOpen() const;
    juce::MemoryBlock currentProgramOrFetch (const juce::String& title); // empty (and fetching) if none yet
    void toggleCompare();
    void revertProgram(); // REVERT: the program as loaded (or last written), to the edit buffer
    void showSynthLocation(); // BANK, FACTORY, the program buttons and the display, or "---" if the location isn't known
    void tapTempo();
    void writeCurrentProgram();
    void renameCurrentProgram();
    teo5::ProgramRef currentLibraryProgram() const;
    // The sequencer's bank drop-down: browsing one of your library banks (-1 = following the TEO-5's banks).
    int browseLibraryBank = -1, browseIndex = -1;
    bool browsedBankValid() const;
    void stepLibraryBank (int delta);
    void initializePreset();
    void oledMessage (const juce::String& title, const juce::String& text);

    TEO5Processor& processor;
    teo5::PanelComponent panel;
    teo5::PanelGuide::HelpButton guideButton;
    teo5::PanelGuide guide;
    teo5::MidiSettingsDrawer drawer;
    teo5::LibraryDrawer library;
    teo5::ModMatrixDrawer modMatrix;
    teo5::SequencerDrawer sequencer;
    teo5::SettingsDrawer globalDrawer, programDrawer;
    DrawerTab drawerTab { "MIDI" }, libraryTab { "LIBRARY" }, modMatrixTab { "MOD MATRIX" }, sequencerTab { "SEQUENCER" };
    DrawerTab globalTab { "GLOBAL" }, programTab { "PROGRAM" };
    void closeLeftDrawersExcept (juce::Component&);
    void closeRightDrawersExcept (juce::Component&);
    void setRightDrawerOpen (juce::Component&, DrawerTab&, bool);
    juce::TooltipWindow tooltips { this, 600 };

    bool sizeRestored = false; // until the saved size is applied, resizes aren't the user's
    std::vector<double> tapTimes; // ms, last few TAP TEMPO presses
    bool comparing = false;
    juce::MemoryBlock compareStash; // the edited program while COMPARE shows the stored one

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TEO5Editor)
};
