// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "MidiPortManager.h"
#include "Nrpn.h"
#include "ProgramLayout.h"
#include "Sequence.h"
#include "SysEx.h"
#include "../Params/ParamTable.h"

namespace teo5
{

// Current program as the editor knows it.
struct ProgramState
{
    juce::String name { "It's an Oberheim" };
    int bank = 1;          // panel bank 1..16
    int program = 1;       // panel program 1..16
    bool factory = false;  // FACTORY button
    juce::MemoryBlock raw; // last unpacked 4096-byte program (edit buffer) received from the synth
    Sequence sequence;     // the program's step sequence, including edits made in the editor
    int sequenceVersion = 0; // bumped whenever a program (and so its sequence) is loaded or received
    int liveSequenceVersion = 0; // bumped when the synth's own recording changes the sequence byte by byte
    Sequence beforeSynthRecording; // the sequence when RECORD last turned on at the TEO-5 (one undo step per pass)
};

struct MidiSettings
{
    juce::String inputId, outputId;
    int channel = 1;              // 1..16 (TEO-5 "All" still accepts a specific channel)
    bool useCc = false;           // false = NRPN (preferred), true = CC fallback
    bool useHostMidi = false;     // also send/receive through the DAW's MIDI ports
    bool requestOnProgramChange = true;
    bool playFromHost = true;     // pass notes/wheels/pedals from the DAW track to the TEO-5
    int sysexManufacturer = SysEx::defaultManufacturerId; // learned from the TEO-5's device-inquiry reply
    int sysexDevice = SysEx::defaultDeviceId;

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree&);
};

