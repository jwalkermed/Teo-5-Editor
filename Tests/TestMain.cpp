// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
// Unit tests for the TEO-5 MIDI layer. Run: build/TEO5Tests_artefacts/Release/TEO5Tests (or `ctest`).

#include <juce_audio_basics/juce_audio_basics.h>
#include "Midi/Nrpn.h"
#include "Midi/SysEx.h"
#include "Midi/ProgramLayout.h"
#include "Midi/MidiSync.h"
#include "Params/ParamTable.h"
#include "Library/LibraryModel.h"
#include "Library/SyxIO.h"

using namespace teo5;

class SysExTests : public juce::UnitTest
{
public:
    SysExTests() : juce::UnitTest ("SysEx", "TEO5") {}

    void runTest() override
    {
        beginTest ("Pack matches the documented bit layout");
        {
            const juce::uint8 in[] = { 0x80, 0x01, 0xff, 0x7f, 0x00, 0x81, 0xc0 };
            auto p = SysEx::pack (in, sizeof (in));
            expectEquals ((int) p.getSize(), 8);
            auto* d = static_cast<const juce::uint8*> (p.getData());
            // MS bits: A=1 B=0 C=1 D=0 E=0 F=1 G=1  ->  0b1100101
            expectEquals ((int) d[0], 0x65);
            expectEquals ((int) d[1], 0x00);
            expectEquals ((int) d[3], 0x7f);
            expectEquals ((int) d[7], 0x40);
            for (size_t i = 0; i < p.getSize(); ++i)
                expect (d[i] < 0x80, "packed bytes must be 7-bit");
        }

        beginTest ("Pack/unpack round-trip, 4096-byte program");
        {
            juce::Random rng (42);
            juce::MemoryBlock prog (SysEx::programSize);
            for (size_t i = 0; i < prog.getSize(); ++i)
                prog[i] = (char) rng.nextInt (256);

            auto packed = SysEx::pack (prog.getData(), prog.getSize());
            expectEquals ((int) packed.getSize(), (int) SysEx::packedSize (SysEx::programSize));
            expectEquals ((int) packed.getSize(), 4682);
            auto unpacked = SysEx::unpack (packed.getData(), packed.getSize());
            expect (unpacked == prog, "round-trip must be lossless");
        }

        beginTest ("Round-trip for every partial-packet length");
        for (int n = 0; n < 30; ++n)
        {
            juce::MemoryBlock b ((size_t) n);
            for (int i = 0; i < n; ++i)
                b[i] = (char) (i * 37 + 0x80);
            auto u = SysEx::unpack (SysEx::pack (b.getData(), b.getSize()).getData(), SysEx::packedSize ((size_t) n));
            expect (u == b, "length " + juce::String (n));
        }

        beginTest ("Request messages");
        {
            auto r = SysEx::requestEditBuffer();
            expect (r.isSysEx());
            auto* d = r.getRawData();
            expectEquals (r.getRawDataSize(), 5);
            expectEquals ((int) d[0], 0xf0);
            expectEquals ((int) d[1], 0x10); // real Oberheim ID (spec prints 0x02)
            expectEquals ((int) d[2], 0x5a);
            expectEquals ((int) d[3], 0x06);
            expectEquals ((int) d[4], 0xf7);

            auto p = SysEx::requestProgram (3, 99);
            expectEquals (p.getRawDataSize(), 7);
            expectEquals ((int) p.getRawData()[3], 0x05);
            expectEquals ((int) p.getRawData()[4], 3);
            expectEquals ((int) p.getRawData()[5], 99);
        }

        beginTest ("Edit buffer and program dumps parse back");
        {
            juce::MemoryBlock prog (SysEx::programSize);
            for (size_t i = 0; i < prog.getSize(); ++i)
                prog[i] = (char) (i & 0xff);

            auto eb = SysEx::parse (SysEx::editBufferDump (prog));
            expect (eb.type == SysEx::Parsed::Type::EditBuffer);
            expect (eb.data == prog);

            auto pd = SysEx::parse (SysEx::programDump (2, 17, prog));
            expect (pd.type == SysEx::Parsed::Type::Program);
            expectEquals (pd.bank, 2);
            expectEquals (pd.program, 17);
            expect (pd.data == prog);
        }

        beginTest ("Real TEO-5 program size: 4102 bytes = 4688 packed");
        {
            expectEquals ((int) SysEx::packedSize (4102), 4688);
            expect (SysEx::isProgramSize (4096) && SysEx::isProgramSize (4102) && ! SysEx::isProgramSize (4000));
            juce::MemoryBlock prog (4102);
            for (size_t i = 0; i < prog.getSize(); ++i)
                prog[i] = (char) (i * 7);
            auto dump = SysEx::programDump (4, 11, prog);
            expectEquals (dump.getRawDataSize(), 4695); // as captured from "Dump Preset" of user 512
            auto parsed = SysEx::parse (dump);
            expect (parsed.type == SysEx::Parsed::Type::Program && parsed.bank == 4 && parsed.program == 11);
            expect (parsed.data == prog);
            expectEquals (SysEx::editBufferDump (prog).getRawDataSize(), 4693);
        }

        beginTest ("Bank/program addressing matches the hardware capture");
        {
            auto a = MidiSync::addressFor (1, 1, false);   // user 101
            expect (a.bankSelect == 0 && a.programChange == 0);
            a = MidiSync::addressFor (16, 16, false);       // user G16
            expect (a.bankSelect == 15 && a.programChange == 15);
            a = MidiSync::addressFor (1, 1, true);          // factory 101
            expect (a.bankSelect == 16 && a.programChange == 0);
            a = MidiSync::addressFor (16, 16, true);        // factory G16
            expect (a.bankSelect == 31 && a.programChange == 15);
            a = MidiSync::addressFor (5, 12, false);        // user 512
            expect (a.bankSelect == 4 && a.programChange == 11 && a.dumpBank == 4 && a.dumpProgram == 11);
            auto r = SysEx::requestProgram (a.dumpBank + 16, a.dumpProgram);
            expectEquals ((int) r.getRawData()[4], 20); // factory banks aren't masked off
        }

        beginTest ("Device inquiry reply");
        {
            const juce::uint8 reply[] = { 0x7e, 0x7f, 0x06, 0x02, 0x10, 0x5a, 0x01, 0x00, 0x00, 0x01, 0x02, 0x03 };
            auto parsed = SysEx::parse (juce::MidiMessage::createSysExMessage (reply, sizeof (reply)));
            expect (parsed.type == SysEx::Parsed::Type::InquiryReply);
            expectEquals (parsed.firmware, juce::String ("1.2.3"));
            expectEquals (parsed.manufacturer, 0x10);
            expectEquals (parsed.family, 0x5a);
        }

        beginTest ("IDs learned from the synth are used for requests and parsing");
        {
            SysEx::setIds (0x10, 0x33);
            auto r = SysEx::requestEditBuffer();
            expectEquals ((int) r.getRawData()[1], 0x10);
            expectEquals ((int) r.getRawData()[2], 0x33);
            juce::MemoryBlock prog (SysEx::programSize, true);
            expect (SysEx::parse (SysEx::editBufferDump (prog)).type == SysEx::Parsed::Type::EditBuffer);
            SysEx::setIds (SysEx::defaultManufacturerId, SysEx::defaultDeviceId);
        }

        beginTest ("Global dump captured from a real TEO-5: unpacked byte i = global NRPN 4096 + i");
        {
            const juce::uint8 dump[] = { 0x10, 0x5a, 0x0f,
                0x00, 0x0c, 0x32, 0x00, 0x05, 0x01, 0x00, 0x02, 0x00, 0x02, 0x01, 0x01, 0x01, 0x03, 0x01, 0x01,
                0x00, 0x01, 0x02, 0x01, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00 };
            auto parsed = SysEx::parse (juce::MidiMessage::createSysExMessage (dump, sizeof (dump)));
            expect (parsed.type == SysEx::Parsed::Type::Globals);
            expectEquals ((int) parsed.data.getSize(), 28);
            auto at = [&parsed] (int nrpn) { return (int) static_cast<const juce::uint8*> (parsed.data.getData())[nrpn - 4096]; };
            expectEquals (at (4096), 12); // coarse tune centre
            expectEquals (at (4097), 50); // fine tune centre
            expectEquals (at (4099), 5);  // MIDI Clock = In No S/S
            expectEquals (at (4102), 2);  // Param Send = NRPN
            expectEquals (at (4107), 3);  // MIDI Out Select = All
            expectEquals (at (4110), 1);  // MIDI Arp Notes = On
            expectEquals (at (4114), 3);  // Foot Pedal = LPF Full
        }

        beginTest ("Basic Program (captured from a TEO-5) is built in");
        {
            const auto basic = MidiSync::basicProgram();
            expect (SysEx::isProgramSize (basic.getSize()));
            if (SysEx::isProgramSize (basic.getSize()))
            {
                auto* b = static_cast<const juce::uint8*> (basic.getData());
                expectEquals (juce::String (juce::CharPointer_ASCII ((const char*) b + 159), 13), juce::String ("Basic Program"));
                expect (b[13] == 1 && b[14] == 0, "only Osc 1 is on");
            }
        }

        beginTest ("Foreign SysEx is ignored");
        {
            const juce::uint8 other[] = { 0x01, 0x23, 0x01, 0x02 };
            expect (SysEx::parse (juce::MidiMessage::createSysExMessage (other, sizeof (other))).type == SysEx::Parsed::Type::None);
        }
    }
};

