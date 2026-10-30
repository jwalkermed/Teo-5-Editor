// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "MidiSettingsDrawer.h"
#include "TEO5LookAndFeel.h"

namespace teo5
{

MidiSettingsDrawer::MidiSettingsDrawer (MidiSync& s, ProgramState& ps) : sync (s), programState (ps)
{
    const auto text = Palette::silkscreen;
    for (auto* l : { &title, &inLabel, &outLabel, &chLabel, &modeLabel, &nameLabel, &statusLabel })
    {
        l->setColour (juce::Label::textColourId, text);
        l->setFont (TEO5LookAndFeel::panelFont (l == &title ? 14.0f : 10.5f));
        addAndMakeVisible (l);
    }
    statusLabel.setJustificationType (juce::Justification::topLeft);
    statusLabel.setMinimumHorizontalScale (1.0f);

    for (auto* b : { &inBox, &outBox, &chBox, &modeBox })
    {
        b->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff141517));
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
    for (auto* t : { &hostToggle, &requestToggle, &playToggle })
    {
        t->setColour (juce::ToggleButton::textColourId, text);
        t->setColour (juce::ToggleButton::tickColourId, Palette::ledOn);
        t->setColour (juce::ToggleButton::tickDisabledColourId, Palette::pinstripe);
        t->onClick = [this] { pushSettings(); };
        addAndMakeVisible (t);
    }

    for (auto* b : { &refreshButton, &inquiryButton, &setupButton, &receiveButton, &sendButton, &sendAllButton, &testNoteButton, &probeButton, &locateButton, &clearMonitorButton })
    {
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff232427));
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
    monitor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0b0c0d));
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
    probeButton.onClick = [this]
    {
        if (sync.isProbing())
        {
            sync.cancelLayoutProbe();
            return;
        }
        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::QuestionIcon, "Map program dump layout",
            "The editor will step every parameter on the TEO-5 to its minimum and maximum, request the edit buffer "
            "after each change, and work out where each value is stored. It takes a few minutes.\n\n"
            "Only the edit buffer is changed (each value is restored afterwards); nothing is written to memory.\n\n"
            "On the TEO-5, set Global > MIDI Param Receive = NRPN and MIDI SysEx = On, with the SysEx cable matching this connection.",
            "Start", "Cancel", this,
            juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) sync.startLayoutProbe(); }));
    };
    locateButton.onClick = [this] { sync.locateName (nameEditor.getText()); };

    nameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff141517));
    nameEditor.setColour (juce::TextEditor::textColourId, text);
    nameEditor.setColour (juce::TextEditor::outlineColourId, Palette::pinstripe.withAlpha (0.6f));
    nameEditor.setText (programState.name, false);
    addAndMakeVisible (nameEditor);

    progressBar.setColour (juce::ProgressBar::foregroundColourId, Palette::pinstripe);
    addAndMakeVisible (progressBar);
    progressBar.setVisible (false);

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
    auto layoutInfo = sync.getLayout().isEmpty()
                          ? juce::String ("Dump layout: not mapped yet")
                          : "Dump layout: " + juce::String (sync.getLayout().size()) + " parameters mapped"
                                + (sync.getLayout().nameOffset >= 0 ? ", name located" : ", name not located");
    statusLabel.setText (sync.getStatus() + "\n" + layoutInfo
                             + (sync.firmware.isNotEmpty() ? "\nTEO-5 OS " + sync.firmware : juce::String()),
                         juce::dontSendNotification);
}

void MidiSettingsDrawer::timerCallback()
{
    const bool probing = sync.isProbing();
    progressBar.setVisible (probing);
    progress = sync.probeProgress();
    probeButton.setButtonText (probing ? "Cancel mapping" : "Map dump layout...");
    if (! nameEditor.hasKeyboardFocus (true) && nameEditor.getText() != programState.name)
        nameEditor.setText (programState.name, false);

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
    g.fillAll (juce::Colour (0xf0181a1c));
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
    probeButton.setBounds (row());
    progressBar.setBounds (row (14));
    nameLabel.setBounds (row (16));
    {
        auto x = row();
        locateButton.setBounds (x.removeFromRight (96));
        x.removeFromRight (6);
        nameEditor.setBounds (x);
    }
    statusLabel.setBounds (r.removeFromTop (46));
    r.removeFromTop (4);
    monitor.setBounds (r);
    clearMonitorButton.setBounds (monitor.getRight() - 54, monitor.getY() + 3, 48, 18); // overlays the monitor's top-right corner
}

} // namespace teo5
