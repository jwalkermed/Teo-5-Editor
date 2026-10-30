#pragma once

#include <juce_core/juce_core.h>
#include <array>

namespace teo5
{

// The TEO-5 step sequence stored in every program. Measured on a TEO-5 (496 programs compared):
//   byte 189             length, 1-64 steps
//   bytes 190-229        rest flags: 5 lanes x 64 bits (lane-major, bit = step % 8 of byte step / 8); 1 = no note
//   bytes 230-269        tie flags, same layout; 1 = this lane's note holds into the next step
//   bytes 270-589        pitches, 5 lanes x 64 steps (MIDI note numbers)
//   bytes 590-909        velocities, same layout
// A step can sound up to five notes, one per lane. A held (tied) step is stored as a rest with the tie flag
// set on the step before it. Empty slots keep pitch 60 and velocity 100. Bytes are kept exactly as read
// (unused slots on real programs often hold leftover values); only edited values are limited to MIDI ranges.
struct Sequence
{
    static constexpr int numSteps = 64, numLanes = 5;
    static constexpr int lengthOffset = 189, restOffset = 190, tieOffset = 230, noteOffset = 270, velocityOffset = 590;
    static constexpr int emptyNote = 60, emptyVelocity = 100;

    struct Slot
    {
        int note = emptyNote, velocity = emptyVelocity;
        bool rest = true;       // no new note in this step (it may still be held from the step before)
        bool tieToNext = false; // the note sounding here carries on into the next step
        bool operator== (const Slot& o) const { return note == o.note && velocity == o.velocity && rest == o.rest && tieToNext == o.tieToNext; }
    };

    std::array<std::array<Slot, numSteps>, numLanes> lanes {};
    int length = 16;
    bool operator== (const Sequence& o) const { return length == o.length && lanes == o.lanes; }
    bool operator!= (const Sequence& o) const { return ! (*this == o); }

    static Sequence read (const juce::MemoryBlock& program);
    void write (juce::MemoryBlock& program) const; // only the sequence bytes are changed

    Slot& at (int lane, int step) { return lanes[(size_t) lane][(size_t) step]; }
    const Slot& at (int lane, int step) const { return lanes[(size_t) lane][(size_t) step]; }

    // ---- How a slot reads on screen ----
    bool sounds (int lane, int step) const { return ! at (lane, step).rest; }
    bool isHeld (int lane, int step) const { return step > 0 && at (lane, step - 1).tieToNext; }
    // Rest flag set but a real note kept (a muted note, or a step switched to REST that can be switched back).
    bool isMuted (int lane, int step) const;
    bool isEmpty (int lane, int step) const { return ! sounds (lane, step) && ! isHeld (lane, step) && ! isMuted (lane, step); }
    // The pitch a held slot is sustaining (follows the ties back to the note that started it), or -1.
    int heldNote (int lane, int step) const;
    // The step whose note a held slot is sustaining (follows the ties back), or -1.
    int heldFrom (int lane, int step) const;

    enum class StepMode { Play, Rest, Tie };
    StepMode modeOf (int step) const; // Tie if any lane is held; Rest if nothing sounds; else Play

    // ---- Edits (each keeps the stored bytes consistent with the hardware's format) ----
    // Puts one note in the step (first free lane; replaces the step's notes when `replace`). Returns the lane or -1.
    int addNote (int step, int note, int velocity, bool replace);
    void removeNote (int lane, int step);
    void setMuted (int lane, int step, bool muted);
    void setStepMode (int step, StepMode);
    void transpose (int fromStep, int toStep, int semitones); // inclusive range; returns nothing, clamps to 0-127
    void clearStep (int step);   // no notes in the step (and nothing held into it)
    // One step's five slots, as copied. A note held into the step from the one before is copied as a note of its
    // own (its pitch and velocity), so it still sounds wherever it's pasted.
    using StepData = std::array<Slot, numLanes>;
    StepData copyStep (int step) const;
    // Replaces the step with copied data (cutting any tie from the step before into it).
    void pasteStep (int step, const StepData&);
    void clearAll();             // every step empty; the length is kept
};

} // namespace teo5
