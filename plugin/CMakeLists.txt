cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(MelodyLift VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static MSVC runtime: the .vst3 works without installing the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(MELODYLIFT_TESTS "Build the core unit tests" ON)
option(MELODYLIFT_PLUGIN_TESTS "Build the end-to-end processor/editor tests" OFF)

include(FetchContent)
# Point FETCHCONTENT_SOURCE_DIR_JUCE at a local checkout to build offline.
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(MELODYLIFT_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MELODYLIFT_FORMATS AU)
endif()

juce_add_plugin(MelodyLift
    COMPANY_NAME "MelodyLift"
    COMPANY_WEBSITE "https://github.com/beerjaspaldhaliwal-ctrl/vocal.melody-"
    BUNDLE_ID com.melodylift.melodylift
    PLUGIN_MANUFACTURER_CODE Mlft
    PLUGIN_CODE Mlf1
    FORMATS ${MELODYLIFT_FORMATS}
    PRODUCT_NAME "MelodyLift"
    DESCRIPTION "Vocal to MIDI melody extractor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Analyzer
    AU_MAIN_TYPE kAudioUnitType_Effect)

# Same code as an instrument, so it can sit in FL Studio's Channel Rack like
# other MIDI generators and play the pattern with the song.
juce_add_plugin(MelodyLiftGen
    COMPANY_NAME "MelodyLift"
    COMPANY_WEBSITE "https://github.com/beerjaspaldhaliwal-ctrl/vocal.melody-"
    BUNDLE_ID com.melodylift.melodyliftgen
    PLUGIN_MANUFACTURER_CODE Mlft
    PLUGIN_CODE Mlf2
    FORMATS ${MELODYLIFT_FORMATS}
    PRODUCT_NAME "MelodyLift Generator"
    DESCRIPTION "Pop melody, chord and bassline generator"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Generator
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

set(MELODYLIFT_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PianoRoll.cpp
    Source/core/MelodyCore.cpp
    Source/core/Generator.cpp)

foreach(target MelodyLift MelodyLiftGen)
    target_sources(${target} PRIVATE ${MELODYLIFT_SOURCES})
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_formats
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags)
endforeach()

if(MELODYLIFT_TESTS)
    enable_testing()
    add_executable(MelodyLiftCoreTests tests/CoreTests.cpp Source/core/MelodyCore.cpp Source/core/Generator.cpp)
    add_test(NAME core COMMAND MelodyLiftCoreTests)
endif()

if(MELODYLIFT_PLUGIN_TESTS)
    # Drives the real processor + editor: capture during simulated playback,
    # MIDI out, file export, state restore and an editor screenshot.
    juce_add_console_app(MelodyLiftPluginTests PRODUCT_NAME "MelodyLiftPluginTests")
    target_sources(MelodyLiftPluginTests PRIVATE
        tests/PluginTests.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PianoRoll.cpp
        Source/core/MelodyCore.cpp
        Source/core/Generator.cpp)
    target_compile_definitions(MelodyLiftPluginTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(MelodyLiftPluginTests PRIVATE
        juce::juce_audio_utils juce::juce_audio_formats juce::juce_recommended_config_flags)
    add_test(NAME plugin COMMAND MelodyLiftPluginTests)
endif()
