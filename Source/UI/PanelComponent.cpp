// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "PanelComponent.h"

namespace teo5
{

namespace
{
// Photo pixel -> design space.
constexpr float X (float x) { return x - 93.0f; }
constexpr float Y (float y) { return y - 447.0f; }

constexpr float stdKnob = 58.0f;
constexpr float squareBtn = 28.0f;
constexpr float buttonPad = PanelButton::bloomPad; // room for the red bloom
constexpr float ledSize = Led::componentSize;      // the LED plus room for its glow
constexpr float pitch = 17.4f;    // pinstripe spacing

const juce::Justification left = juce::Justification::centredLeft;
} // namespace

PanelComponent::PanelComponent (juce::AudioProcessorValueTreeState& state, juce::String& name, int& b, int& p)
    : apvts (state), programName (name), bank (b), program (p)
{
    setLookAndFeel (&lnf);
    setSize (designWidth, designHeight);

    // Pre-render a matte noise tile for the anodised panel finish.
    {
        noiseTile = juce::Image (juce::Image::ARGB, 256, 256, true);
        juce::Random rng (0x7e05);
        juce::Image::BitmapData bd (noiseTile, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x)
            {
                const auto v = (juce::uint8) rng.nextInt (256);
                bd.setPixelColour (x, y, juce::Colour (v, v, v, (juce::uint8) 9));
            }
    }

    // ================= MASTER =================
    addKnob ("masterVolume", 183, 538, stdKnob);
    addKnob ("overdrive", 180, 643, stdKnob);
    label ("VOLUME", 190, 574);
    label ("OVERDRIVE", 190, 678);

    // ================= LFO 1: MONO | LFO 2: POLY =================
    lfoSync = &addButton (277, 538, false);
    lfoSync->setClickingTogglesState (true);
    lfoFreq = &addKnob ("lfo1Freq", 340, 538, stdKnob);
    {
        auto& shape = addButton (407, 538, false);
        shape.litWhen = [] { return false; };
        shape.onClick = [this]
        {
            const auto id = nativeValue ("lfoSelect") == 0 ? "lfo1Shape" : "lfo2Shape";
            setNativeValue (id, (nativeValue (id) + 1) % 5);
            touched (id);
        };
    }
    const float shapeLedY[] = { 521, 538, 555, 573, 590 };
    for (int i = 0; i < 5; ++i)
        lfoShapeLeds[i] = &addLed (447, shapeLedY[i]);
    label ("S/H", 469, 590, 8.0f, left);

    {
        auto& dest = addButton (277, 643, false);
        dest.litWhen = [] { return false; };
        dest.onClick = [this, &dest]
        {
            const auto id = juce::String (nativeValue ("lfoSelect") == 0 ? "lfo1Dest" : "lfo2Dest");
            const auto* def = findById (id);
            juce::PopupMenu m;
            auto names = choiceNames (*def);
            const int current = nativeValue (id);
            for (int i = 0; i < names.size(); ++i)
                m.addItem (i + 1, names[i], true, i == current);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&dest),
                             [this, id] (int r) { if (r > 0) { setNativeValue (id, r - 1); touched (id); } });
        };
    }
    lfoAmount = &addKnob ("lfo1Amt", 340, 643, stdKnob);
    {
        auto& sel = addButton (407, 643, false);
        sel.litWhen = [] { return false; };
        sel.onClick = [this]
        {
            setNativeValue ("lfoSelect", 1 - nativeValue ("lfoSelect"));
            bindLfoControls();
            touched ("lfoSelect");
        };
    }
    lfoSelLeds[0] = &addLed (447, 626);
    lfoSelLeds[1] = &addLed (447, 643);
    label ("1", 461, 626, 8.0f, left);
    label ("2", 461, 643, 8.0f, left);
    label ("SYNC", 277, 573);
    label ("FREQ", 342, 573);
    label ("SHAPE", 408, 573);
    label ("DEST", 277, 677);
    label ("AMOUNT", 343, 677);
    label ("LFO", 408, 677);
    bindLfoControls();

    // ================= MOD =================
    // One button, level with SHAPE, opens the mod matrix (lit while it's open). No label.
    {
        auto& mod = addButton (533, 538, false);
        mod.onClick = [this] { if (onPanelAction) onPanelAction ("modmatrix"); };
        mod.litWhen = [this] { return isActionLit && isActionLit ("modmatrix"); };
        mod.setTooltip ("Mod matrix: opens and closes the drawer with all 16 modulation slots (source, amount, "
                        "destination). Tip: right-click a knob to modulate it");
    }
    // REVERT, under MOD and level with LFO (label level with LFO's): back to the program as it was loaded.
    {
        auto& revert = addButton (533, 643, false);
        revert.onClick = [this] { if (onPanelAction) onPanelAction ("revert"); };
        revert.setTooltip ("Revert: undo all edits and go back to the program as it was loaded or last saved "
                           "(sent to the TEO-5's edit buffer; nothing is written to memory)");
    }
    label ("REVERT", 533, 677);

    // ================= SETTINGS / OLED / MEMORY =================
    // GLOBAL and PROGRAM open their drawers (lit while open).
    for (auto [action, y] : { std::pair<const char*, float> { "global", 520.0f }, { "program", 585.0f } })
    {
        auto& b = addButton (620, y, false);
        b.onClick = [this, action] { if (onPanelAction) onPanelAction (action); };
        b.litWhen = [this, action] { return isActionLit && isActionLit (action); };
    }
    label ("GLOBAL", 620, 547);
    label ("PROGRAM", 620, 612);

    addAndMakeVisible (oled);
    oled.setBounds (juce::Rectangle<float> (X (667), Y (500), 133, 104).toNearestInt());

    // BANK and PATCH up/down buttons (replace the hardware's SELECT/VALUE encoders).
    // PATCH steps run on into the next/previous bank; BANK keeps the patch number.
    for (auto [x, bankStep, patchStep] : { std::tuple<float, int, int> { 648.0f, -1, 0 }, { 700.0f, 1, 0 },
                                           { 765.0f, 0, -1 }, { 817.0f, 0, 1 } })
        addButton (x, 643, false).onClick = [this, bankStep, patchStep] { stepProgram (bankStep, patchStep); };
    label ("<", 648, 675, 13.0f);
    label (">", 700, 675, 13.0f);
    label ("BANK", 674, 676);
    label ("<", 765, 675, 13.0f);
    label (">", 817, 675, 13.0f);
    label ("PATCH", 791, 676);

    {
        auto& compare = addButton (846, 520, false);
        compare.onClick = [this] { if (onPanelAction) onPanelAction ("compare"); };
        compare.litWhen = [this] { return isActionLit && isActionLit ("compare"); };
        compare.setTooltip ("Compare: toggle between your edit and the program as loaded");
    }
    addButton (846, 585, false).onClick = [this] { if (onPanelAction) onPanelAction ("write"); };
    oled.setTooltip ("Double-click to rename the program");
    label ("COMPARE", 846, 546);
    label ("WRITE", 846, 611);

    // ================= CLOCK =================
    addKnob ("clockBpm", 932, 537, stdKnob);
    addKnob ("clockDiv", 1020, 537, stdKnob);
    addButton (976, 641, true).onClick = [this] { if (onPanelAction) onPanelAction ("tap"); };
    label ("BPM", 933, 571);
    label ("CLK DIV", 1020, 571);
    label ("TAP TEMPO", 977, 675);

    // ================= ARPEGGIATOR =================
    addToggle ("arpOn", 1106, 537, false);
    addKnob ("arpRange", 1203, 535, stdKnob);
    addKnob ("arpMode", 1107, 640, stdKnob);
    addKnob ("arpRepeat", 1203, 640, stdKnob);
    for (int i = 0; i < 3; ++i)
    {
        arpOctLeds[i] = &addLed (1163, 554 - 17.5f * (float) i);
        arpRepLeds[i] = &addLed (1163, 658 - 17.5f * (float) i);
        label (juce::String (i + 1), 1150, 554 - 17.5f * (float) i, 7.4f);
        label (juce::String (i + 1), 1150, 658 - 17.5f * (float) i, 7.4f);
    }
    label ("ON", 1106, 571);
    label ("OCTAVE", 1202, 571);
    label ("MODE", 1107, 675);
    label ("REPEAT", 1202, 675);

    // ================= SEQ =================
    // RECORD is the sequencer drawer's REC (step entry; lit while on). The TEO-5's own RECORD stays on the synth.
    {
        auto& rec = addButton (1289, 537, false);
        rec.onClick = [this] { if (onPanelAction) onPanelAction ("record"); };
        rec.litWhen = [this] { return isActionLit && isActionLit ("record"); };
        rec.setTooltip ("Step entry: opens the sequencer with REC on");
    }
    addToggle ("seqPlay", 1289, 641, false);
    label ("RECORD", 1289, 571);
    label ("PLAY", 1289, 675);

    // ================= EFFECT / REVERB =================
    addToggle ("fxOn", 1376, 537, false);
    addKnob ("fxType", 1448, 537, stdKnob);
    addKnob ("fxTime", 1540, 537, stdKnob);
    addKnob ("fxMix", 1632, 537, stdKnob);
    addKnob ("fxMisc", 1722, 537, stdKnob);
    addToggle ("fxSync", 1807, 537, false);
    label ("ON", 1376, 571);
    label ("TYPE", 1445, 571);
    label ("TIME", 1536, 571);
    label ("DEPTH/MIX", 1627, 571);
    label ("FBACK/MISC", 1718, 571);
    label ("CLK SYNC", 1808, 571);

    addToggle ("revOn", 1376, 641, false);
    addKnob ("revMix", 1448, 640, stdKnob);
    addKnob ("revSize", 1540, 640, stdKnob);
    addKnob ("revPredelay", 1632, 640, stdKnob);
    addKnob ("revDecay", 1718, 640, stdKnob);
    addKnob ("revTone", 1810, 640, stdKnob);
    label ("ON", 1376, 675);
    label ("MIX", 1445, 675);
    label ("SIZE", 1536, 675);
    label ("PREDELAY", 1627, 675);
    label ("DECAY", 1718, 675);
    label ("TONE", 1810, 675);
    label ("Dry", 1418, 660, 5.6f);
    label ("Wet", 1476, 660, 5.6f);
    label ("LP", 1774, 660, 5.6f);
    label ("HP", 1846, 660, 5.6f);

    // ================= MISC =================
    addKnob ("vintage", 182, 765, stdKnob);
    addKnob ("osc1Glide", 182, 870, stdKnob); // TODO: verify - panel "Rate" may drive both osc glides
    label ("VINTAGE", 192, 800);
    label ("RATE", 192, 905);

    // ================= OSCILLATORS =================
    addKnob ("osc1Freq", 318, 765, stdKnob);
    addKnob ("osc1PW", 437, 765, stdKnob); // TODO: verify - single PW knob vs. per-osc NRPN 5/6
    addKnob ("osc2Freq", 560, 765, stdKnob);
    detuneLed = &addLed (443, 826);
    addToggle ("osc1Tri", 277, 868, false);
    addToggle ("osc1Saw", 322, 868, false);
    addToggle ("osc1Pulse", 365, 868, false);
    addKnob ("osc2Detune", 437, 870, stdKnob);
    addToggle ("osc2Tri", 521, 868, false);
    addToggle ("osc2Saw", 565, 868, false);
    addToggle ("osc2Pulse", 608, 868, false);
    label ("FREQUENCY 1", 322, 799);
    label ("PULSE WIDTH", 443, 799);
    label ("FREQUENCY 2", 566, 799);
    label ("OSC 2 DETUNE", 445, 903);

    // ================= OSC MOD =================
    addKnob ("xMod", 685, 765, stdKnob);
    addToggle ("oscSync", 688, 868, true);
    label ("X-MOD", 688, 798);
    label ("SYNC", 688, 902);

    // ================= FILTER =================
    addKnob ("resonance", 808, 765, stdKnob);
    addKnob ("cutoff", 938, 815, 122.0f, KnobStyle::Cutoff);
    addKnob ("filterState", 1068, 765, 52.0f);
    bpLed = &addLed (1068, 815);
    addToggle ("osc1On", 782, 868, true);
    addToggle ("subOn", 834, 868, true);
    addToggle ("osc2On", 1041, 868, true);
    addToggle ("noiseOn", 1094, 868, true);
    label ("RESONANCE", 808, 798);
    label ("NOTCH", 1068, 728, 7.0f);
    label ("LP", 1027, 779, 7.0f);
    label ("HP", 1110, 779, 7.0f);
    label ("STATE", 1068, 797);
    label ("BP", 1068, 831, 7.4f);
    label ("OSC 1", 783, 902);
    label ("SUB", 835, 902);
    label ("CUTOFF", 938, 901);
    label ("OSC 2", 1043, 901);
    label ("NOISE", 1094, 901);

    // ================= ENVELOPES =================
    const float envX[] = { 1315, 1420, 1525, 1630, 1736 };
    const char* env1Ids[] = { "env1Attack", "env1Decay", "env1Sustain", "env1Release", "env1Amt" };
    const char* env2Ids[] = { "env2Attack", "env2Decay", "env2Sustain", "env2Release", "env2Amt" };
    const char* envLabels[] = { "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "AMOUNT" };
    for (int i = 0; i < 5; ++i)
    {
        addKnob (env1Ids[i], envX[i], 763, stdKnob);
        addKnob (env2Ids[i], envX[i], 868, stdKnob);
        label (envLabels[i], envX[i] - 3, 796);
        label (envLabels[i], envX[i] - 3, 900);
    }
    addToggle ("env1Vel", 1805, 763, false);
    addToggle ("env2Vel", 1805, 866, false);
    label ("VELOCITY", 1805, 796);
    label ("VELOCITY", 1805, 900);

    label ("ENV 1", 1205, 727, 9.6f, left);
    label ("ENV 2", 1205, 831, 9.6f, left);
    env1Leds[0] = &addLed (1211, 745);
    env1Leds[1] = &addLed (1211, 762);
    env2Leds[0] = &addLed (1211, 848);
    env2Leds[1] = &addLed (1211, 865);
    env2Leds[2] = &addLed (1211, 883);
    label ("Filter", 1221, 745, 7.8f, left);
    label ("Aux", 1221, 762, 7.8f, left);
    label ("Amp", 1221, 848, 7.8f, left);
    label ("Filter+Amp", 1221, 865, 7.8f, left);
    label ("Filter+Gate", 1221, 883, 7.8f, left);
    addCycle ("envRoute", 1181, 901, false); // the ENV 1 / ENV 2 routing button, the size of the others

    // ================= Bottom strip =================
    addToggle ("glideOn", 192, 955, true);
    addToggle ("unisonOn", 323, 955, true);
    addToggle ("hold", 455, 955, true);
    // LIBRARY opens the library drawer; FACTORY sits next to BANK (MANUAL was removed: the TEO-5's Live Panel
    // mode has no MIDI message, and the editor's one-shot version wasn't useful).
    auto& libraryButton = addButton (552, 955, true);
    libraryButton.onClick = [this] { if (onPanelAction) onPanelAction ("library"); };
    libraryButton.litWhen = [this] { return isActionLit && isActionLit ("library"); };
    factoryButton = &addButton (624, 955, true);
    factoryButton->onClick = [this] { factoryBanks = ! factoryBanks; selectProgram (program - 1); };
    factoryButton->litWhen = [this] { return factoryBanks; };
    bankButton = &addButton (688, 955, true);
    bankButton->onClick = [this] { bankSelectMode = ! bankSelectMode; repaint(); };
    bankButton->litWhen = [this] { return bankSelectMode; };
    label ("GLIDE", 193, 991);
    label ("UNISON", 324, 990);
    label ("HOLD", 455, 990);
    label ("LIBRARY", 552, 990);
    label ("FACTORY", 624, 990);
    label ("BANK", 688, 988);

    addAndMakeVisible (sevenSeg);
    sevenSeg.setBounds (juce::Rectangle<float> (X (726), Y (940), 80, 28).toNearestInt());

    const float progX[] = { 834, 886, 938, 990, 1051, 1103, 1155, 1207, 1268, 1320, 1372, 1424, 1484, 1536, 1588, 1640 };
    for (int i = 0; i < 16; ++i)
    {
        auto& pb = addButton (progX[i], 954, false);
        programButtons[i] = &pb;
        pb.litWhen = [this, i] { return bankSelectMode ? (bank - 1 == i) : (program - 1 == i); };
        pb.onClick = [this, i] { selectProgram (i); };
    }

    // PANIC: all notes and sounds off, against hung notes. Left of INIT, styled like it.
    addButton (1742, 954, false).onClick = [this] { if (onPanelAction) onPanelAction ("panic"); };
    label ("PANIC", 1742, 987);

    // INIT: loads the Basic Program into the edit buffer. In line with the program buttons, under VELOCITY.
    addButton (1805, 954, false).onClick = [this] { if (onPanelAction) onPanelAction ("init"); };
    label ("INIT", 1805, 987);

    selectProgram (program - 1);
    refreshIndicators();
    startTimerHz (15);
}