class NrpnTests : public juce::UnitTest
{
public:
    NrpnTests() : juce::UnitTest ("NRPN", "TEO5") {}

    void runTest() override
    {
        beginTest ("Encode produces CC 99/98/6/38");
        {
            auto msgs = Nrpn::encode (3, 34, 1000);
            expectEquals (msgs.size(), 4);
            expectEquals (msgs[0].getControllerNumber(), 99);
            expectEquals (msgs[0].getControllerValue(), 0);
            expectEquals (msgs[1].getControllerNumber(), 98);
            expectEquals (msgs[1].getControllerValue(), 34);
            expectEquals (msgs[2].getControllerNumber(), 6);
            expectEquals (msgs[2].getControllerValue(), 1000 >> 7);
            expectEquals (msgs[3].getControllerNumber(), 38);
            expectEquals (msgs[3].getControllerValue(), 1000 & 0x7f);
            expectEquals (msgs[0].getChannel(), 3);
        }

        beginTest ("Encode/decode round-trip for every program parameter");
        {
            NrpnDecoder dec;
            int gotParam = -1, gotValue = -1;
            dec.onNrpn = [&] (int, int p, int v) { gotParam = p; gotValue = v; };
            for (auto& def : allParams())
            {
                if (def.nrpn < 0)
                    continue;
                for (int v : { def.minValue, def.maxValue, (def.minValue + def.maxValue) / 2 })
                {
                    for (auto& m : Nrpn::encode (1, def.nrpn, v))
                        dec.process (m);
                    expectEquals (gotParam, def.nrpn);
                    expectEquals (gotValue, v);
                }
            }
        }

        beginTest ("Running parameter: value-only messages reuse the selected NRPN");
        {
            NrpnDecoder dec;
            int count = 0, last = -1;
            dec.onNrpn = [&] (int, int p, int v) { ++count; last = v; expectEquals (p, 81); };
            for (auto& m : Nrpn::encode (1, 81, 10))
                dec.process (m);
            dec.process (juce::MidiMessage::controllerEvent (1, 6, 1));
            dec.process (juce::MidiMessage::controllerEvent (1, 38, 2));
            expectEquals (count, 2);
            expectEquals (last, 130);
        }

        beginTest ("Increment / decrement");
        {
            NrpnDecoder dec;
            int delta = 0;
            dec.onNrpnDelta = [&] (int, int p, int d) { expectEquals (p, 58); delta += d; };
            for (auto& m : Nrpn::encode (1, 58, 64))
                dec.process (m);
            dec.process (juce::MidiMessage::controllerEvent (1, 96, 0));
            dec.process (juce::MidiMessage::controllerEvent (1, 96, 0));
            dec.process (juce::MidiMessage::controllerEvent (1, 97, 0));
            expectEquals (delta, 1);
        }

        beginTest ("RPN null resets the selected NRPN");
        {
            NrpnDecoder dec;
            int count = 0;
            dec.onNrpn = [&] (int, int, int) { ++count; };
            dec.process (juce::MidiMessage::controllerEvent (1, 99, 0));
            dec.process (juce::MidiMessage::controllerEvent (1, 98, 5));
            dec.process (juce::MidiMessage::controllerEvent (1, 101, 127));
            dec.process (juce::MidiMessage::controllerEvent (1, 100, 127));
            expect (! dec.process (juce::MidiMessage::controllerEvent (1, 6, 1)), "data entry ignored after reset");
            expect (! dec.process (juce::MidiMessage::controllerEvent (1, 38, 1)));
            expectEquals (count, 0);
        }

        beginTest ("Channels are tracked independently");
        {
            NrpnDecoder dec;
            juce::Array<int> params;
            dec.onNrpn = [&] (int ch, int p, int) { params.add (ch * 1000 + p); };
            auto a = Nrpn::encode (1, 10, 1);
            auto b = Nrpn::encode (2, 20, 1);
            for (int i = 0; i < 4; ++i) { dec.process (a[i]); dec.process (b[i]); }
            expect (params.contains (1010) && params.contains (2020));
        }

        beginTest ("Unrelated CCs pass through");
        {
            NrpnDecoder dec;
            expect (! dec.process (juce::MidiMessage::controllerEvent (1, 74, 10)));
        }
    }
};

