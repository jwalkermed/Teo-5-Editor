// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "UI/Skin.h"

using namespace teo5;

TEO5Processor::TEO5Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TEO5", createLayout())
{
    teo5::Skin::initialise(); // the saved skin choice (and custom skins)
    midiSync = std::make_unique<MidiSync> (apvts, programState);
    transfer = std::make_unique<HardwareTransfer> (*midiSync, *library);
    seqPlayValue = apvts.getRawParameterValue ("seqPlay");
    // A plug-in talks to the synth only once the host runs it (prepareToPlay) or opens its editor: DAWs load hidden
    // copies to scan or test plug-ins, and those must not send the synth anything. The app (and tools) start at once.
    // TEO5_QUIET (the load test, screenshots) keeps a copy quiet altogether.
    quiet = juce::SystemStats::getEnvironmentVariable ("TEO5_QUIET", {}).isNotEmpty();
    if (! quiet && wrapperType != wrapperType_Undefined) // the plug-in and the app; not the test tools
        midiSync->getPorts().setLogFile (juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                             .getChildFile ("Library/TEO5Editor/midi-log.txt"));
    if (! quiet && (wrapperType == wrapperType_Standalone || wrapperType == wrapperType_Undefined))
        midiSync->startTalking(); // a new instance starts on User 1, program 01
}

void TEO5Processor::startTalking()
{
    if (! quiet)
        midiSync->startTalking();
}

TEO5Processor::~TEO5Processor()
{
    transfer.reset(); // stop transfers before MIDI goes away
    midiSync.reset();
}

juce::AudioProcessorValueTreeState::ParameterLayout TEO5Processor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (auto& def : allParams())
    {
        if (def.scope == Scope::Global)
            continue; // globals live in the Global page, not in DAW automation

        const auto* d = &def;
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { def.id, 1 }, def.name, def.minValue, def.maxValue, def.defaultValue,
            juce::AudioParameterIntAttributes()
                .withStringFromValueFunction ([d] (int v, int) { return valueToText (*d, v); })
                .withValueFromStringFunction ([d] (const juce::String& s)
                {
                    const int idx = choiceNames (*d).indexOf (s, true);
                    return idx >= 0 ? d->minValue + idx : s.getIntValue();
                })));
    }
    return layout;
}

void TEO5Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Editor/librarian only: no audio is produced.
    buffer.clear();

    // The DAW's song position, for the sequencer drawer's playhead (the TEO-5 follows Live's MIDI clock).
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            const bool playing = pos->getIsPlaying();
            const bool seqPlay = seqPlayValue->load() >= 0.5f;
            if (auto ppq = pos->getPpqPosition())
                hostPpq = *ppq;
            if (auto bpm = pos->getBpm())
                hostBpm = *bpm;

            // With PLAY on, every DAW start restarts the TEO-5's sequence at step 1 and every stop stops it
            // (with MIDI Clock "In No S/S" the synth ignores the DAW's own Start/Stop and would resume mid-pattern).
            if (playing != wasPlaying)
            {
                if (playing)
                    seqStartPpq = hostPpq.load();
                if (seqPlay)
                    playing ? midiSync->restartSequencerForHost() : midiSync->stopSequencerForHost();
            }
            else if (playing && seqPlay && ! wasSeqPlay)
            {
                seqStartPpq = hostPpq.load(); // PLAY turned on mid-song: the sequence starts from here
            }
            wasPlaying = playing;
            wasSeqPlay = seqPlay;
            hostPlaying = playing;
        }

    // Notes etc. played on this track go straight to the TEO-5.
    midiSync->forwardPlayedMidi (midi);

    // Optional routing through the host's MIDI ports (direct ports are handled by MidiPortManager).
    midiSync->handleHostMidiIn (midi);
    midi.clear();
    midiSync->drainHostMidiOut (midi);
}

int TEO5Processor::getNative (const juce::String& id) const
{
    if (auto* p = dynamic_cast<juce::AudioParameterInt*> (apvts.getParameter (id)))
        return p->get();
    return 0;
}

void TEO5Processor::setNative (const juce::String& id, int value)
{
    if (auto* p = dynamic_cast<juce::AudioParameterInt*> (apvts.getParameter (id)))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) value));
}

void TEO5Processor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("programName", programState.name, nullptr);
    state.setProperty ("bank", programState.bank, nullptr);
    state.setProperty ("program", programState.program, nullptr);
    state.setProperty ("factory", programState.factory, nullptr);
    if (editorWidth > 0)
        state.setProperty ("editorWidth", editorWidth.load(), nullptr);
    if (programState.raw.getSize() > 0)
        state.setProperty ("rawProgram", programState.raw.toBase64Encoding(), nullptr);
    state.removeChild (state.getChildWithName ("MIDI"), nullptr);
    state.appendChild (midiSync->getSettings().toValueTree(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void TEO5Processor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (state.hasType (apvts.state.getType()))
        {
            programState.name = state.getProperty ("programName", programState.name).toString();
            programState.bank = state.getProperty ("bank", programState.bank);
            programState.program = state.getProperty ("program", programState.program);
            programState.factory = state.getProperty ("factory", programState.factory);
            programState.raw.reset();
            programState.raw.fromBase64Encoding (state.getProperty ("rawProgram").toString());

            MidiSettings settings;
            settings.fromValueTree (state.getChildWithName ("MIDI"));
            state.removeChild (state.getChildWithName ("MIDI"), nullptr);
            state.removeProperty ("rawProgram", nullptr);
            editorWidth = (int) state.getProperty ("editorWidth", editorWidth.load());
            state.removeProperty ("editorWidth", nullptr);

            apvts.replaceState (state);
            midiSync->applySettings (settings);

            // The saved program's step sequence lives in the raw program; captureCurrent writes it back.
            if (SysEx::isProgramSize (programState.raw.getSize()))
            {
                programState.sequence = Sequence::read (programState.raw);
                ++programState.sequenceVersion;
            }
            // Make the synth play the project's patch, not whatever it had loaded.
            midiSync->restoreSynthFromState();
        }
    }
}

juce::AudioProcessorEditor* TEO5Processor::createEditor()
{
    return new TEO5Editor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TEO5Processor();
}
