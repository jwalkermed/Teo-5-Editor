// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "MidiSync.h"
#include "BinaryData.h"
#include <cstring>

namespace teo5
{

// ---- Settings -------------------------------------------------------------------------------

juce::ValueTree MidiSettings::toValueTree() const
{
    juce::ValueTree t ("MIDI");
    t.setProperty ("in", inputId, nullptr);
    t.setProperty ("out", outputId, nullptr);
    t.setProperty ("channel", channel, nullptr);
    t.setProperty ("useCc", useCc, nullptr);
    t.setProperty ("useHost", useHostMidi, nullptr);
    t.setProperty ("requestOnPC", requestOnProgramChange, nullptr);
    t.setProperty ("playFromHost", playFromHost, nullptr);
    t.setProperty ("sysexManufacturer", sysexManufacturer, nullptr);
    t.setProperty ("sysexDevice", sysexDevice, nullptr);
    return t;
}

void MidiSettings::fromValueTree (const juce::ValueTree& t)
{
    if (! t.isValid())
        return;
    inputId = t.getProperty ("in", inputId).toString();
    outputId = t.getProperty ("out", outputId).toString();
    channel = juce::jlimit (1, 16, (int) t.getProperty ("channel", channel));
    useCc = t.getProperty ("useCc", useCc);
    useHostMidi = t.getProperty ("useHost", useHostMidi);
    requestOnProgramChange = t.getProperty ("requestOnPC", requestOnProgramChange);
    playFromHost = t.getProperty ("playFromHost", playFromHost);
    sysexManufacturer = t.getProperty ("sysexManufacturer", sysexManufacturer);
    sysexDevice = t.getProperty ("sysexDevice", sysexDevice);
}

// ---- Layout probe ---------------------------------------------------------------------------

class MidiSync::ProbeThread : public juce::Thread
{
public:
    explicit ProbeThread (MidiSync& s) : juce::Thread ("TEO5 layout probe"), sync (s) {}

    void run() override
    {
        juce::Array<const ParamDef*> targets;
        for (auto& def : allParams())
            if (def.scope == Scope::Program && def.nrpn >= 0 && def.maxValue > def.minValue)
                targets.add (&def);

        ProgramLayout found = sync.layout; // keep name offset etc.
        int resolved = 0;
        juce::StringArray unresolved;

        for (int i = 0; i < targets.size() && ! threadShouldExit(); ++i)
        {
            auto& def = *targets[i];
            sync.probeProgressValue = (float) i / (float) targets.size();
            sync.setStatus ("Probing " + juce::String (def.name) + " (" + juce::String (i + 1) + "/" + juce::String (targets.size()) + ")");

            const int original = sync.currentValue (def.id);

            // Three distinct values (min, max, and an odd mid value) so 2-byte fields are unambiguous.
            juce::Array<int> values { def.minValue, def.maxValue };
            const int mid = juce::jlimit (def.minValue, def.maxValue, (def.minValue + def.maxValue) / 2 | 1);
            if (! values.contains (mid))
                values.add (mid);

            std::vector<ProgramLayout::Sample> samples;
            for (int v : values)
            {
                auto dump = dumpWith (def.nrpn, v);
                if (dump.isEmpty())
                {
                    sync.sendNrpnDirect (def.nrpn, original);
                    sync.setStatus ("Probe stopped at " + juce::String (def.name) + ": no edit buffer reply. Check MIDI ports and SysEx settings on the TEO-5.");
                    sync.probeProgressValue = 0.0f;
                    return;
                }
                samples.emplace_back (std::move (dump), v);
            }
            sync.sendNrpnDirect (def.nrpn, original);

            if (auto f = ProgramLayout::infer (samples, def.nrpn))
            {
                found.setField (def.id, *f);
                ++resolved;
            }
            else
            {
                unresolved.add (def.id);
            }
        }

        if (threadShouldExit())
            return;

        found.fieldsVersion = ProgramLayout::currentFieldsVersion;
        found.save();
        juce::MessageManager::callAsync ([&s = sync, found, resolved, unresolved]
        {
            s.layout = found;
            s.setStatus ("Layout probe done: " + juce::String (resolved) + " parameters mapped"
                         + (unresolved.isEmpty() ? juce::String() : ", unresolved: " + unresolved.joinIntoString (", ")));
            s.probeProgressValue = 1.0f;
            s.requestEditBuffer();
        });
    }

private:
    juce::MemoryBlock dumpWith (int nrpn, int value)
    {
        sync.sendNrpnDirect (nrpn, value);
        // Ordered messages jump ahead of coalesced ones, so make sure the NRPN is out before requesting.
        for (int i = 0; i < 200 && sync.ports.pendingCount() > 0 && ! threadShouldExit(); ++i)
            wait (5);
        wait (80); // let the synth apply the value
        if (threadShouldExit())
            return {};
        return sync.fetchEditBuffer().value_or (juce::MemoryBlock());
    }

