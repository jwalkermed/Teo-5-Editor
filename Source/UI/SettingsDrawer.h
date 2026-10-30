#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../Midi/MidiSync.h"

namespace teo5
{

// Right-hand drawer listing settings by section, one row per parameter:
//  - Global: the TEO-5's GLOBAL settings (not saved in programs). Read from the synth's global dump when the
//    drawer opens; edits go out as NRPN.
//  - Program: program settings that have no knob on the panel (optionally all of them). Bound to the APVTS,
//    so they follow the synth through the normal NRPN and edit-buffer sync.
class SettingsDrawer : public juce::Component,
                       private juce::Timer
{
public:
    enum class Kind { Global, Program };

    SettingsDrawer (Kind, MidiSync&, ProgramState&, juce::AudioProcessorValueTreeState&);
    ~SettingsDrawer() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    class Row;
    struct Section { juce::String title; juce::StringArray ids; };

    void timerCallback() override;
    void rebuildRows();
    juce::Array<Section> sections() const;
    juce::String headerText() const;

    const Kind kind;
    MidiSync& sync;
    ProgramState& programState;
    juce::AudioProcessorValueTreeState& apvts;

    juce::Label title;
    juce::TextButton readButton { "Read from TEO-5" };
    juce::ToggleButton showAllToggle { "Also list settings that have a panel knob" };
    juce::Viewport viewport;
    struct Content : juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
    } content;
    juce::OwnedArray<Row> rows;
    juce::Array<std::pair<juce::String, int>> sectionHeaders; // title, y in content
    juce::String header;
    juce::Time globalsReadAt;
    int seenGlobalsVersion = -1;
    bool readRequested = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsDrawer)
};

} // namespace teo5
