#pragma once

#include <juce_core/juce_core.h>
#include <map>
#include <optional>
#include <vector>

namespace teo5
{

// Where each parameter lives inside the unpacked program.
// The MIDI Implementation doesn't document this. Measured on a TEO-5: each program parameter is stored at
// the byte matching its NRPN number (two bytes, little-endian, for Cutoff and Filter State), and the name
// takes bytes 159-178. builtIn() encodes that. The layout probe (see MidiSync) can still re-measure it;
// its results are saved as JSON in the user folder.
class ProgramLayout
{
public:
    struct Field
    {
        int offset = -1;
        int width = 1;          // bytes
        bool littleEndian = true;
    };

    static juce::File defaultFile(); // TEO5_LAYOUT_FILE overrides it (tests)

    // The measured TEO-5 layout (see above).
    static ProgramLayout builtIn();
    // Saved layouts from before this version came from a probe that could mistake one waveform switch for
    // another; only probe results at least this version are used.
    static constexpr int currentFieldsVersion = 2;

    bool load (const juce::File& = defaultFile());
    bool save (const juce::File& = defaultFile()) const;

    bool isEmpty() const { return fields.empty(); }
    int size() const { return (int) fields.size(); }

    void setField (const juce::String& paramId, Field f) { fields[paramId] = f; }
    const Field* field (const juce::String& paramId) const;

    int read (const juce::MemoryBlock& program, const Field&) const;
    void write (juce::MemoryBlock& program, const Field&, int value) const;

    // Program name (offset found by searching a dump for the name the user types in).
    int nameOffset = -1;
    int nameLength = 20;
    // Which detector found the name field. Fields from older (less strict) detectors are discarded on load.
    int nameVersion = 0;
    static constexpr int currentNameVersion = 2;
    int fieldsVersion = 0; // see currentFieldsVersion
    juce::String readName (const juce::MemoryBlock& program) const;
    void writeName (juce::MemoryBlock& program, const juce::String& name) const;

    // Find the program-name field from several different programs (e.g. a fetched bank): the offset where every
    // program has a run of readable text and the texts mostly differ. Returns {offset, length}.
    static std::optional<std::pair<int, int>> detectNameField (const std::vector<juce::MemoryBlock>& programs);

    // Infer a field from dumps that differ only in one parameter, each paired with the value it was set to.
    // Use at least three distinct values: two can be ambiguous when a byte of the field stays zero.
    using Sample = std::pair<juce::MemoryBlock, int>;
    // A field at `preferredOffset` (if it fits all samples) wins over other bytes that happen to change too.
    static std::optional<Field> infer (const std::vector<Sample>& samples, int preferredOffset = -1);

    const std::map<juce::String, Field>& allFields() const { return fields; }

private:
    std::map<juce::String, Field> fields;
};

} // namespace teo5
