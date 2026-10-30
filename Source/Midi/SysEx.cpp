#include "SysEx.h"

namespace teo5::SysEx
{

static std::atomic<int> currentManufacturer { defaultManufacturerId }, currentDevice { defaultDeviceId };

juce::uint8 manufacturerId() { return (juce::uint8) currentManufacturer.load(); }
juce::uint8 deviceId() { return (juce::uint8) currentDevice.load(); }

void setIds (juce::uint8 manufacturer, juce::uint8 device)
{
    currentManufacturer = manufacturer & 0x7f;
    currentDevice = device & 0x7f;
}

juce::MemoryBlock pack (const void* data, size_t size)
{
    auto* in = static_cast<const juce::uint8*> (data);
    juce::MemoryBlock out;

    for (size_t i = 0; i < size; i += 7)
    {
        const size_t n = juce::jmin ((size_t) 7, size - i);
        juce::uint8 msbs = 0;
        for (size_t j = 0; j < n; ++j)
            msbs |= (juce::uint8) (((in[i + j] >> 7) & 1) << j);
        out.append (&msbs, 1);
        for (size_t j = 0; j < n; ++j)
        {
            const juce::uint8 b = in[i + j] & 0x7f;
            out.append (&b, 1);
        }
    }
    return out;
}

juce::MemoryBlock unpack (const void* data, size_t size)
{
    auto* in = static_cast<const juce::uint8*> (data);
    juce::MemoryBlock out;

    for (size_t i = 0; i < size; i += 8)
    {
        const juce::uint8 msbs = in[i];
        const size_t n = juce::jmin ((size_t) 7, size - i - 1);
        for (size_t j = 0; j < n; ++j)
        {
            const juce::uint8 b = (juce::uint8) ((in[i + 1 + j] & 0x7f) | (((msbs >> j) & 1) << 7));
            out.append (&b, 1);
        }
    }
    return out;
}

namespace
{
juce::MidiMessage make (std::initializer_list<juce::uint8> header, const juce::MemoryBlock* payload = nullptr)
{
    juce::MemoryBlock body;
    for (auto b : header)
        body.append (&b, 1);
    if (payload != nullptr)
        body.append (payload->getData(), payload->getSize());
    return juce::MidiMessage::createSysExMessage (body.getData(), (int) body.getSize());
}
} // namespace

juce::MidiMessage deviceInquiry (int deviceChannel)
{
    return make ({ 0x7e, (juce::uint8) (deviceChannel & 0x7f), 0x06, 0x01 });
}

juce::MidiMessage requestProgram (int bank, int program)
{
    return make ({ manufacturerId(), deviceId(), cmdRequestProgram, (juce::uint8) (bank & 0x7f), (juce::uint8) (program & 0x7f) });
}

juce::MidiMessage requestEditBuffer()
{
    return make ({ manufacturerId(), deviceId(), cmdRequestEditBuffer });
}

juce::MidiMessage requestGlobals()
{
    return make ({ manufacturerId(), deviceId(), cmdRequestGlobals });
}

juce::MidiMessage programDump (int bank, int program, const juce::MemoryBlock& programData)
{
    auto packed = pack (programData.getData(), programData.getSize());
    return make ({ manufacturerId(), deviceId(), cmdProgramData, (juce::uint8) (bank & 0x7f), (juce::uint8) (program & 0x7f) }, &packed);
}

juce::MidiMessage editBufferDump (const juce::MemoryBlock& programData)
{
    auto packed = pack (programData.getData(), programData.getSize());
    return make ({ manufacturerId(), deviceId(), cmdEditBufferData }, &packed);
}

Parsed parse (const juce::MidiMessage& m)
{
    Parsed r;
    if (! m.isSysEx())
        return r;

    auto* d = m.getSysExData(); // excludes F0 / F7
    const int n = m.getSysExDataSize();

    // Universal device inquiry reply: 7E cc 06 02 <manufacturer> <family LS> <family MS> <member LS> <member MS> v v v
    if (n >= 12 && d[0] == 0x7e && d[2] == 0x06 && d[3] == 0x02)
    {
        r.type = Parsed::Type::InquiryReply;
        r.manufacturer = d[4];
        r.family = d[5];
        r.firmware = juce::String (d[9]) + "." + juce::String (d[10]) + "." + juce::String (d[11]);
        return r;
    }

    // Accept the documented 0x02 as well as the configured manufacturer ID.
    if (n < 3 || (d[0] != manufacturerId() && d[0] != 0x02) || d[1] != deviceId())
        return r;

    switch (d[2])
    {
        case cmdProgramData:
            if (n < 5) break;
            r.type = Parsed::Type::Program;
            r.bank = d[3];
            r.program = d[4];
            r.data = unpack (d + 5, (size_t) (n - 5));
            break;

        case cmdEditBufferData:
            r.type = Parsed::Type::EditBuffer;
            r.data = unpack (d + 3, (size_t) (n - 3));
            break;

        case cmdGlobalData:
            // The spec says 50 nibbles; a real TEO-5 sends 32 bytes in packed-MS-bit format (28 values).
            // Unpacked byte i is the value of global NRPN 4096 + i.
            r.type = Parsed::Type::Globals;
            r.data = unpack (d + 3, (size_t) (n - 3));
            break;

        default:
            break;
    }
    return r;
}

} // namespace teo5::SysEx
