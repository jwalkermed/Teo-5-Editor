// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "Skin.h"
#include "MidiSettingsDrawer.h"
#include "TEO5LookAndFeel.h"

namespace teo5
{

MidiSettingsDrawer::MidiSettingsDrawer (MidiSync& s, ProgramState& ps) : sync (s), programState (ps)
{
    const auto text = Palette::silkscreen;
    for (auto* l : { &title, &inLabel, &outLabel, &chLabel, &modeLabel, &statusLabel })
    {
        l->setColour (juce::Label::textColourId, text);
        l->setFont (TEO5LookAndFeel::panelFont (l == &title ? 14.0f : 10.5f));
        addAndMakeVisible (l);
    }
    statusLabel.setJustificationType (juce::Justification::topLeft);
    statusLabel.setMinimumHorizontalScale (1.0f);

    for (auto* b : { &inBox, &outBox, &chBox, &modeBox })
    {
        b->setColour (juce::ComboBox::backgroundColourId, skinned (0xff141517, Skin::Area::Drawer));
        b->setColour (juce::ComboBox::outlineColourId, Palette::pinstripe.withAlpha (0.6f));
        b->setColour (juce::ComboBox::textColourId, text);
        b->setColour (juce::ComboBox::arrowColourId, Palette::pinstripe);
        b->onChange = [this] { pushSettings(); };
        addAndMakeVisible (b);
    }
    for (int c = 1; c <= 16; ++c)
        chBox.addItem (juce::String (c), c);
    modeBox.addItem ("NRPN", 1);
    modeBox.addItem ("CC", 2);

    playToggle.setTooltip ("Notes, pitch bend, mod wheel, aftertouch and pedals played into this track are sent to the TEO-5 on its channel");
    hostToggle.setTooltip ("For when the DAW holds the TEO-5's MIDI port and the editor can't open it. Knobs work this way, "
                           "but many DAWs don't pass SysEx, which program transfers need");
    for (auto* t : { &hostToggle, &requestToggle, &playToggle })
    {
        t->setColour (juce::ToggleButton::textColourId, text);
        t->setColour (juce::ToggleButton::tickColourId, Palette::ledOn);
        t->setColour (juce::ToggleButton::tickDisabledColourId, Palette::pinstripe);
        t->onClick = [this] { pushSettings(); };
        addAndMakeVisible (t);
    }
    // Routing through the DAW is only for Windows, where a MIDI port the DAW has open often can't be shared.
    // On macOS ports are shared, so the editor always opens them itself and the option is hidden.
    hostToggle.setVisible (showHostRouting);

    for (auto* b : { &refreshButton, &inquiryButton, &setupButton, &receiveButton, &sendButton, &sendAllButton, &testNoteButton, &clearMonitorButton })
    {
        b->setColour (juce::TextButton::buttonColourId, skinned (0xff232427, Skin::Area::Drawer));
        b->setColour (juce::TextButton::textColourOffId, text);
        addAndMakeVisible (b);
    }
    refreshButton.onClick = [this] { refreshPorts(); };
    inquiryButton.onClick = [this] { sync.deviceInquiry(); };
    setupButton.setTooltip ("Set the TEO-5's MIDI global settings so it talks to this editor");
    setupButton.onClick = [this]
    {
        if (! sync.hasOutput())
        {
            sync.deviceInquiry(); // reports the missing output
            return;
        }
        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::QuestionIcon, "Set up TEO-5",
            "This changes these Global settings on the TEO-5:\n\n"
            "  MIDI Param Send = NRPN\n  MIDI Param Receive = NRPN\n  MIDI Control = On\n  MIDI SysEx = On\n"
            "  MIDI SysEx Cable = the connection in use\n  MIDI Out Select = All\n  MIDI Program Send / Receive = On\n\n"
            "Programs are not affected.",
            "Change settings", "Cancel", this,
            juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) sync.configureSynthGlobals(); }));
    };
    receiveButton.onClick = [this] { sync.requestEditBuffer(); };
    sendButton.onClick = [this] { sync.sendEditBuffer(); };
    sendAllButton.onClick = [this] { sync.sendAllParameters(); };
    testNoteButton.onClick = [this] { sync.sendTestNote(); };
    testNoteButton.setTooltip ("Plays middle C on the TEO-5: if you hear it, the editor's MIDI output reaches the synth");

    monitor.setMultiLine (true);
    monitor.setReadOnly (true);
    monitor.setCaretVisible (false);
    monitor.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain));
    monitor.setColour (juce::TextEditor::backgroundColourId, skinned (0xff0b0c0d, Skin::Area::Drawer));
    monitor.setColour (juce::TextEditor::textColourId, text.withAlpha (0.8f));
    monitor.setColour (juce::TextEditor::outlineColourId, Palette::pinstripe.withAlpha (0.4f));
    addAndMakeVisible (monitor);
    clearMonitorButton.toFront (false);
    clearMonitorButton.setTooltip ("Clear the MIDI monitor and reset its counters");
    clearMonitorButton.onClick = [this]
    {
        sync.getPorts().clearLog();
        lastLogSize = lastSent = lastReceived = -1;
    };
    refreshPorts();
    sync.addChangeListener (this);
    changeListenerCallback (nullptr);
    startTimerHz (10);
}

MidiSettingsDrawer::~MidiSettingsDrawer()
{
    sync.removeChangeListener (this);
}

