cmake_minimum_required(VERSION 3.22)

# macOS: one universal binary for Apple silicon and Intel Macs. Windows: link the C/C++ runtime
# statically so the plugin does not need the Visual C++ redistributable. (Both must be set before project().)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(LAVocalChain VERSION 0.4.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    # M_PI and friends; sources are UTF-8
    add_compile_definitions(_USE_MATH_DEFINES)
    add_compile_options(/utf-8)
endif()

option(LAVC_BUILD_TESTS "Build the DSP and plugin tests" ON)
option(LAVC_COPY_AFTER_BUILD "Install the plugins into the user/system plugin folders after building" OFF)
set(LAVC_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (fetched from GitHub when empty)")

# ---------------------------------------------------------------------------------------------
# JUCE
if(LAVC_JUCE_DIR)
    add_subdirectory(${LAVC_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------------------------
# DSP engine (no JUCE dependency, unit tested separately)
add_library(lavc_engine STATIC
    Source/dsp/PitchEngine.cpp
    Source/dsp/Modules.cpp
    Source/dsp/Effects.cpp
    Source/dsp/VocalEngine.cpp)
target_include_directories(lavc_engine PUBLIC Source)
target_compile_options(lavc_engine PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -O3>)

set(LAVC_PLUGIN_SOURCES
    Source/Parameters.cpp
    Source/PluginProcessor.cpp
    Source/gui/Theme.cpp
    Source/gui/Widgets.cpp
    Source/gui/Displays.cpp
    Source/gui/PluginEditor.cpp)

# VST3 everywhere; LV2 + Standalone on Linux; AU on macOS
if(WIN32)
    set(LAVC_FORMATS VST3)
elseif(APPLE)
    set(LAVC_FORMATS VST3 AU)
else()
    set(LAVC_FORMATS VST3 LV2 Standalone)
endif()

# ---------------------------------------------------------------------------------------------
# Plugin
juce_add_plugin(LAVocalChain
    PRODUCT_NAME "LA-VocalChain"
    COMPANY_NAME "LA Audio"
    BUNDLE_ID "com.la-audio.lavocalchain"
    PLUGIN_MANUFACTURER_CODE LaAu
    PLUGIN_CODE LaVc
    FORMATS ${LAVC_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Pitch Dynamics EQ Reverb
    LV2URI "https://github.com/AzoghMartins/LA-VocalChain"
    COPY_PLUGIN_AFTER_BUILD ${LAVC_COPY_AFTER_BUILD})

target_sources(LAVocalChain PRIVATE ${LAVC_PLUGIN_SOURCES})

target_compile_definitions(LAVocalChain PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(LAVocalChain
    PRIVATE
        lavc_engine
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------------------------
if(LAVC_BUILD_TESTS)
    enable_testing()
    add_executable(lavc_tests tests/EngineTests.cpp)
    target_link_libraries(lavc_tests PRIVATE lavc_engine)
    target_compile_options(lavc_tests PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-O2>)
    add_test(NAME engine COMMAND lavc_tests)

    # Drives the real processor + editor like a host (needs an X display, e.g. xvfb-run).
    juce_add_console_app(lavc_integration PRODUCT_NAME "LA-VocalChain Integration Test")
    target_sources(lavc_integration PRIVATE tests/PluginIntegrationTest.cpp ${LAVC_PLUGIN_SOURCES})
    target_compile_definitions(lavc_integration PRIVATE
        JucePlugin_Name="LA-VocalChain"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(lavc_integration PRIVATE
        lavc_engine
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags)
    add_test(NAME integration COMMAND lavc_integration)
endif()
