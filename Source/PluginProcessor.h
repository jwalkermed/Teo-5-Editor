// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Params/ParamTable.h"
#include "Midi/MidiSync.h"
#include "Library/LibraryModel.h"
#include "Library/HardwareTransfer.h"

class TEO5Processor : public juce::AudioProcessor
{
public:
    TEO5Processor();
    ~TEO5Processor() override;

    void prepareToPlay (double, int) override { startTalking(); }
    // Lets the plug-in talk to the synth (see the constructor); called when the host runs it or opens the editor.
    void startTalking();
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "TEO-5 Editor"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // Integer access in the TEO-5's native units (as in the NRPN table).
    int getNative (const juce::String& id) const;
    void setNative (const juce::String& id, int value);

    teo5::ProgramState& getProgramState() { return programState; }
    teo5::MidiSync& getMidiSync() { return *midiSync; }

    // DAW transport, written on the audio thread.
    std::atomic<bool> hostPlaying { false };
    std::atomic<double> hostPpq { 0.0 };
    std::atomic<double> hostBpm { 0.0 }; // the DAW's tempo (0 = unknown); its MIDI clock drives the TEO-5
    // Song position where the TEO-5's sequence last started at step 1 (the playhead counts from here).
    std::atomic<double> seqStartPpq { 0.0 };
    // The editor's width as the user last sized it (0 = default), kept while the window is closed and in the project.
    std::atomic<int> editorWidth { 0 };
    teo5::LibraryModel& getLibrary() { return *library; }
    teo5::HardwareTransfer& getTransfer() { return *transfer; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;
    teo5::ProgramState programState;
    std::unique_ptr<teo5::MidiSync> midiSync;
    juce::SharedResourcePointer<teo5::LibraryModel> library;
    bool quiet = false; // TEO5_QUIET: never talk to the synth // one library shared by all instances in the process
    std::unique_ptr<teo5::HardwareTransfer> transfer;
    std::atomic<float>* seqPlayValue = nullptr;
    bool wasPlaying = false, wasSeqPlay = false; // audio thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TEO5Processor)
};
