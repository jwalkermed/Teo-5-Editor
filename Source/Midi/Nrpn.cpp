#include "Nrpn.h"

namespace teo5
{

juce::Array<juce::MidiMessage> Nrpn::encode (int channel, int parameter, int value)
{
    parameter = juce::jlimit (0, 16383, parameter);
    value = juce::jlimit (0, 16383, value);
    return {
        juce::MidiMessage::controllerEvent (channel, 99, (parameter >> 7) & 0x7f),
        juce::MidiMessage::controllerEvent (channel, 98, parameter & 0x7f),
        juce::MidiMessage::controllerEvent (channel, 6, (value >> 7) & 0x7f),
        juce::MidiMessage::controllerEvent (channel, 38, value & 0x7f)
    };
}

void NrpnDecoder::reset()
{
    for (auto& s : state)
        s = {};
}

bool NrpnDecoder::process (const juce::MidiMessage& m)
{
    if (! m.isController())
        return false;

    const int ch = m.getChannel();
    auto& s = state[ch - 1];
    const int cc = m.getControllerNumber();
    const int v = m.getControllerValue();

    auto emit = [&] (int value)
    {
        s.lastValue = value;
        if (onNrpn)
            onNrpn (ch, s.parameter(), value);
    };

    switch (cc)
    {
        case 99: s.paramMsb = v; s.valueMsbSeen = false; s.rpnMsb = s.rpnLsb = -1; return true;
        case 98: s.paramLsb = v; s.valueMsbSeen = false; s.rpnMsb = s.rpnLsb = -1; return true;

        case 101:
        case 100:
            (cc == 101 ? s.rpnMsb : s.rpnLsb) = v;
            if (s.rpnMsb == 127 && s.rpnLsb == 127) // RPN null: deselect the NRPN
            {
                s.paramMsb = s.paramLsb = -1;
                s.valueMsbSeen = false;
            }
            return true;

        case 6:
            if (! s.nrpnSelected())
                return false;
            s.valueMsb = v;
            s.valueMsbSeen = true;
            return true; // wait for CC 38

        case 38:
            if (! s.nrpnSelected())
                return false;
            // The synth always sends MSB then LSB; if only LSB arrives, reuse the last MSB.
            emit (((s.valueMsbSeen ? s.valueMsb : (s.lastValue >> 7)) << 7) | v);
            s.valueMsbSeen = false;
            return true;

        case 96:
        case 97:
            if (! s.nrpnSelected())
                return false;
            if (onNrpnDelta)
                onNrpnDelta (ch, s.parameter(), cc == 96 ? 1 : -1);
            return true;

        default:
            return false;
    }
}

} // namespace teo5
