// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>

namespace teo5
{

// Left-side drawer showing all 16 mod matrix slots: source, bipolar amount and destination,
// bound to the mod{n}Src / mod{n}Amt / mod{n}Dest parameters (so edits go out over NRPN like any knob).
class ModMatrixDrawer : public juce::Component,
                        private juce::Timer
{
public:
    explicit ModMatrixDrawer (juce::AudioProcessorValueTreeState&);

    // A parameter's value in the program as loaded (double-click on an amount returns to it).
    std::function<std::optional<int> (const juce::String& paramId)> storedValue;
    ~ModMatrixDrawer() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    // Slots by what they hold (source/destination as indices into modSourceNames / modDestinationNames).
    int firstFreeSlot() const;                        // 1-16: no source and no destination; 0 if all are in use
    juce::Array<int> slotsFor (int destination) const; // assigned slots modulating this destination
    int sourceOf (int slot) const;
    int amountOf (int slot) const;                    // -127..127
    // Fills the first free slot (amount none, so nothing changes yet); returns it, or 0 if all are in use.
    int assign (int source, int destination);
    // Outlines the slot and puts the message in the hint line until the slot is cleared or restored, another slot
    // is touched, or the drawer closes.
    void focusSlot (int slot, const juce::String& message);

    // Bumped by the editor whenever another program is loaded: the undo history starts again.
    std::function<int()> programVersion;

    static constexpr int numSlots = 16;

private:
    class AmountBar;
    class SlotRow;

    void timerCallback() override;
    int activeCount() const;

    // Undo history: snapshots of all 48 mod parameters (source, amount, destination per slot). `stable` follows
    // every change (from the synth too); an edit in the editor first pushes it, so each edit is one step.
    using State = std::array<int, numSlots * 3>;
    struct Step { State state; juce::String what; };
    State capture() const;
    void apply (const State&);
    void userEdit (int slot, const juce::String& what); // just before an edit made here (slot 0: not a slot's own)
    void pushStep (const juce::String& what);
    void touchedSlot (int slot);                        // a click on a slot: another slot's outline goes
    void clearFocus();
    void undoRedo (bool undo);
    void updateHint();
    std::vector<Step> undoStack, redoStack;
    State stable {};
    juce::String lastWhat, historyMessage;
    juce::uint32 lastEditAt = 0, historyUntil = 0;
    int seenProgramVersion = -1;
    bool comboOpen = false; // a slot's drop-down is open: its change is about to arrive (see SlotRow)
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };

    juce::AudioProcessorValueTreeState& apvts;
    juce::Label title { {}, "MOD MATRIX" };
    juce::Label summary;
    juce::Label hint;
    juce::OwnedArray<SlotRow> rows;
    int focusedSlot = 0;
    juce::String defaultHint, focusMessage;
    void visibilityChanged() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModMatrixDrawer)
};

} // namespace teo5
