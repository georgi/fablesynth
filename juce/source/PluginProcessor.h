#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Engine.h"
#include "dsp/Fx.h"
#include "dsp/Params.h"
#include "dsp/Presets.h"

// FableSynth VST/AU processor. Owns the JUCE-independent DSP core (Engine + Fx)
// and bridges the APVTS parameter tree + MIDI to it.
class FableAudioProcessor : public juce::AudioProcessor {
public:
    FableAudioProcessor();
    ~FableAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FableSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return (int)fable::factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- wavetable visualization feed (read on the message thread) ----
    // Tables are generated once and never mutated at runtime, so the editor can
    // read their viz frames directly. The live modulated frame position per
    // oscillator is published from the audio thread via atomics (-1 = idle).
    const std::vector<fable::GeneratedTable>& getTables() const { return tables; }
    float getVizPos(int osc) const { return (osc == 0 ? vizPosA : vizPosB).load(); }

    // ---- HUD feeds (scope / spectrum / voices / MIDI led) ----
    int    getVoiceCount() const { return voiceCount.load(); }
    bool   getMidiActive() const { return midiGlow.load() > 0; }
    double getCurrentSr()  const { return currentSr.load(); }
    // Copy the most recent n post-FX mono samples (oldest -> newest) into dst.
    void   readScope(float* dst, int n) const;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    fable::Engine engine;
    fable::Fx fx;
    std::vector<fable::GeneratedTable> tables;   // procedural tables (+ viz frames)
    std::atomic<float> vizPosA{-1.0f}, vizPosB{-1.0f};
    juce::AudioBuffer<float> scratchR; // mono-output downmix scratch

    // HUD feeds
    static constexpr int kScopeSize = 4096; // power of two
    std::array<float, kScopeSize> scopeBuf{};
    std::atomic<int> scopeW{0};
    std::atomic<int> voiceCount{0};
    std::atomic<int> midiGlow{0};
    std::atomic<double> currentSr{48000.0};
    std::array<std::atomic<float>*, fable::NUM_PARAMS> rawParams{};
    int currentProgram = 0;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FableAudioProcessor)
};