    MidiSync& sync;
};

// ---- MidiSync -------------------------------------------------------------------------------

MidiSync::MidiSync (juce::AudioProcessorValueTreeState& state, ProgramState& ps)
    : apvts (state), programState (ps)
{
    // Start from the measured TEO-5 layout; a saved layout only overrides it where it is trustworthy.
    layout = ProgramLayout::builtIn();
    ProgramLayout saved;
    if (saved.load())
    {
        if (saved.fieldsVersion >= ProgramLayout::currentFieldsVersion)
        {
            for (auto& [id, f] : saved.allFields())
                layout.setField (id, f);
            layout.fieldsVersion = saved.fieldsVersion;
        }
        if (saved.nameOffset >= 0 && saved.nameVersion >= ProgramLayout::currentNameVersion)
        {
            layout.nameOffset = saved.nameOffset;
            layout.nameLength = saved.nameLength;
        }
        else if (saved.nameOffset >= 0 && saved.nameOffset != layout.nameOffset)
        {
            // Found by the old detector, which could mistake other data (e.g. sequencer notes) for the name.
            staleNameField = std::make_pair (saved.nameOffset, saved.nameLength);
        }
    }

    for (auto& def : allParams())
    {
        if (apvts.getParameter (def.id) != nullptr)
            apvts.addParameterListener (def.id, this);
        if (def.scope == Scope::Global && juce::isPositiveAndBelow (def.nrpn - 4096, numGlobals))
            globals[(size_t) (def.nrpn - 4096)] = def.defaultValue; // until the synth's dump arrives
    }

    decoder.onNrpn = [this] (int, int nrpn, int value)
    {
        // While recording on the synth, the TEO-5 sends each changed sequence byte as an NRPN numbered by its
        // position in the program (measured): follow along live.
        if (nrpn >= Sequence::lengthOffset && nrpn < Sequence::velocityOffset + Sequence::numLanes * Sequence::numSteps)
        {
            applySequenceByte (nrpn, value);
            return;
        }
        if (auto* def = findByNrpn (nrpn))
            applyIncoming (*def, value);
    };
    decoder.onNrpnDelta = [this] (int, int nrpn, int delta)
    {
        if (auto* def = findByNrpn (nrpn))
            applyIncoming (*def, currentValue (def->id) + delta);
    };

    ports.onMessage = [this] (const juce::MidiMessage& m) { handleMessage (m); };
    startTimer (50);

    // Connect to a TEO-5 automatically unless saved settings (restored right after construction) say otherwise.
    juce::MessageManager::callAsync ([weak = juce::WeakReference<MidiSync> (this)]
    {
        if (weak != nullptr && weak->ports.inputId().isEmpty() && weak->ports.outputId().isEmpty() && ! weak->settings.useHostMidi)
            weak->applySettings (weak->settings);
    });
}

MidiSync::~MidiSync()
{
    cancelLayoutProbe();
    stopTimer();
    for (auto& def : allParams())
        if (apvts.getParameter (def.id) != nullptr)
            apvts.removeParameterListener (def.id, this);
    ports.setHostSink (nullptr);
}

void MidiSync::applySettings (const MidiSettings& newSettings, bool fromUser)
{
    auto s = newSettings;
    if (! fromUser && s.inputId.isEmpty() && s.outputId.isEmpty() && ! s.useHostMidi)
    {
        for (auto& d : MidiPortManager::inputs())
            if (d.name.containsIgnoreCase ("TEO") && s.inputId.isEmpty())
                s.inputId = d.identifier;
        for (auto& d : MidiPortManager::outputs())
            if (d.name.containsIgnoreCase ("TEO") && s.outputId.isEmpty())
                s.outputId = d.identifier;
    }

    const bool inChanged = s.inputId != ports.inputId() || ports.inputId().isEmpty();
    const bool outChanged = s.outputId != ports.outputId() || ports.outputId().isEmpty();
    settings = s;

    if (inChanged)
        ports.openInput (settings.inputId);
    if (outChanged)
        ports.openOutput (settings.outputId);

    if (settings.useHostMidi)
        ports.setHostSink ([this] (const juce::MidiMessage& m)
        {
            const juce::ScopedLock sl (hostLock);
            hostOut.addEvent (m, 0);
        });
    else
        ports.setHostSink (nullptr);

    decoder.reset();
    playFromHostFlag = settings.playFromHost;
    channelForAudioThread = settings.channel;
    useCcForAudioThread = settings.useCc;
    SysEx::setIds ((juce::uint8) settings.sysexManufacturer, (juce::uint8) settings.sysexDevice);
    if (outChanged && ports.outputId().isNotEmpty())
        deviceInquiry(); // identify the synth (and learn its SysEx IDs) as soon as the output is open

    if (settings.outputId.isNotEmpty() && ports.outputId().isEmpty())
        setStatus ("Could not open the selected MIDI output. Is another app holding it?");
    else if (settings.inputId.isNotEmpty() && ports.inputId().isEmpty())
        setStatus ("Could not open the selected MIDI input");
    else
        setStatus (ports.outputId().isNotEmpty() || settings.useHostMidi ? "MIDI connected" : "MIDI not connected");
}

juce::String MidiSync::getStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return status;
}

void MidiSync::setStatus (const juce::String& s)
{
    {
        const juce::ScopedLock sl (statusLock);
        status = s;
    }
    sendChangeMessage(); // async, safe from any thread
}

