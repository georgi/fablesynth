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
        PRODUCT_NAME "FableSynth WT-1"
        COMPANY_NAME "FableSynth"
        PLUGIN_MANUFACTURER_CODE Fabl
        PLUGIN_CODE Fwt1
        FORMATS AU VST3 Standalone
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
        source/ui/Modulation.cpp
        source/ui/NoteSeqView.cpp
        source/ui/Panels.cpp
        source/ui/WavetableEditor.cpp
        source/dsp/Engine.cpp
        source/dsp/Wavetables.cpp
        source/dsp/Fx.cpp
        source/dsp/Params.cpp
        source/dsp/Presets.cpp
        source/dsp/UserTables.cpp)

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

    # ---- FableSynth DR-1 (drum machine) — second plugin target in the same
    # project. Shares the WT-1 DSP sources (Wavetables/Params/UserTables/Fx)
    # by direct source inclusion; its own core lives in source/drum/.
    juce_add_plugin(FableDrum
        PRODUCT_NAME "FableSynth DR-1"
        COMPANY_NAME "FableSynth"
        PLUGIN_MANUFACTURER_CODE Fabl
        PLUGIN_CODE Fdr1
        FORMATS AU VST3 Standalone
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    # Shared between the FableDrum plugin and the drum_host_test console app.
    set(FABLE_DRUM_SOURCES
        source/drum/DrumProcessor.cpp
        source/drum/DrumEditor.cpp
        source/drum/ui/DrumHeader.cpp
        source/drum/ui/PadGrid.cpp
        source/drum/ui/PadStrip.cpp
        source/drum/ui/DrumPanels.cpp
        source/drum/ui/StepSeqView.cpp
        source/drum/ui/DrumFxRack.cpp
        source/ui/Controls.cpp
        source/ui/Modulation.cpp
        source/drum/dsp/DrumParams.cpp
        source/drum/dsp/DrumTables.cpp
        source/drum/dsp/SampledTables.gen.cpp
        source/drum/dsp/DrumEngine.cpp
        source/drum/dsp/DrumFx.cpp
        source/drum/dsp/DrumKits.cpp
        source/drum/dsp/DrumPatches.cpp
        source/dsp/Wavetables.cpp
        source/dsp/Params.cpp
        source/dsp/UserTables.cpp
        source/dsp/Fx.cpp)

    target_sources(FableDrum PRIVATE ${FABLE_DRUM_SOURCES})

    target_compile_definitions(FableDrum PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    juce_generate_juce_header(FableDrum)

    target_link_libraries(FableDrum PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

    # ---- FableSynth BL-1 (acid bassline) — third plugin target in the same
    # project. Shares the WT-1 table/param core (Wavetables/Params/Fx) and the
    # fui controls by direct source inclusion; its own core lives in source/bass/.
    juce_add_plugin(FableBass
        PRODUCT_NAME "FableSynth BL-1"
        COMPANY_NAME "FableSynth"
        PLUGIN_MANUFACTURER_CODE Fabl
        PLUGIN_CODE Fbl1
        FORMATS AU VST3 Standalone
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    # Shared between the FableBass plugin and the bass_host_test console app.
    set(FABLE_BASS_SOURCES
        source/bass/BassProcessor.cpp
        source/bass/BassEditor.cpp
        source/bass/ui/BassHeader.cpp
        source/bass/ui/BassPanels.cpp
        source/bass/ui/PitchSeqView.cpp
        source/bass/ui/BassFxRack.cpp
        source/ui/Controls.cpp
        source/ui/Modulation.cpp
        source/bass/dsp/BassParams.cpp
        source/bass/dsp/BassEngine.cpp
        source/bass/dsp/BassFx.cpp
        source/bass/dsp/BassPatches.cpp
        source/dsp/Wavetables.cpp
        source/dsp/Params.cpp
        source/dsp/UserTables.cpp
        source/dsp/Fx.cpp)

    target_sources(FableBass PRIVATE ${FABLE_BASS_SOURCES})

    target_compile_definitions(FableBass PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    juce_generate_juce_header(FableBass)

    target_link_libraries(FableBass PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

    # ---- FableSynth SQ-4 (session launcher) — fourth plugin target. Hosts the
    # DR-1/BL-1/WT-1 engines in one processor (docs/sq4-clips.md, JUCE surface).
    juce_add_plugin(FableSeq
        PRODUCT_NAME "FableSynth SQ-4"
        COMPANY_NAME "FableSynth"
        PLUGIN_MANUFACTURER_CODE Fabl
        PLUGIN_CODE Fsq4
        FORMATS AU VST3 Standalone
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    set(FABLE_SEQ_SOURCES
        source/seq/SeqProcessor.cpp
        source/seq/SeqEditor.cpp
        source/seq/ui/SeqHeader.cpp
        source/seq/ui/TrackHeadsView.cpp
        source/seq/ui/SceneGridView.cpp
        source/seq/ui/SeqFooterView.cpp
        source/seq/ui/ClipEditView.cpp
        source/seq/dsp/SeqFactory.cpp
        source/seq/dsp/Conductor.cpp
        source/ui/Controls.cpp source/ui/Modulation.cpp
        # all three machines' DSP (each list already exists above):
        source/dsp/Engine.cpp source/dsp/Presets.cpp
        source/drum/dsp/DrumParams.cpp source/drum/dsp/DrumTables.cpp
        source/drum/dsp/SampledTables.gen.cpp source/drum/dsp/DrumEngine.cpp
        source/drum/dsp/DrumFx.cpp source/drum/dsp/DrumKits.cpp source/drum/dsp/DrumPatches.cpp
        source/bass/dsp/BassParams.cpp source/bass/dsp/BassEngine.cpp
        source/bass/dsp/BassFx.cpp source/bass/dsp/BassPatches.cpp
        source/dsp/Wavetables.cpp source/dsp/Params.cpp source/dsp/UserTables.cpp source/dsp/Fx.cpp)

    target_sources(FableSeq PRIVATE ${FABLE_SEQ_SOURCES})
    target_compile_definitions(FableSeq PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    juce_generate_juce_header(FableSeq)
    target_link_libraries(FableSeq PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

    # On macOS, JUCE regenerates moduleinfo.json as a post-build step *after* the
    # bundle is code-signed, which invalidates the signature seal. Hosts with a
    # hardened scanner then refuse to load the plugin ("a sealed resource is
    # missing or invalid" — e.g. Ableton silently skips it). Re-sign (ad-hoc)
    # after all of JUCE's post-build steps so the seal matches the contents.
    if(APPLE)
        foreach(tgt FableSynth FableDrum FableBass FableSeq)
            foreach(fmt VST3 AU Standalone)
                if(TARGET ${tgt}_${fmt})
                    add_custom_command(TARGET ${tgt}_${fmt} POST_BUILD
                        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:${tgt}_${fmt}>"
                        VERBATIM)
                endif()
            endforeach()
        endforeach()
    endif()

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
        source/ui/Modulation.cpp
        source/ui/NoteSeqView.cpp
        source/ui/Panels.cpp
        source/ui/WavetableEditor.cpp
        source/dsp/Engine.cpp
        source/dsp/Wavetables.cpp
        source/dsp/Fx.cpp
        source/dsp/Params.cpp
        source/dsp/Presets.cpp
        source/dsp/UserTables.cpp)
    target_compile_definitions(plugin_host_test PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(plugin_host_test PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME plugin_host_test COMMAND plugin_host_test)

    # DR-1 plugin-boundary harness (instantiates the real DrumAudioProcessor).
    juce_add_console_app(drum_host_test PRODUCT_NAME "drum_host_test")
    target_sources(drum_host_test PRIVATE
        test/drum_host_test.cpp
        ${FABLE_DRUM_SOURCES})
    target_compile_definitions(drum_host_test PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(drum_host_test PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME drum_host_test COMMAND drum_host_test)

    # BL-1 plugin-boundary harness (instantiates the real BassAudioProcessor).
    juce_add_console_app(bass_host_test PRODUCT_NAME "bass_host_test")
    target_sources(bass_host_test PRIVATE
        test/bass_host_test.cpp
        ${FABLE_BASS_SOURCES})
    target_compile_definitions(bass_host_test PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(bass_host_test PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME bass_host_test COMMAND bass_host_test)

    # SQ-4 plugin-boundary harness (instantiates the real SeqAudioProcessor).
    juce_add_console_app(sq4_host_test PRODUCT_NAME "sq4_host_test")
    target_sources(sq4_host_test PRIVATE
        test/sq4_host_test.cpp
        ${FABLE_SEQ_SOURCES})
    target_compile_definitions(sq4_host_test PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(sq4_host_test PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME sq4_host_test COMMAND sq4_host_test)
endif()

# ---- Headless DSP verification harness (no JUCE) ----
add_executable(engine_test
    test/engine_test.cpp
    source/dsp/Engine.cpp
    source/dsp/Wavetables.cpp
    source/dsp/Fx.cpp
    source/dsp/Params.cpp
    source/dsp/Presets.cpp
    source/dsp/UserTables.cpp)
target_compile_features(engine_test PRIVATE cxx_std_17)

# ---- DR-1 headless DSP verification harness (no JUCE) ----
add_executable(drum_engine_test
    test/drum_engine_test.cpp
    source/drum/dsp/DrumParams.cpp
    source/drum/dsp/DrumTables.cpp
    source/drum/dsp/SampledTables.gen.cpp
    source/drum/dsp/DrumEngine.cpp
    source/drum/dsp/DrumFx.cpp
    source/drum/dsp/DrumKits.cpp
    source/drum/dsp/DrumPatches.cpp
    source/dsp/Fx.cpp
    source/dsp/Params.cpp
    source/dsp/Wavetables.cpp
    source/dsp/UserTables.cpp)
target_compile_features(drum_engine_test PRIVATE cxx_std_17)

# ---- BL-1 headless DSP verification harness (no JUCE) ----
add_executable(bass_engine_test
    test/bass_engine_test.cpp
    source/bass/dsp/BassParams.cpp
    source/bass/dsp/BassEngine.cpp
    source/bass/dsp/BassFx.cpp
    source/bass/dsp/BassPatches.cpp
    source/dsp/Fx.cpp
    source/dsp/Params.cpp
    source/dsp/Wavetables.cpp
    source/dsp/UserTables.cpp)
target_compile_features(bass_engine_test PRIVATE cxx_std_17)

# ---- SQ-4 headless DSP verification harness (no JUCE) ----
add_executable(sq4_engine_test
    test/sq4_engine_test.cpp
    source/dsp/Engine.cpp
    source/dsp/Wavetables.cpp
    source/dsp/Fx.cpp
    source/dsp/Params.cpp
    source/dsp/Presets.cpp
    source/dsp/UserTables.cpp
    source/bass/dsp/BassParams.cpp
    source/bass/dsp/BassEngine.cpp
    source/bass/dsp/BassFx.cpp
    source/bass/dsp/BassPatches.cpp
    source/drum/dsp/DrumParams.cpp
    source/drum/dsp/DrumTables.cpp
    source/drum/dsp/SampledTables.gen.cpp
    source/drum/dsp/DrumEngine.cpp
    source/drum/dsp/DrumFx.cpp
    source/drum/dsp/DrumKits.cpp
    source/drum/dsp/DrumPatches.cpp
    source/seq/dsp/SeqFactory.cpp
    source/seq/dsp/Conductor.cpp)
target_compile_features(sq4_engine_test PRIVATE cxx_std_17)

enable_testing()
add_test(NAME engine_test COMMAND engine_test)
add_test(NAME drum_engine_test COMMAND drum_engine_test)
add_test(NAME bass_engine_test COMMAND bass_engine_test)
add_test(NAME sq4_engine_test COMMAND sq4_engine_test)
