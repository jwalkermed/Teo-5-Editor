#pragma once

#include <juce_core/juce_core.h>

// Single source of truth for every TEO-5 parameter.
// Numbers and ranges come from "TEO-5 MIDI Implementation" (docs/TEO5-MIDI-Implementation.txt).
// Anything that could not be confirmed from that document is flagged `verify = true`
// and listed in VERIFY.md.

namespace teo5
{

enum class ParamKind
{
    Continuous, // knob, 0..max
    Bipolar,    // knob with a centre detent
    Switch,     // 0/1 button
    Choice      // discrete list (stepped knob, LED selector or popup)
};

enum class Scope
{
    Program, // saved with the program, sent via NRPN 0..4095
    Global,  // global settings, NRPN 4096+
    Control, // transmitted but not stored (e.g. Seq Play)
    Panel    // panel-only / plugin-side state, no direct MIDI number
};

struct ParamDef
{
    const char* id;      // APVTS parameter ID (stable, used in saved state)
    const char* name;    // human-readable name (DAW + OLED)
    int nrpn;            // -1 if none
    int cc;              // -1 if none
    int minValue;
    int maxValue;
    int defaultValue;
    ParamKind kind;
    Scope scope;
    const char* choices; // '|' separated value names for Choice/Switch, or nullptr
    bool verify;         // true -> see VERIFY.md
};

const juce::Array<ParamDef>& allParams();
const ParamDef* findById (const juce::String& id);
const ParamDef* findByNrpn (int nrpn);
const ParamDef* findByCc (int cc);

// Readable value text (used by APVTS and the OLED).
juce::String valueToText (const ParamDef& def, int value);
juce::StringArray choiceNames (const ParamDef& def);

// Shared value lists, also used by the mod-matrix popup.
const juce::StringArray& modSourceNames();      // Appendix A, 0..19
const juce::StringArray& modDestinationNames(); // Appendix B, 0..64

} // namespace teo5