class LayoutTests : public juce::UnitTest
{
public:
    LayoutTests() : juce::UnitTest ("ProgramLayout", "TEO5") {}

    void runTest() override
    {
        beginTest ("Built-in layout: byte = NRPN, as measured on a TEO-5");
        {
            auto l = ProgramLayout::builtIn();
            auto* cutoff = l.field ("cutoff");
            expect (cutoff != nullptr && cutoff->offset == 34 && cutoff->width == 2 && cutoff->littleEndian);
            auto* amt = l.field ("mod1Amt");
            expect (amt != nullptr && amt->offset == 121 && amt->width == 1);
            expect (l.field ("mod16Dest") != nullptr && l.field ("mod16Dest")->offset == 152);
            expect (l.field ("arpRelatch") != nullptr && l.field ("arpRelatch")->offset == 187);
            expectEquals (l.nameOffset, 159);

            // Bytes captured from a TEO-5 edit buffer: mod 1-3 amounts and cutoff 213 (D5 00).
            juce::MemoryBlock p (4102, true);
            p[121] = (char) 130; p[122] = (char) 129; p[123] = (char) 138; p[34] = (char) 213;
            expectEquals (l.read (p, *amt), 130);
            expectEquals (l.read (p, *cutoff), 213);
        }

        beginTest ("Saved layouts from the old probe are not used for fields");
        {
            auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("teo5-layout", ".json");
            ProgramLayout old;
            old.setField ("osc2Pulse", { 7, 1, true }); // a real mis-detection by the old probe
            old.save (file);
            ProgramLayout loaded;
            expect (loaded.load (file));
            expect (loaded.fieldsVersion < ProgramLayout::currentFieldsVersion, "old file is recognisable as old");
            file.deleteFile();
        }

        beginTest ("Infer prefers the expected offset when another byte changes in step");
        {
            juce::MemoryBlock a (4096, true), b (4096, true);
            b[7] = 1;  b[12] = 1; // probing NRPN 12 also flips byte 7
            auto f = ProgramLayout::infer ({ { a, 0 }, { b, 1 }, { a, 0 } }, 12);
            expect (f.has_value() && f->offset == 12);
        }

        beginTest ("Infer 1-byte field");
        {
            juce::MemoryBlock a (4096, true), b (4096, true);
            b[100] = 127;
            auto f = ProgramLayout::infer ({ { a, 0 }, { b, 127 } });
            expect (f.has_value());
            expectEquals (f->offset, 100);
            expectEquals (f->width, 1);
        }

        beginTest ("Infer 2-byte little- and big-endian fields");
        {
            auto le = [] (int off, int v) { juce::MemoryBlock m (4096, true); m[off] = (char) (v & 0xff); m[off + 1] = (char) (v >> 8); return m; };
            auto be = [] (int off, int v) { juce::MemoryBlock m (4096, true); m[off] = (char) (v >> 8); m[off + 1] = (char) (v & 0xff); return m; };

            auto f = ProgramLayout::infer ({ { le (200, 0), 0 }, { le (200, 1024), 1024 }, { le (200, 513), 513 } });
            expect (f.has_value() && f->offset == 200 && f->width == 2 && f->littleEndian);

            auto g = ProgramLayout::infer ({ { be (300, 0), 0 }, { be (300, 256), 256 }, { be (300, 129), 129 } });
            expect (g.has_value() && g->offset == 300 && g->width == 2 && ! g->littleEndian);
        }

        beginTest ("Name field detected from a bank of programs");
        {
            const char* names[] = { "Glacial Pad", "Eastern Bells", "Baumann's Seq", "Pulse Ping", "LeadReed", "Bounce Back",
                                    "BuzzFlow", "Super Collider", "Plektrum", "Rosewood Perc", "Solid Square", "Dirty Keys",
                                    "Bandpass Arp", "Life Clock", "Mouth Pad", "2049" };
            std::vector<juce::MemoryBlock> progs;
            juce::Random rng (5);
            for (auto* n : names)
            {
                juce::MemoryBlock m (4096);
                for (size_t i = 0; i < m.getSize(); ++i)
                    m[i] = (char) rng.nextInt (128); // parameter-like bytes, many of them printable
                for (int i = 0; i < 20; ++i)
                    m[(size_t) (1700 + i)] = i < (int) strlen (n) ? n[i] : ' ';
                m[1699] = 0; // field boundary
                m[1720] = 0;
                // Decoy seen on real hardware: sequencer steps full of note 60 ('<') with a varying tail.
                for (int i = 0; i < 27; ++i)
                    m[(size_t) (900 + i)] = '<';
                for (int i = 27; i < 30; ++i)
                    m[(size_t) (900 + i)] = (char) (0x41 + rng.nextInt (60));
                m[899] = 0;
                m[930] = 0;
                progs.push_back (m);
            }
            auto field = ProgramLayout::detectNameField (progs);
            expect (field.has_value());
            if (field)
            {
                expectEquals (field->first, 1700);
                expectEquals (field->second, 20);
                ProgramLayout l;
                l.nameOffset = field->first;
                l.nameLength = field->second;
                expectEquals (l.readName (progs[2]), juce::String ("Baumann's Seq"));
            }
        }

        beginTest ("Read/write and name round-trip");
        {
            ProgramLayout l;
            juce::MemoryBlock p (4096, true);
            ProgramLayout::Field f { 10, 2, true };
            l.write (p, f, 777);
            expectEquals (l.read (p, f), 777);
            l.nameOffset = 50;
            l.writeName (p, "It's an Oberheim");
            expectEquals (l.readName (p), juce::String ("It's an Oberheim"));
        }
    }
};