PanelComponent::~PanelComponent()
{
    stopTimer();
    lfoFreqAtt.reset();
    lfoAmtAtt.reset();
    lfoSyncAtt.reset();
    sliderAttachments.clear();
    buttonAttachments.clear();
    setLookAndFeel (nullptr);
}

// ---- Control factories ----------------------------------------------------------------------

Knob& PanelComponent::addKnob (const juce::String& paramId, float x, float y, float diameter, KnobStyle style)
{
    auto* k = knobs.add (new Knob (style));
    k->setBounds (juce::Rectangle<float> (diameter, diameter).withCentre ({ X (x), Y (y) }).toNearestInt());
    addAndMakeVisible (k);

    if (paramId.isNotEmpty() && param (paramId) != nullptr)
    {
        if (paramId != "lfo1Freq" && paramId != "lfo1Amt") // LFO knobs are bound in bindLfoControls()
            sliderAttachments.add (new juce::AudioProcessorValueTreeState::SliderAttachment (apvts, paramId, *k));

        if (auto* def = findById (paramId))
        {
            const int centre = (def->minValue + def->maxValue + 1) / 2;
            const int fallback = def->kind == ParamKind::Bipolar ? centre : def->defaultValue;
            k->setDoubleClickReturnValue (false, 0.0);
            k->resetValue = [this, k, fallback]() -> std::optional<double>
            {
                // The value stored in the program (as loaded), else the parameter's default.
                const auto id = k->getProperties()["paramId"].toString();
                if (storedValue)
                    if (auto v = storedValue (id))
                        return (double) *v;
                if (auto* d = findById (id))
                    return (double) (id.startsWith ("lfo") ? d->defaultValue : fallback);
                return std::nullopt;
            };
            k->textFromValueFunction = [def] (double v) { return valueToText (*def, juce::roundToInt (v)); };
        }
        k->onTouched = [this, k]
        {
            touched (k->getProperties()["paramId"].toString());
        };
        k->onContextMenu = [this, k]
        {
            if (onControlMenu)
                onControlMenu (k->getProperties()["paramId"].toString());
        };
        k->getProperties().set ("paramId", paramId);
    }
    return *k;
}

