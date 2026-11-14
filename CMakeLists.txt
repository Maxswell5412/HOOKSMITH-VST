cmake_minimum_required(VERSION 3.22)

project(Hooksmith VERSION 0.5.1)

# ---------------------------------------------------------------------------
# JUCE: use a local copy if you point JUCE_DIR at one, otherwise download it.
#   cmake -B build -DJUCE_DIR=/path/to/JUCE
# ---------------------------------------------------------------------------
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")

if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Copy the plugin into the system plug-in folders after building (turned off on the build servers)
option(HS_COPY_AFTER_BUILD "Install the plugin after building" ON)

# Windows: link the C++ runtime statically, so friends don't need to install anything extra
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# On a Mac this builds AU + VST3 + a standalone app.
set(HS_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND HS_FORMATS AU)
endif()

juce_add_plugin(Hooksmith
    COMPANY_NAME                "MB Audio"
    BUNDLE_ID                   com.mbaudio.hooksmith
    PLUGIN_MANUFACTURER_CODE    Mbau
    PLUGIN_CODE                 Hksm
    FORMATS                     ${HS_FORMATS}
    PRODUCT_NAME                "HOOKSMITH"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${HS_COPY_AFTER_BUILD}
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    VST3_CATEGORIES             Instrument Generator)

# Fonts (SIL Open Font License, see Resources/Fonts/OFL-*.txt)
juce_add_binary_data(HooksmithFonts SOURCES
    Resources/Fonts/Michroma-Regular.ttf
    Resources/Fonts/Rajdhani-SemiBold.ttf
    Resources/Fonts/Rajdhani-Bold.ttf
    Resources/Fonts/ShareTechMono-Regular.ttf)

target_sources(Hooksmith PRIVATE
    Source/Music.cpp
    Source/Genres.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UIKit.cpp)

target_compile_features(Hooksmith PRIVATE cxx_std_17)

target_compile_definitions(Hooksmith PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1)

target_link_libraries(Hooksmith
    PRIVATE
        HooksmithFonts
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Music engine tests (no JUCE): cmake --build build --target HooksmithEngineTests
add_executable(HooksmithEngineTests EXCLUDE_FROM_ALL Tests/EngineTests.cpp Source/Music.cpp Source/Genres.cpp)
target_compile_features(HooksmithEngineTests PRIVATE cxx_std_17)

# Plugin tests (drives the processor like a DAW): cmake --build build --target HooksmithPluginTests
juce_add_console_app(HooksmithPluginTests PRODUCT_NAME "HooksmithPluginTests")
set_target_properties(HooksmithPluginTests PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(HooksmithPluginTests PRIVATE Tests/PluginTests.cpp)
target_compile_features(HooksmithPluginTests PRIVATE cxx_std_17)
target_compile_definitions(HooksmithPluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)
target_link_libraries(HooksmithPluginTests PRIVATE Hooksmith juce::juce_audio_utils juce::juce_recommended_config_flags)

# A stand-in chord plugin (like Cthulhu) used by the CAPTURE test
juce_add_plugin(FakeChord
    COMPANY_NAME "MB Audio Tests" PLUGIN_MANUFACTURER_CODE Mbat PLUGIN_CODE Fkch
    FORMATS VST3 PRODUCT_NAME "FakeChord" IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)
set_target_properties(FakeChord FakeChord_VST3 PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(FakeChord PRIVATE Tests/FakeChord/FakeChord.cpp)
target_compile_definitions(FakeChord PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(FakeChord PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)

# Stress test (arps, odd block sizes, loops): cmake --build build --target HooksmithStressTests
juce_add_console_app(HooksmithStressTests PRODUCT_NAME "HooksmithStressTests")
set_target_properties(HooksmithStressTests PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(HooksmithStressTests PRIVATE Tests/StressTests.cpp)
target_compile_features(HooksmithStressTests PRIVATE cxx_std_17)
target_compile_definitions(HooksmithStressTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)
target_link_libraries(HooksmithStressTests PRIVATE Hooksmith juce::juce_audio_utils juce::juce_recommended_config_flags)

# Promo video renderer (real screen + real sound): cmake --build build --target HooksmithVideoKit
juce_add_console_app(HooksmithVideoKit PRODUCT_NAME "HooksmithVideoKit")
set_target_properties(HooksmithVideoKit PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(HooksmithVideoKit PRIVATE Tools/VideoKit.cpp)
target_compile_features(HooksmithVideoKit PRIVATE cxx_std_17)
target_compile_definitions(HooksmithVideoKit PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)
target_link_libraries(HooksmithVideoKit PRIVATE Hooksmith juce::juce_audio_utils juce::juce_recommended_config_flags)
