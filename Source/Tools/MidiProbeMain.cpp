// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
// Talks to the TEO-5 directly, for hardware experiments (used to map the global settings dump).
// Usage: TEO5MidiProbe [--port NAME] [--channel N] command...
//   globals          request the global dump and print its data bytes (hex), one line
//   nrpn N V         send NRPN N = V
//   wait MS          wait (incoming messages are printed while waiting when --listen is given)
//   listen MS        print every incoming message for MS milliseconds
//   editbuf FILE     request the edit buffer and save the SysEx message (without F0/F7) to FILE
//   sendsyx FILE     send a saved SysEx message (e.g. to restore an edit buffer)
//   note N V MS      play note N at velocity V for MS milliseconds (prints what comes back meanwhile)
// Example: TEO5MidiProbe globals nrpn 4110 0 wait 200 globals
#include <juce_audio_devices/juce_audio_devices.h>

namespace
{
struct Input : juce::MidiInputCallback
{
    juce::CriticalSection lock;
    juce::MemoryBlock lastGlobals, lastEditBuffer;
    juce::WaitableEvent globalsArrived, editBufferArrived;
    std::atomic<bool> printAll { false };

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
    {
        if (m.isSysEx())
        {
            auto* d = m.getSysExData();
            const int n = m.getSysExDataSize();
            if (n >= 3 && d[2] == 0x0f)
            {
                const juce::ScopedLock sl (lock);
                lastGlobals = juce::MemoryBlock (d + 3, (size_t) (n - 3));
                globalsArrived.signal();
            }
            if (n >= 3 && d[2] == 0x03)
            {
                const juce::ScopedLock sl (lock);
                lastEditBuffer = juce::MemoryBlock (d, (size_t) n);
                editBufferArrived.signal();
            }
        }
        if (printAll && ! m.isMidiClock() && ! m.isActiveSense())
        {
            if (m.isSysEx())
            {
                juce::String hexText;
                for (int k = 0; k < juce::jmin (16, m.getSysExDataSize()); ++k)
                    hexText << juce::String::toHexString ((int) m.getSysExData()[k]).paddedLeft ('0', 2) << " ";
                std::printf ("SYSEX %d bytes: %s\n", m.getSysExDataSize(), hexText.toRawUTF8());
                return;
            }
            juce::String text = m.isController() ? "CC " + juce::String (m.getControllerNumber()) + " = "
                                                       + juce::String (m.getControllerValue())
                                                 : m.getDescription();
            std::printf ("%8.1f ms  IN ch%d %s\n", juce::Time::getMillisecondCounterHiRes(), m.getChannel(), text.toRawUTF8());
        }
    }
};

juce::String hex (const juce::MemoryBlock& b)
{
    juce::String s;
    for (size_t i = 0; i < b.getSize(); ++i)
        s << juce::String::toHexString ((int) static_cast<const juce::uint8*> (b.getData())[i]).paddedLeft ('0', 2) << " ";
    return s.trimEnd();
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    juce::String portName = "Network Teo-5";
    int channel = 1;
    if (int i = args.indexOf ("--port"); i >= 0) { portName = args[i + 1]; args.removeRange (i, 2); }
    if (int i = args.indexOf ("--channel"); i >= 0) { channel = args[i + 1].getIntValue(); args.removeRange (i, 2); }

    auto find = [&portName] (const juce::Array<juce::MidiDeviceInfo>& list) -> juce::String
    {
        for (auto& d : list)
            if (d.name == portName)
                return d.identifier;
        return {};
    };
    Input input;
    auto in = juce::MidiInput::openDevice (find (juce::MidiInput::getAvailableDevices()), &input);
    auto out = juce::MidiOutput::openDevice (find (juce::MidiOutput::getAvailableDevices()));
    if (in == nullptr || out == nullptr)
    {
        std::printf ("FAIL: can't open port \"%s\"\n", portName.toRawUTF8());
        return 1;
    }
    in->start();

    for (int i = 0; i < args.size(); ++i)
    {
        const auto cmd = args[i];
        if (cmd == "globals")
        {
            input.globalsArrived.reset();
            const juce::uint8 req[] = { 0x10, 0x5a, 0x0e };
            out->sendMessageNow (juce::MidiMessage::createSysExMessage (req, (int) sizeof (req)));
            if (! input.globalsArrived.wait (2000))
            {
                std::printf ("FAIL: no global dump\n");
                return 1;
            }
            const juce::ScopedLock sl (input.lock);
            std::printf ("GLOBALS %s\n", hex (input.lastGlobals).toRawUTF8());
        }
        else if (cmd == "inquiry") // universal device inquiry; prints any SysEx that comes back
        {
            const juce::uint8 req[] = { 0x7e, 0x7f, 0x06, 0x01 };
            input.printAll = true;
            out->sendMessageNow (juce::MidiMessage::createSysExMessage (req, (int) sizeof (req)));
            juce::Thread::sleep (800);
            input.printAll = false;
        }
        else if (cmd == "editbuf")
        {
            input.editBufferArrived.reset();
            const juce::uint8 req[] = { 0x10, 0x5a, 0x06 };
            out->sendMessageNow (juce::MidiMessage::createSysExMessage (req, (int) sizeof (req)));
            if (! input.editBufferArrived.wait (3000))
            {
                std::printf ("FAIL: no edit buffer\n");
                return 1;
            }
            const juce::ScopedLock sl (input.lock);
            juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (args[++i])).replaceWithData (input.lastEditBuffer.getData(), input.lastEditBuffer.getSize());
            std::printf ("EDITBUF %d bytes\n", (int) input.lastEditBuffer.getSize());
        }
        else if (cmd == "sendsyx")
        {
            juce::MemoryBlock data;
            juce::File::getCurrentWorkingDirectory().getChildFile (args[++i]).loadFileAsData (data);
            out->sendMessageNow (juce::MidiMessage::createSysExMessage (data.getData(), (int) data.getSize()));
            juce::Thread::sleep (300);
        }
        else if (cmd == "note") // note N V MS: note on, wait MS, note off
        {
            const int n = args[++i].getIntValue(), v = args[++i].getIntValue(), ms = args[++i].getIntValue();
            out->sendMessageNow (juce::MidiMessage::noteOn (channel, n, (juce::uint8) v));
            input.printAll = true;
            juce::Thread::sleep (ms);
            out->sendMessageNow (juce::MidiMessage::noteOff (channel, n));
            input.printAll = false;
        }
        else if (cmd == "nrpn")
        {
            const int n = args[++i].getIntValue(), v = args[++i].getIntValue();
            for (auto m : { juce::MidiMessage::controllerEvent (channel, 99, (n >> 7) & 0x7f),
                            juce::MidiMessage::controllerEvent (channel, 98, n & 0x7f),
                            juce::MidiMessage::controllerEvent (channel, 6, (v >> 7) & 0x7f),
                            juce::MidiMessage::controllerEvent (channel, 38, v & 0x7f) })
            {
                out->sendMessageNow (m);
                juce::Thread::sleep (2);
            }
        }
        else if (cmd == "wait" || cmd == "listen")
        {
            input.printAll = cmd == "listen";
            juce::Thread::sleep (args[++i].getIntValue());
            input.printAll = false;
        }
        else
        {
            std::printf ("FAIL: unknown command %s\n", cmd.toRawUTF8());
            return 1;
        }
    }
    in->stop();
    return 0;
}
