// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace teo5
{

// NRPN framing per the TEO-5 MIDI Implementation:
//   CC 99 param MSB, CC 98 param LSB, CC 6 value MSB, CC 38 value LSB (7 bits each).
//   CC 96/97 data increment/decrement, CC 101/100 = 127/127 resets the selected NRPN.
namespace Nrpn
{
    // The four CC messages for one NRPN (channel is 1-16).
    juce::Array<juce::MidiMessage> encode (int channel, int parameter, int value);
}

// Stateful decoder. Feed it every CC from one source; it calls onNrpn when a complete
// parameter/value pair is known. Tracks state per channel so interleaved channels don't corrupt.
class NrpnDecoder
{
public:
    std::function<void (int channel, int parameter, int value)> onNrpn;
    // Data increment/decrement (CC 96/97) on the selected NRPN; delta is +1 or -1.
    std::function<void (int channel, int parameter, int delta)> onNrpnDelta;

    // Returns true if the CC was part of NRPN/RPN framing (and therefore consumed).
    bool process (const juce::MidiMessage& message);
    void reset();

private:
    struct ChannelState
    {
        int paramMsb = -1, paramLsb = -1;
        int valueMsb = 0;
        bool valueMsbSeen = false;
        int lastValue = 0;
        int rpnMsb = -1, rpnLsb = -1;
        bool nrpnSelected() const { return paramMsb >= 0 && paramLsb >= 0; }
        int parameter() const { return (paramMsb << 7) | paramLsb; }
    };

    ChannelState state[16];
};

} // namespace teo5
