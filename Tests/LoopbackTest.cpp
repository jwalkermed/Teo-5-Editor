// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
// End-to-end checks of the MIDI path without hardware, against a fake TEO-5.
//
// The fake stores each NRPN at offset 2*NRPN (16-bit little-endian) of a 4096-byte program, keeps a
// name at offset 3000, and answers edit-buffer requests. (A real TEO-5 uses offset = NRPN; the fake's
// different layout checks that the probe can learn a layout the editor doesn't know.)
//
// Part A: real CoreMIDI ports. The fake runs in its own process (this executable with --fake),
//         like hardware behind the CoreMIDI server: NRPN out/in, coalescing, a dump.
// Part B: the plugin's host-MIDI routing, with the test shuttling messages to an in-process fake:
//         layout probe, dump -> panel, name, edit-buffer send, state save/restore.
//         (Large SysEx bursts through a JUCE virtual *source* stall after a few dozen dumps, which is a
//         property of the virtual port, not of real hardware, so the dump-heavy flows run here.)
// macOS/Linux only (virtual ports).

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/LibraryDrawer.h"
#include "UI/ModMatrixDrawer.h"
#include "Params/ParamTable.h"
#include "Midi/Nrpn.h"
#include "Midi/SysEx.h"
#include <map>

using namespace teo5;

namespace
{
// ---- Fake synth core (no I/O) ----------------------------------------------------------------

struct FakeCore
{
    FakeCore()
    {
        decoder.onNrpn = [this] (int, int nrpn, int value) { set16 (2 * nrpn, value); };
        const char* name = "Fake Oberheim";
        memcpy (static_cast<char*> (program.getData()) + 3000, name, strlen (name));
    }

    // Handles one message from the editor; returns the synth's replies.
    juce::Array<juce::MidiMessage> process (const juce::MidiMessage& m)
    {
        const juce::ScopedLock sl (lock);
        if (m.isController())
        {
            if (m.isAllNotesOff())
                program[4002] = 1; // test hook: All Notes Off arrived (PANIC)
            decoder.process (m);
            return {};
        }
        if (m.isNoteOff() && m.getNoteNumber() == 127)
            program[4003] = 1; // test hook: PANIC's note-offs reach the top note
        if (m.isNoteOn())
        {
            program[4000] = (char) m.getNoteNumber(); // test hook: last note played and its channel
            program[4001] = (char) m.getChannel();
            return {};
        }
        if (! m.isSysEx())
            return {};

        auto* d = m.getSysExData();
        const int n = m.getSysExDataSize();
        if (n == 3 && d[0] == SysEx::manufacturerId() && d[2] == SysEx::cmdRequestEditBuffer)
            return { SysEx::editBufferDump (program) };
        if (n > 3 && d[0] == SysEx::manufacturerId() && d[2] == SysEx::cmdEditBufferData)
            program = SysEx::parse (m).data;
        if (n == 5 && d[0] == SysEx::manufacturerId() && d[2] == SysEx::cmdRequestProgram)
            return { SysEx::programDump (d[3], d[4], stored (d[3], d[4])) };
        if (n > 5 && d[0] == SysEx::manufacturerId() && d[2] == SysEx::cmdProgramData)
        {
            auto parsed = SysEx::parse (m);
            memory[parsed.bank * 128 + parsed.program] = parsed.data;
        }
        return {};
    }

    // Stored program at a memory location: distinct, deterministic content with a name.
    juce::MemoryBlock stored (int bank, int prog)
    {
        const int key = bank * 128 + prog;
        if (memory.count (key) == 0)
        {
            juce::MemoryBlock m ((size_t) SysEx::programSize, true);
            juce::Random rng (key + 1);
            for (int i = 0; i < 400; ++i)
                m[i] = (char) rng.nextInt (256);
            auto name = "Stored " + juce::String (key);
            memcpy (static_cast<char*> (m.getData()) + 3000, name.toRawUTF8(), (size_t) name.length());
            memory[key] = m;
        }
        return memory[key];
    }

    std::map<int, juce::MemoryBlock> memory;

    int value (int nrpn) const
    {
        const juce::ScopedLock sl (lock);
        auto* d = static_cast<const juce::uint8*> (program.getData());
        return d[2 * nrpn] | (d[2 * nrpn + 1] << 8);
    }

