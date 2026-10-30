// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// TEO-5 System Exclusive, per "TEO-5 MIDI Implementation" pp. 12-15.
// All builders return complete juce::MidiMessage SysEx messages (F0 ... F7).
namespace teo5::SysEx
{
    // Manufacturer + device bytes that start every TEO-5 SysEx message. The spec prints the Oberheim ID as
    // "0000 0010" (0x02), but a real TEO-5 answers the device inquiry with 0x10 (Oberheim's MIDI manufacturer ID),
    // so 0x10 is the default and the editor adopts whatever the synth reports (see MidiSync::handleSysEx).
    constexpr juce::uint8 defaultManufacturerId = 0x10;
    constexpr juce::uint8 defaultDeviceId = 0x5a; // 0b1011010, "TEO-5 ID (Family LS)"
    juce::uint8 manufacturerId();
    juce::uint8 deviceId();
    void setIds (juce::uint8 manufacturer, juce::uint8 device);

    constexpr juce::uint8 cmdProgramData       = 0x02;
    constexpr juce::uint8 cmdEditBufferData    = 0x03;
    constexpr juce::uint8 cmdRequestProgram    = 0x05;
    constexpr juce::uint8 cmdRequestEditBuffer = 0x06;
    constexpr juce::uint8 cmdRequestGlobals    = 0x0e;
    constexpr juce::uint8 cmdGlobalData        = 0x0f;

    // The spec says 4096 bytes per program; a real TEO-5 sends 4102 (4688 packed bytes). Programs are kept
    // verbatim at whatever size the synth sends, and anything at least this big is accepted.
    constexpr int programSize = 4096;
    inline bool isProgramSize (size_t n) { return n >= (size_t) programSize && n <= (size_t) programSize + 64; }

    // "Packed MS bit" format: each group of up to 7 bytes is preceded by a byte holding their MS bits
    // (bit 0 = first byte's bit 7, ... bit 6 = seventh byte's bit 7).
    juce::MemoryBlock pack (const void* data, size_t size);
    juce::MemoryBlock unpack (const void* data, size_t size);
    inline size_t packedSize (size_t unpacked) { return unpacked / 7 * 8 + (unpacked % 7 ? unpacked % 7 + 1 : 0); }

    juce::MidiMessage deviceInquiry (int deviceChannel = 0x7f);
    juce::MidiMessage requestProgram (int bank, int program);
    juce::MidiMessage requestEditBuffer();
    juce::MidiMessage requestGlobals();
    juce::MidiMessage programDump (int bank, int program, const juce::MemoryBlock& programData);
    juce::MidiMessage editBufferDump (const juce::MemoryBlock& programData);

    struct Parsed
    {
        enum class Type { None, InquiryReply, Program, EditBuffer, Globals } type = Type::None;
        int bank = -1, program = -1;
        juce::MemoryBlock data;       // unpacked program bytes, or global values (one byte each)
        juce::String firmware;        // inquiry reply only, "x.y.z"
        int manufacturer = -1, family = -1; // inquiry reply only
    };

    // Recognises TEO-5 messages; returns type None for anything else.
    Parsed parse (const juce::MidiMessage&);
}