PanelButton& PanelComponent::addButton (float x, float y, bool wide)
{
    auto* b = buttons.add (new PanelButton());
    const float w = (wide ? 40.0f : squareBtn) + buttonPad * 2;
    const float h = (wide ? 22.0f : squareBtn) + buttonPad * 2;
    b->setBounds (juce::Rectangle<float> (w, h).withCentre ({ X (x), Y (y) }).toNearestInt());
    addAndMakeVisible (b);
    return *b;
}

PanelButton& PanelComponent::addToggle (const juce::String& paramId, float x, float y, bool wide)
{
    auto& b = addButton (x, y, wide);
    b.setClickingTogglesState (true);
    if (param (paramId) != nullptr)
    {
        buttonAttachments.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (apvts, paramId, b));
        b.onUserClick = [this, paramId] { touched (paramId); };
        b.onContextMenu = [this, paramId] { if (onControlMenu) onControlMenu (paramId); };
    }
    return b;
}

PanelButton& PanelComponent::addCycle (const juce::String& paramId, float x, float y, bool wide)
{
    auto& b = addButton (x, y, wide);
    b.litWhen = [] { return false; };
    b.onClick = [this, paramId]
    {
        if (auto* def = findById (paramId))
        {
            const int span = def->maxValue - def->minValue + 1;
            setNativeValue (paramId, def->minValue + (nativeValue (paramId) - def->minValue + 1) % span);
            touched (paramId);
        }
    };
    return b;
}