    void set16 (int offset, int value)
    {
        const juce::ScopedLock sl (lock);
        program[offset] = (char) (value & 0xff);
        program[offset + 1] = (char) (value >> 8);
    }

    juce::MemoryBlock program { (size_t) SysEx::programSize, true };
    NrpnDecoder decoder;
    juce::CriticalSection lock;
};

// ---- Fake synth process (part A) -------------------------------------------------------------

const juce::String fakeOutName = "FakeTEO5 Out", fakeInName = "FakeTEO5 In";

// Private test SysEx (0x7D = non-commercial ID): F0 7D 02 nrpnMSB nrpnLSB valMSB valLSB F7
// makes the fake transmit that NRPN, as if a knob was turned on the synth.
juce::MidiMessage transmitNrpnMessage (int nrpn, int value)
{
    const juce::uint8 d[] = { 0x7d, 0x02, (juce::uint8) (nrpn >> 7), (juce::uint8) (nrpn & 0x7f),
                              (juce::uint8) (value >> 7), (juce::uint8) (value & 0x7f) };
    return juce::MidiMessage::createSysExMessage (d, sizeof (d));
}

struct FakeProcess : juce::MidiInputCallback, juce::Timer
{
    explicit FakeProcess (juce::File mirrorFile) : mirror (std::move (mirrorFile))
    {
        toEditor = juce::MidiOutput::createNewDevice (fakeOutName);
        fromEditor = juce::MidiInput::createNewDevice (fakeInName, this);
        fromEditor->start();
        startTimer (10);
    }

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
    {
        auto* d = m.getSysExData();
        if (m.isSysEx() && m.getSysExDataSize() == 6 && d[0] == 0x7d && d[1] == 0x02)
        {
            const int nrpn = (d[2] << 7) | d[3], value = (d[4] << 7) | d[5];
            core.set16 (2 * nrpn, value);
            juce::MessageManager::callAsync ([this, nrpn, value]
            {
                for (auto& msg : Nrpn::encode (1, nrpn, value))
                    toEditor->sendMessageNow (msg);
            });
            return;
        }
        auto replies = core.process (m);
        if (! replies.isEmpty())
            juce::MessageManager::callAsync ([this, replies] { for (auto& r : replies) toEditor->sendMessageNow (r); });
    }

    void timerCallback() override
    {
        juce::MemoryBlock copy;
        {
            const juce::ScopedLock sl (core.lock);
            copy = core.program;
        }
        auto tmp = mirror.getSiblingFile (mirror.getFileName() + ".tmp");
        tmp.replaceWithData (copy.getData(), copy.getSize());
        tmp.moveFileTo (mirror);
    }