int MidiSync::currentValue (const juce::String& id) const
{
    if (auto* p = dynamic_cast<juce::AudioParameterInt*> (apvts.getParameter (id)))
        return p->get();
    return 0;
}

// ---- Outgoing -------------------------------------------------------------------------------

void MidiSync::parameterChanged (const juce::String& id, float newValue)
{
    if (applyingIncoming.load())
        return; // came from the synth: don't echo it back

    if (auto* def = findById (id))
        if (def->scope == Scope::Program || def->scope == Scope::Control)
            sendParameter (*def, juce::roundToInt (newValue));
}

static int toCcValue (const ParamDef& def, int value)
{
    if (def.minValue >= 0 && def.maxValue <= 127)
        return value;
    return juce::roundToInt (127.0 * (value - def.minValue) / (double) (def.maxValue - def.minValue));
}

static int fromCcValue (const ParamDef& def, int cc)
{
    if (def.minValue >= 0 && def.maxValue <= 127)
        return cc;
    return def.minValue + juce::roundToInt ((def.maxValue - def.minValue) * cc / 127.0);
}

void MidiSync::sendParameter (const ParamDef& def, int value)
{
    programActivity = true;
    const int ch = settings.channel;
    const bool viaNrpn = def.nrpn >= 0 && (! settings.useCc || def.cc < 0);

    if (viaNrpn)
        ports.queueCoalesced (def.nrpn, Nrpn::encode (ch, def.nrpn, value));
    else if (def.cc >= 0)
        ports.queueCoalesced (100000 + def.cc, { juce::MidiMessage::controllerEvent (ch, def.cc, toCcValue (def, value)) });
}

// Recording on the TEO-5 starts over (it sets the length to 1 and grows it), so a recording pass replaces the whole
// sequence: when RECORD turns on, every step is cleared and only what's recorded remains. When it turns off, the
// cleared sequence goes to the edit buffer, or the synth would keep the old steps past the new length.
void MidiSync::followSynthRecording()
{
    const bool on = currentValue ("seqRecord") != 0;
    if (on == synthRecording)
        return;
    synthRecording = on;
    if (on)
    {
        programState.beforeSynthRecording = programState.sequence;
        programState.sequence.clearAll();
        programState.sequence.length = 1;
        ++programState.liveSequenceVersion;
    }
    else if (programState.sequence != programState.beforeSynthRecording)
    {
        sendSequence(); // the TEO-5 stops its sequencer on an edit buffer: restart it if it was playing
        if (currentValue ("seqPlay") != 0)
            resumeSequencer();
    }
}

void MidiSync::applySequenceByte (int offset, int value)
{
    followSynthRecording(); // RECORD's NRPN came just before: clear first
    // Patch the byte in a program image of the current sequence and read the sequence back.
    juce::MemoryBlock image = SysEx::isProgramSize (programState.raw.getSize()) ? programState.raw
                                                                                : juce::MemoryBlock ((size_t) SysEx::programSize + 6, true);
    // Recording over a step, the TEO-5 replaces the whole step but only reports the notes played (measured):
    // when a step's first note (lane 1's pitch) arrives, clear the step's other lanes as the synth does.
    const int pitchStart = Sequence::noteOffset, steps = Sequence::numSteps;
    if (offset >= pitchStart && offset < pitchStart + steps && currentValue ("seqRecord") != 0)
        for (int lane = 1; lane < Sequence::numLanes; ++lane)
            programState.sequence.removeNote (lane, offset - pitchStart);

    programState.sequence.write (image);
    static_cast<juce::uint8*> (image.getData())[offset] = (juce::uint8) juce::jlimit (0, 255, value);
    programState.sequence = Sequence::read (image);
    ++programState.liveSequenceVersion; // not a new program: the undo history carries on
}

void MidiSync::requestGlobals()
{
    ports.queueOrdered (SysEx::requestGlobals());
}

bool MidiSync::clockIsExternal() const
{
    const int mode = globals[(size_t) (4099 - 4096)]; // Off|Out|Out No S/S|In|In Thru|In No S/S
    return globalsReceived && mode >= 3;
}

int MidiSync::globalValue (const ParamDef& def) const
{
    if (! juce::isPositiveAndBelow (def.nrpn - 4096, numGlobals))
        return def.defaultValue;
    return globals[(size_t) (def.nrpn - 4096)];
}

void MidiSync::setGlobal (const ParamDef& def, int value)
{
    if (! juce::isPositiveAndBelow (def.nrpn - 4096, numGlobals))
        return;
    value = juce::jlimit (def.minValue, def.maxValue, value);
    globals[(size_t) (def.nrpn - 4096)] = value;
    ++globalsVersion;
    sendNrpnDirect (def.nrpn, value);
}

void MidiSync::sendNrpnDirect (int nrpn, int value)
{
    ports.queueCoalesced (nrpn, Nrpn::encode (settings.channel, nrpn, value));
}

void MidiSync::sendAllParameters()
{
    for (auto& def : allParams())
        if (def.scope == Scope::Program)
            sendParameter (def, currentValue (def.id));
    setStatus ("Sent all parameters");
}

