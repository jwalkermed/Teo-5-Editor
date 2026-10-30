// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <set>
#include "../Midi/MidiSync.h"

namespace teo5
{

// Left-side drawer showing the program's step sequence as five note lanes per step (as the TEO-5 stores it):
// length by dragging along the step numbers, rests, ties, per-note velocity and mute, transpose, and step entry
// from the on-screen keyboard, the DAW track or the TEO-5's keys. Edits are sent to the synth as the edit buffer.
// The playhead follows the DAW's song position (the TEO-5 follows the DAW's MIDI clock).
class SequencerDrawer : public juce::Component,
                        private juce::Timer,
                        private juce::MidiKeyboardState::Listener
{
public:
    // hosted = running as a plug-in (the standalone app has no DAW transport to wait for).
    struct HostPosition { bool playing = false; double ppq = 0.0; double startPpq = 0.0; bool hosted = false; };

    SequencerDrawer (MidiSync&, ProgramState&, juce::AudioProcessorValueTreeState&, std::function<HostPosition()> hostPosition);
    ~SequencerDrawer() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    // ---- Used by the lanes view and tests ----
    Sequence& sequence() { return programState.sequence; }
    const Sequence& sequence() const { return programState.sequence; }
    const std::set<int>& selection() const { return selected; }
    int recordStep() const { return recording ? cursor : -1; }
    int playheadStep() const { return playhead; }
    int flashingStep() const { return flashStep; }
    // audition: play the step's notes while the mouse is held (stopAudition ends it).
    void selectStep (int step, const juce::ModifierKeys&, bool audition = true);
    // Select one note of a step (its lane) and play it while the mouse is held; keys then replace that note.
    void selectNote (int step, int lane);
    int selectedNoteLane() const { return selectedLane; }
    void stopAudition();
    void setLength (int length);
    void enterNote (int note, int velocity);
    void setRecording (bool);
    void restPressed();
    void tiePressed();
    // Marks the sequence changed (refresh, then send to the synth shortly) and records an undo step named `what`.
    // Repeats of the same edit within a moment (e.g. a velocity drag) are one undo step.
    void edited (const juce::String& what = "Edit");

    class LanesView;

private:
    class NoteRow;
    class Look;

    void timerCallback() override;
    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int, int, float) override {}
    void rebuildInspector();
    void updateButtons();
    void transposeBy (int semitones);
    int firstSelected() const { return selected.empty() ? 0 : *selected.begin(); }
    void advanceCursor();
    void showStepMenu();
    bool isSequenceNote (int note, int velocity) const;
    int lastInputNote = -1;
    juce::uint32 lastInputTime = 0;
    std::vector<Sequence::StepData> stepClipboard; // copied steps (right-click menu)
    Sequence onSynth; // the sequence the TEO-5 has (last received or sent), for telling its notes from keys   // right-click on the steps: clear the selected steps or the whole sequence
    void showHelp (bool);  // the ? button: a guide to everything in the sequencer
    juce::String statusText() const;
    void syncFromSynth(); // a new program/edit buffer arrived

    MidiSync& sync;
    ProgramState& programState;
    juce::AudioProcessorValueTreeState& apvts;
    std::function<HostPosition()> hostPosition;
public:
    // The header's bank drop-down and PATCH arrows (left of the program name). Bank ids: 1-16 TEO-5 User banks,
    // 17-32 TEO-5 Factory banks, firstLibraryBankId + n = library bank n (MY BANKS).
    static constexpr int firstLibraryBankId = 100;
    std::function<std::vector<std::pair<int, juce::String>>()> myBanks; // (library bank index, name), MY BANKS only
    std::function<int()> selectedBank;                                  // the bank id to show as chosen
    std::function<void (int bankId)> onBankChosen;
    std::function<void (int delta)> onPatchStep;
    std::function<juce::String()> locationText;                         // e.g. "User 1-01" or "My Programs 03"
private:
    juce::String headerText() const;
    void layoutProgramArrows();
    void refreshBankBox();
    juce::String shownHeader;
    float headerTextWidth = 0.0f;
    juce::ComboBox bankBox;
    std::vector<std::pair<int, juce::String>> shownMyBanks;
    juce::TextButton patchPrev { juce::String::fromUTF8 ("\xe2\x97\x80") }, patchNext { juce::String::fromUTF8 ("\xe2\x96\xb6") };

