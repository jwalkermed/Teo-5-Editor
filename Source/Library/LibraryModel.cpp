// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "LibraryModel.h"
#include "../Midi/SysEx.h"
#include <set>

namespace teo5
{

// Undo step: swaps between two complete library states (program data is shared, so this is cheap).
class LibraryModel::SnapshotAction : public juce::UndoableAction
{
public:
    SnapshotAction (LibraryModel& m, State b, State a) : model (m), before (std::move (b)), after (std::move (a)) {}
    bool perform() override { model.restore (after); return true; }
    bool undo() override { model.restore (before); return true; }
    int getSizeInUnits() override { return 1; }

private:
    LibraryModel& model;
    State before, after;
};

LibraryModel::LibraryModel (juce::File f) : folder (std::move (f))
{
    load();
}

LibraryModel::~LibraryModel()
{
    if (isTimerRunning())
        save();
}

juce::File LibraryModel::defaultFolder()
{
    // Override for tests/tools so they never touch the user's library.
    if (auto dir = juce::SystemStats::getEnvironmentVariable ("TEO5_LIBRARY_DIR", {}); dir.isNotEmpty())
        return juce::File (dir);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("TEO5Editor").getChildFile ("Library");
}

const LibraryProgram* LibraryModel::program (ProgramRef r) const
{
    if (! juce::isPositiveAndBelow (r.bank, (int) state.size()))
        return nullptr;
    auto& progs = state[(size_t) r.bank].programs;
    return juce::isPositiveAndBelow (r.index, (int) progs.size()) ? &progs[(size_t) r.index] : nullptr;
}

LibraryProgram LibraryModel::makeProgram (const juce::MemoryBlock& raw, const juce::String& name, const juce::String& source)
{
    LibraryProgram p;
    p.uuid = juce::Uuid().toString();
    p.name = name;
    p.raw = std::make_shared<const juce::MemoryBlock> (raw);
    p.source = source;
    p.modified = juce::Time::getCurrentTime();
    return p;
}

LibraryProgram LibraryProgram::empty()
{
    LibraryProgram p;
    p.uuid = juce::Uuid().toString();
    return p;
}

namespace
{
    // TEO-5 User banks always have their 16 slots (a deleted program leaves an empty one); your own banks (MY BANKS)
    // hold only programs, as many as you like. Factory banks are kept exactly as fetched.
    void normalise (LibraryBank& b)
    {
        if (b.kind == LibraryBank::Kind::Hardware)
        {
            if (! b.factory)
                while ((int) b.programs.size() < LibraryModel::slotsPerBank)
                    b.programs.push_back (LibraryProgram::empty());
            return;
        }
        b.programs.erase (std::remove_if (b.programs.begin(), b.programs.end(), [] (const LibraryProgram& p) { return p.isEmpty(); }),
                          b.programs.end());
    }

    // For undo/redo descriptions: "U3-07" for a TEO-5 slot, "My Programs 07" for one of your banks.
    juce::String where (const LibraryBank& b, int index)
    {
        const auto n = juce::String (index + 1).paddedLeft ('0', 2);
        return b.kind == LibraryBank::Kind::Hardware ? juce::String (b.factory ? "F" : "U") + juce::String (b.hardwareBank) + "-" + n
                                                     : b.name + " " + n;
    }
    juce::String quoted (const LibraryProgram& p) { return p.isEmpty() ? juce::String ("the empty slot") : "\"" + p.name + "\""; }

    // The empty slot left behind when a program is deleted or moved out: in a TEO-5 bank it's "not sent" (the synth
    // still has the program there until the bank is sent).
    LibraryProgram emptySlotFor (const LibraryBank& b, const juce::String& source = {})
    {
        auto e = LibraryProgram::empty();
        e.unsent = b.kind == LibraryBank::Kind::Hardware;
        e.source = source;
        return e;
    }

