// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "HardwareTransfer.h"

namespace teo5
{

HardwareTransfer::HardwareTransfer (MidiSync& s, LibraryModel& l) : juce::Thread ("TEO5 transfer"), sync (s), library (l) {}

HardwareTransfer::~HardwareTransfer()
{
    *alive = false;
    cancel();
}

juce::String HardwareTransfer::bankName (BankAddress b)
{
    return juce::String ("TEO-5 ") + (b.factory ? "Factory " : "User ") + juce::String (b.panelBank);
}

juce::String HardwareTransfer::locationName (BankAddress b, int program)
{
    return juce::String (b.factory ? "F" : "U") + juce::String (b.panelBank) + "-" + juce::String (program).paddedLeft ('0', 2);
}

juce::String HardwareTransfer::getStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return status;
}

void HardwareTransfer::setStatus (const juce::String& s)
{
    const juce::ScopedLock sl (statusLock);
    status = s;
}

// Shared by the fetch thread and detectNamesFromLibrary.
static std::optional<std::pair<int, int>> detectName (const std::vector<juce::MemoryBlock>& raws)
{
    return ProgramLayout::detectNameField (raws);
}

static juce::String readNameAt (const juce::MemoryBlock& raw, std::pair<int, int> field)
{
    ProgramLayout l;
    l.nameOffset = field.first;
    l.nameLength = field.second;
    return l.readName (raw);
}

void HardwareTransfer::detectNamesFromLibrary()
{
    // Names read with a discarded (wrong) name field get redone too.
    const auto stale = sync.takeStaleNameField();
    auto misread = [stale] (const LibraryProgram& p)
    {
        return stale.has_value() && p.raw != nullptr && p.name == readNameAt (*p.raw, *stale);
    };

    if (sync.hasNameLayout())
    {
        library.applyNames ([this] (const juce::MemoryBlock& raw) { return sync.nameOf (raw); }, misread);
        return;
    }
    std::vector<juce::MemoryBlock> raws;
    for (auto& b : library.banks())
        if (b.kind == LibraryBank::Kind::Hardware)
            for (auto& p : b.programs)
                if (p.raw != nullptr && raws.size() < 64)
                    raws.push_back (*p.raw);
    if (auto field = detectName (raws))
    {
        sync.setNameField (field->first, field->second);
        library.applyNames ([f = *field] (const juce::MemoryBlock& raw) { return readNameAt (raw, f); }, misread);
    }
    else if (stale)
    {
        // The real names can't be found yet: show locations rather than the mis-read text.
        library.renamePrograms ([&misread] (const LibraryBank& b, int index, const LibraryProgram& p)
        {
            return b.kind == LibraryBank::Kind::Hardware && misread (p) ? locationName ({ b.hardwareBank, b.factory }, index + 1)
                                                                        : juce::String();
        }, "Reset mis-read names");
    }
}

void HardwareTransfer::fetchBanks (juce::Array<BankAddress> banks)
{
    cancel();
    job = Job::Fetch;
    fetchList = std::move (banks);
    progress = 0.0f;
    startThread();
}

void HardwareTransfer::sendBank (std::vector<juce::MemoryBlock> programs, BankAddress target)
{
    std::vector<Write> writes;
    for (int i = 0; i < (int) programs.size(); ++i)
        if (! programs[(size_t) i].isEmpty()) // an empty slot in the library: leave the synth's program there alone
            writes.push_back ({ target, i + 1, std::move (programs[(size_t) i]) });
    sendWrites (std::move (writes));
}

void HardwareTransfer::sendWrites (std::vector<Write> writes)
{
    cancel();
    job = Job::Send;
    sendList = std::move (writes);
    sendCompleted = false;
    progress = 0.0f;
    startThread();
}

void HardwareTransfer::cancel()
{
    if (isThreadRunning())
    {
        signalThreadShouldExit();
        stopThread (6000);
        setStatus ("Cancelled");
    }
}

void HardwareTransfer::run()
{
    if (job == Job::Fetch)
    {
        const int total = fetchList.size() * programsPerBank;
        int done = 0, failed = 0;

        for (auto addr : fetchList)
        {
            std::vector<LibraryProgram> programs;
            std::vector<juce::MemoryBlock> raws;
            for (int p = 1; p <= programsPerBank && ! threadShouldExit(); ++p)
            {
                setStatus ("Fetching " + locationName (addr, p) + "  (" + juce::String (done + 1) + "/" + juce::String (total) + ")");
                if (auto raw = sync.fetchProgram (addr.panelBank, p, addr.factory))
                {
                    auto name = sync.hasNameLayout() ? sync.nameOf (*raw) : juce::String();
                    if (name.isEmpty())
                        name = locationName (addr, p);
                    programs.push_back (LibraryModel::makeProgram (*raw, name, bankName (addr) + " / " + juce::String (p)));
                    raws.push_back (*raw);
                }
                else
                {
                    ++failed;
                }
                progress = (float) ++done / (float) total;
            }
            if (threadShouldExit())
                return;

            // First fetch without a known name field: find it from this bank and name the programs.
            if (! sync.hasNameLayout())
                if (auto field = detectName (raws))
                {
                    for (auto& lp : programs)
                        if (auto n = readNameAt (*lp.raw, *field); n.isNotEmpty())
                            lp.name = n;
                    juce::MessageManager::callAsync ([alive = alive, &s = sync, &lib = library, f = *field]
                    {
                        if (! *alive)
                            return;
                        s.setNameField (f.first, f.second);
                        lib.applyNames ([&s] (const juce::MemoryBlock& raw) { return s.nameOf (raw); });
                    });
                }

            // Hand the bank to the library on the message thread (replacing an earlier fetch of the same bank).
            juce::MessageManager::callAsync ([alive = alive, &lib = library, addr, programs = std::move (programs)]() mutable
            {
                if (*alive)
                    lib.upsertHardwareBank (bankName (addr), addr.panelBank, addr.factory, std::move (programs));
            });
        }

        setStatus (failed == 0 ? "Fetched " + juce::String (done) + " programs"
                               : "Fetched " + juce::String (done - failed) + " programs, " + juce::String (failed) + " did not answer");
    }
    else if (job == Job::Send)
    {
        const int total = (int) sendList.size();
        for (int i = 0; i < total && ! threadShouldExit(); ++i)
        {
            const auto& w = sendList[(size_t) i];
            setStatus ("Writing " + locationName (w.target, w.program) + " (" + juce::String (i + 1) + " of " + juce::String (total) + ")");
            sync.writeProgram (w.target.panelBank, w.program, w.target.factory, w.data);
            // Let the dump go out and give the synth time to store it.
            for (int w = 0; w < 400 && sync.getPorts().pendingCount() > 0 && ! threadShouldExit(); ++w)
                wait (5);
            wait (250);
            progress = (float) (i + 1) / (float) total;
        }
        if (! threadShouldExit())
        {
            sendCompleted = true;
            juce::StringArray banks;
            for (auto& w : sendList)
                banks.addIfNotAlreadyThere (bankName (w.target));
            setStatus ("Wrote " + juce::String (total) + " program" + (total == 1 ? "" : "s") + " to "
                       + (banks.size() <= 2 ? banks.joinIntoString (" and ") : juce::String (banks.size()) + " banks"));
        }
    }

    juce::MessageManager::callAsync ([this, alive = alive] { if (*alive && onFinished) onFinished(); });
}

} // namespace teo5