static juce::MemoryBlock fakeProgram (int seed)
{
    juce::MemoryBlock m (SysEx::programSize);
    juce::Random rng (seed);
    for (size_t i = 0; i < m.getSize(); ++i)
        m[i] = (char) rng.nextInt (256);
    return m;
}

class LibraryTests : public juce::UnitTest
{
public:
    LibraryTests() : juce::UnitTest ("Library", "TEO5") {}

    void runTest() override
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("teo5-lib-test", {});
        // Programs in a bank, not counting empty slots (banks always show at least 16 slots).
        auto used = [] (const LibraryModel& lib, int b)
        {
            int n = 0;
            for (auto& p : lib.banks()[(size_t) b].programs)
                n += p.isEmpty() ? 0 : 1;
            return n;
        };

        beginTest ("New library has a default bank");
        {
            LibraryModel lib (dir);
            expectEquals ((int) lib.banks().size(), 1);
        }

        beginTest ("Insert, rename, tags, favourite, undo/redo");
        {
            LibraryModel lib (dir);
            for (int i = 0; i < 4; ++i)
                lib.insertProgram (0, -1, LibraryModel::makeProgram (fakeProgram (i), "P" + juce::String (i), "test"));
            expectEquals ((int) lib.banks()[0].programs.size(), 4, "your own banks hold just their programs");
            lib.renameProgram ({ 0, 1 }, "Brass");
            lib.setTags ({ 0, 1 }, { "brass", "warm" });
            lib.setFavorite ({ 0, 2 }, true);
            expectEquals (lib.program ({ 0, 1 })->name, juce::String ("Brass"));

            lib.getUndoManager().undo(); // favourite
            lib.getUndoManager().undo(); // tags
            lib.getUndoManager().undo(); // rename
            expectEquals (lib.program ({ 0, 1 })->name, juce::String ("P1"));
            expect (! lib.program ({ 0, 2 })->favorite);
            lib.getUndoManager().redo();
            expectEquals (lib.program ({ 0, 1 })->name, juce::String ("Brass"));
            lib.getUndoManager().redo();
            lib.getUndoManager().redo();
            expect (lib.program ({ 0, 2 })->favorite);
            expect (lib.save());
        }

        beginTest ("Persistence round-trip");
        {
            LibraryModel lib (dir);
            expectEquals ((int) lib.banks()[0].programs.size(), 4);
            auto* p = lib.program ({ 0, 1 });
            expectEquals (p->name, juce::String ("Brass"));
            expect (p->tags.contains ("warm"));
            expect (*p->raw == fakeProgram (1), "program data survives save/load");
            expect (lib.program ({ 0, 2 })->favorite);
            expect (dir.getChildFile ("programs").getChildFile (p->uuid + ".syx").existsAsFile());
        }

