#include "MidiPortManager.h"

namespace teo5
{

// Short one-line description for the MIDI monitor.
static juce::String describe (const juce::MidiMessage& m)
{
    if (m.isSysEx())
    {
        juce::String hex;
        auto* d = m.getRawData();
        const int shown = m.getRawDataSize() <= 24 ? m.getRawDataSize() : 8; // short messages in full
        for (int i = 0; i < shown; ++i)
            hex << juce::String::toHexString (d[i]).paddedLeft ('0', 2) << " ";
        return "SysEx " + juce::String (m.getRawDataSize()) + " bytes: " + hex + (m.getRawDataSize() > shown ? "..." : "");
    }
    if (m.isController())
        return "ch" + juce::String (m.getChannel()) + " CC " + juce::String (m.getControllerNumber()) + " = " + juce::String (m.getControllerValue());
    return m.getDescription();
}

MidiPortManager::MidiPortManager() : juce::Thread ("TEO5 MIDI out")
{
    startThread (juce::Thread::Priority::high);
}

MidiPortManager::~MidiPortManager()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (2000);
    cancelPendingUpdate();
    closeInput();
    closeOutput();
}

bool MidiPortManager::openInput (const juce::String& identifier)
{
    closeInput();
    if (identifier.isEmpty())
        return false;
    input = juce::MidiInput::openDevice (identifier, this);
    if (input == nullptr)
    {
        const juce::ScopedLock sl (logLock);
        error = "Could not open MIDI input";
        logLines.add ("!! " + error);
        return false;
    }
    inId = identifier;
    input->start();
    {
        const juce::ScopedLock sl (logLock);
        error.clear();
    }
    log ("-- input open: " + input->getName());
    return true;
}

bool MidiPortManager::openOutput (const juce::String& identifier)
{
    closeOutput();
    if (identifier.isEmpty())
        return false;
    auto out = juce::MidiOutput::openDevice (identifier);
    if (out == nullptr)
    {
        const juce::ScopedLock sl (logLock);
        error = "Could not open MIDI output";
        logLines.add ("!! " + error);
        return false;
    }
    {
        const juce::ScopedLock sl (logLock);
        error.clear();
    }
    log ("-- output open: " + out->getName());
    const juce::ScopedLock sl (outLock);
    output = std::move (out);
    outId = identifier;
    return true;
}

void MidiPortManager::closeInput()
{
    if (input != nullptr)
        input->stop();
    input.reset();
    inId.clear();
}

void MidiPortManager::closeOutput()
{
    const juce::ScopedLock sl (outLock);
    output.reset();
    outId.clear();
}

bool MidiPortManager::hasOutput() const
{
    const juce::ScopedLock sl (outLock);
    return output != nullptr || hostSink != nullptr;
}

void MidiPortManager::setHostSink (std::function<void (const juce::MidiMessage&)> sink)
{
    const juce::ScopedLock sl (outLock);
    hostSink = std::move (sink);
}

void MidiPortManager::queueCoalesced (int key, const juce::Array<juce::MidiMessage>& messages)
{
    {
        const juce::ScopedLock sl (outLock);
        if (coalesced.find (key) == coalesced.end())
            coalescedOrder.push_back (key);
        coalesced[key] = messages;
    }
    wake.signal();
}

void MidiPortManager::queueOrdered (const juce::MidiMessage& m)
{
    {
        const juce::ScopedLock sl (outLock);
        ordered.push_back (m);
    }
    wake.signal();
}

int MidiPortManager::pendingCount() const
{
    const juce::ScopedLock sl (outLock);
    return (int) (ordered.size() + coalescedOrder.size());
}

void MidiPortManager::send (const juce::MidiMessage& m)
{
    // Called with outLock held.
    if (output != nullptr)
        output->sendMessageNow (m);
    if (hostSink)
        hostSink (m);
    ++numSent;
    log ((output == nullptr && ! hostSink ? "OUT (no port!) " : "OUT ") + describe (m));
}

juce::StringArray MidiPortManager::recentLog() const
{
    const juce::ScopedLock sl (logLock);
    return logLines;
}

void MidiPortManager::clearLog()
{
    const juce::ScopedLock sl (logLock);
    logLines.clear();
    error.clear();
    numSent = 0;
    numReceived = 0;
}

juce::String MidiPortManager::lastError() const
{
    const juce::ScopedLock sl (logLock);
    return error;
}

void MidiPortManager::log (const juce::String& line)
{
    const juce::ScopedLock sl (logLock);
    logLines.add (juce::Time::getCurrentTime().formatted ("%H:%M:%S ") + line);
    if (logLines.size() > 40)
        logLines.removeRange (0, logLines.size() - 40);
}

bool MidiPortManager::pushRealtime (const juce::MidiMessage& m)
{
    const int size = m.getRawDataSize();
    if (size < 1 || size > 3)
        return false;
    auto* d = m.getRawData();
    juce::uint32 packed = (juce::uint32) size << 24;
    for (int i = 0; i < size; ++i)
        packed |= (juce::uint32) d[i] << (8 * i);

    const auto scope = realtimeFifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 < 1)
        return false;
    realtimeData[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = packed;
    return true;
}

bool MidiPortManager::drainRealtime()
{
    bool any = false;
    while (realtimeFifo.getNumReady() > 0)
    {
        juce::uint32 packed = 0;
        {
            const auto scope = realtimeFifo.read (1);
            packed = realtimeData[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
        }
        const int size = (int) (packed >> 24);
        const juce::uint8 bytes[3] = { (juce::uint8) packed, (juce::uint8) (packed >> 8), (juce::uint8) (packed >> 16) };
        send (juce::MidiMessage (bytes, size));
        any = true;
    }
    return any;
}

void MidiPortManager::run()
{
    while (! threadShouldExit())
    {
        bool sentParameter = false;
        {
            const juce::ScopedLock sl (outLock);
            drainRealtime(); // played notes first, never delayed by parameter traffic
            if (! ordered.empty())
            {
                auto m = ordered.front();
                ordered.pop_front();
                send (m);
                sentParameter = true;
            }
            else if (! coalescedOrder.empty())
            {
                const int key = coalescedOrder.front();
                coalescedOrder.pop_front();
                auto group = std::move (coalesced[key]);
                coalesced.erase (key);
                for (auto& m : group)
                    send (m);
                sentParameter = true;
            }
        }

        // ~1 ms between parameter messages; when idle, poll the note queue every millisecond
        // (the audio thread can't signal an event without risking a lock).
        if (sentParameter)
            wait (1);
        else
            wake.wait (1);
    }
}

void MidiPortManager::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (m.isNoteOn()) // when the synth played it, for recognising the same note coming back through the DAW
        lastSynthNoteOn[(size_t) (m.getNoteNumber() & 127)].store (juce::Time::getMillisecondCounter());
    inject (m);
}

void MidiPortManager::inject (const juce::MidiMessage& m)
{
    if (m.isActiveSense() || m.isMidiClock())
        return;
    ++numReceived;
    log ("IN  " + describe (m));
    {
        const juce::ScopedLock sl (inLock);
        incoming.push_back (m);
    }
    triggerAsyncUpdate();
}

void MidiPortManager::handleAsyncUpdate()
{
    std::vector<juce::MidiMessage> batch;
    {
        const juce::ScopedLock sl (inLock);
        batch.swap (incoming);
    }
    for (auto& m : batch)
        if (onMessage)
            onMessage (m);
}

} // namespace teo5