void MidiSync::requestEditBuffer()
{
    ports.queueOrdered (SysEx::requestEditBuffer());
}

void MidiSync::deviceInquiry()
{
    if (! hasOutput())
    {
        setStatus ("No MIDI output selected: choose the TEO-5 under Output first");
        return;
    }
    ports.queueOrdered (SysEx::deviceInquiry());
    setStatus ("Asked the TEO-5 for its ID... (no answer = output or SysEx settings problem)");
}

juce::String MidiSync::configureSynthGlobals()
{
    // Global NRPNs from the MIDI Implementation ("Global Parameter Data").
    juce::String outName;
    for (auto& d : MidiPortManager::outputs())
        if (d.identifier == ports.outputId())
            outName = d.name;
    const bool viaUsb = outName.containsIgnoreCase ("TEO"); // the TEO-5's own USB port, not a MIDI interface

    struct G { int nrpn, value; const char* text; };
    const G setup[] = {
        { 4102, 2, "MIDI Param Send = NRPN" },    // Off|CC|NRPN (the spec's table order is wrong)
        { 4103, 2, "MIDI Param Receive = NRPN" },
        { 4104, 1, "MIDI Control = On" },
        { 4105, 1, "MIDI SysEx = On" },
        { 4106, viaUsb ? 1 : 0, viaUsb ? "MIDI SysEx Cable = USB" : "MIDI SysEx Cable = MIDI" },
        { 4107, 3, "MIDI Out Select = All" },     // Off|MIDI|USB|All
        { 4108, 1, "MIDI Program Send = On" },
        { 4109, 1, "MIDI Program Receive = On" },
    };
    juce::StringArray done;
    for (auto& g : setup)
    {
        for (auto& m : Nrpn::encode (settings.channel, g.nrpn, g.value))
            ports.queueOrdered (m);
        done.add (g.text);
    }
    deviceInquiry();
    setStatus ("Sent TEO-5 global settings. Turn a knob on the synth to check it now transmits");
    return done.joinIntoString (", ");
}

void MidiSync::sendTestNote()
{
    const int ch = settings.channel;
    ports.queueOrdered (juce::MidiMessage::noteOn (ch, 60, (juce::uint8) 100));
    juce::Timer::callAfterDelay (400, [this, ch] { ports.queueOrdered (juce::MidiMessage::noteOff (ch, 60)); });
    setStatus ("Sent middle C on channel " + juce::String (ch));
}

void MidiSync::sendEditBuffer()
{
    auto raw = captureCurrent();
    if (raw.isEmpty())
    {
        setStatus ("Can't send edit buffer: receive a program from the TEO-5 first");
        return;
    }
    ports.queueOrdered (SysEx::editBufferDump (raw));
    setStatus ("Edit buffer sent");
}

void MidiSync::fetchSynthUnlessRestored()
{
    juce::MessageManager::callAsync ([weak = juce::WeakReference<MidiSync> (this)]
    {
        juce::Timer::callAfterDelay (1500, [weak]
        {
            if (weak != nullptr && weak->hasOutput())
                weak->requestGlobals(); // e.g. whether the synth follows external clock
            // Not if a project was restored, or the editor already sent or received a program meanwhile.
            if (weak != nullptr && ! weak->stateRestored && ! weak->programActivity && ! weak->isProbing() && weak->hasOutput())
                weak->requestEditBuffer();
        });
    });
}

juce::MemoryBlock MidiSync::basicProgram()
{
    int size = 0;
    if (auto* data = BinaryData::getNamedResource ("BasicProgram_syx", size))
    {
        // An edit-buffer dump as received from the TEO-5 (without F0/F7).
        const auto parsed = SysEx::parse (juce::MidiMessage::createSysExMessage (data, size));
        if (parsed.type == SysEx::Parsed::Type::EditBuffer && SysEx::isProgramSize (parsed.data.getSize()))
            return parsed.data;
    }
    return {};
}

void MidiSync::restoreSynthFromState()
{
    stateRestored = true;
    // The host may restore state off the message thread and before the ports are open, so wait a moment.
    juce::MessageManager::callAsync ([weak = juce::WeakReference<MidiSync> (this)]
    {
        juce::Timer::callAfterDelay (500, [weak]
        {
            if (weak == nullptr || ! weak->hasOutput())
                return;
            if (SysEx::isProgramSize (weak->programState.raw.getSize()))
                weak->sendEditBuffer();      // the whole program, including the name and sequence
            else
                weak->sendAllParameters();   // saved before any program came from the synth: parameters only
        });
    });
}

MidiSync::Address MidiSync::addressFor (int panelBank, int panelProgram, bool factory)
{
    // Confirmed on hardware: Bank Select CC 32 = bank-1 (+16 for Factory), Program Change = program-1;
    // SysEx program dumps use the same bank/program numbers (User 512 dumps as bank 4, program 11).
    const int bank = juce::jlimit (0, 15, panelBank - 1) + (factory ? 16 : 0);
    const int program = juce::jlimit (0, 15, panelProgram - 1);
    return { bank, program, bank, program };
}

