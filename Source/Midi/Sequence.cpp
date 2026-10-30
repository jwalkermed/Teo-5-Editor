#include "Sequence.h"

namespace teo5
{

static bool getBit (const juce::uint8* d, int base, int lane, int step)
{
    return ((d[base + lane * 8 + step / 8] >> (step % 8)) & 1) != 0;
}

static void setBit (juce::uint8* d, int base, int lane, int step, bool on)
{
    auto& b = d[base + lane * 8 + step / 8];
    const auto mask = (juce::uint8) (1 << (step % 8));
    b = on ? (juce::uint8) (b | mask) : (juce::uint8) (b & ~mask);
}

Sequence Sequence::read (const juce::MemoryBlock& program)
{
    Sequence s;
    if (program.getSize() <= (size_t) velocityOffset + numLanes * numSteps)
        return s;
    auto* d = static_cast<const juce::uint8*> (program.getData());
    s.length = juce::jlimit (1, numSteps, (int) d[lengthOffset]);
    for (int l = 0; l < numLanes; ++l)
        for (int st = 0; st < numSteps; ++st)
        {
            auto& slot = s.at (l, st);
            // Kept verbatim: unused slots often hold leftover values (velocity 0, values above 127).
            slot.note = d[noteOffset + l * numSteps + st];
            slot.velocity = d[velocityOffset + l * numSteps + st];
            slot.rest = getBit (d, restOffset, l, st);
            slot.tieToNext = getBit (d, tieOffset, l, st);
        }
    return s;
}

void Sequence::write (juce::MemoryBlock& program) const
{
    if (program.getSize() <= (size_t) velocityOffset + numLanes * numSteps)
        return;
    auto* d = static_cast<juce::uint8*> (program.getData());
    d[lengthOffset] = (juce::uint8) juce::jlimit (1, numSteps, length);
    for (int l = 0; l < numLanes; ++l)
        for (int st = 0; st < numSteps; ++st)
        {
            const auto& slot = at (l, st);
            d[noteOffset + l * numSteps + st] = (juce::uint8) juce::jlimit (0, 255, slot.note);
            d[velocityOffset + l * numSteps + st] = (juce::uint8) juce::jlimit (0, 255, slot.velocity);
            setBit (d, restOffset, l, st, slot.rest);
            setBit (d, tieOffset, l, st, slot.tieToNext);
        }
}

bool Sequence::isMuted (int lane, int step) const
{
    const auto& s = at (lane, step);
    const bool realNote = s.note <= 127 && s.velocity >= 1 && s.velocity <= 127; // not leftover data
    return s.rest && ! isHeld (lane, step) && realNote && (s.note != emptyNote || s.velocity != emptyVelocity);
}

int Sequence::heldNote (int lane, int step) const
{
    if (! isHeld (lane, step))
        return -1;
    int st = step - 1;
    while (st > 0 && at (lane, st).rest && at (lane, st - 1).tieToNext)
        --st;
    return at (lane, st).note;
}

Sequence::StepMode Sequence::modeOf (int step) const
{
    bool anySounds = false;
    for (int l = 0; l < numLanes; ++l)
    {
        if (isHeld (l, step))
            return StepMode::Tie;
        anySounds = anySounds || sounds (l, step);
    }
    return anySounds ? StepMode::Play : StepMode::Rest;
}

int Sequence::addNote (int step, int note, int velocity, bool replace)
{
    if (replace)
        for (int l = 0; l < numLanes; ++l)
            removeNote (l, step);

    // Already there: just make sure it sounds.
    for (int l = 0; l < numLanes; ++l)
        if (sounds (l, step) && at (l, step).note == note)
            return l;

    for (int l = 0; l < numLanes; ++l)
        if (! sounds (l, step) && ! isMuted (l, step))
        {
            if (step > 0)
                at (l, step - 1).tieToNext = false; // a new note isn't held from before
            auto& s = at (l, step);
            s = { juce::jlimit (0, 127, note), juce::jlimit (1, 127, velocity), false, false };
            return l;
        }
    return -1; // five notes already
}

void Sequence::removeNote (int lane, int step)
{
    if (step > 0)
        at (lane, step - 1).tieToNext = false;
    at (lane, step) = {};
}

int Sequence::heldFrom (int lane, int step) const
{
    if (! isHeld (lane, step))
        return -1;
    int st = step - 1;
    while (st > 0 && isHeld (lane, st))
        --st;
    return sounds (lane, st) ? st : -1;
}

Sequence::StepData Sequence::copyStep (int step) const
{
    StepData d {};
    for (int l = 0; l < numLanes; ++l)
    {
        d[(size_t) l] = at (l, step);
        if (const int from = heldFrom (l, step); from >= 0)
        {
            d[(size_t) l].note = at (l, from).note;
            d[(size_t) l].velocity = at (l, from).velocity;
            d[(size_t) l].rest = false;
        }
    }
    return d;
}

void Sequence::pasteStep (int step, const StepData& d)
{
    for (int l = 0; l < numLanes; ++l)
    {
        if (step > 0)
            at (l, step - 1).tieToNext = false;
        at (l, step) = d[(size_t) l];
    }
}

void Sequence::clearStep (int step)
{
    for (int l = 0; l < numLanes; ++l)
        removeNote (l, step);
}

void Sequence::clearAll()
{
    for (int st = 0; st < numSteps; ++st)
        clearStep (st);
}

void Sequence::setMuted (int lane, int step, bool muted)
{
    auto& s = at (lane, step);
    if (muted && s.note == emptyNote && s.velocity == emptyVelocity)
        s.velocity = emptyVelocity - 1; // keep a muted middle C distinguishable from an empty slot
    s.rest = muted;
    if (muted)
        s.tieToNext = false;
}

void Sequence::setStepMode (int step, StepMode mode)
{
    switch (mode)
    {
        case StepMode::Rest:
            for (int l = 0; l < numLanes; ++l)
            {
                if (step > 0)
                    at (l, step - 1).tieToNext = false;
                if (sounds (l, step))
                    setMuted (l, step, true); // keeps the notes so PLAY can bring them back
            }
            break;

        case StepMode::Tie:
            if (step == 0)
                break;
            for (int l = 0; l < numLanes; ++l)
            {
                const bool prevSounding = sounds (l, step - 1) || isHeld (l, step - 1);
                if (prevSounding)
                {
                    at (l, step - 1).tieToNext = true;
                    at (l, step).rest = true;
                }
                else if (sounds (l, step))
                {
                    setMuted (l, step, true);
                }
            }
            break;

        case StepMode::Play:
            for (int l = 0; l < numLanes; ++l)
            {
                if (step > 0)
                    at (l, step - 1).tieToNext = false;
                if (isMuted (l, step))
                    at (l, step).rest = false;
            }
            break;
    }
}

void Sequence::transpose (int fromStep, int toStep, int semitones)
{
    for (int st = juce::jmax (0, fromStep); st <= juce::jmin (numSteps - 1, toStep); ++st)
        for (int l = 0; l < numLanes; ++l)
            if ((sounds (l, st) || isMuted (l, st)) && at (l, st).note <= 127)
            {
                auto& s = at (l, st);
                s.note = juce::jlimit (0, 127, s.note + semitones);
            }
}

} // namespace teo5
