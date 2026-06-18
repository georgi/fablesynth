cmake_minimum_required(VERSION 3.22)
project(FableSynth VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- JUCE ----
# By default fetch JUCE from GitHub. To build offline, point JUCE_DIR at a local
# checkout: cmake -B build -DJUCE_DIR=/path/to/JUCE
option(FABLE_BUILD_PLUGIN "Build the JUCE VST3/Standalone plugin" ON)

if(FABLE_BUILD_PLUGIN)
    if(DEFINED JUCE_DIR)
        add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    juce_add_plugin(FableSynth
        PRODUCT_NAME "FableSynth"
        COMPANY_NAME "FableSynth"
        PLUGIN_MANUFACTURER_CODE Fabl
        PLUGIN_CODE Fwt1
        FORMATS VST3 Standalone
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(FableSynth PRIVATE
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/WavetableView.cpp
        source/ui/Controls.cpp
        source/ui/Displays.cpp
        source/ui/Panels.cpp
        source/dsp/Engine.cpp
        source/dsp/Wavetables.cpp
        source/dsp/Fx.cpp
        source/dsp/Params.cpp
        source/dsp/Presets.cpp)

    target_compile_definitions(FableSynth PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    juce_generate_juce_header(FableSynth)

    target_link_libraries(FableSynth PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

    # Plugin-boundary verification harness (instantiates the real processor).
    # Compiles the plugin + JUCE modules into its own console exe so there are no
    # duplicate-symbol clashes with the plugin's shared-code library.
    juce_add_console_app(plugin_host_test PRODUCT_NAME "plugin_host_test")
    target_sources(plugin_host_test PRIVATE
        test/plugin_host_test.cpp
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/WavetableView.cpp
        source/ui/Controls.cpp
        source/ui/Displays.cpp
        source/ui/Panels.cpp
        source/dsp/Engine.cpp
        source/dsp/Wavetables.cpp
        source/dsp/Fx.cpp
        source/dsp/Params.cpp
        source/dsp/Presets.cpp)
    target_compile_definitions(plugin_host_test PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(plugin_host_test PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME plugin_host_test COMMAND plugin_host_test)
endif()

# ---- Headless DSP verification harness (no JUCE) ----
add_executable(engine_test
    test/engine_test.cpp
    source/dsp/Engine.cpp
    source/dsp/Wavetables.cpp
    source/dsp/Fx.cpp
    source/dsp/Params.cpp
    source/dsp/Presets.cpp)
target_compile_features(engine_test PRIVATE cxx_std_17)

enable_testing()
add_test(NAME engine_test COMMAND engine_test)
