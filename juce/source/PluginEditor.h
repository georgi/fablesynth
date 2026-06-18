#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "WavetableView.h"

// Functional editor: a preset bar above JUCE's generic parameter panel, which
// auto-binds every APVTS parameter. (The bespoke 3D-wavetable rack UI lives in
// the web build; this exposes the full engine for host/DAW use.)
class FableAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit FableAudioProcessorEditor(FableAudioProcessor&);
    ~FableAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    FableAudioProcessor& proc;
    juce::Label title;
    juce::ComboBox presets;
    WavetableView viewA, viewB;
    juce::GenericAudioProcessorEditor generic;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FableAudioProcessorEditor)
};