        beginTest ("Your banks: swap within a bank, move between banks, no empty slots");
        {
            LibraryModel lib (dir);
            const auto n0 = lib.program ({ 0, 0 })->name, n3 = lib.program ({ 0, 3 })->name;
            lib.swapPrograms ({ 0, 0 }, { 0, 3 });
            expectEquals (lib.program ({ 0, 0 })->name, n3);
            expectEquals (lib.program ({ 0, 3 })->name, n0);
            expect (lib.getUndoManager().getUndoDescription().startsWith ("Swapped"), "undo says what it undoes");
            lib.getUndoManager().undo();
            expectEquals (lib.program ({ 0, 0 })->name, n0);

            const int b = lib.addBank ("Second");
            lib.movePrograms ({ ProgramRef (0, 1) }, b, -1);
            expectEquals ((int) lib.banks()[0].programs.size(), 3, "the bank closes up");
            expectEquals ((int) lib.banks()[(size_t) b].programs.size(), 1);
            lib.getUndoManager().undo();
            expectEquals ((int) lib.banks()[0].programs.size(), 4);
            lib.removeBank (b);
        }

        beginTest ("Within a TEO-5 User bank a drag moves: the old slot is empty and NOT SENT");
        {
            LibraryModel lib (dir);
            std::vector<LibraryProgram> hw;
            for (int i = 0; i < 16; ++i)
                hw.push_back (LibraryModel::makeProgram (fakeProgram (60 + i), "M" + juce::String (i + 1), "TEO-5 User 9 / " + juce::String (i + 1)));
            const int hb = lib.upsertHardwareBank ("TEO-5 User 9", 9, false, hw);
            lib.removePrograms ({ ProgramRef (hb, 1) });
            expect (lib.program ({ hb, 1 })->isEmpty() && lib.program ({ hb, 1 })->unsent, "deleted slot is NOT SENT");
            expectEquals (lib.moveIntoSlots ({ ProgramRef (hb, 5) }, hb, 1), 1);
            expectEquals (lib.program ({ hb, 1 })->name, juce::String ("M6"));
            expect (lib.program ({ hb, 1 })->unsent);
            expect (lib.program ({ hb, 5 })->isEmpty() && lib.program ({ hb, 5 })->unsent, "the slot it left is empty and NOT SENT");
            expectEquals ((int) lib.banks()[(size_t) hb].programs.size(), 16);
            const auto n3 = lib.program ({ hb, 2 })->name, n7 = lib.program ({ hb, 6 })->name;
            lib.swapPrograms ({ hb, 2 }, { hb, 6 });
            expectEquals (lib.program ({ hb, 2 })->name, n7);
            expectEquals (lib.program ({ hb, 6 })->name, n3);
            expect (lib.program ({ hb, 2 })->unsent && lib.program ({ hb, 6 })->unsent, "both swapped slots are NOT SENT");
            expectEquals (lib.program ({ hb, 2 })->source, juce::String ("TEO-5 User 9 / 3"), "slots keep their location labels");
            lib.removeBank (hb);
        }

        beginTest ("Copy / paste makes independent copies");
        {
            LibraryModel lib (dir);
            lib.copyPrograms ({ ProgramRef (0, 1), ProgramRef (0, 2) });
            lib.pastePrograms (0, -1); // after the last program
            expectEquals (used (lib, 0), 6);
            expect (lib.program ({ 0, 4 })->uuid != lib.program ({ 0, 1 })->uuid);
            expectEquals (lib.program ({ 0, 4 })->name, lib.program ({ 0, 1 })->name);
        }

        beginTest ("Hardware bank upsert replaces the earlier fetch");
        {
            LibraryModel lib (dir);
            const int before = (int) lib.banks().size();
            std::vector<LibraryProgram> progs { LibraryModel::makeProgram (fakeProgram (9), "A", "hw") };
            const int i1 = lib.upsertHardwareBank ("TEO-5 User 2", 2, false, progs);
            const int i2 = lib.upsertHardwareBank ("TEO-5 User 2", 2, false, progs);
            expectEquals (i1, i2);
            expectEquals ((int) lib.banks().size(), before + 1);
            lib.upsertHardwareBank ("TEO-5 Factory 2", 2, true, progs);
            expectEquals ((int) lib.banks().size(), before + 2);
        }

        beginTest ("Deleting from your own bank removes the program; a TEO-5 bank keeps an empty slot");
        {
            LibraryModel lib (dir);
            const int b = lib.addBank ("Slots");
            for (int i = 0; i < 3; ++i)
                lib.insertProgram (b, -1, LibraryModel::makeProgram (fakeProgram (40 + i), "S" + juce::String (i + 1), "test"));
            lib.removePrograms ({ ProgramRef (b, 1) });
            expectEquals ((int) lib.banks()[(size_t) b].programs.size(), 2);
            expectEquals (lib.program ({ b, 1 })->name, juce::String ("S3"));
            expect (lib.getUndoManager().getUndoDescription().contains ("S2"), "undo names the deleted program");
            for (int i = 0; i < 20; ++i) // any number of programs
                lib.insertProgram (b, -1, LibraryModel::makeProgram (fakeProgram (80), "X", "test"));
            expectEquals ((int) lib.banks()[(size_t) b].programs.size(), 22);

            std::vector<LibraryProgram> hw;
            for (int i = 0; i < 16; ++i)
                hw.push_back (LibraryModel::makeProgram (fakeProgram (90 + i), "H" + juce::String (i + 1), "hw"));
            const int hb = lib.upsertHardwareBank ("TEO-5 User 8", 8, false, hw);
            lib.removePrograms ({ ProgramRef (hb, 1) });
            expect (lib.program ({ hb, 1 })->isEmpty() && lib.program ({ hb, 1 })->unsent);
            expect (lib.save());
            LibraryModel reloaded (dir);
            expect (reloaded.program ({ hb, 1 })->isEmpty() && reloaded.program ({ hb, 1 })->unsent, "empty NOT SENT slot is saved");
            expectEquals ((int) reloaded.banks()[(size_t) hb].programs.size(), 16);
            lib.removeBank (hb);
            lib.removeBank (b);
        }

