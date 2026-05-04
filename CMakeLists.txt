cmake_minimum_required(VERSION 3.22)
project(FreeVox8 VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build universal binary")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
elseif (DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" JUCE)
else()
    message(FATAL_ERROR "JUCE not found. Add it as ./JUCE or configure with -DJUCE_DIR=/path/to/JUCE")
endif()

juce_add_plugin(FreeVox8
    COMPANY_NAME "TizWildinEntertainment"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE TzWd
    PLUGIN_CODE FVX8
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "FreeVox8"
    BUNDLE_ID "com.tizwildinentertainment.freevox8"
    COMPANY_URL "https://github.com/GareBear99/FreeVox8"
    COMPANY_EMAIL "GareBear99@users.noreply.github.com"
    DESCRIPTION "Free spectral vocoder, ghost freeze, and dynamic vocal masking plugin"
)

target_sources(FreeVox8 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/Config.h
    Source/DSP/FreeVox8DSP.h
    Source/DSP/FreeVox8SpectralEngine.h
    Source/UI/SpectralDisplay.h
)

target_compile_definitions(FreeVox8 PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(FreeVox8 PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
)

option(FREEVOX8_BUILD_TESTS "Build lightweight non-JUCE repo validation tests" OFF)
if (FREEVOX8_BUILD_TESTS)
    add_executable(FreeVox8_RepoAudit Tests/RepoAudit.cpp)
    target_compile_features(FreeVox8_RepoAudit PRIVATE cxx_std_17)
endif()
