cmake_minimum_required(VERSION 3.22)

project(GenerativeMidi VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDK sources shared between plugin projects, override with -DJUCE_DIR=... / -DCLAP_JUCE_EXTENSIONS_DIR=...
#   JUCE                   7.0.9  (git tag)
#   clap-juce-extensions   54d83e1 (0.26.0-83), with submodules
set(JUCE_DIR "$ENV{HOME}/SDKs/JUCE" CACHE PATH "Path to a JUCE source tree")
set(CLAP_JUCE_EXTENSIONS_DIR "$ENV{HOME}/SDKs/clap-juce-extensions" CACHE PATH
    "Path to a clap-juce-extensions source tree")

foreach(sdk_dir JUCE_DIR CLAP_JUCE_EXTENSIONS_DIR)
    if(NOT EXISTS "${${sdk_dir}}/CMakeLists.txt")
        message(FATAL_ERROR "${sdk_dir} is '${${sdk_dir}}' but no CMakeLists.txt was found there")
    endif()
endforeach()

add_subdirectory(${JUCE_DIR} juce EXCLUDE_FROM_ALL)
add_subdirectory(${CLAP_JUCE_EXTENSIONS_DIR} clap-juce-extensions EXCLUDE_FROM_ALL)

# JUCE has no CLAP format of its own: clap-juce-extensions wraps this (VST3-formatted) target as a CLAP.
juce_add_plugin(GenerativeMidi
    COMPANY_NAME "Henrik"
    PLUGIN_MANUFACTURER_CODE Hjuh
    PLUGIN_CODE Gnmd
    FORMATS VST3
    PRODUCT_NAME "GenerativeMidi"
    IS_SYNTH FALSE
    IS_MIDI_EFFECT FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE)

target_sources(GenerativeMidi PRIVATE
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/PluginEditor.cpp
    src/PluginEditor.h)

target_compile_definitions(GenerativeMidi PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(GenerativeMidi
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

clap_juce_extensions_plugin(TARGET GenerativeMidi
    CLAP_ID "com.henrik.generative-midi"
    CLAP_FEATURES note-effect utility)
