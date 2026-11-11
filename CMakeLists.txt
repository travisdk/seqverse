cmake_minimum_required(VERSION 3.22)

project(SeqVerse VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDK sources shared between plugin projects, override with -DJUCE_DIR=... / -DCLAP_JUCE_EXTENSIONS_DIR=...
#   JUCE                   8.0.15  (git tag)
#   clap-juce-extensions   7adee3a (0.26.0-117), with submodules
if(WIN32)
    set(JUCE_DIR "$ENV{USERPROFILE}/SDKs/JUCE-8.0.15" CACHE PATH "Path to a JUCE source tree")
    set(CLAP_JUCE_EXTENSIONS_DIR "$ENV{USERPROFILE}/SDKs/clap-juce-extensions-main" CACHE PATH
        "Path to a clap-juce-extensions source tree")
else()
    set(JUCE_DIR "$ENV{HOME}/SDKs/JUCE-8.0.15" CACHE PATH "Path to a JUCE source tree")
    set(CLAP_JUCE_EXTENSIONS_DIR "$ENV{HOME}/SDKs/clap-juce-extensions-main" CACHE PATH
        "Path to a clap-juce-extensions source tree")
endif()

foreach(sdk_dir JUCE_DIR CLAP_JUCE_EXTENSIONS_DIR)
    if(NOT EXISTS "${${sdk_dir}}/CMakeLists.txt")
        message(FATAL_ERROR "${sdk_dir} is '${${sdk_dir}}' but no CMakeLists.txt was found there")
    endif()
endforeach()

add_subdirectory(${JUCE_DIR} juce EXCLUDE_FROM_ALL)
add_subdirectory(${CLAP_JUCE_EXTENSIONS_DIR} clap-juce-extensions EXCLUDE_FROM_ALL)

# The editor's fonts, as BinaryData (SIL Open Font License, see resources/fonts/*/OFL.txt)
juce_add_binary_data(SeqVerseFonts SOURCES
    resources/fonts/ChakraPetch/ChakraPetch-SemiBold.ttf
    resources/fonts/ChakraPetch/ChakraPetch-Bold.ttf
    resources/fonts/JetBrainsMono/JetBrainsMono-Medium.ttf)
set_target_properties(SeqVerseFonts PROPERTIES POSITION_INDEPENDENT_CODE ON) # linked into the plugin's shared library

# JUCE has no CLAP format of its own: clap-juce-extensions wraps this (VST3-formatted) target as a CLAP.
juce_add_plugin(SeqVerse
    COMPANY_NAME "Henrik"
    PLUGIN_MANUFACTURER_CODE Hjuh
    PLUGIN_CODE Sqvs
    FORMATS VST3
    PRODUCT_NAME "SeqVerse"
    IS_SYNTH FALSE
    IS_MIDI_EFFECT FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE)

target_sources(SeqVerse PRIVATE
    src/DarkLookAndFeel.h
    src/Music.h
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/PluginEditor.cpp
    src/PluginEditor.h)

target_compile_definitions(SeqVerse PUBLIC
    SEQVERSE_VERSION="${PROJECT_VERSION}"   # shown in the editor's header, so a download can be told apart
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(SeqVerse
    PRIVATE
        SeqVerseFonts
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# CLAP_USE_JUCE_PARAMETER_RANGES ALL: without it every parameter is reported to the host as 0..1, but the choice
# parameters are still flagged as stepped, so a host may only offer two values of a 43-entry Scale list.
clap_juce_extensions_plugin(TARGET SeqVerse
    CLAP_ID "com.henrik.seqverse"
    CLAP_FEATURES note-effect utility
    CLAP_USE_JUCE_PARAMETER_RANGES ALL)

# Headless CLAP host for scripted tests. Loads the built .clap straight from the build tree, not from ~/.clap.
#   cmake --build build --target SeqVerse_CLAP clap_test_host && ctest --test-dir build --output-on-failure
enable_testing()

add_executable(clap_test_host tests/clap_test_host.cpp)
target_include_directories(clap_test_host SYSTEM PRIVATE "${CLAP_JUCE_EXTENSIONS_DIR}/clap-libs/clap/include")
target_link_libraries(clap_test_host PRIVATE ${CMAKE_DL_LIBS})
if(NOT WIN32)
    target_compile_options(clap_test_host PRIVATE -Wall -Wextra -Wpedantic)
endif()
endif()

add_test(NAME clap_host_generator COMMAND clap_test_host $<TARGET_FILE:SeqVerse_CLAP>)

# Headless editor test: builds the real editor and processor without a host or a window and checks which controls
# are enabled. Given a file name prefix it also writes PNG snapshots of the editor.
#   cmake --build build --target SeqVerseUiTest && ./build/SeqVerseUiTest /tmp/editor
juce_add_console_app(SeqVerseUiTest PRODUCT_NAME "SeqVerseUiTest")

target_sources(SeqVerseUiTest PRIVATE
    tests/ui_test.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_compile_definitions(SeqVerseUiTest PRIVATE
    SEQVERSE_VERSION="${PROJECT_VERSION}"
    JucePlugin_Name="SeqVerse"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1) # the test runs the message loop for a while, so that the editor's timer fires

target_link_libraries(SeqVerseUiTest
    PRIVATE
        SeqVerseFonts
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

add_test(NAME ui_enabled_controls COMMAND SeqVerseUiTest)

# Design prototype, not part of the plugin: renders the same controls in several knob styles as PNG snapshots.
#   cmake --build build --target KnobStyles && ./build/KnobStyles_artefacts/Release/KnobStyles <output directory>
juce_add_console_app(KnobStyles PRODUCT_NAME "KnobStyles")

target_sources(KnobStyles PRIVATE tools/knob_styles.cpp)

target_compile_definitions(KnobStyles PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(KnobStyles
    PRIVATE
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Design prototype, not part of the plugin: the controls in other layouts (with linear sliders), as PNG snapshots.
#   cmake --build build --target LayoutMockup && ./build/LayoutMockup_artefacts/Release/LayoutMockup <output directory>
juce_add_console_app(LayoutMockup PRODUCT_NAME "LayoutMockup")

target_sources(LayoutMockup PRIVATE tools/layout_mockup.cpp)

target_compile_definitions(LayoutMockup PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(LayoutMockup
    PRIVATE
        SeqVerseFonts
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
