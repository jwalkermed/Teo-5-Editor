// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 The TEO-5 Editor project
// Loads "TEO-5 Editor.vst3" the way a DAW's plug-in scanner does and reports what happens.
// Usage: TEO5PluginCheck [path/to/TEO-5 Editor.vst3]
#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdlib>

int main (int argc, char** argv)
{
    setenv ("TEO5_QUIET", "1", 1); // a load test: the plug-in must not send anything to a connected TEO-5
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String path = argc > 1 ? juce::String (argv[1]) : "/Library/Audio/Plug-Ins/VST3/TEO-5 Editor.vst3";
    std::printf ("Checking %s\n", path.toRawUTF8());

    if (! juce::File (path).isDirectory())
    {
        std::printf ("FAIL: not found\n");
        return 1;
    }

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, path);
    if (found.isEmpty())
    {
        std::printf ("FAIL: the VST3 could not be loaded (macOS blocked it, or the binary doesn't match this Mac)\n");
        return 1;
    }

    for (auto* d : found)
    {
        std::printf ("  found \"%s\" by %s, version %s, category %s\n", d->name.toRawUTF8(),
                     d->manufacturerName.toRawUTF8(), d->version.toRawUTF8(), d->category.toRawUTF8());
        juce::String error;
        auto instance = format.createInstanceFromDescription (*d, 48000.0, 512, error);
        if (instance == nullptr)
        {
            std::printf ("FAIL: could not create the plug-in: %s\n", error.toRawUTF8());
            return 1;
        }
        instance->prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        instance->processBlock (buffer, midi);
        std::unique_ptr<juce::AudioProcessorEditor> editor (instance->createEditorIfNeeded());
        std::printf ("  created, processed audio, editor %s\n", editor != nullptr ? "opened" : "MISSING");
        editor.reset();
        instance->releaseResources();
    }
    std::printf ("OK: the VST3 loads. If your DAW doesn't list it, make the DAW rescan (see README).\n");
    return 0;
}