        beginTest ("Banks can be reordered");
        {
            LibraryModel lib (dir);
            while (! lib.banks().empty())
                lib.removeBank (0);
            for (auto* n : { "A", "B", "C", "D" })
                lib.addBank (n);
            auto order = [&lib] { juce::String o; for (auto& b : lib.banks()) o << b.name; return o; };
            expectEquals (lib.moveBank (0, 3), 2);
            expectEquals (order(), juce::String ("BCAD"));
            expectEquals (lib.moveBank (3, 0), 0);
            expectEquals (order(), juce::String ("DBCA"));
            lib.moveBank (1, 4);
            expectEquals (order(), juce::String ("DCAB"));
            lib.getUndoManager().undo();
            expectEquals (order(), juce::String ("DBCA"));
        }

        beginTest ("Factory banks are read-only");
        {
            LibraryModel lib (dir);
            std::vector<LibraryProgram> progs { LibraryModel::makeProgram (fakeProgram (11), "F1", "hw"),
                                                LibraryModel::makeProgram (fakeProgram (12), "F2", "hw") };
            const int fb = lib.upsertHardwareBank ("TEO-5 Factory 7", 7, true, progs);
            const int mine = lib.addBank ("Mine");
            expect (lib.isFactoryBank (fb) && ! lib.isFactoryBank (mine));
            lib.removePrograms ({ ProgramRef (fb, 0) });
            lib.renameProgram ({ fb, 1 }, "Renamed");
            lib.copyPrograms ({ ProgramRef (fb, 0) });
            lib.pastePrograms (fb, -1);
            lib.insertCopies ({ ProgramRef (fb, 0) }, fb, -1);
            expectEquals ((int) lib.banks()[(size_t) fb].programs.size(), 2);
            expectEquals (lib.program ({ fb, 1 })->name, juce::String ("F2"));
            lib.movePrograms ({ ProgramRef (fb, 0) }, mine, -1); // dragging out copies instead of moving
            expectEquals ((int) lib.banks()[(size_t) fb].programs.size(), 2);
            expectEquals (used (lib, mine), 1);
            lib.pastePrograms (mine, -1); // other banks still take pastes
            expectEquals (used (lib, mine), 2);
        }

        beginTest ("Two-column drags: copies into custom banks, replacing into TEO-5 banks");
        {
            LibraryModel lib (dir);
            std::vector<LibraryProgram> hw;
            for (int i = 0; i < 16; ++i)
                hw.push_back (LibraryModel::makeProgram (fakeProgram (20 + i), "H" + juce::String (i + 1), "TEO-5 User 3 / " + juce::String (i + 1)));
            const int hwBank = lib.upsertHardwareBank ("TEO-5 User 3", 3, false, hw);
            const int mine = lib.addBank ("Mine");

            // TEO-5 -> my bank: copies, the TEO-5 bank is untouched.
            lib.insertCopies ({ ProgramRef (hwBank, 1), ProgramRef (hwBank, 0) }, mine, -1);
            expectEquals (used (lib, mine), 2);
            expectEquals (lib.program ({ mine, 0 })->name, juce::String ("H1")); // display order kept
            expectEquals (lib.program ({ mine, 1 })->name, juce::String ("H2"));
            expect (lib.program ({ mine, 0 })->uuid != lib.program ({ hwBank, 0 })->uuid);
            expectEquals ((int) lib.banks()[(size_t) hwBank].programs.size(), 16);

            // My bank -> TEO-5 slot 15: replaces 15 and 16, marks them unsent, keeps the slot label; the rest don't fit.
            lib.insertProgram (mine, -1, LibraryModel::makeProgram (fakeProgram (99), "Mine 3", "Pads.syx"));
            const int placed = lib.overwritePrograms ({ ProgramRef (mine, 0), ProgramRef (mine, 1), ProgramRef (mine, 2) }, hwBank, 14);
            expectEquals (placed, 2);
            expectEquals ((int) lib.banks()[(size_t) hwBank].programs.size(), 16);
            expectEquals (lib.program ({ hwBank, 14 })->name, juce::String ("H1"));
            expectEquals (lib.program ({ hwBank, 15 })->name, juce::String ("H2"));
            expect (lib.program ({ hwBank, 14 })->unsent && lib.program ({ hwBank, 15 })->unsent);
            expect (! lib.program ({ hwBank, 13 })->unsent);
            expectEquals (lib.program ({ hwBank, 14 })->source, juce::String ("TEO-5 User 3 / 15"));
            expect (*lib.program ({ hwBank, 14 })->raw == fakeProgram (20));

            // Unsent marks survive save/load, then clear when the bank has been sent; undo brings them back.
            expect (lib.save());
            {
                LibraryModel reloaded (dir);
                expect (reloaded.program ({ hwBank, 15 })->unsent, "unsent mark is saved");
            }
            lib.clearUnsent (hwBank);
            expect (! lib.program ({ hwBank, 14 })->unsent);
            lib.getUndoManager().undo();
            expect (lib.program ({ hwBank, 14 })->unsent);
            lib.getUndoManager().undo(); // the replace itself
            expectEquals (lib.program ({ hwBank, 14 })->name, juce::String ("H15"));
            lib.removeBank (mine);
            lib.removeBank (hwBank);
        }

