// Renders the plugin editor offscreen to a PNG so the panel can be compared with the reference photo.
// Usage: TEO5Snapshot <output.png> [scale] [drawer|library|modmatrix|sequencer|global|program|oled "message"]

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UI/WriteLocationDialog.h"

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    // Use a scratch library with a few demo entries instead of the user's library.
    auto libDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("teo5-snapshot-lib", {});
    setenv ("TEO5_LIBRARY_DIR", libDir.getFullPathName().toRawUTF8(), 1);

    const juce::File out = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                    : juce::File::getCurrentWorkingDirectory().getChildFile ("panel.png");
    const float scale = argc > 2 ? (float) juce::String (argv[2]).getDoubleValue() : 1.0f;

    TEO5Processor processor;
    // Match the reference photo's state: a few lit buttons and LEDs.
    processor.setNative ("osc1Saw", 1);
    processor.setNative ("osc2Saw", 1);
    processor.setNative ("osc1On", 1);
    processor.setNative ("subOn", 1);
    processor.setNative ("osc2On", 1);
    processor.setNative ("fxOn", 1);
    processor.setNative ("fxSync", 1);
    processor.setNative ("revOn", 1);
    processor.setNative ("env1Vel", 1);
    processor.setNative ("env2Vel", 1);
    processor.setNative ("osc2Detune", 40);

    if (argc > 3 && juce::String (argv[3]) == "sevenseg")
    {
        // The bank/program display for banks 1, 9 and 10-16 (A-G).
        juce::StringArray texts { "101", "901", "A01", "B02", "C03", "D04", "E05", "F06", "G16" };
        juce::Image img (juce::Image::ARGB, 90 * texts.size(), 34, true);
        juce::Graphics g (img);
        for (int i = 0; i < texts.size(); ++i)
        {
            teo5::SevenSegment s;
            s.setBounds (0, 0, 80, 28);
            s.setText (texts[i]);
            g.drawImageAt (s.createComponentSnapshot (s.getLocalBounds()), i * 90 + 5, 3);
        }
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat().writeImageToStream (img.rescaled (img.getWidth() * 2, img.getHeight() * 2), stream);
        return 0;
    }
    if (argc > 3 && juce::String (argv[3]) == "oledhome")
    {
        // The OLED's home page; argv[4] = BPM label (e.g. "EXT BPM"), argv[5] = BPM.
        teo5::OledDisplay oled;
        oled.setBounds (0, 0, 133, 104);
        teo5::OledDisplay::HomePage home;
        home.bpmLabel = argc > 4 ? juce::String (argv[4]) : juce::String ("BPM");
        home.bpm = argc > 5 ? juce::String (argv[5]) : juce::String ("120");
        oled.setHome (home);
        auto image = oled.createComponentSnapshot (oled.getLocalBounds(), true, scale);
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        return 0;
    }
    if (argc > 3 && juce::String (argv[3]) == "oled")
    {
        // The OLED showing a message (argv[4]), at the panel's size for a 1700-px-wide editor.
        teo5::OledDisplay oled;
        oled.setBounds (0, 0, 133, 104);
        oled.showParameter (argc > 6 ? juce::String (argv[6]) : juce::String ("WRITE"),
                            argc > 4 ? juce::String (argv[4]) : juce::String ("Stored in User 1-01"),
                            argc > 5 ? juce::String (argv[5]) : juce::String());
        auto image = oled.createComponentSnapshot (oled.getLocalBounds(), true, scale);
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat().writeImageToStream (image, stream);
        return 0;
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setVisible (true);
    const juce::String mode = argc > 3 ? juce::String (argv[3]) : juce::String();
    if (mode == "library")
    {
        // Demo content for the screenshot.
        auto& lib = processor.getLibrary();
        juce::MemoryBlock blank ((size_t) teo5::SysEx::programSize, true);
        auto bankOf = [&] (std::initializer_list<const char*> names, const juce::String& source)
        {
            std::vector<teo5::LibraryProgram> progs;
            for (auto* n : names)
                progs.push_back (teo5::LibraryModel::makeProgram (blank, n, source));
            return progs;
        };
        auto user1 = bankOf ({ "It's an Oberheim", "Glacial Pad", "Eastern Bells", "Baumann's Seq", "Pulse Ping",
                               "LeadReed", "Bounce Back", "BuzzFlow", "Super Collider", "Plektrum", "Wet Strings",
                               "Sync Sweep", "Hollow Keys", "Deep Sub", "Tape Choir", "Night Drive" }, "TEO-5 User 1");
        user1[0].favorite = true;
        user1[1].favorite = true;
        user1[1].tags = { "pad", "ambient" };
        user1[3].tags = { "seq" };
        user1[4].unsent = true;
        user1[5].unsent = true;
        const int user1Bank = lib.upsertHardwareBank ("TEO-5 User 1", 1, false, user1);
        lib.removePrograms ({ teo5::ProgramRef (user1Bank, 2) }); // a deleted slot, not yet sent
        lib.upsertHardwareBank ("TEO-5 Factory 1", 1, true,
                                bankOf ({ "OB Brass", "Matrix Strings", "Poly Sweep", "Xpander Bell", "Big Unison",
                                          "Soft Saw Pad", "Hard Sync Lead", "Vox Filter" }, "TEO-5 Factory 1"));
        lib.renameBank (0, "My Programs");
        lib.insertProgram (0, -1, teo5::LibraryModel::makeProgram (blank, "Warm Brass Stack", "Panel"));
        lib.insertProgram (0, -1, teo5::LibraryModel::makeProgram (blank, "Sync Lead 2", "Panel"));
        lib.insertProgram (0, -1, teo5::LibraryModel::makeProgram (blank, "Pulse Ping", "TEO-5 User 1"));
        const int pads = lib.addBank ("Pads.syx");
        for (auto* n : { "Aurora", "Slow Bloom", "Frozen Lake", "Cathedral", "Halo" })
            lib.insertProgram (pads, -1, teo5::LibraryModel::makeProgram (blank, n, "Pads.syx"));
        lib.addBank ("Live Set");
    }
    if (mode == "sequencer")
    {
        // The sequence from a real TEO-5 program ("It's an Oberheim"), with a rest, a tie and a muted note.
        auto& seq = processor.getProgramState().sequence;
        const int lane1[] = { 29,59,60,64,62,60,55,57, 29,59,60,64,62,60,59,57, 29,59,60,64,62,60,55,57, 29,59,60,64,62,60,59,57,
                              33,59,60,64,62,60,55,57, 33,59,60,64,62,60,59,57, 33,59,60,64,62,60,55,57, 33,59,60,67,64,60,67,69 };
        seq = {};
        seq.length = 64;
        for (int i = 0; i < 64; ++i)
        {
            seq.addNote (i, lane1[i], 90 + (i * 7) % 30, true);
            if (i % 8 == 0)
                seq.addNote (i, 57, 100, false);
        }
        seq.setStepMode (30, teo5::Sequence::StepMode::Rest);
        seq.setStepMode (62, teo5::Sequence::StepMode::Tie);
        seq.setStepMode (63, teo5::Sequence::StepMode::Tie);
        seq.setMuted (0, 20, true);
        processor.getProgramState().sequenceVersion++;
    }
    if (mode == "modmatrix")
    {
        // Demo routings for the screenshot: { slot, source, amount (127 = 0), destination }.
        const int demo[][4] = { { 1, 3, 180, 15 }, { 2, 5, 200, 1 }, { 3, 9, 160, 29 }, { 4, 14, 96, 44 },
                                { 5, 10, 230, 15 }, { 6, 17, 60, 45 }, { 7, 0, 128, 0 }, { 8, 4, 140, 7 } };
        for (auto& d : demo)
        {
            processor.setNative ("mod" + juce::String (d[0]) + "Src", d[1]);
            processor.setNative ("mod" + juce::String (d[0]) + "Amt", d[2]);
            processor.setNative ("mod" + juce::String (d[0]) + "Dest", d[3]);
        }
    }
    if (auto* e = dynamic_cast<TEO5Editor*> (editor.get()))
    {
        if (mode == "modmatrix")
            e->setModMatrixOpen (true);
        if (mode == "modmatrix" && argc > 4 && juce::String (argv[4]) == "assign")
            e->addModulation (3, 26); // as if chosen from a knob's right-click menu: LFO 1 -> LFO 1 Freq
        if (mode == "sequencer")
            e->setSequencerOpen (true);
        if (mode == "drawer")
            e->setDrawerOpen (true);
        if (mode == "library")
            e->setLibraryOpen (true);
        if (mode == "keys")
        {
            // Arrow keys with no drawer open: Right/Left = patch, Up/Down = bank (prints the resulting locations).
            auto& ps = processor.getProgramState();
            for (auto k : { juce::KeyPress::rightKey, juce::KeyPress::rightKey, juce::KeyPress::upKey, juce::KeyPress::leftKey,
                            juce::KeyPress::downKey, juce::KeyPress::downKey })
            {
                e->keyPressed (juce::KeyPress (k));
                std::printf ("%d-%02d ", ps.bank, ps.program);
            }
            e->setGlobalOpen (true);
            std::printf ("| with a drawer open: %s\n", e->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)) ? "handled" : "ignored");
            return 0;
        }
        if (mode == "global")
            e->setGlobalOpen (true);
        if (mode == "program")
            e->setProgramOpen (true);
    }

    if (mode == "modmatrix" && argc > 4)
    {
        // Click the LED of slot argv[4] (as at 1700 px wide) to show a slot turned off.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        const int slot = juce::String (argv[4]).getIntValue();
        const juce::Point<float> at (38.0f, 72.0f + 28.0f * (float) (slot - 1));
        if (auto* c = editor->getComponentAt (at.toInt()))
        {
            const auto local = c->getLocalPoint (editor.get(), at);
            const auto now = juce::Time::getCurrentTime();
            c->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), local, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                            c, c, now, local, now, 1, false));
        }
    }

    juce::MessageManager::getInstance()->runDispatchLoopUntil (300); // deliver pending change messages

    // TEO5_SNAPSHOT_CLICKS="x,y;x,y": clicks (at 1700 px wide) applied in order before the snapshot.
    for (auto& click : juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("TEO5_SNAPSHOT_CLICKS", {}), ";", {}))
    {
        const juce::Point<float> at ((float) click.upToFirstOccurrenceOf (",", false, false).getIntValue(),
                                     (float) click.fromFirstOccurrenceOf (",", false, false).getIntValue());
        if (auto* c = editor->getComponentAt (at.toInt()))
        {
            const auto local = c->getLocalPoint (editor.get(), at);
            const auto now = juce::Time::getCurrentTime();
            juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), local, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                c, c, now, local, now, 1, false);
            c->mouseDown (e);
            c->mouseUp (e);
            c->mouseMove (e);
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
    }

    if (argc > 4 && juce::String (argv[4]) == "write")
    {
        // The write-location dialog over the library (argv[5] = an optional second file for it alone).
        teo5::chooseWriteLocation (editor.get(), "Glacial Pad", 1, 2, processor.getLibrary(), nullptr);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        if (auto* modal = juce::Component::getCurrentlyModalComponent())
        {
            auto dlg = modal->createComponentSnapshot (modal->getLocalBounds(), true, scale);
            juce::File dlgFile = out.getSiblingFile (out.getFileNameWithoutExtension() + "-write.png");
            dlgFile.deleteFile();
            juce::FileOutputStream ds (dlgFile);
            juce::PNGImageFormat().writeImageToStream (dlg, ds);
            modal->exitModalState (0);
        }
    }
    auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
    {
        std::cerr << "Failed to write " << out.getFullPathName() << std::endl;
        return 1;
    }
    std::cout << "Wrote " << out.getFullPathName() << " (" << image.getWidth() << "x" << image.getHeight() << ")" << std::endl;
    editor.reset();
    return 0;
}
