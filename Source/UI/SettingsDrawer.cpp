// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "SettingsDrawer.h"
#include "TEO5LookAndFeel.h"

namespace teo5
{

namespace
{
const juce::Colour fieldBg (0xff141517), dim (0xff8d939c);
constexpr int rowH = 24, headerH = 26;

// Globals that can cut the editor off from the synth: changing them asks first.
bool isRiskyGlobal (const juce::String& id)
{
    return id == "gMidiChannel" || id == "gParamRecv" || id == "gMidiControl" || id == "gSysexEnable"
        || id == "gSysexCable" || id == "gCableOut";
}

// Settings listed in the Program drawer, by section, in the order the user's guide describes the synth's
// PROGRAM menu (it has no complete list). Most have no knob on the panel; a few (Osc 1 Glide, Osc 2 Detune)
// are also here because the menu has them.
const juce::StringArray programIds[] = {
    { "osc1KeyOn", "osc2KeyOn", "osc1Detune", "osc2Detune", "osc1PW", "osc2PW", "osc1Glide", "osc2Glide",
      "osc1Level", "osc2Level", "subLevel", "noiseType", "noiseLevel", "osc2FiltBypass" },
    { "filterKeyAmt", "filterBP" },
    { "env1Delay", "env2Delay", "envRepeat", "env1Dest" },
    { "lfo1Slew", "lfo2Slew", "lfo1NoteReset", "lfo2NoteReset", "lfo1FreqSync", "lfo2FreqSync" },
    { "unisonVoices", "unisonDetune", "keyMode", "envRetrig",
      "unisonNote1", "unisonNote2", "unisonNote3", "unisonNote4", "unisonNote5" },
    { "fxSyncRate" },
    { "arpRelatch", "transpose", "keySplit", "keySplit2", "keySplitNote" },
    { "voiceVolume", "glideMode", "pbUp", "pbDown" },
    { "scale" },
};
const char* programTitles[] = { "OSCILLATORS", "FILTER", "ENVELOPES", "LFOS", "UNISON", "EFFECTS", "ARP AND KEYBOARD",
                                "VOLUME, PORTAMENTO AND PITCH BEND", "SCALE" };
} // namespace

// ---- One setting: name on the left, a control in TEO-5 units on the right --------------------------

class SettingsDrawer::Row : public juce::Component,
                            public juce::SettableTooltipClient
{
public:
    Row (SettingsDrawer& o, const ParamDef& d) : def (d), owner (o)
    {
        const auto names = choiceNames (def);
        if (def.kind == ParamKind::Switch)
        {
            toggle = std::make_unique<juce::ToggleButton>();
            toggle->setColour (juce::ToggleButton::tickColourId, Palette::pinstripe);
            toggle->setColour (juce::ToggleButton::tickDisabledColourId, dim);
            toggle->onClick = [this] { userChanged (toggle->getToggleState() ? 1 : 0); };
            addAndMakeVisible (*toggle);
        }
        else if (def.kind == ParamKind::Choice && names.size() == def.maxValue - def.minValue + 1)
        {
            combo = std::make_unique<juce::ComboBox>();
            for (int i = 0; i < names.size(); ++i)
                combo->addItem (names[i], i + 1);
            combo->setColour (juce::ComboBox::backgroundColourId, fieldBg);
            combo->setColour (juce::ComboBox::outlineColourId, Palette::pinstripe.withAlpha (0.6f));
            combo->setColour (juce::ComboBox::textColourId, Palette::silkscreen);
            combo->setColour (juce::ComboBox::arrowColourId, Palette::pinstripe);
            combo->onChange = [this] { userChanged (def.minValue + combo->getSelectedItemIndex()); };
            addAndMakeVisible (*combo);
        }
        else
        {
            slider = std::make_unique<juce::Slider> (juce::Slider::LinearBar, juce::Slider::TextBoxLeft);
            slider->setRange (def.minValue, def.maxValue, 1.0);
            slider->textFromValueFunction = [this] (double v) { return valueToText (def, juce::roundToInt (v)); };
            slider->setColour (juce::Slider::backgroundColourId, fieldBg);
            slider->setColour (juce::Slider::trackColourId, Palette::pinstripe.withAlpha (0.45f));
            slider->setColour (juce::Slider::textBoxTextColourId, Palette::silkscreen);
            slider->setColour (juce::Slider::textBoxOutlineColourId, Palette::pinstripe.withAlpha (0.6f));
            slider->setVelocityBasedMode (false);
            slider->setMouseDragSensitivity (juce::jlimit (60, 400, (def.maxValue - def.minValue) * 3));
            slider->onValueChange = [this] { userChanged (juce::roundToInt (slider->getValue())); };
            addAndMakeVisible (*slider);
        }

        if (def.scope == Scope::Program)
            if (auto* p = owner.apvts.getParameter (def.id))
            {
                attachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { show (juce::roundToInt (v)); });
                attachment->sendInitialUpdate();
            }
        setTooltip (juce::String (def.name) + (def.verify ? " (value names unconfirmed, see VERIFY.md)" : ""));
    }