        beginTest ("Placeholder names are replaced from program data");
        {
            expect (LibraryModel::isPlaceholderName ("U1-01"));
            expect (LibraryModel::isPlaceholderName ("F12-16"));
            expect (! LibraryModel::isPlaceholderName ("Glacial Pad"));
            expect (! LibraryModel::isPlaceholderName ("U2"));
            LibraryModel lib (dir);
            const int b = lib.addBank ("HW");
            lib.insertProgram (b, -1, LibraryModel::makeProgram (fakeProgram (1), "U1-01", "hw"));
            lib.insertProgram (b, -1, LibraryModel::makeProgram (fakeProgram (2), "My Edit", "hw"));
            const int n = lib.applyNames ([] (const juce::MemoryBlock&) { return juce::String ("Real Name"); });
            expectEquals (n, 1);
            expectEquals (lib.program ({ b, 0 })->name, juce::String ("Real Name"));
            expectEquals (lib.program ({ b, 1 })->name, juce::String ("My Edit"));
        }

        beginTest ("Search");
        {
            LibraryProgram p;
            p.raw = std::make_shared<const juce::MemoryBlock> (fakeProgram (1)); // without data it's an empty slot
            p.name = "Warm Pad";
            p.tags = { "pad", "slow" };
            expect (LibraryModel::matches (p, "warm", false));
            expect (LibraryModel::matches (p, "tag:slow", false));
            expect (! LibraryModel::matches (p, "tag:warm", false));
            expect (! LibraryModel::matches (p, "", true));
            p.favorite = true;
            expect (LibraryModel::matches (p, "", true));
        }

        dir.deleteRecursively();
    }
};

class SyxTests : public juce::UnitTest
{
public:
    SyxTests() : juce::UnitTest ("SyxIO", "TEO5") {}

    void runTest() override
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("teo5-syx-test", {});
        dir.createDirectory();

        beginTest ("Single program export/import");
        {
            auto f = dir.getChildFile ("one.syx");
            expect (SyxIO::exportProgram (f, fakeProgram (1)));
            auto items = SyxIO::importFile (f);
            expectEquals ((int) items.size(), 1);
            expect (items[0].fromEditBuffer);
            expect (items[0].raw == fakeProgram (1));
        }

        beginTest ("Bank export/import keeps order and addresses");
        {
            std::vector<juce::MemoryBlock> progs;
            for (int i = 0; i < 16; ++i)
                progs.push_back (fakeProgram (100 + i));
            auto f = dir.getChildFile ("bank.syx");
            expect (SyxIO::exportBank (f, progs, 3, false));
            auto items = SyxIO::importFile (f);
            expectEquals ((int) items.size(), 16);
            for (int i = 0; i < 16; ++i)
            {
                expect (items[(size_t) i].raw == progs[(size_t) i]);
                expect (! items[(size_t) i].fromEditBuffer);
                const auto a = MidiSync::addressFor (3, i + 1, false);
                expectEquals (items[(size_t) i].dumpBank, a.dumpBank);
                expectEquals (items[(size_t) i].dumpProgram, a.dumpProgram);
            }
        }

        beginTest ("Junk, foreign and truncated messages are skipped");
        {
            juce::MemoryBlock data;
            const juce::uint8 junk[] = { 0x01, 0x02, 0xf0, 0x43, 0x10, 0x01, 0xf7, 0xf0, 0x02, 0x5a, 0x03, 0x00 }; // Yamaha + truncated
            data.append (junk, sizeof (junk));
            auto m = SysEx::editBufferDump (fakeProgram (7));
            data.append (m.getRawData(), (size_t) m.getRawDataSize());
            auto items = SyxIO::parse (data.getData(), data.getSize());
            expectEquals ((int) items.size(), 1);
            expect (items[0].raw == fakeProgram (7));
        }

        dir.deleteRecursively();
    }
};

// ---- Sequence (layout measured on a TEO-5) -------------------------------------------------------

class SequenceTests : public juce::UnitTest
{
public:
    SequenceTests() : juce::UnitTest ("Sequence", "TEO5") {}

    // A program laid out like "OB-X Pad" (User 1): lane 1 plays A1 on step 1 and holds it through step 4
    // (steps 2-4 are rests with the tie flag set on the step before), then G4 on step 5 held through 8.
    static juce::MemoryBlock obxPad()
    {
        juce::MemoryBlock p (4102, true);
        auto* d = static_cast<juce::uint8*> (p.getData());
        d[189] = 32;
        for (int l = 0; l < 5; ++l)
            for (int s = 0; s < 64; ++s)
            {
                d[270 + l * 64 + s] = 60;
                d[590 + l * 64 + s] = 100;
            }
        for (int b = 190; b < 230; ++b) d[b] = 0xff;      // everything rests...
        d[270 + 0] = 33; d[270 + 4] = 67;                   // ...except lane 1 steps 1 and 5
        d[190] = 0xee;                                      // rest bits: steps 1 and 5 play (bits 0 and 4 clear)
        d[230] = 0x77;                                      // tie bits: steps 1-3 and 5-7 hold into the next
        return p;
    }