    // Puts programs at `before`: into empty slots where they are, otherwise inserting. Appending (before = -1)
    // starts right after the last program, so a bank's empty slots fill up first.
    void place (std::vector<LibraryProgram>& dest, int before, const std::vector<LibraryProgram>& programs)
    {
        if (! juce::isPositiveAndNotGreaterThan (before, (int) dest.size()))
        {
            before = (int) dest.size();
            while (before > 0 && dest[(size_t) before - 1].isEmpty())
                --before;
        }
        for (auto& p : programs)
        {
            if (before < (int) dest.size() && dest[(size_t) before].isEmpty())
                dest[(size_t) before] = p;
            else
                dest.insert (dest.begin() + before, p);
            ++before;
        }
    }
}

void LibraryModel::commit (State newState, const juce::String& actionName)
{
    for (auto& b : newState)
        normalise (b);
    undo.beginNewTransaction (actionName);
    undo.perform (new SnapshotAction (*this, state, std::move (newState)), actionName);
}

void LibraryModel::restore (const State& s)
{
    state = s;
    updateUnsent (state);
    startTimer (800); // debounced save
    sendChangeMessage();
}

void LibraryModel::timerCallback()
{
    stopTimer();
    save();
}

// ---- Edits ------------------------------------------------------------------------------------

int LibraryModel::addBank (const juce::String& name, LibraryBank::Kind kind)
{
    auto s = state;
    LibraryBank b;
    b.uuid = juce::Uuid().toString();
    b.name = name;
    b.kind = kind;
    s.push_back (std::move (b));
    commit (std::move (s), "New bank \"" + name + "\"");
    return (int) state.size() - 1;
}

void LibraryModel::renameBank (int bank, const juce::String& name)
{
    if (! juce::isPositiveAndBelow (bank, (int) state.size()))
        return;
    auto s = state;
    const auto old = s[(size_t) bank].name;
    s[(size_t) bank].name = name;
    commit (std::move (s), "Renamed bank \"" + old + "\" to \"" + name + "\"");
}

void LibraryModel::removeBank (int bank)
{
    if (! juce::isPositiveAndBelow (bank, (int) state.size()))
        return;
    auto s = state;
    const auto old = s[(size_t) bank].name;
    s.erase (s.begin() + bank);
    commit (std::move (s), "Deleted bank \"" + old + "\"");
}

int LibraryModel::moveBank (int bank, int before)
{
    if (! juce::isPositiveAndBelow (bank, (int) state.size()))
        return bank;
    before = juce::jlimit (0, (int) state.size(), before);
    if (before == bank || before == bank + 1)
        return bank; // already there
    auto s = state;
    auto moving = std::move (s[(size_t) bank]);
    s.erase (s.begin() + bank);
    const int to = before > bank ? before - 1 : before;
    const auto movedName = moving.name;
    s.insert (s.begin() + to, std::move (moving));
    commit (std::move (s), "Moved bank \"" + movedName + "\"");
    return to;
}

int LibraryModel::upsertHardwareBank (const juce::String& name, int panelBank, bool factory, std::vector<LibraryProgram> programs)
{
    auto s = state;
    int index = -1;
    for (int i = 0; i < (int) s.size(); ++i)
        if (s[(size_t) i].kind == LibraryBank::Kind::Hardware && s[(size_t) i].hardwareBank == panelBank && s[(size_t) i].factory == factory)
            index = i;
    if (index < 0)
    {
        LibraryBank b;
        b.uuid = juce::Uuid().toString();
        b.kind = LibraryBank::Kind::Hardware;
        b.hardwareBank = panelBank;
        b.factory = factory;
        s.push_back (std::move (b));
        index = (int) s.size() - 1;
    }
    s[(size_t) index].name = name;
    s[(size_t) index].programs = std::move (programs);
    if (! factory)
        for (int i = 0; i < (int) s[(size_t) index].programs.size(); ++i)
            recordSynthSlot (panelBank, i, s[(size_t) index].programs[(size_t) i]); // fetched: this is what the synth has
    commit (std::move (s), "Fetched " + name);
    return index;
}

bool LibraryModel::isFactoryBank (int bank) const
{
    return juce::isPositiveAndBelow (bank, (int) state.size()) && state[(size_t) bank].kind == LibraryBank::Kind::Hardware
        && state[(size_t) bank].factory;
}

void LibraryModel::setBankPrograms (int bank, std::vector<LibraryProgram> programs, const juce::String& actionName)
{
    if (! juce::isPositiveAndBelow (bank, (int) state.size()) || isFactoryBank (bank))
        return;
    auto s = state;
    s[(size_t) bank].programs = std::move (programs);
    commit (std::move (s), actionName);
}

ProgramRef LibraryModel::insertProgram (int bank, int index, LibraryProgram p)
{
    if (! juce::isPositiveAndBelow (bank, (int) state.size()) || isFactoryBank (bank))
        return {};
    auto s = state;
    auto& progs = s[(size_t) bank].programs;
    if (! juce::isPositiveAndNotGreaterThan (index, (int) progs.size()))
    {
        // Append: into the first empty slot, if any.
        index = (int) progs.size();
        for (int i = 0; i < (int) progs.size(); ++i)
            if (progs[(size_t) i].isEmpty()) { index = i; break; }
    }
    const auto added = "Added \"" + p.name + "\" to " + where (s[(size_t) bank], index); // (before s is moved)
    place (progs, index, { std::move (p) });
    commit (std::move (s), added);
    return { bank, index };
}

void LibraryModel::replaceProgramData (ProgramRef r, std::shared_ptr<const juce::MemoryBlock> raw, const juce::String& name)
{
    if (program (r) == nullptr || isFactoryBank (r.bank))
        return;
    auto s = state;
    auto& p = s[(size_t) r.bank].programs[(size_t) r.index];
    p.raw = std::move (raw);
    if (name.isNotEmpty())
        p.name = name;
    p.modified = juce::Time::getCurrentTime();
    const auto description = "Updated \"" + p.name + "\" (" + where (s[(size_t) r.bank], r.index) + ")";
    commit (std::move (s), description);
}

void LibraryModel::renameProgram (ProgramRef r, const juce::String& name)
{
    if (program (r) == nullptr || isFactoryBank (r.bank))
        return;
    auto s = state;
    auto& p = s[(size_t) r.bank].programs[(size_t) r.index];
    const auto old = p.name;
    p.name = name;
    p.modified = juce::Time::getCurrentTime();
    commit (std::move (s), "Renamed \"" + old + "\" to \"" + name + "\"");
}

void LibraryModel::setTags (ProgramRef r, const juce::StringArray& tags)
{
    if (program (r) == nullptr)
        return;
    auto s = state;
    s[(size_t) r.bank].programs[(size_t) r.index].tags = tags;
    commit (std::move (s), (tags.isEmpty() ? "Cleared the tags of " : "Tagged ") + quoted (state[(size_t) r.bank].programs[(size_t) r.index])
                               + (tags.isEmpty() ? juce::String() : " " + tags.joinIntoString (", ")));
}

void LibraryModel::setFavorite (ProgramRef r, bool fav)
{
    if (program (r) == nullptr)
        return;
    auto s = state;
    s[(size_t) r.bank].programs[(size_t) r.index].favorite = fav;
    commit (std::move (s), (fav ? "Added " : "Removed ") + quoted (state[(size_t) r.bank].programs[(size_t) r.index])
                               + (fav ? " to favourites" : " from favourites"));
}

static void sortDescending (juce::Array<ProgramRef>& refs)
{
    std::sort (refs.begin(), refs.end(), [] (const ProgramRef& a, const ProgramRef& b)
               { return a.bank != b.bank ? a.bank > b.bank : a.index > b.index; });
}

void LibraryModel::removePrograms (juce::Array<ProgramRef> refs)
{
    refs.removeIf ([this] (const ProgramRef& r) { return isFactoryBank (r.bank); });
    if (refs.isEmpty())
        return;
    auto s = state;
    const auto description = refs.size() == 1 && program (refs[0]) != nullptr
                                 ? "Deleted " + quoted (*program (refs[0])) + " (" + where (state[(size_t) refs[0].bank], refs[0].index) + ")"
                                 : "Deleted " + juce::String (refs.size()) + " programs";
    // In a TEO-5 bank the slot is left empty (NOT SENT); in your own banks the program is simply removed (when
    // the bank is tidied up in commit), so the rest close up.
    for (auto& r : refs)
        if (juce::isPositiveAndBelow (r.bank, (int) s.size()) && juce::isPositiveAndBelow (r.index, (int) s[(size_t) r.bank].programs.size()))
        {
            auto& slot = s[(size_t) r.bank].programs[(size_t) r.index];
            slot = emptySlotFor (s[(size_t) r.bank], slot.source);
        }
    commit (std::move (s), description);
}

void LibraryModel::movePrograms (juce::Array<ProgramRef> refs, int toBank, int before)
{
    if (refs.isEmpty() || ! juce::isPositiveAndBelow (toBank, (int) state.size()) || isFactoryBank (toBank))
        return;
    // Programs dragged out of a factory bank are copied, not moved.
    for (auto& r : refs)
        if (isFactoryBank (r.bank))
        {
            insertCopies (refs, toBank, before);
            return;
        }

    // Keep the user's order (top to bottom).
    std::sort (refs.begin(), refs.end(), [] (const ProgramRef& a, const ProgramRef& b)
               { return a.bank != b.bank ? a.bank < b.bank : a.index < b.index; });

    auto s = state;
    refs.removeIf ([this] (const ProgramRef& r) { auto* p = program (r); return p == nullptr || p->isEmpty(); });
    if (refs.isEmpty() || (refs.size() == 1 && refs[0].bank == toBank && refs[0].index == before))
        return;
    std::vector<LibraryProgram> moving;
    for (auto& r : refs)
        if (auto* p = program (r))
            moving.push_back (*p);

    const auto description = (moving.size() == 1 ? "Moved " + quoted (moving[0]) : "Moved " + juce::String ((int) moving.size()) + " programs")
                             + " to " + s[(size_t) toBank].name;
    // The slots they came from are emptied first (so the drop position doesn't shift), then they go where they
    // were dropped; in your own banks the emptied slots are then removed (commit tidies the bank up).
    for (auto& r : refs)
        s[(size_t) r.bank].programs[(size_t) r.index] = emptySlotFor (s[(size_t) r.bank]);
    place (s[(size_t) toBank].programs, before, moving);
    commit (std::move (s), description);
}

namespace
{
    std::vector<LibraryProgram> copiesOf (const LibraryModel& m, juce::Array<ProgramRef> refs)
    {
        std::sort (refs.begin(), refs.end(), [] (const ProgramRef& a, const ProgramRef& b)
                   { return a.bank != b.bank ? a.bank < b.bank : a.index < b.index; });
        std::vector<LibraryProgram> copies;
        for (auto& r : refs)
            if (auto* p = m.program (r); p != nullptr && ! p->isEmpty())
            {
                auto c = *p;
                c.uuid = juce::Uuid().toString();
                c.modified = juce::Time::getCurrentTime();
                c.unsent = false;
                copies.push_back (std::move (c));
            }
        return copies;
    }
}

void LibraryModel::insertCopies (juce::Array<ProgramRef> refs, int toBank, int before)
{
    if (refs.isEmpty() || ! juce::isPositiveAndBelow (toBank, (int) state.size()) || isFactoryBank (toBank))
        return;
    auto copies = copiesOf (*this, refs);
    if (copies.empty())
        return;
    auto s = state;
    place (s[(size_t) toBank].programs, before, copies);
    commit (std::move (s), (copies.size() == 1 ? "Copied " + quoted (copies[0]) : "Copied " + juce::String ((int) copies.size()) + " programs")
                               + " to " + state[(size_t) toBank].name);
}

void LibraryModel::swapPrograms (ProgramRef a, ProgramRef b)
{
    if (program (a) == nullptr || program (b) == nullptr || a == b || isFactoryBank (a.bank) || isFactoryBank (b.bank))
        return;
    auto s = state;
    auto& pa = s[(size_t) a.bank].programs[(size_t) a.index];
    auto& pb = s[(size_t) b.bank].programs[(size_t) b.index];
    if (pa.isEmpty() && pb.isEmpty())
        return;
    const auto description = "Swapped " + quoted (pa) + " (" + where (s[(size_t) a.bank], a.index) + ") and " + quoted (pb)
                             + " (" + where (s[(size_t) b.bank], b.index) + ")";
    std::swap (pa, pb);
    // In a TEO-5 bank both slots now differ from the synth; each keeps its location label.
    std::swap (pa.source, pb.source);
    for (auto [p, bank] : { std::pair<LibraryProgram*, int> { &pa, a.bank }, { &pb, b.bank } })
        if (s[(size_t) bank].kind == LibraryBank::Kind::Hardware)
            p->unsent = true;
    commit (std::move (s), description);
}

int LibraryModel::moveIntoSlots (juce::Array<ProgramRef> refs, int toBank, int firstIndex)
{
    if (refs.isEmpty() || ! juce::isPositiveAndBelow (toBank, (int) state.size()) || isFactoryBank (toBank))
        return 0;
    std::sort (refs.begin(), refs.end(), [] (const ProgramRef& a, const ProgramRef& b)
               { return a.bank != b.bank ? a.bank < b.bank : a.index < b.index; });
    refs.removeIf ([this] (const ProgramRef& r) { auto* p = program (r); return p == nullptr || p->isEmpty() || isFactoryBank (r.bank); });
    if (refs.isEmpty() || (refs.size() == 1 && refs[0].bank == toBank && refs[0].index == firstIndex))
        return 0;

    auto s = state;
    std::vector<LibraryProgram> moving;
    for (auto& r : refs)
        moving.push_back (s[(size_t) r.bank].programs[(size_t) r.index]);
    for (auto& r : refs) // the slots they leave are empty (and not yet sent, in a TEO-5 bank)
        s[(size_t) r.bank].programs[(size_t) r.index] = emptySlotFor (s[(size_t) r.bank], s[(size_t) r.bank].programs[(size_t) r.index].source);

    auto& dest = s[(size_t) toBank].programs;
    int placed = 0;
    for (auto& p : moving)
    {
        const int i = firstIndex + placed;
        if (! juce::isPositiveAndBelow (i, (int) dest.size()))
            break;
        p.unsent = dest[(size_t) i].unsent || s[(size_t) toBank].kind == LibraryBank::Kind::Hardware;
        p.source = dest[(size_t) i].source; // keeps its location label ("TEO-5 User 3-07")
        dest[(size_t) i] = std::move (p);
        ++placed;
    }
    if (placed > 0)
    {
        // Built before commit: the argument order would otherwise let `s` be moved first.
        const auto description = placed == 1 ? "Moved " + quoted (dest[(size_t) firstIndex]) + " to " + where (s[(size_t) toBank], firstIndex)
                                             : "Moved " + juce::String (placed) + " programs to " + s[(size_t) toBank].name;
        commit (std::move (s), description);
    }
    return placed;
}

int LibraryModel::overwritePrograms (juce::Array<ProgramRef> refs, int toBank, int firstIndex)
{
    if (refs.isEmpty() || ! juce::isPositiveAndBelow (toBank, (int) state.size()) || isFactoryBank (toBank))
        return 0;
    auto copies = copiesOf (*this, refs);
    auto s = state;
    auto& dest = s[(size_t) toBank].programs;
    int placed = 0;
    for (auto& c : copies)
    {
        const int i = firstIndex + placed;
        if (! juce::isPositiveAndBelow (i, (int) dest.size()))
            break;
        c.unsent = true;
        c.source = dest[(size_t) i].source; // keeps its location label ("TEO-5 User 3-07")
        dest[(size_t) i] = std::move (c);
        ++placed;
    }
    if (placed > 0)
    {
        const auto description = placed == 1 ? "Replaced " + where (s[(size_t) toBank], firstIndex) + " with " + quoted (dest[(size_t) firstIndex])
                                             : "Replaced " + juce::String (placed) + " programs in " + s[(size_t) toBank].name;
        commit (std::move (s), description);
    }
    return placed;
}

void LibraryModel::clearUnsent (int bank)
{
    if (! juce::isPositiveAndBelow (bank, (int) state.size()) || state[(size_t) bank].kind != LibraryBank::Kind::Hardware
        || state[(size_t) bank].factory)
        return;
    const auto& b = state[(size_t) bank];
    for (int i = 0; i < (int) b.programs.size(); ++i)
        if (b.programs[(size_t) i].unsent)
            recordSynthSlot (b.hardwareBank, i, b.programs[(size_t) i]); // sent: the synth now has it
    restore (state); // marks follow the record; not an undo step (undo doesn't change the synth)
}

LibraryModel::SynthSlot LibraryModel::synthSlot (int panelBank, int index) const
{
    if (auto it = synthRecord.find (panelBank); it != synthRecord.end() && juce::isPositiveAndBelow (index, 16))
        return it->second[(size_t) index];
    return {};
}

juce::String LibraryModel::fingerprint (const LibraryProgram& p)
{
    if (p.raw == nullptr)
        return "-";
    juce::uint64 h = 14695981039346656037ull; // FNV-1a over the program data
    auto* d = static_cast<const juce::uint8*> (p.raw->getData());
    for (size_t i = 0; i < p.raw->getSize(); ++i)
        h = (h ^ d[i]) * 1099511628211ull;
    return juce::String::toHexString ((juce::int64) h);
}

void LibraryModel::recordSynthSlot (int panelBank, int index, const LibraryProgram& p)
{
    if (juce::isPositiveAndBelow (index, 16))
        synthRecord[panelBank][(size_t) index] = { p.isEmpty() ? juce::String ("Basic Program") : p.name, fingerprint (p) };
}

void LibraryModel::noteWrittenToSynth (int panelBank, int index, const juce::MemoryBlock& raw, const juce::String& name)
{
    recordSynthSlot (panelBank, index, makeProgram (raw, name, {}));
    restore (state);
}

void LibraryModel::updateUnsent (State& s) const
{
    for (auto& b : s)
    {
        if (b.kind != LibraryBank::Kind::Hardware || b.factory)
            continue;
        for (int i = 0; i < (int) b.programs.size() && i < 16; ++i)
            if (const auto onSynth = synthSlot (b.hardwareBank, i); onSynth.fingerprint.isNotEmpty())
                b.programs[(size_t) i].unsent = fingerprint (b.programs[(size_t) i]) != onSynth.fingerprint;
    }
}

ProgramRef LibraryModel::saveProgram (ProgramRef target, const juce::MemoryBlock& raw, const juce::String& name, bool writtenToSynth)
{
    if (! juce::isPositiveAndBelow (target.bank, (int) state.size()) || isFactoryBank (target.bank) || target.index < 0)
        return {};
    auto s = state;
    auto& b = s[(size_t) target.bank];
    const bool hardware = b.kind == LibraryBank::Kind::Hardware;
    if (hardware && target.index >= slotsPerBank)
        return {};
    auto p = makeProgram (raw, name, "Saved from the panel");
    const auto what = "Save \"" + name + "\" to " + where (b, target.index);
    if (! hardware && target.index >= (int) b.programs.size())
    {
        b.programs.push_back (std::move (p));
        target.index = (int) b.programs.size() - 1;
    }
    else
    {
        while ((int) b.programs.size() <= target.index)
            b.programs.push_back (LibraryProgram::empty());
        if (hardware)
        {
            p.unsent = ! writtenToSynth;
            if (writtenToSynth)
                recordSynthSlot (b.hardwareBank, target.index, p);
        }
        b.programs[(size_t) target.index] = std::move (p);
    }
    commit (std::move (s), what);
    return target;
}

bool LibraryModel::isPlaceholderName (const juce::String& n)
{
    // HardwareTransfer::locationName format: U3-07 / F12-16
    return n.length() >= 4 && (n[0] == 'U' || n[0] == 'F') && n.containsChar ('-')
        && n.substring (1).removeCharacters ("-").containsOnly ("0123456789");
}

int LibraryModel::renamePrograms (std::function<juce::String (const LibraryBank&, int, const LibraryProgram&)> newName,
                                  const juce::String& actionName)
{
    auto s = state;
    int renamed = 0;
    for (auto& b : s)
        for (int i = 0; i < (int) b.programs.size(); ++i)
        {
            auto& p = b.programs[(size_t) i];
            if (auto n = newName (b, i, p); n.isNotEmpty() && n != p.name)
            {
                p.name = n;
                ++renamed;
            }
        }
    if (renamed > 0)
        commit (std::move (s), actionName);
    return renamed;
}

int LibraryModel::applyNames (std::function<juce::String (const juce::MemoryBlock&)> nameOf,
                              std::function<bool (const LibraryProgram&)> alsoRename)
{
    return renamePrograms ([&] (const LibraryBank&, int, const LibraryProgram& p)
    {
        const bool eligible = p.raw != nullptr && (isPlaceholderName (p.name) || (alsoRename && alsoRename (p)));
        return eligible ? nameOf (*p.raw) : juce::String();
    }, "Read program names");
}

void LibraryModel::copyPrograms (const juce::Array<ProgramRef>& refs)
{
    clipboard.clear();
    for (auto& r : refs)
        if (auto* p = program (r); p != nullptr && ! p->isEmpty())
            clipboard.push_back (*p);
}

void LibraryModel::pastePrograms (int bank, int before)
{
    if (clipboard.empty() || ! juce::isPositiveAndBelow (bank, (int) state.size()) || isFactoryBank (bank))
        return;
    auto s = state;
    std::vector<LibraryProgram> copies;
    for (auto p : clipboard)
    {
        p.uuid = juce::Uuid().toString();
        p.modified = juce::Time::getCurrentTime();
        copies.push_back (std::move (p));
    }
    place (s[(size_t) bank].programs, before, copies);
    commit (std::move (s), "Pasted " + juce::String ((int) clipboard.size()) + " program" + (clipboard.size() == 1 ? "" : "s")
                               + " into " + state[(size_t) bank].name);
}

bool LibraryModel::matches (const LibraryProgram& p, const juce::String& query, bool favouritesOnly)
{
    if (favouritesOnly && ! p.favorite)
        return false;
    auto q = query.trim();
    if (q.isEmpty())
        return true;
    if (p.isEmpty())
        return false; // empty slots don't match a search
    if (q.startsWithIgnoreCase ("tag:"))
    {
        auto t = q.substring (4).trim();
        for (auto& tag : p.tags)
            if (tag.containsIgnoreCase (t))
                return true;
        return false;
    }
    if (p.name.containsIgnoreCase (q) || p.source.containsIgnoreCase (q))
        return true;
    for (auto& tag : p.tags)
        if (tag.containsIgnoreCase (q))
            return true;
    return false;
}

// ---- Persistence ------------------------------------------------------------------------------
// library.json holds banks and program metadata; programs/<uuid>.syx holds each program as an
// edit-buffer SysEx dump, so the files can also be sent with any SysEx librarian.

bool LibraryModel::save()
{
    auto progDir = folder.getChildFile ("programs");
    if (! progDir.createDirectory())
        return false;

    juce::Array<juce::var> banksJson;
    for (auto& b : state)
    {
        auto* bj = new juce::DynamicObject();
        bj->setProperty ("uuid", b.uuid);
        bj->setProperty ("name", b.name);
        bj->setProperty ("kind", b.kind == LibraryBank::Kind::Hardware ? "hardware" : "disk");
        bj->setProperty ("factory", b.factory);
        bj->setProperty ("hardwareBank", b.hardwareBank);

        juce::Array<juce::var> progs;
        for (auto& p : b.programs)
        {
            auto* pj = new juce::DynamicObject();
            pj->setProperty ("uuid", p.uuid);
            if (p.isEmpty())
            {
                pj->setProperty ("empty", true);
                if (p.unsent)
                    pj->setProperty ("unsent", true);
                progs.add (juce::var (pj));
                continue;
            }
            pj->setProperty ("name", p.name);
            pj->setProperty ("source", p.source);
            pj->setProperty ("favorite", p.favorite);
            if (p.unsent)
                pj->setProperty ("unsent", true);
            pj->setProperty ("tags", juce::var (p.tags));
            pj->setProperty ("modified", p.modified.toISO8601 (true));
            progs.add (juce::var (pj));
        }
        bj->setProperty ("programs", progs);
        banksJson.add (juce::var (bj));
    }

    // Write program files that are new or whose data changed.
    for (auto& b : state)
        for (auto& p : b.programs)
            if (p.raw != nullptr)
            {
                auto f = progDir.getChildFile (p.uuid + ".syx");
                auto m = SysEx::editBufferDump (*p.raw);
                juce::MemoryBlock existing;
                f.loadFileAsData (existing);
                if (existing.getSize() != (size_t) m.getRawDataSize()
                    || memcmp (existing.getData(), m.getRawData(), existing.getSize()) != 0)
                    f.replaceWithData (m.getRawData(), (size_t) m.getRawDataSize());
            }

    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("banks", banksJson);
    juce::Array<juce::var> synthJson; // what's on the TEO-5, per User bank
    for (auto& [bank, slots] : synthRecord)
    {
        auto* sj = new juce::DynamicObject();
        sj->setProperty ("bank", bank);
        juce::Array<juce::var> names, prints;
        for (auto& slot : slots)
        {
            names.add (slot.name);
            prints.add (slot.fingerprint);
        }
        sj->setProperty ("names", names);
        sj->setProperty ("fingerprints", prints);
        synthJson.add (juce::var (sj));
    }
    root->setProperty ("synth", synthJson);

    // Unreferenced program files are kept (undo may bring them back) and pruned on the next load.
    return folder.getChildFile ("library.json").replaceWithText (juce::JSON::toString (juce::var (root)));
}

bool LibraryModel::load()
{
    auto json = juce::JSON::parse (folder.getChildFile ("library.json"));
    state.clear();
    undo.clearUndoHistory();
    if (auto* banksArr = json.getProperty ("banks", {}).getArray())
    {
        auto progDir = folder.getChildFile ("programs");
        for (auto& bv : *banksArr)
        {
            LibraryBank b;
            b.uuid = bv.getProperty ("uuid", juce::Uuid().toString());
            b.name = bv.getProperty ("name", "Bank");
            b.kind = bv.getProperty ("kind", "disk").toString() == "hardware" ? LibraryBank::Kind::Hardware : LibraryBank::Kind::Disk;
            b.factory = bv.getProperty ("factory", false);
            b.hardwareBank = bv.getProperty ("hardwareBank", 0);
            if (auto* progs = bv.getProperty ("programs", {}).getArray())
                for (auto& pv : *progs)
                {
                    LibraryProgram p;
                    p.uuid = pv.getProperty ("uuid", {});
                    p.name = pv.getProperty ("name", {});
                    p.source = pv.getProperty ("source", {});
                    p.favorite = pv.getProperty ("favorite", false);
                    p.unsent = pv.getProperty ("unsent", false);
                    if (auto* tags = pv.getProperty ("tags", {}).getArray())
                        for (auto& t : *tags)
                            p.tags.add (t.toString());
                    p.modified = juce::Time::fromISO8601 (pv.getProperty ("modified", {}).toString());

                    juce::MemoryBlock fileData;
                    if (progDir.getChildFile (p.uuid + ".syx").loadFileAsData (fileData))
                    {
                        auto parsed = SysEx::parse (juce::MidiMessage (fileData.getData(), (int) fileData.getSize()));
                        if (SysEx::isProgramSize (parsed.data.getSize()))
                            p.raw = std::make_shared<const juce::MemoryBlock> (parsed.data);
                    }
                    if (p.uuid.isNotEmpty() && (p.raw != nullptr || (bool) pv.getProperty ("empty", false)))
                        b.programs.push_back (std::move (p)); // an empty slot keeps its place
                }
            normalise (b);
            state.push_back (std::move (b));
        }
    }

    // What's on the TEO-5. Libraries saved before 1.0.11 don't have it: a slot not marked NOT SENT holds what the
    // synth has; a NOT SENT slot stays unknown (keeps its mark) until it's sent or fetched.
    synthRecord.clear();
    if (auto* synth = json.getProperty ("synth", {}).getArray())
    {
        for (auto& sv : *synth)
        {
            auto& slots = synthRecord[(int) sv.getProperty ("bank", 0)];
            auto* names = sv.getProperty ("names", {}).getArray();
            auto* prints = sv.getProperty ("fingerprints", {}).getArray();
            for (int i = 0; i < 16; ++i)
                slots[(size_t) i] = { names != nullptr && i < names->size() ? (*names)[i].toString() : juce::String(),
                                      prints != nullptr && i < prints->size() ? (*prints)[i].toString() : juce::String() };
        }
    }
    else
        for (auto& b : state)
            if (b.kind == LibraryBank::Kind::Hardware && ! b.factory)
                for (int i = 0; i < (int) b.programs.size() && i < 16; ++i)
                    if (! b.programs[(size_t) i].unsent)
                        recordSynthSlot (b.hardwareBank, i, b.programs[(size_t) i]);
    updateUnsent (state);

    // Remove program files no bank references any more.
    std::set<juce::String> live;
    for (auto& b : state)
        for (auto& p : b.programs)
            live.insert (p.uuid);
    for (auto& f : folder.getChildFile ("programs").findChildFiles (juce::File::findFiles, false, "*.syx"))
        if (live.count (f.getFileNameWithoutExtension()) == 0)
            f.deleteFile();

    if (state.empty())
    {
        LibraryBank b;
        b.uuid = juce::Uuid().toString();
        b.name = "My Programs";
        normalise (b);
        state.push_back (std::move (b));
    }
    sendChangeMessage();
    return true;
}

} // namespace teo5
