#include "SyxIO.h"
#include "../Midi/SysEx.h"
#include "../Midi/MidiSync.h"

namespace teo5::SyxIO
{

std::vector<Item> parse (const void* data, size_t size)
{
    std::vector<Item> items;
    auto* d = static_cast<const juce::uint8*> (data);

    for (size_t i = 0; i < size; ++i)
    {
        if (d[i] != 0xf0)
            continue;
        size_t end = i + 1;
        while (end < size && d[end] != 0xf7 && d[end] != 0xf0)
            ++end;
        if (end >= size || d[end] != 0xf7)
            continue; // truncated message: skip to the next F0

        auto parsed = SysEx::parse (juce::MidiMessage (d + i, (int) (end - i + 1)));
        using T = SysEx::Parsed::Type;
        if ((parsed.type == T::Program || parsed.type == T::EditBuffer) && SysEx::isProgramSize (parsed.data.getSize()))
            items.push_back ({ parsed.data, parsed.type == T::EditBuffer, parsed.bank, parsed.program });
        i = end;
    }
    return items;
}

std::vector<Item> importFile (const juce::File& file)
{
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data))
        return {};
    return parse (data.getData(), data.getSize());
}

static bool writeMessages (const juce::File& file, const juce::Array<juce::MidiMessage>& messages)
{
    juce::MemoryBlock out;
    for (auto& m : messages)
        out.append (m.getRawData(), (size_t) m.getRawDataSize());
    return file.replaceWithData (out.getData(), out.getSize());
}

bool exportProgram (const juce::File& file, const juce::MemoryBlock& raw)
{
    return writeMessages (file, { SysEx::editBufferDump (raw) });
}

bool exportBank (const juce::File& file, const std::vector<juce::MemoryBlock>& programs, int panelBank, bool factory)
{
    juce::Array<juce::MidiMessage> messages;
    for (size_t i = 0; i < programs.size(); ++i)
    {
        const auto a = MidiSync::addressFor (panelBank, (int) i + 1, factory);
        messages.add (SysEx::programDump (a.dumpBank, a.dumpProgram, programs[i]));
    }
    return writeMessages (file, messages);
}

} // namespace teo5::SyxIO
