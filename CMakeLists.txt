cmake_minimum_required(VERSION 3.22)

project(Changeling VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

# ---------------------------------------------------------------------------
# JUCE: use a local copy if you have one (-DCHANGELING_JUCE_PATH=...),
# otherwise download JUCE 8 automatically.
# ---------------------------------------------------------------------------
set(CHANGELING_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")
option(CHANGELING_BUILD_PLUGIN "Build the plugin (needs JUCE)" ON)
option(CHANGELING_BUILD_TESTS "Build the offline render test and the footswitch test" ON)

if(CHANGELING_BUILD_PLUGIN)
    if(CHANGELING_JUCE_PATH)
        add_subdirectory(${CHANGELING_JUCE_PATH} JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(
            JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    set(CHANGELING_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND CHANGELING_FORMATS AU)
    endif()

    juce_add_plugin(Changeling
        COMPANY_NAME "Kevin Kaye"
        COMPANY_WEBSITE ""
        BUNDLE_ID "uk.kevinkaye.changeling"
        PLUGIN_MANUFACTURER_CODE Kkay
        PLUGIN_CODE Chlg
        FORMATS ${CHANGELING_FORMATS}
        PRODUCT_NAME "Changeling"
        DESCRIPTION "Multi-voice instrument synthesizer: your notes, born again as synth"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Synth)

    juce_add_binary_data(ChangelingData SOURCES Resources/RobotoCondensed-Bold.ttf)
    if(UNIX AND NOT APPLE)
        set_target_properties(ChangelingData PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()

    target_sources(Changeling PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/PedalControl.cpp
        Source/Keeper.cpp)

    target_include_directories(Changeling PRIVATE Source Source/engine)

    target_compile_definitions(Changeling PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(Changeling
        PRIVATE
            ChangelingData
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Footswitch test: drives the real processor with plain 127/0 MIDI messages
    if(CHANGELING_BUILD_TESTS)
        juce_add_console_app(changeling_foot_test PRODUCT_NAME "changeling_foot_test")
        target_sources(changeling_foot_test PRIVATE
            Tests/FootTest.cpp
            Source/PluginProcessor.cpp
            Source/PluginEditor.cpp
            Source/PedalControl.cpp
            Source/Keeper.cpp)
        target_include_directories(changeling_foot_test PRIVATE Source Source/engine)
        target_compile_definitions(changeling_foot_test PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=1
            JUCE_STANDALONE_APPLICATION=1
            JucePlugin_Name="Changeling"
            JucePlugin_VersionString="0.1.0")
        target_link_libraries(changeling_foot_test PRIVATE
            ChangelingData
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_recommended_config_flags)
    endif()
endif()

# ---------------------------------------------------------------------------
# Offline engine test (no JUCE needed): tracking, every mode, MIDI, latch, chaos
# ---------------------------------------------------------------------------
if(CHANGELING_BUILD_TESTS)
    add_executable(changeling_render_test Tests/RenderTest.cpp)
    target_include_directories(changeling_render_test PRIVATE Source/engine)
endif()