    std::unique_ptr<Look> look;
    std::unique_ptr<LanesView> lanes;
    juce::TextButton lenMinus { juce::String::fromUTF8 ("\xe2\x88\x92") }, lenPlus { "+" }, playButton { "PLAY" }, recButton { "REC" };
    juce::TextButton modePlay { "PLAY" }, modeRest { "REST" }, modeTie { "TIE" };
    juce::OwnedArray<NoteRow> noteRows;
    juce::TextButton trOctDown { juce::String::fromUTF8 ("\xe2\x88\x92OCT") }, trDown { juce::String::fromUTF8 ("\xe2\x88\x92" "1") },
                     trUp { "+1" }, trOctUp { "+OCT" }, scopeAll { "ALL STEPS" }, scopeSel { "SELECTED" };
    juce::TextButton octDown { juce::String::fromUTF8 ("OCT \xe2\x88\x92") }, octUp { "OCT +" }, chordButton { "CHORD" },
                     restButton { "REST" }, tieButton { "TIE" }, backButton { juce::String::fromUTF8 ("\xe2\x97\x80 BACK") },
                     nextButton { juce::String::fromUTF8 ("NEXT \xe2\x96\xb6") };
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };

    std::set<int> selected { 0 };
    int anchor = 0, cursor = 0, playhead = -1, flashStep = -1, flashTicks = 0;
    bool waitingForHost = false; // PLAY is on and the DAW is stopped
    juce::TextButton helpButton { "?" };
    struct HelpPanel : juce::Component
    {
        HelpPanel();
        void paint (juce::Graphics&) override;
        void resized() override;
        juce::TextEditor text;
        juce::TextButton close { "Close" };
    } helpPanel;
    bool sequenceRunning = false;
    bool sendPending = false;    // an edit waiting to go to the synth (at the end of the pass, while playing)
    int lastSeenPlayhead = -1;
    int selectedLane = -1;          // a single selected note (lane) in the selected step, or -1
    int lastEnteredStep = 0;
    std::vector<int> soundingNotes; // notes held on while the mouse is down on a step or note

    // Undo/redo of sequence edits within the current program (a new program starts over).
    Sequence stable;
    std::vector<std::pair<Sequence, juce::String>> undoStack, redoStack;
    juce::String lastEditWhat;
    juce::uint32 lastEditTime = 0;
    juce::String historyMessage;
    juce::uint32 historyMessageUntil = 0;
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" }, resetButton { "Reset" }, readButton { "From TEO-5" };
    void readFromSynth (bool quiet); // fetch the TEO-5's edit buffer; the sequence it brings is one undo step
    bool readPending = false, readQuiet = false;
    Sequence beforeRead;
    bool hostedCached = false;
    bool clockWarning = false;
    bool synthWasRecording = false;
    int seenLiveVersion = 0;
    void undoRedo (bool isUndo);
    void resetHistory(); // the TEO-5's sequencer is playing (notes from it aren't entered while recording)
    int chordStep = -1;     // CHORD: the step the current chord is going into
    bool recording = false, chord = false, transposeSelected = false;
    int transposed = 0, keyboardBase = 48;
    int seenVersion = -1;
    int sendTicks = 0;      // debounce for sending edits
    int pollTicks = 0;      // polling the synth while it records
    juce::String synthRecordStatus;
    Sequence lastSeen; // to spot the step the TEO-5 just recorded

    juce::Rectangle<int> headerArea, statusArea, inspectorBox, transposeBox, keyboardBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequencerDrawer)
};

} // namespace teo5
