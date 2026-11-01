# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 The TEO-5 Editor project
cmake_minimum_required(VERSION 3.22)

# Must precede project(): universal binary (Apple Silicon + Intel), macOS 11+.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")

project(TEO5Editor VERSION 1.0.15 LANGUAGES C CXX)
add_compile_definitions(TEO5_VERSION="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: either point JUCE_PATH at a local checkout (fast, offline) or let CMake fetch JUCE 8.
set(JUCE_PATH "" CACHE PATH "Path to a local JUCE 8 checkout (optional)")
if(JUCE_PATH)
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Optional: an external VST3 SDK. JUCE 8 bundles the VST3 SDK headers, so this is normally unnecessary.
set(TEO5_VST3_SDK "" CACHE PATH "Optional external VST3 SDK path")
if(TEO5_VST3_SDK)
    juce_set_vst3_sdk_path(${TEO5_VST3_SDK})
endif()

juce_add_binary_data(TEO5Assets SOURCES
    Resources/Fonts/Michroma-Regular.ttf
    Resources/BasicProgram.syx)  # the TEO-5's Basic Program edit buffer, captured from the synth (Initialize preset)

# Copy the built plugins into the system plug-in folders after every build.
option(TEO5_INSTALL_AFTER_BUILD "Copy VST3/AU into the plug-in folders after building" ON)
if(APPLE)
    set(TEO5_VST3_INSTALL_DIR "/Library/Audio/Plug-Ins/VST3" CACHE PATH "VST3 install folder")
    set(TEO5_AU_INSTALL_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/Components" CACHE PATH "AU install folder")
else()
    set(TEO5_VST3_INSTALL_DIR "$ENV{CommonProgramFiles}/VST3" CACHE PATH "VST3 install folder")
    set(TEO5_AU_INSTALL_DIR "" CACHE PATH "AU install folder (unused)")
endif()

set(TEO5_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND TEO5_FORMATS AU)
endif()

juce_add_plugin(TEO5Editor
    # The name hosts show. Before 1.0 it was "TEO5Editor"; the codes below are unchanged, so saved projects
    # still find the plug-in. (The CMake target and the data folders keep the old spelling.)
    COMPANY_NAME "TEO-5 Editor"
    BUNDLE_ID "local.teo5editor.plugin"
    PLUGIN_MANUFACTURER_CODE Teo5
    PLUGIN_CODE Te5e
    FORMATS ${TEO5_FORMATS}
    PRODUCT_NAME "TEO-5 Editor"
    IS_SYNTH TRUE                 # silent instrument: most hosts route MIDI to instruments, not MIDI effects
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    VST3_CATEGORIES Instrument Tools
    COPY_PLUGIN_AFTER_BUILD ${TEO5_INSTALL_AFTER_BUILD}
    VST3_COPY_DIR "${TEO5_VST3_INSTALL_DIR}"
    AU_COPY_DIR "${TEO5_AU_INSTALL_DIR}")

set(TEO5_MIDI_SOURCES
    Source/Midi/Nrpn.cpp
    Source/Midi/SysEx.cpp
    Source/Midi/ProgramLayout.cpp
    Source/Midi/Sequence.cpp)

set(TEO5_CORE_SOURCES
    Source/Params/ParamTable.cpp
    ${TEO5_MIDI_SOURCES}
    Source/Midi/MidiPortManager.cpp
    Source/Midi/MidiSync.cpp
    Source/UI/MidiSettingsDrawer.cpp
    Source/UI/SettingsDrawer.cpp
    Source/Library/LibraryModel.cpp
    Source/Library/SyxIO.cpp
    Source/Library/HardwareTransfer.cpp
    Source/UI/LibraryDrawer.cpp
    Source/UI/ModMatrixDrawer.cpp
    Source/UI/SequencerDrawer.cpp
    Source/UI/TEO5LookAndFeel.cpp
    Source/UI/Widgets.cpp
    Source/UI/PanelComponent.cpp
    Source/UI/PanelGuide.cpp)

target_sources(TEO5Editor PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ${TEO5_CORE_SOURCES})

target_include_directories(TEO5Editor PRIVATE Source)

target_compile_definitions(TEO5Editor PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TEO5Editor
    PRIVATE
        TEO5Assets
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Panel snapshot tool: renders the editor to a PNG without a host (used for visual checks) ----
juce_add_console_app(TEO5Snapshot PRODUCT_NAME "TEO5Snapshot")
target_sources(TEO5Snapshot PRIVATE
    Source/Tools/SnapshotMain.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ${TEO5_CORE_SOURCES})
target_include_directories(TEO5Snapshot PRIVATE Source)
target_compile_definitions(TEO5Snapshot PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JucePlugin_Build_Standalone=0 TEO5_SNAPSHOT=1 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(TEO5Snapshot PRIVATE
    TEO5Assets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_extra
    juce::juce_recommended_config_flags)

# ---- MIDI probe: talks to the TEO-5 directly for hardware experiments (not shipped) ----
juce_add_console_app(TEO5MidiProbe PRODUCT_NAME "TEO5MidiProbe")
target_sources(TEO5MidiProbe PRIVATE Source/Tools/MidiProbeMain.cpp)
target_compile_definitions(TEO5MidiProbe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(TEO5MidiProbe PRIVATE juce::juce_audio_devices juce::juce_recommended_config_flags)

# ---- Plug-in check: loads the VST3 like a DAW scanner (shipped in the zip for troubleshooting) ----
juce_add_console_app(TEO5PluginCheck PRODUCT_NAME "TEO5PluginCheck")
target_sources(TEO5PluginCheck PRIVATE Source/Tools/PluginCheckMain.cpp)
target_compile_definitions(TEO5PluginCheck PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(TEO5PluginCheck PRIVATE
    juce::juce_audio_processors juce::juce_audio_utils
    juce::juce_recommended_config_flags)

# ---- Unit tests: SysEx pack/unpack, NRPN encode/decode, layout inference ----
juce_add_console_app(TEO5Tests PRODUCT_NAME "TEO5Tests")
target_sources(TEO5Tests PRIVATE
    Tests/TestMain.cpp
    ${TEO5_MIDI_SOURCES}
    Source/Params/ParamTable.cpp
    Source/Midi/MidiPortManager.cpp
    Source/Midi/MidiSync.cpp
    Source/Library/LibraryModel.cpp
    Source/Library/SyxIO.cpp)
target_include_directories(TEO5Tests PRIVATE Source)
target_compile_definitions(TEO5Tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(TEO5Tests PRIVATE
    TEO5Assets juce::juce_audio_processors juce::juce_audio_devices juce::juce_data_structures
    juce::juce_recommended_config_flags)

# Loopback test against a fake TEO-5 on virtual MIDI ports (macOS/Linux).
if(NOT WIN32)
    juce_add_console_app(TEO5LoopbackTest PRODUCT_NAME "TEO5LoopbackTest")
    target_sources(TEO5LoopbackTest PRIVATE
        Tests/LoopbackTest.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        ${TEO5_CORE_SOURCES})
    target_include_directories(TEO5LoopbackTest PRIVATE Source)
    target_compile_definitions(TEO5LoopbackTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Build_Standalone=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(TEO5LoopbackTest PRIVATE
        TEO5Assets juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_extra
        juce::juce_recommended_config_flags)
endif()

enable_testing()
add_test(NAME TEO5Tests COMMAND TEO5Tests)
