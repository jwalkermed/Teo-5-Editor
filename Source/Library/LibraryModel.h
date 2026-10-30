// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>
#include <memory>
#include <vector>

namespace teo5
{

struct LibraryProgram
{
    juce::String uuid;
    juce::String name;
    std::shared_ptr<const juce::MemoryBlock> raw; // unpacked 4096-byte program; shared between undo snapshots
    juce::StringArray tags;
    bool favorite = false;
    bool unsent = false;   // hardware banks: changed in the library but not yet sent to the TEO-5
    juce::String source;   // e.g. "TEO-5 User 3-07" or "Pads.syx"
    juce::Time modified;

    // An empty slot: deleting a program leaves one in its place, and every bank shows at least 16 slots.
    bool isEmpty() const { return raw == nullptr; }
    static LibraryProgram empty();
};

struct LibraryBank
{
    enum class Kind { Disk, Hardware };
    juce::String uuid;
    juce::String name;
    Kind kind = Kind::Disk;
    bool factory = false;   // hardware banks only
    int hardwareBank = 0;   // panel bank 1..16 for hardware banks
    std::vector<LibraryProgram> programs;
};

// Reference to a program by position.
struct ProgramRef
{
    ProgramRef() = default;
    ProgramRef (int b, int i) : bank (b), index (i) {}
    int bank = -1, index = -1;
    bool isValid() const { return bank >= 0 && index >= 0; }
    bool operator== (const ProgramRef& o) const { return bank == o.bank && index == o.index; }
};

// The user's library: banks of programs, persisted as library.json + one .syx per program in the user
// folder. Every edit goes through an undoable snapshot so undo/redo covers all librarian operations.
class LibraryModel : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    explicit LibraryModel (juce::File folder = defaultFolder());
    ~LibraryModel() override;

    static juce::File defaultFolder();
    juce::File getFolder() const { return folder; }

    const std::vector<LibraryBank>& banks() const { return state; }
    const LibraryProgram* program (ProgramRef r) const;
    // A factory bank fetched from the TEO-5. Factory programs can't be changed on the synth, so the library keeps
    // these read-only: edits that would delete, replace, rename or add programs in them are ignored.
    bool isFactoryBank (int bank) const;

    // ---- Edits (all undoable) ----
    int addBank (const juce::String& name, LibraryBank::Kind = LibraryBank::Kind::Disk);
    void renameBank (int bank, const juce::String& name);
    void removeBank (int bank);
    // Moves a bank so it ends up before the bank now at `before` (banks().size() = to the end). Returns its new index.
    int moveBank (int bank, int before);
    static constexpr int slotsPerBank = 16; // TEO-5 User banks; your own banks hold any number of programs
    // Creates or replaces the library copy of a hardware bank (one undo step). Returns its index.
    int upsertHardwareBank (const juce::String& name, int panelBank, bool factory, std::vector<LibraryProgram> programs);
    // Replaces the bank's programs.
    void setBankPrograms (int bank, std::vector<LibraryProgram> programs, const juce::String& actionName);
    ProgramRef insertProgram (int bank, int index, LibraryProgram p); // index -1 = append
    void replaceProgramData (ProgramRef, std::shared_ptr<const juce::MemoryBlock> raw, const juce::String& name);
    void renameProgram (ProgramRef, const juce::String& name);
    void setTags (ProgramRef, const juce::StringArray& tags);
    void setFavorite (ProgramRef, bool);
    void removePrograms (juce::Array<ProgramRef>);
    // Moves programs (in order) so they end up before `before` in `toBank` (index -1 = append).
    void movePrograms (juce::Array<ProgramRef>, int toBank, int before);
    void pastePrograms (int bank, int before); // copies of the clipboard, with new IDs
    // Inserts copies (new IDs) of the programs before `before` in `toBank` (index -1 = append).
    void insertCopies (juce::Array<ProgramRef>, int toBank, int before);
    // Replaces the programs from `firstIndex` onwards in a fixed-size (hardware) bank with copies, marking
    // them unsent. Programs that would run past the end of the bank are dropped. Returns how many were placed.
    int overwritePrograms (juce::Array<ProgramRef>, int toBank, int firstIndex);
    // Like overwritePrograms, but moving: the programs leave empty slots behind (NOT SENT in a TEO-5 bank).
    int moveIntoSlots (juce::Array<ProgramRef>, int toBank, int firstIndex);
    // Exchanges two programs (or a program and an empty slot); in TEO-5 banks both become NOT SENT.
    void swapPrograms (ProgramRef, ProgramRef);
    void clearUnsent (int bank);
    // Renames programs that still carry a placeholder location name (e.g. "U1-01") using their data.
    // Returns how many were renamed (one undo step).
    int applyNames (std::function<juce::String (const juce::MemoryBlock&)> nameOf,
                    std::function<bool (const LibraryProgram&)> alsoRename = nullptr);
    static bool isPlaceholderName (const juce::String&);
    // General form: newName returns the new name, or an empty string to leave the program alone. One undo step.
    int renamePrograms (std::function<juce::String (const LibraryBank&, int index, const LibraryProgram&)> newName,
                        const juce::String& actionName);

    // ---- Clipboard ----
    void copyPrograms (const juce::Array<ProgramRef>&);
    bool hasClipboard() const { return ! clipboard.empty(); }

    // ---- Undo ----
    juce::UndoManager& getUndoManager() { return undo; }

    // ---- Search ----
    // Matches name, source or tags (case-insensitive); "tag:foo" matches tags only; "fav:" favourites only.
    static bool matches (const LibraryProgram&, const juce::String& query, bool favouritesOnly);

    static LibraryProgram makeProgram (const juce::MemoryBlock& raw, const juce::String& name, const juce::String& source);

    bool save();   // normally automatic (debounced)
    bool load();

private:
    class SnapshotAction;
    using State = std::vector<LibraryBank>;

    void commit (State newState, const juce::String& actionName);
    void restore (const State&);
    void timerCallback() override;

    juce::File folder;
    State state;
    std::vector<LibraryProgram> clipboard;
    juce::UndoManager undo { 0, 200 };
};

} // namespace teo5
