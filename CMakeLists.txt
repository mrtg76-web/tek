cmake_minimum_required(VERSION 3.22)
project(TEK VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TEK_BUILD_PLUGIN "Build the JUCE plugin" ON)
option(TEK_BUILD_TESTS "Build the headless core tests" ON)
set(TEK_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (fetched automatically when empty)")

# ---------------------------------------------------------------- core (framework independent)
add_library(tek_core STATIC core/Model.cpp core/Chords.cpp core/Engine.cpp core/Controller.cpp)
target_include_directories(tek_core PUBLIC core)

if(TEK_BUILD_TESTS)
    enable_testing()
    add_executable(tek_tests tests/test_core.cpp tests/test_controller.cpp)
    target_link_libraries(tek_tests PRIVATE tek_core)
    add_test(NAME tek_tests COMMAND tek_tests)
endif()

# ---------------------------------------------------------------- plugin
if(TEK_BUILD_PLUGIN)
    if(TEK_JUCE_DIR)
        add_subdirectory(${TEK_JUCE_DIR} JUCE EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW ON)
        FetchContent_MakeAvailable(JUCE)
    endif()

    if(APPLE)
        set(TEK_FORMATS VST3 AU Standalone)
    elseif(WIN32)
        set(TEK_FORMATS VST3 Standalone)
    else()
        set(TEK_FORMATS VST3)
    endif()

    juce_add_plugin(TEK
        COMPANY_NAME "TEK"
        BUNDLE_ID "com.tek.sequencer"
        PLUGIN_MANUFACTURER_CODE Tekk
        PLUGIN_CODE Tek1
        FORMATS ${TEK_FORMATS}
        PRODUCT_NAME "TEK"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Instrument Generator
        AU_MAIN_TYPE kAudioUnitType_MusicDevice)

    target_sources(TEK PRIVATE
        plugin/PluginProcessor.cpp
        plugin/PluginEditor.cpp)

    target_compile_definitions(TEK PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    if(UNIX AND NOT APPLE)
        target_compile_definitions(TEK PUBLIC JUCE_ALSA=0 JUCE_JACK=0)
    endif()

    target_link_libraries(TEK
        PRIVATE
            tek_core
            juce::juce_audio_processors
            juce::juce_audio_devices
            juce::juce_audio_utils
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Headless test of the real processor (Linux CI / development only)
    option(TEK_BUILD_PLUGIN_TEST "Build the processor smoke test" OFF)
    if(TEK_BUILD_PLUGIN_TEST)
        juce_add_console_app(tek_plugin_test PRODUCT_NAME "tek_plugin_test")
        target_sources(tek_plugin_test PRIVATE tests/test_plugin.cpp plugin/PluginProcessor.cpp plugin/PluginEditor.cpp)
        target_compile_definitions(tek_plugin_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="TEK"
            JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=1 JucePlugin_IsMidiEffect=0)
        if(UNIX AND NOT APPLE)
            target_compile_definitions(tek_plugin_test PRIVATE JUCE_ALSA=0 JUCE_JACK=0)
        endif()
        target_link_libraries(tek_plugin_test PRIVATE tek_core juce::juce_audio_processors juce::juce_audio_devices
            juce::juce_gui_basics juce::juce_recommended_config_flags)
    endif()
endif()
