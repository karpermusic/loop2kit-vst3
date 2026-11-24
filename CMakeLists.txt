cmake_minimum_required(VERSION 3.22)

# Universal Binary en macOS (Apple Silicon arm64 + Intel x86_64)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS Universal Architectures" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS Deployment Target" FORCE)
endif()

project(Loop2KitVST VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(LOOP2KIT_FORMATS VST3 AU Standalone)
else()
    set(LOOP2KIT_FORMATS VST3 Standalone)
endif()

juce_add_plugin(Loop2KitVST
    COMPANY_NAME "Loop2KitAudio"
    PLUGIN_MANUFACTURER_CODE L2ka
    PLUGIN_CODE L2kt
    FORMATS ${LOOP2KIT_FORMATS}
    PRODUCT_NAME "Loop2Kit VST"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(Loop2KitVST
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/Loop2KitPreset.h
)

target_compile_definitions(Loop2KitVST
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Loop2KitVST
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