Led& PanelComponent::addLed (float x, float y)
{
    auto* l = leds.add (new Led());
    l->setBounds (juce::Rectangle<float> (ledSize, ledSize).withCentre ({ X (x), Y (y) }).toNearestInt());
    addAndMakeVisible (l);
    return *l;
}

void PanelComponent::label (const juce::String& text, float x, float y, float size, juce::Justification just)
{
    texts.add ({ text, X (x), Y (y), size, just });
}

// ---- Parameter helpers ----------------------------------------------------------------------

int PanelComponent::nativeValue (const juce::String& id) const
{
    if (auto* p = dynamic_cast<juce::AudioParameterInt*> (param (id)))
        return p->get();
    return 0;
}

void PanelComponent::setNativeValue (const juce::String& id, int value)
{
    if (auto* p = dynamic_cast<juce::AudioParameterInt*> (param (id)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) value));
        p->endChangeGesture();
    }
    refreshIndicators();
}

void PanelComponent::touched (const juce::String& paramId)
{
    if (auto* def = findById (paramId))
    {
        // The value as you change it, then (in parentheses) the value stored in the loaded program.
        juce::String stored;
        if (storedValue)
            if (auto v = storedValue (paramId))
                stored = valueToText (*def, *v);
        oled.showParameter (def->name, valueToText (*def, nativeValue (paramId)), stored);
    }
}