void MidiSync::selectProgram (int bank, int program, bool factory)
{
    programState.bank = bank;
    programState.program = program;
    programState.factory = factory;

    const auto a = addressFor (bank, program, factory);
    const int ch = settings.channel;
    quietUntil = juce::Time::getMillisecondCounter() + 1500;
    ports.queueOrdered (juce::MidiMessage::controllerEvent (ch, 32, a.bankSelect));
    ports.queueOrdered (juce::MidiMessage::programChange (ch, a.programChange));
    sequencerStoppedByProgramChange();
    if (settings.requestOnProgramChange)
        pendingRequestTicks = 3; // give the synth ~150 ms to load the program
}

// A program change stops the TEO-5's sequencer: show PLAY off (nothing is sent; PLAY starts it again).
void MidiSync::sequencerStoppedByProgramChange()
{
    if (auto* p = apvts.getParameter ("seqPlay"); p != nullptr && currentValue ("seqPlay") != 0)
    {
        applyingIncoming = true; // the synth has already stopped: don't send Stop
        p->setValueNotifyingHost (0.0f);
        applyingIncoming = false;
    }
}

// ---- Incoming -------------------------------------------------------------------------------

void MidiSync::handleMessage (const juce::MidiMessage& m)
{
    if (m.isSysEx())
    {
        handleSysEx (m);
        return;
    }

    if (m.getChannel() != settings.channel)
        return;

    if (m.isNoteOn())
    {
        synthNotes.push_back ({ m.getNoteNumber(), (int) m.getVelocity() }); // keys played on the TEO-5
        if (synthNotes.size() > 64)
            synthNotes.erase (synthNotes.begin());
        return;
    }

    if (m.isController())
    {
        if (decoder.process (m))
            return;
        if (m.getControllerNumber() == 32)
        {
            lastBankSelect = m.getControllerValue(); // applies to the following program change
            return;
        }
        if (m.getControllerNumber() == 0)
            return;
        if (auto* def = findByCc (m.getControllerNumber()))
            applyIncoming (*def, fromCcValue (*def, m.getControllerValue()));
    }
    else if (m.isProgramChange())
    {
        // The synth changed program: follow it on the panel and refresh the edit buffer.
        programState.factory = lastBankSelect >= 16;
        programState.bank = (lastBankSelect % 16) + 1;
        programState.program = juce::jlimit (0, 15, m.getProgramChangeNumber()) + 1;
        quietUntil = juce::Time::getMillisecondCounter() + 1500;
        sequencerStoppedByProgramChange();
        if (onProgramChanged)
            onProgramChanged();
        if (settings.requestOnProgramChange)
            pendingRequestTicks = 3;
    }
}

void MidiSync::handleSysEx (const juce::MidiMessage& m)
{
    auto parsed = SysEx::parse (m);
    using T = SysEx::Parsed::Type;

    switch (parsed.type)
    {
        case T::InquiryReply:
            if (parsed.manufacturer == SysEx::defaultManufacturerId || parsed.manufacturer == 0x02)
            {
                // Use the IDs the synth reports for all further SysEx (the spec's printed ID is wrong).
                settings.sysexManufacturer = parsed.manufacturer;
                settings.sysexDevice = parsed.family;
                SysEx::setIds ((juce::uint8) parsed.manufacturer, (juce::uint8) parsed.family);
                firmware = parsed.firmware;
                setStatus ("TEO-5 found, OS " + firmware + "  (SysEx ID "
                           + juce::String::toHexString (parsed.manufacturer).paddedLeft ('0', 2) + " "
                           + juce::String::toHexString (parsed.family).paddedLeft ('0', 2) + ")");
            }
            else
            {
                setStatus ("A non-Oberheim device answered (manufacturer ID "
                           + juce::String::toHexString (parsed.manufacturer) + "). Check the selected ports.");
            }
            break;

        case T::EditBuffer:
        case T::Program:
            if (! SysEx::isProgramSize (parsed.data.getSize()))
                setStatus ("Unexpected dump size " + juce::String ((int) parsed.data.getSize()) + " bytes. See VERIFY.md");

            {
                const juce::ScopedLock sl (dumpLock);
                lastDump = parsed.data;
                if (waiterActive && waiter.type == parsed.type
                    && (parsed.type == T::EditBuffer || (waiter.bank == parsed.bank && waiter.program == parsed.program)))
                {
                    waiter.result = parsed.data;
                    waiterActive = false;
                    waiterDone.signal();
                    break; // requested by a background job: don't touch the panel
                }
            }
            if (parsed.type == T::EditBuffer)
                applyProgramData (parsed.data);
            break;

        case T::Globals:
            for (int i = 0; i < numGlobals && i < (int) parsed.data.getSize(); ++i)
                globals[(size_t) i] = static_cast<const juce::uint8*> (parsed.data.getData())[i];
            globalsReceived = true;
            ++globalsVersion;
            break;

        case T::None:
            break;
    }
}