    // Value from the synth or the APVTS: update the control without sending anything.
    void show (int v)
    {
        value = v;
        if (toggle != nullptr) toggle->setToggleState (v != 0, juce::dontSendNotification);
        if (combo != nullptr) combo->setSelectedItemIndex (v - def.minValue, juce::dontSendNotification);
        if (slider != nullptr) slider->setValue (v, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (Palette::silkscreen.withAlpha (0.85f));
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (def.name, getLocalBounds().withRight (getWidth() - controlWidth() - 8), juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().removeFromRight (controlWidth()).reduced (0, 2);
        if (toggle != nullptr) toggle->setBounds (r.removeFromRight (28));
        if (combo != nullptr) combo->setBounds (r);
        if (slider != nullptr) slider->setBounds (r);
    }

    const ParamDef& def;

private:
    int controlWidth() const { return juce::jmin (combo != nullptr && combo->getNumItems() > 20 ? 200 : 140, getWidth() / 2); }

    void userChanged (int v)
    {
        if (v == value)
            return;
        if (def.scope == Scope::Program)
        {
            if (attachment != nullptr)
                attachment->setValueAsCompleteGesture ((float) v);
            value = v;
            return;
        }

        // Global: some settings can stop the editor reaching the synth, so confirm those.
        const int previous = value;
        auto send = [safe = juce::Component::SafePointer<Row> (this), v]
        {
            if (safe != nullptr)
            {
                safe->owner.sync.setGlobal (safe->def, v);
                safe->value = v;
            }
        };
        if (! isRiskyGlobal (def.id))
        {
            send();
            return;
        }
        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::WarningIcon, "Change " + juce::String (def.name) + "?",
            "Setting " + juce::String (def.name) + " to \"" + valueToText (def, v) + "\" on the TEO-5 can stop it "
                "talking to this editor. If that happens, change it back on the synth's GLOBAL menu.",
            "Change it", "Cancel", this,
            juce::ModalCallbackFunction::create ([send, safe = juce::Component::SafePointer<Row> (this), previous] (int r)
            {
                if (r == 1)
                    send();
                else if (safe != nullptr)
                    safe->show (previous);
            }));
    }

    SettingsDrawer& owner;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int value = -1;
};

// ---- Drawer ------------------------------------------------------------------------------------------

SettingsDrawer::SettingsDrawer (Kind k, MidiSync& s, ProgramState& ps, juce::AudioProcessorValueTreeState& state)
    : kind (k), sync (s), programState (ps), apvts (state)
{
    title.setText (kind == Kind::Global ? "GLOBAL" : "PROGRAM", juce::dontSendNotification);
    title.setColour (juce::Label::textColourId, Palette::silkscreen);
    title.setFont (TEO5LookAndFeel::panelFont (14.0f));
    addAndMakeVisible (title);

    if (kind == Kind::Global)
    {
        readButton.setTooltip ("Read the current global settings from the TEO-5");
        readButton.onClick = [this] { readRequested = true; sync.requestGlobals(); };
        addAndMakeVisible (readButton);
    }
    else
    {
        showAllToggle.setColour (juce::ToggleButton::textColourId, Palette::silkscreen);
        showAllToggle.setColour (juce::ToggleButton::tickColourId, Palette::pinstripe);
        showAllToggle.onClick = [this] { rebuildRows(); };
        addAndMakeVisible (showAllToggle);
    }

    content.painter = [this] (juce::Graphics& g)
    {
        for (auto& [text, y] : sectionHeaders)
        {
            g.setColour (dim);
            g.setFont (TEO5LookAndFeel::panelFont (8.5f));
            g.drawText (text, 0, y + 8, content.getWidth(), 14, juce::Justification::centredLeft);
            g.setColour (Palette::pinstripe.withAlpha (0.4f));
            g.fillRect (0, y + headerH - 3, content.getWidth(), 1);
        }
    };
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    rebuildRows();
    startTimerHz (10);
}

SettingsDrawer::~SettingsDrawer()
{
    rows.clear(); // before the attachments' parameters or the content go away
}

juce::Array<SettingsDrawer::Section> SettingsDrawer::sections() const
{
    juce::Array<Section> result;
    if (kind == Kind::Global)
    {
        // The synth's GLOBAL menu order (items 1-27 in the user's guide).
        result.add ({ "TUNING", { "gCoarseTune", "gFineTune" } });
        result.add ({ "PLAYING", { "gLocalControl", "gStereo", "gPotMode", "gSusMode", "gSusPolarity", "gPedalAssign",
                                   "gPressure", "gPressureCurve", "gVelocityCurve", "gScale", "gScreensaver" } });
        result.add ({ "MIDI", { "gMidiChannel", "gClockMode", "gClockCableIn", "gClockCableOut", "gParamSend", "gParamRecv",
                                "gMidiControl", "gSysexEnable", "gSysexCable", "gCableOut", "gProgSend", "gProgRecv",
                                "gArpNotes", "gArpBeatSync" } });
        return result;
    }

    juce::StringArray listed;
    for (int i = 0; i < (int) std::size (programIds); ++i)
    {
        result.add ({ programTitles[i], programIds[i] });
        listed.addArray (programIds[i]);
    }
    if (showAllToggle.getToggleState())
    {
        // Everything else in the program except the mod matrix, which has its own drawer.
        Section onPanel { "ON THE PANEL", {} };
        for (auto& def : allParams())
            if (def.scope == Scope::Program && ! listed.contains (def.id) && ! juce::String (def.id).startsWith ("mod"))
                onPanel.ids.add (def.id);
        result.add (onPanel);
    }
    return result;
}

void SettingsDrawer::rebuildRows()
{
    rows.clear();
    sectionHeaders.clear();
    int y = 0;
    for (auto& section : sections())
    {
        sectionHeaders.add ({ section.title, y });
        y += headerH;
        for (auto& id : section.ids)
            if (auto* def = findById (id))
            {
                auto* row = rows.add (new Row (*this, *def));
                if (kind == Kind::Global)
                    row->show (sync.globalValue (*def));
                row->setTopLeftPosition (0, y);
                content.addAndMakeVisible (row);
                y += rowH;
            }
    }
    content.setSize (content.getWidth(), y + 8);
    resized();
    content.repaint();
}

juce::String SettingsDrawer::headerText() const
{
    if (kind == Kind::Program)
        return programState.name + juce::String::fromUTF8 ("  \xc2\xb7  ") + (programState.factory ? "Factory " : "User ")
             + juce::String (programState.bank) + "-" + juce::String (programState.program).paddedLeft ('0', 2);
    if (! sync.hasGlobals())
        return readRequested ? "Waiting for the TEO-5's global settings..." : "Not read from the TEO-5 yet.";
    return "Read from the TEO-5 at " + globalsReadAt.toString (false, true, false) + ". Changes are sent as you make them.";
}

void SettingsDrawer::visibilityChanged()
{
    // Global settings can change on the synth's own menu, so read them each time the drawer opens.
    if (kind == Kind::Global && isVisible())
    {
        readRequested = true;
        sync.requestGlobals();
    }
}

void SettingsDrawer::timerCallback()
{
    if (kind == Kind::Global && sync.globalsVersion != seenGlobalsVersion)
    {
        seenGlobalsVersion = sync.globalsVersion;
        if (readRequested && sync.hasGlobals())
        {
            globalsReadAt = juce::Time::getCurrentTime();
            readRequested = false;
        }
        for (auto* row : rows)
            row->show (sync.globalValue (row->def));
    }
    const auto text = headerText();
    if (text != header)
    {
        header = text;
        repaint (0, 34, getWidth(), 24);
    }
}

void SettingsDrawer::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xf0181a1c));
    g.setColour (Palette::pinstripe);
    g.fillRect (0, 0, 2, getHeight());
    g.fillRect (12, 34, getWidth() - 24, 1);
    g.setColour (dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (header, juce::Rectangle<int> (16, 38, getWidth() - 30, 18), juce::Justification::centredLeft, true);
}

void SettingsDrawer::resized()
{
    auto r = getLocalBounds().reduced (14, 8);
    r.removeFromLeft (2);
    auto top = r.removeFromTop (24);
    if (kind == Kind::Global)
        readButton.setBounds (top.removeFromRight (120));
    title.setBounds (top);
    r.removeFromTop (26); // status line
    if (kind == Kind::Program)
    {
        showAllToggle.setBounds (r.removeFromBottom (24));
        r.removeFromBottom (4);
    }
    viewport.setBounds (r);
    const int w = r.getWidth() - viewport.getScrollBarThickness() - 4;
    content.setSize (w, content.getHeight());
    for (auto* row : rows)
        row->setSize (w, rowH);
}

} // namespace teo5