void PanelComponent::bindLfoControls()
{
    const int which = nativeValue ("lfoSelect");
    if (which == boundLfo)
        return;
    boundLfo = which;

    const juce::String n (which + 1);
    lfoFreqAtt.reset();
    lfoAmtAtt.reset();
    lfoSyncAtt.reset();
    lfoFreqAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "lfo" + n + "Freq", *lfoFreq);
    lfoAmtAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "lfo" + n + "Amt", *lfoAmount);
    lfoSyncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, "lfo" + n + "Sync", *lfoSync);

    lfoFreq->getProperties().set ("paramId", "lfo" + n + "Freq");
    lfoAmount->getProperties().set ("paramId", "lfo" + n + "Amt");
    lfoSync->onUserClick = [this, n] { touched ("lfo" + n + "Sync"); };

    for (auto [knob, suffix] : { std::pair<Knob*, const char*> { lfoFreq, "Freq" }, { lfoAmount, "Amt" } })
        if (auto* def = findById ("lfo" + n + suffix))
        {
            knob->textFromValueFunction = [def] (double v) { return valueToText (*def, juce::roundToInt (v)); };
        }
}

void PanelComponent::selectProgram (int index)
{
    if (bankSelectMode)
    {
        bank = index + 1;
        bankSelectMode = false;
    }
    else
    {
        program = index + 1;
    }
    updateProgramDisplay();
    if (onPanelAction)
        onPanelAction ("programChange");
}

void PanelComponent::stepProgram (int bankDelta, int patchDelta)
{
    // Banks 1-16 x patches 1-16, wrapping at both ends.
    int index = (bank - 1) * 16 + (program - 1) + bankDelta * 16 + patchDelta;
    index = (index % 256 + 256) % 256;
    bank = index / 16 + 1;
    program = index % 16 + 1;
    bankSelectMode = false;
    updateProgramDisplay();
    if (onPanelAction)
        onPanelAction ("programChange");
}

void PanelComponent::stepThroughAllBanks (int bankDelta, int patchDelta)
{
    int index = ((factoryBanks ? 16 : 0) + bank - 1) * 16 + (program - 1) + bankDelta * 16 + patchDelta;
    index = (index % 512 + 512) % 512; // 32 banks x 16 programs, wrapping at both ends
    factoryBanks = index / 256 > 0;
    bank = (index / 16) % 16 + 1;
    program = index % 16 + 1;
    bankSelectMode = false;
    updateProgramDisplay();
    repaint();
    if (onPanelAction)
        onPanelAction ("programChange");
}

void PanelComponent::goTo (int newBank, int newProgram, bool factory)
{
    factoryBanks = factory;
    bank = juce::jlimit (1, 16, newBank);
    program = juce::jlimit (1, 16, newProgram);
    bankSelectMode = false;
    updateProgramDisplay();
    repaint();
    if (onPanelAction)
        onPanelAction ("programChange");
}

void PanelComponent::showProgram (bool factory)
{
    factoryBanks = factory;
    bankSelectMode = false;
    updateProgramDisplay();
}

void PanelComponent::updateProgramDisplay()
{
    // Bank digit + two-digit program, e.g. "101". Banks 10-16 show as A-G (as printed on the panel).
    const juce::String bankChar = bank <= 9 ? juce::String (bank) : juce::String::charToString ((juce::juce_wchar) ('A' + bank - 10));
    sevenSeg.setText (bankChar + juce::String (program).paddedLeft ('0', 2));
    repaint();
}

void PanelComponent::timerCallback()
{
    bindLfoControls(); // follows host automation / state recall of lfoSelect
    refreshIndicators();
    oled.refreshFavourite(); // the library may have changed it
}

