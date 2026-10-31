// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Widgets.h"
#include "../Params/ParamTable.h"

namespace teo5
{

// The TEO-5 desktop module front panel.
// Laid out in a fixed design space that maps 1:1 onto the reference photo's pixels
// (photo panel spans x 93..1907, y 447..1037). The editor scales this component with an
// AffineTransform, so all coordinates below are "photo pixels".
class PanelComponent : public juce::Component, private juce::Timer
{
public:
    static constexpr int designWidth = 1814;
    static constexpr int designHeight = 590;

    PanelComponent (juce::AudioProcessorValueTreeState&, juce::String& programName, int& bank, int& program);
    ~PanelComponent() override;

    void paint (juce::Graphics&) override;

    OledDisplay& getOled() { return oled; }
    void showParameter (const juce::String& paramId) { touched (paramId); }
    bool isFactory() const { return factoryBanks; }
    // Reflect a program change made on the synth (bank/program are already updated); sends nothing.
    void showProgram (bool factory);
    // The editor opened on a sound the library doesn't know: the display shows "---" and no program button is lit.
    void showUnknownLocation();
    // Same as the BANK/PATCH -/+ buttons: select the next/previous program (wraps) and send the program change.
    void stepBankOrPatch (int bankDelta, int patchDelta) { stepProgram (bankDelta, patchDelta); }
    // Like stepBankOrPatch, but through all 32 banks in turn: User 1-16, then Factory 1-16 (FACTORY follows).
    void stepThroughAllBanks (int bankDelta, int patchDelta);
    // Select a location (as if picked with BANK, a program button and FACTORY) and send the program change.
    void goTo (int bank, int program, bool factory);

    // Hooks for features wired up later (mod matrix, settings pages, librarian, memory).
    std::function<void (const juce::String& action)> onPanelAction;
    // Right-click on a knob or toggle, with the parameter it edits now (the LFO knobs follow the LFO 1/2 choice).
    std::function<void (const juce::String& paramId)> onControlMenu;
    // A parameter's value in the program as loaded (double-click on a knob returns to it).
    std::function<std::optional<int> (const juce::String& paramId)> storedValue;
    // Lit state for panel function buttons driven by the editor (e.g. "compare").
    std::function<bool (const juce::String& action)> isActionLit;
    // The tempo of the external MIDI clock the TEO-5 is following, if any (shown on the OLED instead of BPM).
    std::function<std::optional<double>()> externalBpm;
    // The sound differs from the stored program (the OLED shows EDITED under the name).
    std::function<bool()> isEdited;

private:
    struct TextItem { juce::String text; float x, y, size; juce::Justification just; };

    void timerCallback() override;

    Knob& addKnob (const juce::String& paramId, float x, float y, float diameter, KnobStyle = KnobStyle::Standard);
    PanelButton& addButton (float x, float y, bool wide);
    PanelButton& addToggle (const juce::String& paramId, float x, float y, bool wide);
    PanelButton& addCycle (const juce::String& paramId, float x, float y, bool wide);
    Led& addLed (float x, float y);
    void label (const juce::String& text, float x, float y, float size = 9.3f,
                juce::Justification just = juce::Justification::centred);

    void stepProgram (int bankDelta, int patchDelta);
    void bindLfoControls();
    void touched (const juce::String& paramId);
    juce::RangedAudioParameter* param (const juce::String& id) const { return apvts.getParameter (id); }
    int nativeValue (const juce::String& id) const;
    void setNativeValue (const juce::String& id, int value);
    void refreshIndicators();
    void selectProgram (int index);
    void updateProgramDisplay();

    void paintBackground (juce::Graphics&);
    void paintPinstripes (juce::Graphics&);
    void paintSilkscreen (juce::Graphics&);
    void paintGlyphs (juce::Graphics&);

    juce::AudioProcessorValueTreeState& apvts;
    juce::String& programName;
    int& bank;
    int& program;

    TEO5LookAndFeel lnf;

    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<PanelButton> buttons;
    juce::OwnedArray<Led> leds;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachments;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachments;
    juce::Array<TextItem> texts;

    // LFO section edits whichever LFO is selected.
    Knob* lfoFreq = nullptr;
    Knob* lfoAmount = nullptr;
    PanelButton* lfoSync = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lfoFreqAtt, lfoAmtAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lfoSyncAtt;
    int boundLfo = -1;

    Led* lfoShapeLeds[5] {};
    Led* lfoSelLeds[2] {};
    Led* arpOctLeds[3] {};
    Led* arpRepLeds[3] {};
    Led* detuneLed = nullptr;
    Led* bpLed = nullptr;
    Led* env1Leds[2] {};
    Led* env2Leds[3] {};

    PanelButton* programButtons[16] {};
    PanelButton* bankButton = nullptr;
    PanelButton* factoryButton = nullptr;
    bool bankSelectMode = false;
    bool factoryBanks = false;
    bool locationUnknown = false;

    OledDisplay oled;
    SevenSegment sevenSeg;
    juce::Image noiseTile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanelComponent)
};

} // namespace teo5
