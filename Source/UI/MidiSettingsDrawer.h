#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Midi/MidiSync.h"

namespace teo5
{

// Slide-in drawer with MIDI port/channel/mode settings and hardware actions.
class MidiSettingsDrawer : public juce::Component,
                           private juce::ChangeListener,
                           private juce::Timer
{
public:
    MidiSettingsDrawer (MidiSync&, ProgramState&);
    ~MidiSettingsDrawer() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void refreshPorts();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void pushSettings();
    void loadSettings();

    MidiSync& sync;
    ProgramState& programState;

    juce::Label title { {}, "MIDI" };
    juce::Label inLabel { {}, "Input" }, outLabel { {}, "Output" }, chLabel { {}, "Channel" }, modeLabel { {}, "Send as" };
    juce::ComboBox inBox, outBox, chBox, modeBox;
    juce::ToggleButton hostToggle { "Also route through host MIDI" };
    juce::ToggleButton requestToggle { "Fetch edit buffer on program change" };
    juce::ToggleButton playToggle { "Play the TEO-5 from the DAW track" };
    juce::TextButton refreshButton { "Rescan" }, inquiryButton { "Find TEO-5" }, setupButton { "Set up TEO-5..." };
    juce::TextButton receiveButton { "Receive edit buffer" }, sendButton { "Send edit buffer" };
    juce::TextButton sendAllButton { "Send all parameters" }, testNoteButton { "Send test note" };
    juce::TextEditor monitor;
    juce::TextButton clearMonitorButton { "Clear" };
    int lastLogSize = -1, lastSent = -1, lastReceived = -1;
    juce::TextButton probeButton { "Map dump layout..." };
    juce::Label nameLabel { {}, "Program name as shown on the TEO-5:" };
    juce::TextEditor nameEditor;
    juce::TextButton locateButton { "Locate name" };
    juce::Label statusLabel;
    double progress = 0.0;
    juce::ProgressBar progressBar { progress };

    juce::Array<juce::MidiDeviceInfo> ins, outs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiSettingsDrawer)
};

} // namespace teo5