void PanelComponent::refreshIndicators()
{
    const int lfo = nativeValue ("lfoSelect");
    const int shape = nativeValue (lfo == 0 ? "lfo1Shape" : "lfo2Shape");
    for (int i = 0; i < 5; ++i)
        lfoShapeLeds[i]->setLit (i == shape);
    lfoSelLeds[0]->setLit (lfo == 0);
    lfoSelLeds[1]->setLit (lfo == 1);

    const int oct = nativeValue ("arpRange");
    const int rep = nativeValue ("arpRepeat");
    for (int i = 0; i < 3; ++i)
    {
        arpOctLeds[i]->setLit (i == oct);
        arpRepLeds[i]->setLit (rep == i + 1);
    }

    detuneLed->setLit (nativeValue ("osc2Detune") != 32);
    bpLed->setLit (nativeValue ("filterBP") != 0);

    const int route = nativeValue ("envRoute");
    env1Leds[0]->setLit (route == 0);
    env1Leds[1]->setLit (route != 0);
    for (int i = 0; i < 3; ++i)
        env2Leds[i]->setLit (route == i);

    OledDisplay::HomePage home;
    home.bpm = juce::String (nativeValue ("clockBpm"));
    if (auto ext = externalBpm ? externalBpm() : std::nullopt)
    {
        home.bpm = std::abs (*ext - std::round (*ext)) < 0.05 ? juce::String (juce::roundToInt (*ext)) : juce::String (*ext, 1);
        home.bpmLabel = "EXT BPM";
    }
    home.clkDiv = valueToText (*findById ("clockDiv"), nativeValue ("clockDiv"));
    home.name = programName;
    home.arpMode = valueToText (*findById ("arpMode"), nativeValue ("arpMode"));
    home.effect = valueToText (*findById ("fxType"), nativeValue ("fxType"));
    home.edited = isEdited && isEdited();
    const auto& cur = oled.getHome();
    if (home.bpm != cur.bpm || home.bpmLabel != cur.bpmLabel || home.clkDiv != cur.clkDiv || home.name != cur.name
        || home.arpMode != cur.arpMode || home.effect != cur.effect || home.edited != cur.edited)
        oled.setHome (home);

    for (auto* b : buttons)
        if (b->litWhen)
            b->repaint();
}

// ---- Painting -------------------------------------------------------------------------------

void PanelComponent::paint (juce::Graphics& g)
{
    paintBackground (g);
    paintPinstripes (g);
    paintGlyphs (g);
    paintSilkscreen (g);
}

void PanelComponent::paintBackground (juce::Graphics& g)
{
    using namespace juce;
    const float w = (float) designWidth, h = (float) designHeight;
    const float cheekW = 17.0f;
    const float faceTop = 5.0f, lipTop = Y (1013), lipBottom = Y (1031);

    g.fillAll (Colour (0xff000000));

    // Panel face.
    auto face = Rectangle<float> (cheekW, faceTop, w - cheekW * 2, lipTop - faceTop);
    {
        ColourGradient fg (Colour (0xff2e2f33), 0, face.getY(), Colour (0xff26272a), 0, face.getBottom(), false);
        g.setGradientFill (fg);
        g.fillRect (face);
        g.setTiledImageFill (noiseTile, 0, 0, 1.0f);
        g.fillRect (face);
        g.setColour (Colour (0x30ffffff));
        g.drawHorizontalLine ((int) face.getY(), face.getX(), face.getRight()); // top edge catchlight
    }

    // Front lip with embossed logos.
    auto lip = Rectangle<float> (cheekW, lipTop, w - cheekW * 2, lipBottom - lipTop);
    {
        ColourGradient lg (Colour (0xff222326), 0, lip.getY(), Colour (0xff141517), 0, lip.getBottom(), false);
        g.setGradientFill (lg);
        g.fillRect (lip);
        g.setColour (Colour (0x50000000));
        g.drawHorizontalLine ((int) lip.getY(), lip.getX(), lip.getRight());

        auto emboss = [&] (const String& text, Rectangle<float> r, Font f, Justification j)
        {
            g.setFont (f);
            g.setColour (Colour (0x40000000));
            g.drawText (text, r.translated (0.8f, 0.8f), j);
            g.setColour (Colour (0x1cffffff));
            g.drawText (text, r, j);
        };
        emboss ("TEO-5", { X (185), lip.getY() - 2.0f, 260, lip.getHeight() + 4 },
                TEO5LookAndFeel::panelFont (20.0f).withHorizontalScale (1.25f), Justification::centredLeft);
        emboss ("Oberheim", { X (1400), lip.getY() - 4.0f, 460, lip.getHeight() + 8 },
                Font (FontOptions ("Times New Roman", 27.0f, Font::bold | Font::italic)).withHorizontalScale (1.35f),
                Justification::centredRight);
    }

    // Anodised end cheeks.
    for (float x0 : { 0.0f, w - cheekW })
    {
        auto cheek = Rectangle<float> (x0, 0, cheekW, h);
        ColourGradient cg (Colour (0xff1c1c1e), cheek.getX(), 0, Colour (0xff050506), cheek.getRight(), 0, false);
        cg.addColour (0.35, Colour (0xff141416));
        g.setGradientFill (cg);
        g.fillRect (cheek);
        g.setColour (Colour (0x40ffffff));
        g.drawHorizontalLine (0, cheek.getX(), cheek.getRight());
        g.setColour (Colour (0x20ffffff));
        g.drawVerticalLine ((int) (x0 == 0 ? cheek.getRight() - 1 : cheek.getX()), 0, h);
        g.setColour (Colour (0x60000000));
        g.drawVerticalLine ((int) (x0 == 0 ? cheek.getX() : cheek.getRight() - 1), 0, h);
    }
}