    void runTest() override
    {
        using SM = Sequence::StepMode;

        beginTest ("Reads length, notes, rests and ties in the TEO-5's format");
        {
            auto s = Sequence::read (obxPad());
            expectEquals (s.length, 32);
            expect (s.sounds (0, 0) && s.at (0, 0).note == 33);
            expect (s.isHeld (0, 1) && s.isHeld (0, 2) && s.isHeld (0, 3), "A1 held through step 4");
            expect (! s.isHeld (0, 4) && s.sounds (0, 4) && s.at (0, 4).note == 67, "G4 starts on step 5");
            expectEquals (s.heldNote (0, 3), 33);
            expect (s.modeOf (2) == SM::Tie && s.modeOf (4) == SM::Play && s.modeOf (20) == SM::Rest);
            expect (s.isEmpty (1, 0), "an unused lane is empty, not muted");
        }

        beginTest ("Write round-trips byte for byte and leaves the rest of the program alone");
        {
            auto p = obxPad();
            p[34] = (char) 213; p[159] = 'I'; // a parameter and the name
            auto copy = p;
            Sequence::read (p).write (copy);
            expect (copy == p);
        }

        beginTest ("Copying and pasting steps");
        {
            auto s = Sequence::read (obxPad());
            s.clearAll();
            s.addNote (0, 60, 90, true);
            s.addNote (0, 64, 80, false);
            s.setStepMode (1, SM::Tie);          // step 2 holds step 1's notes
            const auto chord = s.copyStep (0), held = s.copyStep (1);
            s.pasteStep (8, chord);
            expect (s.sounds (0, 8) && s.at (0, 8).note == 60 && s.sounds (1, 8) && s.at (1, 8).note == 64);
            expectEquals (s.at (1, 8).velocity, 80);
            s.pasteStep (12, held);              // a held step pastes as notes of its own
            expect (s.sounds (0, 12) && s.at (0, 12).note == 60, "held note pasted as a note");
            s.addNote (5, 70, 100, true);
            s.at (0, 5).tieToNext = true;        // step 6 (lane 1) ties into step 7
            s.pasteStep (6, chord);
            expect (! s.at (0, 5).tieToNext, "pasting cuts the tie into the step");
        }

        beginTest ("Clearing a step or the whole sequence");
        {
            auto s = Sequence::read (obxPad());
            const int len = s.length;
            s.addNote (3, 64, 100, true);
            s.setStepMode (4, SM::Tie); // step 4 now holds step 3's note
            s.clearStep (4);
            expect (! s.isHeld (0, 4) && ! s.at (0, 3).tieToNext, "clearing a held step cuts the tie into it");
            s.clearStep (3);
            for (int l = 0; l < Sequence::numLanes; ++l)
                expect (s.isEmpty (l, 3));
            s.clearAll();
            bool allEmpty = true;
            for (int st = 0; st < Sequence::numSteps; ++st)
                for (int l = 0; l < Sequence::numLanes; ++l)
                    allEmpty = allEmpty && s.isEmpty (l, st);
            expect (allEmpty, "every step is empty");
            expectEquals (s.length, len, "the length is kept");
        }

        beginTest ("Edits keep the stored format consistent");
        {
            auto s = Sequence::read (obxPad());

            // Note entry replaces the step and cuts the tie from the step before.
            s.addNote (2, 62, 90, true);
            expect (s.sounds (0, 2) && s.at (0, 2).note == 62 && ! s.isHeld (0, 2));
            expect (! s.at (0, 1).tieToNext, "step 2 no longer holds into step 3");

            // Chord: up to five notes, then full.
            for (int n = 0; n < 4; ++n)
                expect (s.addNote (10, 60 + n, 100, false) >= 0);
            expect (s.addNote (10, 72, 100, false) >= 0);
            expectEquals (s.addNote (10, 74, 100, false), -1);

            // REST keeps the notes (muted) and PLAY brings them back.
            s.setStepMode (10, SM::Rest);
            expect (s.modeOf (10) == SM::Rest && s.isMuted (0, 10) && s.at (0, 10).note == 60);
            s.setStepMode (10, SM::Play);
            expect (s.sounds (0, 10) && s.sounds (4, 10));

            // TIE holds what played on the step before.
            s.setStepMode (11, SM::Tie);
            expect (s.isHeld (0, 11) && s.isHeld (4, 11) && s.at (0, 10).tieToNext && s.modeOf (11) == SM::Tie);
            s.setStepMode (12, SM::Tie); // a longer note
            expectEquals (s.heldNote (4, 12), 72);

            // Mute a single note; an empty slot stays empty.
            s.setMuted (0, 4, true);
            expect (s.isMuted (0, 4) && ! s.sounds (0, 4));

            // Transpose moves real notes only.
            s.transpose (0, 63, 12);
            expectEquals (s.at (0, 0).note, 45);
            expectEquals (s.at (0, 4).note, 79);   // muted notes move too
            expectEquals (s.at (1, 0).note, Sequence::emptyNote);

            // And it all survives a write/read.
            juce::MemoryBlock p (4102, true);
            s.write (p);
            auto back = Sequence::read (p);
            for (int l = 0; l < 5; ++l)
                for (int st = 0; st < 64; ++st)
                    expect (back.at (l, st).note == s.at (l, st).note && back.at (l, st).rest == s.at (l, st).rest
                                && back.at (l, st).tieToNext == s.at (l, st).tieToNext && back.at (l, st).velocity == s.at (l, st).velocity);
        }
    }
};

static SequenceTests sequenceTests;
static LibraryTests libraryTests;
static SyxTests syxTests;
static SysExTests sysExTests;
static NrpnTests nrpnTests;
static LayoutTests layoutTests;

int main()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("TEO5");

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;
    std::cout << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " FAILURE(S)") << std::endl;
    return failures == 0 ? 0 : 1;
}