void MidiSettingsDrawer::refreshPorts()
{
    ins = MidiPortManager::inputs();
    outs = MidiPortManager::outputs();

    auto fill = [] (juce::ComboBox& box, const juce::Array<juce::MidiDeviceInfo>& devs)
    {
        box.clear (juce::dontSendNotification);
        box.addItem ("(none)", 1);
        for (int i = 0; i < devs.size(); ++i)
            box.addItem (devs[i].name, i + 2);
    };
    fill (inBox, ins);
    fill (outBox, outs);
    loadSettings();
}

void MidiSettingsDrawer::loadSettings()
{
    const auto& s = sync.getSettings();
    auto select = [] (juce::ComboBox& box, const juce::Array<juce::MidiDeviceInfo>& devs, const juce::String& id)
    {
        int item = 1;
        for (int i = 0; i < devs.size(); ++i)
            if (devs[i].identifier == id)
                item = i + 2;
        box.setSelectedId (item, juce::dontSendNotification);
    };
    select (inBox, ins, s.inputId);
    select (outBox, outs, s.outputId);
    chBox.setSelectedId (s.channel, juce::dontSendNotification);
    modeBox.setSelectedId (s.useCc ? 2 : 1, juce::dontSendNotification);
    hostToggle.setToggleState (s.useHostMidi, juce::dontSendNotification);
    requestToggle.setToggleState (s.requestOnProgramChange, juce::dontSendNotification);
    playToggle.setToggleState (s.playFromHost, juce::dontSendNotification);
}

void MidiSettingsDrawer::pushSettings()
{
    MidiSettings s = sync.getSettings();
    const int in = inBox.getSelectedId() - 2, out = outBox.getSelectedId() - 2;
    s.inputId = juce::isPositiveAndBelow (in, ins.size()) ? ins[in].identifier : juce::String();
    s.outputId = juce::isPositiveAndBelow (out, outs.size()) ? outs[out].identifier : juce::String();
    s.channel = juce::jmax (1, chBox.getSelectedId());
    s.useCc = modeBox.getSelectedId() == 2;
    s.useHostMidi = hostToggle.getToggleState();
    s.requestOnProgramChange = requestToggle.getToggleState();
    s.playFromHost = playToggle.getToggleState();
    sync.applySettings (s, true);
}

void MidiSettingsDrawer::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const auto status = sync.getStatus();
    const bool showFirmware = sync.firmware.isNotEmpty() && ! status.contains (sync.firmware); // "TEO-5 found, OS ..." has it
    statusLabel.setText (status + (showFirmware ? "\nTEO-5 OS " + sync.firmware : juce::String()),
                         juce::dontSendNotification);
}

void MidiSettingsDrawer::timerCallback()
{

    // MIDI monitor.
    auto& ports = sync.getPorts();
    auto lines = ports.recentLog();
    if (lines.size() != lastLogSize || ports.sentCount() != lastSent || ports.receivedCount() != lastReceived)
    {
        lastLogSize = lines.size();
        lastSent = ports.sentCount();
        lastReceived = ports.receivedCount();
        auto err = ports.lastError();
        monitor.setText ("MIDI monitor   sent: " + juce::String (lastSent) + "   received: " + juce::String (lastReceived)
                             + (err.isNotEmpty() ? "   ERROR: " + err : juce::String()) + "\n"
                             + lines.joinIntoString ("\n"),
                         false);
        monitor.moveCaretToEnd();
    }
}

void MidiSettingsDrawer::paint (juce::Graphics& g)
{
    g.fillAll (skinned (0xf0181a1c, Skin::Area::Drawer));
    g.setColour (Palette::pinstripe);
    g.fillRect (0, 0, 2, getHeight());
    g.fillRect (12, 34, getWidth() - 24, 1);
}

void MidiSettingsDrawer::resized()
{
    auto r = getLocalBounds().reduced (14, 8);
    r.removeFromLeft (2);
    title.setBounds (r.removeFromTop (24));
    r.removeFromTop (8);

    auto row = [&r] (int h = 24) { auto x = r.removeFromTop (h); r.removeFromTop (5); return x; };
    auto labelled = [&] (juce::Label& l, juce::Component& c)
    {
        auto x = row();
        l.setBounds (x.removeFromLeft (64));
        c.setBounds (x);
    };
    labelled (inLabel, inBox);
    labelled (outLabel, outBox);
    {
        auto x = row();
        chLabel.setBounds (x.removeFromLeft (64));
        chBox.setBounds (x.removeFromLeft (64));
        x.removeFromLeft (8);
        modeLabel.setBounds (x.removeFromLeft (56));
        modeBox.setBounds (x);
    }
    if (showHostRouting)
        hostToggle.setBounds (row (22));
    requestToggle.setBounds (row (22));
    playToggle.setBounds (row (22));
    {
        auto x = row();
        const int w = (x.getWidth() - 12) / 3;
        refreshButton.setBounds (x.removeFromLeft (w));
        x.removeFromLeft (6);
        inquiryButton.setBounds (x.removeFromLeft (w));
        x.removeFromLeft (6);
        setupButton.setBounds (x);
    }
    {
        auto x = row();
        receiveButton.setBounds (x.removeFromLeft (x.getWidth() / 2 - 3));
        x.removeFromLeft (6);
        sendButton.setBounds (x);
    }
    {
        auto x = row();
        sendAllButton.setBounds (x.removeFromLeft (x.getWidth() / 2 - 3));
        x.removeFromLeft (6);
        testNoteButton.setBounds (x);
    }
    r.removeFromTop (6);
    statusLabel.setBounds (r.removeFromTop (32));
    r.removeFromTop (4);
    monitor.setBounds (r);
    clearMonitorButton.setBounds (monitor.getRight() - 54, monitor.getY() + 3, 48, 18); // overlays the monitor's top-right corner
}

} // namespace teo5