void MidiSync::applyIncoming (const ParamDef& def, int value)
{
    if (std::strcmp (def.id, "seqPlay") == 0 && juce::Time::getMillisecondCounter() < ignoreSeqPlayUntil.load())
        return; // the synth answering a transport restart/stop: PLAY stays as the user set it

    if (def.scope == Scope::Global)
    {
        if (juce::isPositiveAndBelow (def.nrpn - 4096, numGlobals))
        {
            globals[(size_t) (def.nrpn - 4096)] = juce::jlimit (def.minValue, def.maxValue, value);
            ++globalsVersion;
        }
        return;
    }

    auto* p = dynamic_cast<juce::AudioParameterInt*> (apvts.getParameter (def.id));
    if (p == nullptr)
        return;

    applyingIncoming = true;
    p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (def.minValue, def.maxValue, value)));
    applyingIncoming = false;

    // Don't flash the OLED for the burst of parameters the TEO-5 sends when it changes program.
    if (onRemoteChange && juce::Time::getMillisecondCounter() > quietUntil)
        onRemoteChange (def.id);
}

void MidiSync::applyProgramData (const juce::MemoryBlock& raw, bool asOriginal)
{
    programActivity = true;
    if (asOriginal)
        programState.raw = raw;
    programState.sequence = Sequence::read (raw);
    ++programState.sequenceVersion;

    if (layout.nameOffset < 0 && asOriginal)
    {
        // Learn where the name is from a few different programs the user browses to.
        bool seen = false;
        for (auto& d : recentDumps)
            seen = seen || d == raw;
        if (! seen)
            recentDumps.push_back (raw);
        if (recentDumps.size() >= 4)
            if (auto field = ProgramLayout::detectNameField (recentDumps))
            {
                setNameField (field->first, field->second);
                recentDumps.clear();
            }
        if (recentDumps.size() > 12)
            recentDumps.erase (recentDumps.begin());
    }
    if (layout.nameOffset >= 0)
        programState.name = layout.readName (raw);
    else if (asOriginal)
        programState.name = juce::String (programState.factory ? "Factory " : "User ")
                          + juce::String (programState.bank) + "-" + juce::String (programState.program).paddedLeft ('0', 2);
    quietUntil = juce::jmax (quietUntil, juce::Time::getMillisecondCounter() + 800); // the synth's parameter burst after a program change

    if (layout.isEmpty())
    {
        setStatus ("Edit buffer received. Run the layout probe once so dumps can update the panel");
        return;
    }

    applyingIncoming = true;
    for (auto& [id, f] : layout.allFields())
        if (auto* p = dynamic_cast<juce::AudioParameterInt*> (apvts.getParameter (id)))
            if (auto* def = findById (id))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (def->minValue, def->maxValue, layout.read (raw, f))));
    applyingIncoming = false;

    setStatus ("Program loaded from TEO-5");
}

std::optional<int> MidiSync::storedValue (const juce::String& paramId) const
{
    const auto& raw = programState.raw;
    auto* f = layout.field (paramId);
    auto* def = findById (paramId);
    if (f == nullptr || def == nullptr || ! SysEx::isProgramSize (raw.getSize()))
        return std::nullopt;
    return juce::jlimit (def->minValue, def->maxValue, layout.read (raw, *f));
}

void MidiSync::setNameField (int offset, int length)
{
    layout.nameOffset = offset;
    layout.nameLength = length;
    layout.nameVersion = ProgramLayout::currentNameVersion;
    layout.save();
    if (SysEx::isProgramSize (programState.raw.getSize()))
        programState.name = layout.readName (programState.raw);
    setStatus ("Program names found in the dumps (byte " + juce::String (offset) + ")");
    if (onNameFieldFound)
        onNameFieldFound();
}

bool MidiSync::locateName (const juce::String& name)
{
    juce::MemoryBlock dump;
    {
        const juce::ScopedLock sl (dumpLock);
        dump = lastDump;
    }
    const auto needle = name.trim().toStdString();
    if (dump.isEmpty() || needle.empty())
        return false;

    const std::string hay (static_cast<const char*> (dump.getData()), dump.getSize());
    const auto pos = hay.find (needle);
    if (pos == std::string::npos)
    {
        setStatus ("Name \"" + name + "\" not found in the last dump");
        return false;
    }
    layout.nameOffset = (int) pos;
    layout.nameVersion = ProgramLayout::currentNameVersion; // typed by the user: reliable
    layout.save();
    programState.name = layout.readName (dump);
    setStatus ("Program name located at byte " + juce::String ((int) pos));
    return true;
}

// ---- Programs -------------------------------------------------------------------------------

std::optional<juce::MemoryBlock> MidiSync::requestAndWait (const juce::MidiMessage& request, SysEx::Parsed::Type type,
                                                           int bank, int program, int timeoutMs, int attempts)
{
    jassert (! juce::MessageManager::existsAndIsCurrentThread()); // would deadlock: replies arrive on the message thread
    const juce::ScopedLock rl (requestLock);

    // Large SysEx replies are occasionally dropped, so retry a few times.
    for (int attempt = 0; attempt < attempts; ++attempt)
    {
        {
            const juce::ScopedLock sl (dumpLock);
            waiter = { type, bank, program, {} };
            waiterActive = true;
            waiterDone.reset();
        }
        ports.queueOrdered (request);
        const bool ok = waiterDone.wait (timeoutMs);

        const juce::ScopedLock sl (dumpLock);
        waiterActive = false;
        if (ok && ! waiter.result.isEmpty())
            return waiter.result;
        if (auto* t = juce::Thread::getCurrentThread(); t != nullptr && t->threadShouldExit())
            break;
    }
    return std::nullopt;
}

