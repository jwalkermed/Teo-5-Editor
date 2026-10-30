// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Library/LibraryModel.h"

namespace teo5
{

// Asks where to store a program in TEO-5 memory: a User bank (1-16) and program slot (1-16), starting at
// bank/slot. Factory locations aren't offered (the synth's factory programs can't be changed). The dialog names
// the program the library last fetched from the chosen slot, so you can see what will be overwritten.
// `chosen` runs only if the user confirms.
void chooseWriteLocation (juce::Component* parent, const juce::String& programName, int bank, int slot,
                          const LibraryModel& library, std::function<void (int bank, int slot)> chosen);

// The name of the program the library holds for a User location ("" if that bank hasn't been fetched).
juce::String libraryNameAt (const LibraryModel&, int bank, int slot);

} // namespace teo5