void PanelComponent::paintPinstripes (juce::Graphics& g)
{
    using namespace juce;
    auto stripe = [&] (float x0, float x1, float y, float thickness = 1.1f)
    {
        g.setColour (Palette::pinstripe.withAlpha (0.82f));
        g.fillRect (Rectangle<float> (X (x0), Y (y) - thickness * 0.5f, x1 - x0, thickness));
    };
    auto block = [&] (float x0, float x1, float top, int count, int firstLine = 0)
    {
        for (int k = firstLine; k < count; ++k)
            stripe (x0, x1, top + pitch * (float) k, k == 0 ? 1.5f : 1.1f);
    };

    // Top row sections.
    const float topY = 492.0f;
    block (147, 233, topY, 12);
    block (250, 486, topY, 12);
    block (503, 565, topY, 12);
    block (903, 1050, topY, 12);
    block (1068, 1240, topY, 12);
    block (1258, 1320, topY, 12);
    block (1338, 1852, topY, 12);

    // Lower sections.
    const float lowY = 720.0f;
    block (147, 233, lowY, 12);
    block (250, 635, lowY, 12);
    block (643, 733, lowY, 12);
    block (742, 1135, lowY, 12);
    stripe (1145, 1852, lowY, 1.5f);
    block (1145, 1852, lowY, 12, 1); // under ENV 1 / ENV 2 too, as on the synth

    // Bottom strip.
    for (int k = 1; k <= 3; ++k)
    {
        stripe (147, 233, 920.0f + pitch * (float) k);
        stripe (250, 1852, 920.0f + pitch * (float) k);
    }
}

void PanelComponent::paintSilkscreen (juce::Graphics& g)
{
    using namespace juce;

    // Section headers sit on a patch of panel colour so the pinstripes break around them.
    struct Header { const char* text; float x, y; };
    const Header headers[] = {
        { "MASTER", 190, 481 }, { "LFO 1: MONO  |  LFO 2: POLY", 367, 481 }, { "MOD", 533, 480 },
        { "SETTINGS", 620, 479 }, { "MEMORY", 846, 479 }, { "CLOCK", 976, 478 }, { "ARPEGGIATOR", 1155, 478 },
        { "SEQ", 1289, 478 }, { "EFFECT", 1594, 478 }, { "REVERB", 1594, 599 }, { "MISC", 190, 707 },
        { "OSCILLATORS", 443, 706 }, { "OSC MOD", 688, 706 }, { "FILTER", 938, 705 }, { "ENVELOPES", 1498, 704 }
    };
    for (auto& h : headers)
    {
        const bool isLfo = String (h.text).startsWith ("LFO");
        auto f = TEO5LookAndFeel::panelFont (isLfo ? 12.8f : 14.8f);
        const float tw = GlyphArrangement::getStringWidth (f, h.text);
        auto r = Rectangle<float> (tw + 14.0f, 18.0f).withCentre ({ X (h.x), Y (h.y) });
        if (String (h.text) == "REVERB")
        {
            g.setColour (Palette::panel);
            g.fillRect (r);
        }
        g.setFont (f);
        g.setColour (Palette::silkscreen);
        g.drawText (h.text, r, Justification::centred, false);
    }

    for (auto& t : texts)
    {
        auto f = TEO5LookAndFeel::panelFont (t.size);
        g.setFont (f);
        g.setColour (Palette::silkscreen.withAlpha (0.92f));
        const float tw = GlyphArrangement::getStringWidth (f, t.text) + 4.0f;
        auto r = t.just == Justification::centredLeft ? Rectangle<float> (t.x, t.y - t.size, tw, t.size * 2.0f)
                                                       : Rectangle<float> (tw, t.size * 2.0f).withCentre ({ t.x, t.y });
        g.drawText (t.text, r, t.just, false);
    }

    // Version, small in the bottom-right corner.
    g.setFont (TEO5LookAndFeel::panelFont (7.0f));
    g.setColour (Palette::silkscreen.withAlpha (0.45f));
    g.drawText ("v" TEO5_VERSION, Rectangle<float> ((float) getWidth() - 120.0f, (float) getHeight() - 14.0f, 92.0f, 12.0f),
                Justification::centredRight, false);

    // Program numbers along the bank line.
    const float progX[] = { 834, 886, 938, 990, 1051, 1103, 1155, 1207, 1268, 1320, 1372, 1424, 1484, 1536, 1588, 1640 };
    const char* suffix = "ABCDEFG";
    auto numFont = TEO5LookAndFeel::panelFont (8.4f);
    auto subFont = TEO5LookAndFeel::panelFont (5.4f);
    float prevRight = X (716);
    for (int i = 0; i < 16; ++i)
    {
        const String num (i + 1);
        const float cx = X (progX[i]);
        const float nw = GlyphArrangement::getStringWidth (numFont, num);
        const float sw = i >= 9 ? GlyphArrangement::getStringWidth (subFont, String::charToString (suffix[i - 9])) : 0.0f;
        const float total = nw + sw;
        const float x0 = cx - total * 0.5f;

        g.setColour (Palette::silkscreen.withAlpha (0.6f));
        g.fillRect (Rectangle<float> (prevRight + 3.0f, Y (988) - 0.5f, x0 - prevRight - 6.0f, 1.0f));
        prevRight = x0 + total;

        g.setColour (Palette::silkscreen.withAlpha (0.92f));
        g.setFont (numFont);
        g.drawText (num, Rectangle<float> (x0, Y (988) - 8, nw + 2, 16), Justification::centredLeft, false);
        if (i >= 9)
        {
            g.setFont (subFont);
            g.drawText (String::charToString (suffix[i - 9]), Rectangle<float> (x0 + nw, Y (990) - 4, sw + 2, 10),
                        Justification::centredLeft, false);
        }
    }
}