std::optional<juce::MemoryBlock> MidiSync::fetchEditBuffer (int timeoutMs)
{
    return requestAndWait (SysEx::requestEditBuffer(), SysEx::Parsed::Type::EditBuffer, -1, -1, timeoutMs, 3);
}

std::optional<juce::MemoryBlock> MidiSync::fetchProgram (int panelBank, int panelProgram, bool factory, int timeoutMs)
{
    const auto a = addressFor (panelBank, panelProgram, factory);
    return requestAndWait (SysEx::requestProgram (a.dumpBank, a.dumpProgram), SysEx::Parsed::Type::Program,
                           a.dumpBank, a.dumpProgram, timeoutMs, 3);
}

void MidiSync::loadProgram (const juce::MemoryBlock& raw, bool asOriginal)
{
    applyProgramData (raw, asOriginal);
    ports.queueOrdered (SysEx::editBufferDump (raw)); // send the original bytes, including unmapped ones
    if (currentValue ("seqPlay") != 0)
        resumeSequencer();
}

bool MidiSync::revertToOriginal()
{
    if (! SysEx::isProgramSize (programState.raw.getSize()))
        return false;
    const auto raw = programState.raw; // loadProgram replaces programState.raw with its argument
    loadProgram (raw, true);
    return true;
}

juce::MemoryBlock MidiSync::captureCurrent() const
{
    if (! SysEx::isProgramSize (programState.raw.getSize()) || layout.isEmpty())
        return {};
    auto raw = programState.raw;
    for (auto& [id, f] : layout.allFields())
        layout.write (raw, f, currentValue (id));
    layout.writeName (raw, programState.name);
    programState.sequence.write (raw); // bytes 189-909; no parameter lives there on a TEO-5
    return raw;
}

void MidiSync::sendSequence()
{
    auto raw = captureCurrent();
    if (! raw.isEmpty())
        ports.queueOrdered (SysEx::editBufferDump (raw));
}

void MidiSync::resumeSequencer()
{
    ignoreSeqPlayUntil = juce::Time::getMillisecondCounter() + 400;
    if (auto* def = findById ("seqPlay"))
        for (auto& m : Nrpn::encode (settings.channel, def->nrpn, 1))
            ports.queueOrdered (m);
}

void MidiSync::playNote (int note, int velocity, int ms)
{
    const int ch = settings.channel;
    ports.queueOrdered (juce::MidiMessage::noteOn (ch, juce::jlimit (0, 127, note), (juce::uint8) juce::jlimit (1, 127, velocity)));
    juce::Timer::callAfterDelay (ms, [weak = juce::WeakReference<MidiSync> (this), ch, note]
    {
        if (auto* s = weak.get())
            s->ports.queueOrdered (juce::MidiMessage::noteOff (ch, juce::jlimit (0, 127, note)));
    });
}

void MidiSync::noteOn (int note, int velocity)
{
    ports.queueOrdered (juce::MidiMessage::noteOn (settings.channel, juce::jlimit (0, 127, note), (juce::uint8) juce::jlimit (1, 127, velocity)));
}

void MidiSync::noteOff (int note)
{
    ports.queueOrdered (juce::MidiMessage::noteOff (settings.channel, juce::jlimit (0, 127, note)));
}