    FakeCore core;
    juce::File mirror;
    std::unique_ptr<juce::MidiOutput> toEditor;
    std::unique_ptr<juce::MidiInput> fromEditor;
};

int runFake (const juce::File& mirror)
{
    FakeProcess fake (mirror);
    auto marker = mirror.getSiblingFile (mirror.getFileName() + ".run");
    const auto deadline = juce::Time::getMillisecondCounter() + 10 * 60 * 1000;
    while (marker.existsAsFile() && juce::Time::getMillisecondCounter() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    return 0;
}

// ---- Test helpers ----------------------------------------------------------------------------

int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (! ok) ++failures;
}

std::function<void()> idleHook; // runs while waiting (used to shuttle host MIDI in part B)

void pump (int ms)
{
    if (idleHook) idleHook();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

template <typename Pred>
bool waitFor (Pred p, int timeoutMs)
{
    for (int t = 0; t < timeoutMs; t += 10)
    {
        if (p()) return true;
        pump (10);
    }
    return p();
}

juce::String findId (const juce::Array<juce::MidiDeviceInfo>& devs, const juce::String& name)
{
    for (auto& d : devs)
        if (d.name == name)
            return d.identifier;
    return {};
}

int mirrorValue (const juce::File& mirror, int nrpn)
{
    juce::MemoryBlock b;
    if (! mirror.loadFileAsData (b) || (int) b.getSize() < 2 * nrpn + 2)
        return -1;
    auto* d = static_cast<const juce::uint8*> (b.getData());
    return d[2 * nrpn] | (d[2 * nrpn + 1] << 8);
}

// ---- Part A ----------------------------------------------------------------------------------

void partA()
{
    std::cout << "Part A: CoreMIDI ports, fake TEO-5 in a separate process" << std::endl;

    auto tmpDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("teo5-loopback", {});
    tmpDir.createDirectory();
    auto mirror = tmpDir.getChildFile ("fake-program.bin");
    auto marker = mirror.getSiblingFile (mirror.getFileName() + ".run");
    marker.create();
    juce::ChildProcess child;
    const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
    child.start (juce::StringArray { self, "--fake", mirror.getFullPathName() }, 0);

    MidiSettings s;
    waitFor ([&]
    {
        s.inputId = findId (juce::MidiInput::getAvailableDevices(), fakeOutName);
        s.outputId = findId (juce::MidiOutput::getAvailableDevices(), fakeInName);
        return s.inputId.isNotEmpty() && s.outputId.isNotEmpty();
    }, 5000);
    check (s.inputId.isNotEmpty() && s.outputId.isNotEmpty(), "fake TEO-5 ports visible");

    {
        TEO5Processor proc;
        auto& sync = proc.getMidiSync();
        sync.applySettings (s);
        auto direct = juce::MidiOutput::openDevice (s.outputId); // test-only side channel to the fake

        proc.setNative ("cutoff", 777);
        proc.setNative ("env1Attack", 200);
        check (waitFor ([&] { return mirrorValue (mirror, 34) == 777 && mirrorValue (mirror, 81) == 200; }, 3000),
               "editor -> synth: cutoff 777 and env1 attack 200 arrive as 14-bit NRPN");

        direct->sendMessageNow (transmitNrpnMessage (96, 99));
        check (waitFor ([&] { return proc.getNative ("vintage") == 99; }, 3000), "synth -> editor: incoming NRPN moves Vintage");

        // Recording on the synth: it sends each changed sequence byte as an NRPN numbered by its offset.
        proc.getProgramState().sequence.addNote (0, 64, 100, true);       // an older chord on step 1 ...
        proc.getProgramState().sequence.addNote (0, 67, 100, false);      // ... (lanes 1 and 2)
        proc.getProgramState().sequence.addNote (5, 72, 100, false);      // and an older note on step 6
        direct->sendMessageNow (transmitNrpnMessage (4199, 1));          // RECORD on
        direct->sendMessageNow (transmitNrpnMessage (270, 47));          // lane 1, step 1: B1
        direct->sendMessageNow (transmitNrpnMessage (590, 60));          // its velocity
        direct->sendMessageNow (transmitNrpnMessage (190, 254));         // step 1 no longer a rest
        check (waitFor ([&] { const auto& sq = proc.getProgramState().sequence;
                              return sq.sounds (0, 0) && sq.at (0, 0).note == 47 && sq.at (0, 0).velocity == 60; }, 3000),
               "synth recording: sequence bytes sent as NRPN fill in the step live");
        check (proc.getProgramState().sequence.isEmpty (1, 0), "recording over a step clears its other notes, as the synth does");
        check (proc.getNative ("seqRecord") == 1, "synth RECORD (NRPN 4199) shows in the editor");
        check (proc.getProgramState().sequence.isEmpty (0, 5) && proc.getProgramState().sequence.length == 1,
               "RECORD on the synth clears the whole sequence first (length 1, as the synth starts over)");
        check (! proc.getProgramState().beforeSynthRecording.isEmpty (0, 5), "the sequence before recording is kept for undo");
        direct->sendMessageNow (transmitNrpnMessage (4199, 0));
        pump (300);
        check (mirrorValue (mirror, 96) == 99, "incoming change is not echoed back with a different value");

        for (int v = 0; v <= 127; ++v)
            proc.setNative ("resonance", v);
        check (waitFor ([&] { return mirrorValue (mirror, 36) == 127; }, 3000), "128-step sweep: last value arrives");

        // Right-click modulation: a new slot fills the first free one, amount none, and goes out as NRPN.
        {
            ModMatrixDrawer matrix (proc.getAPVTS());
            const int free = matrix.firstFreeSlot();
            const int lfo1 = modSourceNames().indexOf ("LFO 1"), cutoff = modDestinationNames().indexOf ("Cutoff");
            const int slot = matrix.assign (lfo1, cutoff);
            check (slot == free && slot > 0 && matrix.slotsFor (cutoff).contains (slot) && matrix.amountOf (slot) == 0,
                   "modulate from the panel: first free slot gets LFO 1 -> Cutoff, amount none");
            check (waitFor ([&] { return mirrorValue (mirror, 104 + slot) == lfo1 && mirrorValue (mirror, 136 + slot) == cutoff; }, 3000),
                   "the new slot's source and destination reach the synth");
            check (matrix.firstFreeSlot() != slot, "the slot is no longer free");

            auto press = [&matrix] (const juce::String& text)
            {
                for (auto* c : matrix.getChildren())
                    if (auto* tb = dynamic_cast<juce::TextButton*> (c); tb != nullptr && tb->getButtonText() == text)
                        if (tb->isEnabled() && tb->onClick)
                            { tb->onClick(); return true; }
                return false;
            };
            check (press ("Undo") && matrix.firstFreeSlot() == slot && ! matrix.slotsFor (cutoff).contains (slot),
                   "mod matrix Undo takes the new slot back");
            check (waitFor ([&] { return mirrorValue (mirror, 104 + slot) == 0 && mirrorValue (mirror, 136 + slot) == 0; }, 3000),
                   "the undo reaches the synth");
            check (press ("Redo") && matrix.slotsFor (cutoff).contains (slot), "mod matrix Redo puts it back");
        }

        // PANIC: All Notes Off and a note-off for every note.
        proc.getMidiSync().panic();

        // Notes played on the DAW track reach the synth, remapped to its channel.
        {
            juce::AudioBuffer<float> audio (2, 64);
            juce::MidiBuffer played;
            played.addEvent (juce::MidiMessage::noteOn (5, 64, (juce::uint8) 100), 0);
            proc.processBlock (audio, played);
            auto mirrorByte = [&] (int offset)
            {
                juce::MemoryBlock b;
                return mirror.loadFileAsData (b) && (int) b.getSize() > offset ? (int) static_cast<juce::uint8*> (b.getData())[offset] : -1;
            };
            check (waitFor ([&] { return mirrorByte (4000) == 64 && mirrorByte (4001) == 1; }, 3000),
                   "note played on the DAW track (ch 5) reaches the synth on its channel (ch 1)");
            check (waitFor ([&] { return mirrorByte (4002) == 1 && mirrorByte (4003) == 1; }, 3000),
                   "PANIC: All Notes Off and the note-offs reach the synth");
        }

        sync.requestEditBuffer();
        check (waitFor ([&] { return proc.getProgramState().raw.getSize() == (size_t) SysEx::programSize; }, 3000),
               "edit buffer dump received over SysEx (4096 bytes unpacked)");
        check (sync.getStatus().contains ("Program loaded"), "dump applied with the built-in layout (no probe needed)");
    }

    marker.deleteFile();
    child.waitForProcessToFinish (3000);
    child.kill();
    tmpDir.deleteRecursively();
}

// ---- Part B ----------------------------------------------------------------------------------

void partB()
{
    std::cout << "Part B: host MIDI routing, in-process fake (probe and dumps)" << std::endl;

    FakeCore fake;
    TEO5Processor proc;
    auto& sync = proc.getMidiSync();
    MidiSettings s;
    s.useHostMidi = true; // no direct ports: everything goes through processBlock
    sync.applySettings (s);

    // Shuttle: run processBlock, hand its MIDI output to the fake, feed the replies back in.
    juce::AudioBuffer<float> audio (2, 64);
    juce::MidiBuffer pendingIn;
    idleHook = [&]
    {
        juce::MidiBuffer midi;
        midi.swapWith (pendingIn);
        proc.processBlock (audio, midi);
        for (const auto meta : midi)
            for (auto& reply : fake.process (meta.getMessage()))
                pendingIn.addEvent (reply, 0);
    };

    proc.setNative ("cutoff", 777);
    check (waitFor ([&] { return fake.value (34) == 777; }, 2000), "NRPN goes out through the host MIDI output");

    const auto probeStart = juce::Time::getMillisecondCounter();
    sync.startLayoutProbe();
    waitFor ([&] { return ! sync.isProbing(); }, 600000);
    pump (300);
    std::cout << "  " << sync.getStatus() << " (" << (juce::Time::getMillisecondCounter() - probeStart) / 1000 << " s)" << std::endl;
    auto& layout = sync.getLayout();
    check (layout.size() >= 150, "layout probe mapped " + juce::String (layout.size()) + " parameters");
    if (auto* f = layout.field ("cutoff"))
        check (f->offset == 68 && f->width == 2 && f->littleEndian, "cutoff found at offset 68, 16-bit LE");
    else
        check (false, "cutoff mapped");
    if (auto* f = layout.field ("osc1Saw"))
        check (f->offset == 18 && f->width <= 2, "osc1Saw found at offset 18");
    else
        check (false, "osc1Saw mapped");
    if (auto* f = layout.field ("clockBpm"))
        check (f->offset == 314, "clock BPM (30-250) found at offset 314");
    else
        check (false, "clockBpm mapped");
    check (fake.value (34) == 777, "synth values restored after probing");
    check (ProgramLayout::defaultFile().existsAsFile(), "layout saved to user folder");

    // A change made on the synth only, then a dump.
    proc.setNative ("lfo1Freq", 5);
    waitFor ([&] { return fake.value (58) == 5; }, 2000);
    fake.set16 (2 * 58, 111);
    fake.set16 (2 * 157, 140);
    sync.requestEditBuffer();
    check (waitFor ([&] { return proc.getNative ("lfo1Freq") == 111 && proc.getNative ("clockBpm") == 140; }, 3000),
           "edit buffer dump updates the panel (LFO 1 freq 111, BPM 140)");
    pump (200);
    check (fake.value (58) == 111 && fake.value (157) == 140, "dump-applied values are not echoed back");
    proc.setNative ("lfo1Freq", 5); // an edit after loading
    check (sync.storedValue ("lfo1Freq") == 111 && sync.storedValue ("clockBpm") == 140,
           "stored program value (double-click target) ignores later edits");
    proc.setNative ("lfo1Freq", 111);
    check (sync.locateName ("Fake Oberheim"), "program name located in dump");
    check (proc.getProgramState().name == "Fake Oberheim", "program name read from dump");

    proc.setNative ("env2Decay", 222);
    waitFor ([&] { return fake.value (84) == 222; }, 2000);
    fake.set16 (2 * 84, 0); // synth drifts
    sync.sendEditBuffer();
    check (waitFor ([&] { return fake.value (84) == 222; }, 3000), "Send edit buffer: SysEx restores the synth to the panel");

    // Opening a saved DAW project: the saved program goes to the synth's edit buffer.
    {
        juce::MemoryBlock saved;
        proc.getStateInformation (saved);
        fake.set16 (2 * 84, 0); // the synth has something else loaded
        proc.setStateInformation (saved.getData(), (int) saved.getSize());
        check (waitFor ([&] { return fake.value (84) == 222; }, 3000), "project load: the saved program is sent to the synth");
    }

    // Sequencer: the program's sequence arrives with every dump, and edits go back as the edit buffer.
    {
        const juce::ScopedLock sl (fake.lock);
        fake.program[189] = (char) 24;   // length
        fake.program[190] = (char) 0xfe; // lane 1: step 1 plays, steps 2-8 rest
        fake.program[270] = (char) 45;   // A2
        fake.program[590] = (char) 110;
    }
    sync.requestEditBuffer();
    auto& seq = proc.getProgramState().sequence;
    check (waitFor ([&] { return seq.length == 24 && seq.sounds (0, 0) && seq.at (0, 0).note == 45 && ! seq.sounds (0, 1); }, 3000),
           "sequence (length, notes, rests) read from the program dump");
    seq.addNote (1, 50, 90, true);
    seq.setStepMode (2, Sequence::StepMode::Tie);
    seq.length = 12;
    sync.sendSequence();
    check (waitFor ([&]
           {
               const juce::ScopedLock sl (fake.lock);
               auto* d = static_cast<const juce::uint8*> (fake.program.getData());
               return d[189] == 12 && d[271] == 50 && d[591] == 90 && (d[190] & 0x02) == 0 // step 2 plays D3
                   && (d[230] & 0x02) != 0 && (d[190] & 0x04) != 0;                       // ...and holds into step 3
           }, 3000), "sequencer edits (note, tie, length) reach the synth as the edit buffer");

    std::cout << "Librarian" << std::endl;
    auto& lib = proc.getLibrary();
    auto& transfer = proc.getTransfer();
    transfer.fetchBanks ({ { 2, false } });
    pump (100);
    waitFor ([&] { return ! transfer.isBusy(); }, 60000);
    pump (300);
    std::cout << "  " << transfer.getStatus() << std::endl;
    int hwBank = -1;
    for (int i = 0; i < (int) lib.banks().size(); ++i)
        if (lib.banks()[(size_t) i].kind == LibraryBank::Kind::Hardware && lib.banks()[(size_t) i].hardwareBank == 2)
            hwBank = i;
    check (hwBank >= 0 && lib.banks()[(size_t) hwBank].programs.size() == 16, "fetch bank 2: 16 programs in a TEO-5 bank");
    if (hwBank >= 0 && lib.banks()[(size_t) hwBank].programs.size() == 16)
    {
        const auto a = MidiSync::addressFor (2, 3, false);
        const auto* p = lib.program ({ hwBank, 2 });
        check (*p->raw == fake.stored (a.dumpBank, a.dumpProgram), "program 2-03 data matches the synth's memory");
        check (p->name == "Stored " + juce::String (a.dumpBank * 128 + a.dumpProgram), "program name read from the dump");

        // Load into the edit buffer.
        sync.loadProgram (*p->raw);
        check (waitFor ([&] { const juce::ScopedLock sl (fake.lock); return fake.program == *p->raw; }, 3000),
               "library program loaded into the synth's edit buffer");

        // REVERT: an edit, then back to the program as loaded, through the edit buffer only (memory untouched).
        {
            size_t stored = 0;
            {
                const juce::ScopedLock sl (fake.lock);
                stored = fake.memory.size();
            }
            proc.setNative ("vintage", proc.getNative ("vintage") == 10 ? 20 : 10);
            check (waitFor ([&] { const juce::ScopedLock sl (fake.lock); return fake.program != *p->raw; }, 3000),
                   "an edit changes the synth's edit buffer");
            check (sync.revertToOriginal(), "REVERT finds the program as loaded");
            check (waitFor ([&] { const juce::ScopedLock sl (fake.lock); return fake.program == *p->raw; }, 3000),
                   "REVERT puts the loaded program back in the edit buffer");
            const juce::ScopedLock sl (fake.lock);
            check (fake.memory.size() == stored, "REVERT writes nothing to memory");
        }

        // PLAY: a program load keeps the sequence playing; a program change (which stops the synth's sequencer)
        // turns PLAY off.
        proc.setNative ("seqPlay", 1);
        pump (50);
        sync.loadProgram (*p->raw);
        pump (50);
        check (proc.getNative ("seqPlay") == 1, "loading a program keeps PLAY on (Play is resent)");
        sync.selectProgram (3, 2, true);
        check (proc.getNative ("seqPlay") == 0, "a program change turns PLAY off");

        // Write the bank to another location.
        std::vector<juce::MemoryBlock> progs;
        for (auto& lp : lib.banks()[(size_t) hwBank].programs)
            progs.push_back (*lp.raw);
        transfer.sendBank (progs, { 5, false });
        pump (100);
        waitFor ([&] { return ! transfer.isBusy(); }, 60000);
        pump (300);
        bool all = true;
        for (int i = 0; i < 16; ++i)
        {
            const auto t = MidiSync::addressFor (5, i + 1, false);
            const juce::ScopedLock sl (fake.lock);
            all = all && fake.memory.count (t.dumpBank * 128 + t.dumpProgram) && fake.memory[t.dumpBank * 128 + t.dumpProgram] == progs[(size_t) i];
        }
        check (all, "send bank: 16 programs written to bank 5 in order");
        check (transfer.lastSendCompleted(), "send bank reports completion (clears NOT SENT marks)");

        // Two-column library: drag between the TEO-5 column and MY BANKS through the drawer's drop handling.
        {
            teo5::LibraryDrawer drawer (lib, transfer, sync, proc.getProgramState());
            drawer.setSize (900, 600);
            const int mine = lib.addBank ("Loopback Mine");
            pump (50);
            auto rowOfBank = [&] (int pane, int bank)
            {
                const auto& rows = drawer.rows (pane);
                for (int i = 0; i < (int) rows.size(); ++i)
                    if (rows[(size_t) i].isBank && rows[(size_t) i].bank == bank)
                        return i;
                return -1;
            };
            check (rowOfBank (0, hwBank) >= 0 && rowOfBank (1, hwBank) < 0, "TEO-5 bank listed in the TEO-5 column only");
            check (rowOfBank (1, mine) >= 0 && rowOfBank (0, mine) < 0, "custom bank listed in MY BANKS only");
            check (drawer.isCollapsed (hwBank) && drawer.isCollapsed (mine), "banks start collapsed");
            drawer.toggleCollapsed (hwBank); // open them, as the user would to pick programs
            drawer.toggleCollapsed (mine);

            drawer.selectPrograms ({ ProgramRef (hwBank, 4), ProgramRef (hwBank, 5) });
            drawer.dropProgramsAt (1, rowOfBank (1, mine), false);
            pump (50);
            const auto used = std::count_if (lib.banks()[(size_t) mine].programs.begin(), lib.banks()[(size_t) mine].programs.end(),
                                             [] (const LibraryProgram& p) { return ! p.isEmpty(); }); // banks show 16 slots
            check (used == 2 && lib.banks()[(size_t) hwBank].programs.size() == 16
                       && *lib.program ({ mine, 0 })->raw == *lib.program ({ hwBank, 4 })->raw,
                   "drag TEO-5 -> MY BANKS copies two programs, TEO-5 bank unchanged");

            const auto replacedBefore = *lib.program ({ hwBank, 0 })->raw;
            drawer.selectPrograms ({ ProgramRef (mine, 1) });
            drawer.dropProgramsAt (0, rowOfBank (0, hwBank) + 1, false); // onto slot 1
            pump (50);
            check (lib.banks()[(size_t) hwBank].programs.size() == 16 && lib.program ({ hwBank, 0 })->unsent
                       && *lib.program ({ hwBank, 0 })->raw == *lib.program ({ mine, 1 })->raw
                       && *lib.program ({ hwBank, 0 })->raw != replacedBefore,
                   "drag MY BANKS -> TEO-5 slot 1 replaces it and marks it NOT SENT");
            lib.getUndoManager().undo();
            check (*lib.program ({ hwBank, 0 })->raw == replacedBefore, "undo restores the replaced slot");
            lib.removeBank (mine);
        }

        // Capture the panel into a program (used by "+ Current", COMPARE and WRITE).
        // (Cutoff: the fake keeps Vintage at byte 192, inside the area a real TEO-5 uses for the sequence.)
        proc.setNative ("vintage", 77);
        proc.setNative ("cutoff", 500);
        auto cap = sync.captureCurrent();
        auto* cf = sync.getLayout().field ("cutoff");
        check (! cap.isEmpty() && cf != nullptr && sync.getLayout().read (cap, *cf) == 500, "captured program contains panel edits");
    }

    juce::MemoryBlock st;
    proc.getStateInformation (st);
    idleHook = nullptr;
    TEO5Processor proc2;
    proc2.setStateInformation (st.getData(), (int) st.getSize());
    check (proc2.getMidiSync().getSettings().useHostMidi, "MIDI settings restored from DAW state");
    check (proc2.getNative ("cutoff") == proc.getNative ("cutoff") && proc2.getNative ("vintage") == 77
               && proc2.getNative ("lfo1Freq") == proc.getNative ("lfo1Freq"), "parameters restored");
    check (proc2.getProgramState().raw == proc.getProgramState().raw, "raw program restored");
    check (proc2.getProgramState().name == proc.getProgramState().name, "program name restored");
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc > 2 && juce::String (argv[1]) == "--fake")
        return runFake (juce::File (argv[2]));

    // Keep the test library away from the user's library.
    auto libDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("teo5-loopback-lib", {});
    setenv ("TEO5_LIBRARY_DIR", libDir.getFullPathName().toRawUTF8(), 1);

    // ... and the probed (fake) layout away from the user's layout file.
    auto layoutFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("teo5-loopback-layout", ".json");
    setenv ("TEO5_LAYOUT_FILE", layoutFile.getFullPathName().toRawUTF8(), 1);

    partA();
    partB();

    layoutFile.deleteFile();
    libDir.deleteRecursively();

    std::cout << (failures == 0 ? "LOOPBACK PASSED" : juce::String (failures) + " FAILURE(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