void PanelComponent::paintGlyphs (juce::Graphics& g)
{
    using namespace juce;
    const auto ink = Palette::silkscreen.withAlpha (0.9f);
    g.setColour (ink);

    // Waveform icons: 0 = triangle, 1 = saw, 2 = pulse/square, 3 = reverse saw.
    auto wave = [&] (int type, float cx, float cy, float w, float h)
    {
        Path p;
        const float x0 = X (cx) - w * 0.5f, y0 = Y (cy) - h * 0.5f;
        switch (type)
        {
            case 0: p.startNewSubPath (x0, y0 + h); p.lineTo (x0 + w * 0.25f, y0); p.lineTo (x0 + w * 0.5f, y0 + h);
                    p.lineTo (x0 + w * 0.75f, y0); p.lineTo (x0 + w, y0 + h); break;
            case 1: p.startNewSubPath (x0, y0 + h); p.lineTo (x0 + w * 0.45f, y0); p.lineTo (x0 + w * 0.45f, y0 + h);
                    p.lineTo (x0 + w * 0.9f, y0); p.lineTo (x0 + w * 0.9f, y0 + h); break;
            case 3: p.startNewSubPath (x0, y0); p.lineTo (x0, y0 + h); p.lineTo (x0 + w * 0.45f, y0);
                    p.lineTo (x0 + w * 0.45f, y0 + h); p.lineTo (x0 + w * 0.9f, y0); break;
            default: p.startNewSubPath (x0, y0 + h); p.lineTo (x0, y0); p.lineTo (x0 + w * 0.3f, y0);
                     p.lineTo (x0 + w * 0.3f, y0 + h); p.lineTo (x0 + w * 0.6f, y0 + h); p.lineTo (x0 + w * 0.6f, y0);
                     p.lineTo (x0 + w * 0.9f, y0); p.lineTo (x0 + w * 0.9f, y0 + h); break;
        }
        g.strokePath (p, PathStrokeType (1.1f));
    };

    // LFO shapes: triangle, saw, reverse saw, square (S/H is text).
    wave (0, 470, 521, 15, 6);
    wave (1, 470, 538, 15, 6);
    wave (3, 470, 555, 15, 6);
    wave (2, 470, 573, 15, 6);

    // Oscillator waveform legends.
    for (auto [x, t] : { std::pair<float, int> { 279, 0 }, { 322, 1 }, { 366, 2 }, { 522, 0 }, { 566, 1 }, { 609, 2 } })
        wave (t, x, 903, 17, 6);

    // Curved bracket arcs around bipolar knobs.
    auto bracket = [&] (float cx, float cy, float r, const String& lo, const String& hi, float textSize)
    {
        const Point<float> c (X (cx), Y (cy));
        Path p;
        p.addCentredArc (c.x, c.y, r, r, 0.0f, -2.35f, -0.85f, true);
        p.addCentredArc (c.x, c.y, r, r, 0.0f, 0.85f, 2.35f, true);
        g.strokePath (p, PathStrokeType (0.9f));
        // little end ticks
        for (float a : { -2.35f, 2.35f })
            g.drawLine (Line<float> (c.getPointOnCircumference (r, a), c.getPointOnCircumference (r - 4.0f, a)), 1.2f);
        if (lo.isNotEmpty())
        {
            g.setFont (TEO5LookAndFeel::panelFont (textSize));
            g.drawText (lo, Rectangle<float> (20, 12).withCentre (c.getPointOnCircumference (r + 7.0f, -2.2f)), Justification::centred);
            g.drawText (hi, Rectangle<float> (20, 12).withCentre (c.getPointOnCircumference (r + 7.0f, 2.2f)), Justification::centred);
        }
    };
    bracket (437, 868, 32, juce::String::fromUTF8 ("\xe2\x80\x93"), "+", 9.0f); // Osc 2 Detune
    bracket (1736, 763, 32, juce::String::fromUTF8 ("\xe2\x80\x93"), "+", 9.0f); // Env 1 Amount
    bracket (1810, 640, 32, {}, {}, 0);                                          // Reverb Tone (LP/HP labels separate)
    bracket (1068, 765, 33, {}, {}, 0);                                          // Filter State

    // Cutoff tick marks.
    {
        const Point<float> c (X (938), Y (815));
        const float start = MathConstants<float>::pi * 7.0f / 6.0f, end = MathConstants<float>::pi * 17.0f / 6.0f;
        for (int i = 0; i <= 10; ++i)
        {
            const float a = start + (end - start) * (float) i / 10.0f;
            g.drawLine (Line<float> (c.getPointOnCircumference (64.0f, a), c.getPointOnCircumference (71.0f, a)), 1.3f);
        }
    }

    // ENV 1 / ENV 2 selector bracket.
    {
        const float x = X (1182);
        g.fillRect (Rectangle<float> (x, Y (727), 1.2f, Y (884) - Y (727)));
        g.fillRect (Rectangle<float> (x, Y (727), 20.0f, 1.2f));
        g.fillRect (Rectangle<float> (x, Y (831), 20.0f, 1.2f));
    }
}

} // namespace teo5
