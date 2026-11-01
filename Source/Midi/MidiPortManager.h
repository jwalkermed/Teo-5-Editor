// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <array>
#include <deque>
#include <map>

namespace teo5
{

// Owns the direct MIDI ports to the TEO-5 (independent of the host, which often won't pass SysEx),
// delivers incoming messages on the message thread, and sends outgoing messages from a background
// thread with ~1 ms spacing. Repeated parameter changes are coalesced so knob sweeps don't flood the synth.
class MidiPortManager : private juce::MidiInputCallback,
                        private juce::AsyncUpdater,
                        private juce::Thread
{
public:
    MidiPortManager();
    ~MidiPortManager() override;

    static juce::Array<juce::MidiDeviceInfo> inputs() { return juce::MidiInput::getAvailableDevices(); }
    static juce::Array<juce::MidiDeviceInfo> outputs() { return juce::MidiOutput::getAvailableDevices(); }

    bool openInput (const juce::String& identifier);
    bool openOutput (const juce::String& identifier);
    void closeInput();
    void closeOutput();
    juce::String inputId() const { return inId; }
    juce::String outputId() const { return outId; }
    bool hasOutput() const;

    // Called on the message thread for every received message (direct port or injected).
    std::function<void (const juce::MidiMessage&)> onMessage;

    // Optional extra sink for outgoing messages (e.g. the host MIDI output). Called from the sender thread.
    void setHostSink (std::function<void (const juce::MidiMessage&)> sink);

    // Queue a group of messages that describe one parameter; a newer group with the same key
    // replaces an older one that hasn't been sent yet.
    void queueCoalesced (int key, const juce::Array<juce::MidiMessage>& messages);
    // Queue messages that must all be sent in order (SysEx, program changes).
    void queueOrdered (const juce::MidiMessage&);
    // Played notes/controllers from the audio thread: lock-free, sent ahead of parameter traffic.
    // Short messages only (up to 3 bytes); returns false if the queue is full.
    bool pushRealtime (const juce::MidiMessage&);

    // Messages arriving from somewhere other than the direct input (e.g. the host), any thread.
    void inject (const juce::MidiMessage&);

    int pendingCount() const;

    // Outgoing messages wait (queued) until release(): a plug-in copy the host only scans or tests never talks to
    // the synth. Ports start held.
    void release() { held = false; wake.signal(); }
    bool isHeld() const { return held.load(); }
    // Also write every logged message to this file (time, process id); appended, the old one kept as .old once it
    // passes 1 MB. Empty = no file.
    void setLogFile (const juce::File&);

    // ---- Diagnostics (MIDI monitor) ----
    int sentCount() const { return numSent.load(); }
    int receivedCount() const { return numReceived.load(); }
    // When the synth last sent a note-on for this note (ms counter), from the MIDI input thread. Any thread.
    juce::uint32 lastNoteOnFromSynth (int note) const { return lastSynthNoteOn[(size_t) (note & 127)].load(); }
    juce::StringArray recentLog() const; // newest last
    juce::String lastError() const;
    void clearLog();

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void handleAsyncUpdate() override;
    void run() override;
    void send (const juce::MidiMessage&);

    std::unique_ptr<juce::MidiInput> input;
    std::unique_ptr<juce::MidiOutput> output;
    juce::String inId, outId;

    juce::CriticalSection inLock;
    std::vector<juce::MidiMessage> incoming;

    mutable juce::CriticalSection outLock;
    std::deque<juce::MidiMessage> ordered;
    std::deque<int> coalescedOrder;
    std::map<int, juce::Array<juce::MidiMessage>> coalesced;
    std::function<void (const juce::MidiMessage&)> hostSink;
    juce::WaitableEvent wake;

    bool drainRealtime(); // sender thread, with outLock held
    juce::AbstractFifo realtimeFifo { 2048 };
    std::array<juce::uint32, 2048> realtimeData {};

    void log (const juce::String& line);
    std::atomic<bool> held { true };
    juce::File logFile;
    std::unique_ptr<juce::FileOutputStream> logStream; // with logLock
    std::atomic<int> numSent { 0 }, numReceived { 0 };
    std::array<std::atomic<juce::uint32>, 128> lastSynthNoteOn {};
    mutable juce::CriticalSection logLock;
    juce::StringArray logLines;
    juce::String error;
};

} // namespace teo5