// Two-way sync between the APVTS parameters and the TEO-5.
class MidiSync : private juce::AudioProcessorValueTreeState::Listener,
                 private juce::Timer,
                 public juce::ChangeBroadcaster
{
public:
    MidiSync (juce::AudioProcessorValueTreeState&, ProgramState&);
    ~MidiSync() override;

    MidiPortManager& getPorts() { return ports; }
    ProgramLayout& getLayout() { return layout; }

    const MidiSettings& getSettings() const { return settings; }
    // fromUser = false (startup / state restore): if no ports are set, pick ports named like the TEO-5.
    void applySettings (const MidiSettings&, bool fromUser = false);

    // ---- Actions ----
    void requestEditBuffer();
    void sendEditBuffer();                // edit buffer only, never writes to memory
    void sendAllParameters();             // NRPN/CC for every program parameter
    // After a DAW project (or preset) restores the plug-in: shortly afterwards, once the ports are open, make the
    // TEO-5's edit buffer match the saved program. Never writes to memory.
    void restoreSynthFromState();
    // A new plug-in instance: if no saved state is restored within a moment (a new track rather than a saved
    // project), fetch the program that's on the TEO-5 so the editor starts from what the synth is playing.
    void fetchSynthUnlessRestored();
    // The TEO-5 follows external MIDI clock (Global Clock Mode In, In Thru or In No S/S), as last read from it.
    bool clockIsExternal() const;
    // Global Clock Mode as last read (0 Off, 1 Out, 2 Out No S/S, 3 In, 4 In Thru, 5 In No S/S), or -1 if not read yet.
    int clockMode() const { return globalsReceived ? globals[(size_t) (4099 - 4096)] : -1; }
    // The TEO-5's Basic Program (captured from the synth and built into the editor), or empty if not available.
    static juce::MemoryBlock basicProgram();
    void selectProgram (int bank, int program, bool factory);
    void deviceInquiry();
    void sendTestNote(); // middle C for 0.4 s on the selected channel
    // Sets the TEO-5's MIDI globals the editor relies on (Param Send/Receive = NRPN, SysEx on, ports).
    // Callers confirm with the user first; returns a description of what was sent.
    juce::String configureSynthGlobals();
    bool hasOutput() const { return ports.outputId().isNotEmpty() || settings.useHostMidi; }

    // ---- Global settings (kept here, not in the APVTS) ----
    // Values in TEO-5 units by NRPN - 4096, from the global dump and from global NRPNs the synth sends.
    // Until a dump arrives they hold the table defaults. Message thread.
    static constexpr int numGlobals = 28;
    void requestGlobals();
    void applySequenceByte (int offset, int value); // one byte of the sequence, as the synth sends it while recording
    void followSynthRecording(); // RECORD on: clear the sequence; off: send it so the synth has no old steps either
    bool hasGlobals() const { return globalsReceived; }
    int globalValue (const ParamDef&) const;
    void setGlobal (const ParamDef&, int value); // stores the value and sends it as NRPN
    int globalsVersion = 0;                      // bumped whenever a global value changes

    // ---- Programs (librarian) ----
    // Bank/program addressing for MIDI. TODO: verify against hardware (see VERIFY.md).
    struct Address { int bankSelect; int programChange; int dumpBank; int dumpProgram; };
    static Address addressFor (int panelBank, int panelProgram, bool factory);

    // Blocking requests for background threads: send a request and wait for the matching dump (with retries).
    std::optional<juce::MemoryBlock> fetchProgram (int panelBank, int panelProgram, bool factory, int timeoutMs = 2000);
    std::optional<juce::MemoryBlock> fetchEditBuffer (int timeoutMs = 1500);

    // Apply a program to the panel and send it to the TEO-5 edit buffer (never writes memory). The TEO-5 stops its
    // sequencer on an edit buffer, so Play is resent if it was on.
    // asOriginal = false keeps the current "as loaded" program (used by COMPARE to restore the edit).
    void loadProgram (const juce::MemoryBlock& raw, bool asOriginal = true);
    // REVERT: load the program as loaded (or last written) into the edit buffer. False if none has been received.
    bool revertToOriginal();
    // Make `raw` the "as loaded" version (e.g. after it was written to memory).
    void setOriginal (const juce::MemoryBlock& raw) { programState.raw = raw; }
    // The current panel as a program: last received program with every mapped parameter and the name written in.
    // Empty if no program has been received yet or the dump layout is unknown.
    juce::MemoryBlock captureCurrent() const;
    // Store a program in TEO-5 memory. Callers must confirm with the user first.
    void writeProgram (int panelBank, int panelProgram, bool factory, const juce::MemoryBlock& raw);
    juce::String nameOf (const juce::MemoryBlock& raw) const { return layout.readName (raw); }
    // A parameter's value in the current program as it was loaded or received (not the live edit).
    std::optional<int> storedValue (const juce::String& paramId) const;
    bool hasNameLayout() const { return layout.nameOffset >= 0; }
    // Message thread: store the detected program-name field and refresh the current name.
    void setNameField (int offset, int length);
    // A name field found by an older detector and discarded at startup (so mis-read names can be redone). Once.
    std::optional<std::pair<int, int>> takeStaleNameField() { auto f = staleNameField; staleNameField.reset(); return f; }

    // Search the last received edit buffer for `name` (as shown on the TEO-5 OLED) to locate the name field.
    bool locateName (const juce::String& name);

    // Discover the program dump layout from the hardware (see VERIFY.md). Runs in the background.
    void startLayoutProbe();
    void cancelLayoutProbe();
    bool isProbing() const;
    float probeProgress() const { return probeProgressValue.load(); }

    // Audio thread: send played notes, bends, pressure and performance CCs from the DAW track to the TEO-5
    // (remapped to its channel). Lock-free.
    void forwardPlayedMidi (const juce::MidiBuffer&);

    // ---- Sequencer support ----
    // Plays a note on the TEO-5 (sequencer auditioning); the note-off follows after `ms`.
    void playNote (int note, int velocity, int ms = 250);
    // A note held for as long as the caller wants (e.g. while the mouse is down). Message thread.
    void noteOn (int note, int velocity);
    void noteOff (int note);
    // Sends the program with the edited sequence as the edit buffer (the TEO-5 has no per-step messages).
    void sendSequence();
    // Seq Play after an edit buffer (which stops the TEO-5's sequencer); queued after anything already queued.
    void resumeSequencer();
    // Note offs for these notes, then All Notes Off (after the sequencer stops, so nothing is left hanging).
    void releaseNotes (const juce::Array<int>& notes);
    void panic(); // All Sound Off, All Notes Off, Reset All Controllers and a note-off for every note
    // Note-ons played on the DAW track or on the TEO-5's own keys, for step entry. Message thread.
    bool popPlayedNote (int& note, int& velocity) { bool fromSynth; return popPlayedNote (note, velocity, fromSynth); }
    bool popPlayedNote (int& note, int& velocity, bool& fromSynth); // fromSynth: played on the TEO-5 (or its sequencer)
    // Audio thread, lock-free: the DAW's transport started or stopped while PLAY is on. A start sends Seq Stop
    // then Seq Play, so the TEO-5 begins again at step 1; a stop sends Seq Stop and leaves PLAY on in the editor.
    void restartSequencerForHost();
    void stopSequencerForHost();

    // Host MIDI routing: the processor feeds received host MIDI in and drains outgoing host MIDI.
    void handleHostMidiIn (const juce::MidiBuffer&);
    void drainHostMidiOut (juce::MidiBuffer&);

    juce::String getStatus() const;
    juce::String firmware;

    // Fired (message thread) when the TEO-5 changes program; ProgramState already holds the new location.
    std::function<void()> onProgramChanged;
    // Fired (message thread) when the program-name field becomes known.
    std::function<void()> onNameFieldFound;

    // Fired (message thread) when a parameter arrives from the synth, so the UI can flash the OLED.
    std::function<void (const juce::String& paramId)> onRemoteChange;

private:
    class ProbeThread;

    void parameterChanged (const juce::String& id, float newValue) override;
    void timerCallback() override;
    void handleMessage (const juce::MidiMessage&);
    void handleSysEx (const juce::MidiMessage&);
    void applyIncoming (const ParamDef&, int value);
    void applyProgramData (const juce::MemoryBlock& raw, bool asOriginal = true);
    void sendParameter (const ParamDef&, int value);
    void sendNrpnDirect (int nrpn, int value);
    void setStatus (const juce::String&);
    int currentValue (const juce::String& id) const;

    std::optional<juce::MemoryBlock> requestAndWait (const juce::MidiMessage& request, SysEx::Parsed::Type,
                                                     int bank, int program, int timeoutMs, int attempts);

    juce::AudioProcessorValueTreeState& apvts;
    ProgramState& programState;
    MidiPortManager ports;
    NrpnDecoder decoder;
    ProgramLayout layout;
    MidiSettings settings;

    std::atomic<bool> applyingIncoming { false };
    std::atomic<bool> playFromHostFlag { true };
    std::atomic<int> channelForAudioThread { 1 };
    std::atomic<bool> useCcForAudioThread { false };
    std::array<bool, 128> echoed {}; // audio thread: note-ons from the DAW that were the synth's own (see forwardPlayedMidi)
    // Seq Play messages the editor sends for the DAW's transport may come back from the synth; ignore them until then.
    std::atomic<juce::uint32> ignoreSeqPlayUntil { 0 };
    std::atomic<bool> stateRestored { false };
    std::atomic<bool> programActivity { false }; // anything sent or received for the program since startup
    bool synthRecording = false; // seqRecord as last seen by followSynthRecording
    void pushSeqPlay (int value); // audio thread

    // Played note-ons for step entry: written on the audio thread (DAW track), read on the message thread.
    juce::AbstractFifo playedFifo { 64 };
    std::array<std::pair<int, int>, 64> playedNotes {};
    std::vector<std::pair<int, int>> synthNotes; // from the TEO-5's keys (message thread)

    juce::CriticalSection hostLock;
    juce::MidiBuffer hostOut;

    mutable juce::CriticalSection statusLock;
    juce::String status { "MIDI not connected" };

    std::unique_ptr<ProbeThread> probeThread;
    std::atomic<float> probeProgressValue { 0.0f };

    // A background thread waiting for a specific dump (see requestAndWait).
    struct Waiter
    {
        SysEx::Parsed::Type type = SysEx::Parsed::Type::None;
        int bank = -1, program = -1;
        juce::MemoryBlock result;
    };
    juce::CriticalSection dumpLock;   // guards waiter + lastDump
    Waiter waiter;
    bool waiterActive = false;
    juce::WaitableEvent waiterDone;
    juce::CriticalSection requestLock; // one blocking request at a time
    juce::MemoryBlock lastDump;

    std::array<int, numGlobals> globals {};
    bool globalsReceived = false;

    int pendingRequestTicks = -1;
    int lastBankSelect = 0;
    juce::uint32 quietUntil = 0; // no OLED parameter popups until then (message thread)
    std::vector<juce::MemoryBlock> recentDumps; // distinct edit buffers, until the name field is known
    std::optional<std::pair<int, int>> staleNameField;

    JUCE_DECLARE_WEAK_REFERENCEABLE (MidiSync)
};

} // namespace teo5