bool MidiSync::popPlayedNote (int& note, int& velocity, bool& fromSynth)
{
    fromSynth = ! synthNotes.empty();
    if (fromSynth)
    {
        std::tie (note, velocity) = synthNotes.front();
        synthNotes.erase (synthNotes.begin());
        return true;
    }
    const auto scope = playedFifo.read (1);
    if (scope.blockSize1 + scope.blockSize2 == 0)
        return false;
    const auto& p = playedNotes[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
    note = p.first;
    velocity = p.second;
    return true;
}

void MidiSync::writeProgram (int panelBank, int panelProgram, bool factory, const juce::MemoryBlock& raw)
{
    const auto a = addressFor (panelBank, panelProgram, factory);
    ports.queueOrdered (SysEx::programDump (a.dumpBank, a.dumpProgram, raw));
}

// ---- Probe ----------------------------------------------------------------------------------

void MidiSync::startLayoutProbe()
{
    cancelLayoutProbe();
    probeProgressValue = 0.0f;
    probeThread = std::make_unique<ProbeThread> (*this);
    probeThread->startThread();
}

bool MidiSync::isProbing() const
{
    return probeThread != nullptr && probeThread->isThreadRunning();
}

void MidiSync::cancelLayoutProbe()
{
    if (probeThread != nullptr)
    {
        probeThread->signalThreadShouldExit();
        waiterDone.signal();
        probeThread->stopThread (4000);
        probeThread.reset();
    }
}

// ---- Host MIDI ------------------------------------------------------------------------------

void MidiSync::forwardPlayedMidi (const juce::MidiBuffer& buffer)
{
    if (! playFromHostFlag.load())
        return;
    const int ch = channelForAudioThread.load();
    for (const auto meta : buffer)
    {
        auto m = meta.getMessage();
        if (m.isController())
        {
            // NRPN/RPN framing and bank select belong to the editor, not the player.
            const int cc = m.getControllerNumber();
            if (cc == 0 || cc == 6 || cc == 32 || cc == 38 || (cc >= 96 && cc <= 101))
                continue;
        }
        else if (! (m.isNoteOnOrOff() || m.isPitchWheel() || m.isChannelPressure() || m.isAftertouch()))
        {
            continue;
        }
        // A note the TEO-5 itself just played, coming back through the DAW track (e.g. Live's "All Ins"): don't send
        // it back to the synth (it would sound twice, and the synth's recording would take it as a second step),
        // and don't count it as played on the track either. Its note off is dropped too.
        if (m.isNoteOn() && juce::Time::getMillisecondCounter() - ports.lastNoteOnFromSynth (m.getNoteNumber()) < 150)
        {
            echoed[(size_t) (m.getNoteNumber() & 127)] = true;
            continue;
        }
        if (m.isNoteOff() && echoed[(size_t) (m.getNoteNumber() & 127)])
        {
            echoed[(size_t) (m.getNoteNumber() & 127)] = false;
            continue;
        }
        if (m.isNoteOn())
        {
            const auto scope = playedFifo.write (1);
            if (scope.blockSize1 + scope.blockSize2 > 0)
                playedNotes[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = { m.getNoteNumber(), (int) m.getVelocity() };
        }
        m.setChannel (ch);
        ports.pushRealtime (m);
    }
}

void MidiSync::pushSeqPlay (int value)
{
    static const auto* def = findById ("seqPlay");
    const int ch = channelForAudioThread.load();
    if (def->nrpn >= 0 && ! useCcForAudioThread.load())
    {
        // Built here rather than with Nrpn::encode, which allocates.
        ports.pushRealtime (juce::MidiMessage::controllerEvent (ch, 99, (def->nrpn >> 7) & 0x7f));
        ports.pushRealtime (juce::MidiMessage::controllerEvent (ch, 98, def->nrpn & 0x7f));
        ports.pushRealtime (juce::MidiMessage::controllerEvent (ch, 6, 0));
        ports.pushRealtime (juce::MidiMessage::controllerEvent (ch, 38, value));
    }
    else
    {
        ports.pushRealtime (juce::MidiMessage::controllerEvent (ch, def->cc, value));
    }
}

void MidiSync::restartSequencerForHost()
{
    ignoreSeqPlayUntil = juce::Time::getMillisecondCounter() + 400;
    pushSeqPlay (0);
    pushSeqPlay (1);
}

void MidiSync::stopSequencerForHost()
{
    ignoreSeqPlayUntil = juce::Time::getMillisecondCounter() + 400;
    pushSeqPlay (0);
    // Notes the sequencer was playing can be left hanging when it stops: All Notes Off and All Sound Off.
    const int ch = channelForAudioThread.load();
    ports.pushRealtime (juce::MidiMessage::allNotesOff (ch));
    ports.pushRealtime (juce::MidiMessage::allSoundOff (ch));
}

void MidiSync::releaseNotes (const juce::Array<int>& notes)
{
    const int ch = settings.channel;
    for (int n : notes)
        ports.queueOrdered (juce::MidiMessage::noteOff (ch, juce::jlimit (0, 127, n)));
    ports.queueOrdered (juce::MidiMessage::allNotesOff (ch));
}

// PANIC on the panel: everything off, against hung notes. Leaves the sequencer, arpeggiator and program alone.
void MidiSync::panic()
{
    const int ch = settings.channel;
    ports.queueOrdered (juce::MidiMessage::allSoundOff (ch));
    ports.queueOrdered (juce::MidiMessage::allNotesOff (ch));
    ports.queueOrdered (juce::MidiMessage::controllerEvent (ch, 121, 0)); // Reset All Controllers (wheels, pedals)
    for (int n = 0; n < 128; ++n)
        ports.queueOrdered (juce::MidiMessage::noteOff (ch, n)); // in case a held note ignores All Notes Off
}

void MidiSync::handleHostMidiIn (const juce::MidiBuffer& buffer)
{
    if (! settings.useHostMidi)
        return;
    for (const auto meta : buffer)
    {
        const auto m = meta.getMessage();
        if (m.isController() || m.isProgramChange() || m.isSysEx())
            ports.inject (m);
    }
}

void MidiSync::drainHostMidiOut (juce::MidiBuffer& buffer)
{
    const juce::ScopedTryLock sl (hostLock);
    if (sl.isLocked() && ! hostOut.isEmpty())
    {
        buffer.addEvents (hostOut, 0, -1, 0);
        hostOut.clear();
    }
}

void MidiSync::timerCallback()
{
    followSynthRecording();
    if (pendingRequestTicks > 0 && --pendingRequestTicks == 0)
    {
        pendingRequestTicks = -1;
        requestEditBuffer();
    }
}

} // namespace teo5
