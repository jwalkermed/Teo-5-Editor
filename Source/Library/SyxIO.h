#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace teo5::SyxIO
{
    struct Item
    {
        juce::MemoryBlock raw;      // unpacked 4096-byte program
        bool fromEditBuffer = false;
        int dumpBank = -1, dumpProgram = -1; // program dumps only
    };

    // Splits arbitrary SysEx data into F0..F7 messages and returns every TEO-5 program / edit-buffer dump.
    std::vector<Item> parse (const void* data, size_t size);
    std::vector<Item> importFile (const juce::File&);

    // Single program as an edit-buffer dump (loads into whatever location is current).
    bool exportProgram (const juce::File&, const juce::MemoryBlock& raw);
    // Bank: one program dump per program, addressed to panel bank `panelBank` (programs 1..n).
    bool exportBank (const juce::File&, const std::vector<juce::MemoryBlock>& programs, int panelBank, bool factory);
}
