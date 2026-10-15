#pragma once
// Hosts the user's own Serum 1 (VST3) headless to read preset parameters and
// capture wavetables. Nothing from Serum ships with FableSynth: an import only
// works on a machine with a licensed Serum install.
#include <juce_audio_processors/juce_audio_processors.h>
#include "SerumPatch.h"
#include <map>

namespace serumimport {

// Default install locations on this platform.
juce::File findSerumPlugin();                 // Serum.vst3, or File() when absent
juce::Array<juce::File> findSerumPresetRoots(); // folders that hold .fxp presets

class SerumHost {
public:
    // Loads Serum at the capture rate. error explains a failure.
    bool load(const juce::File& vst3, juce::String& error);
    bool loaded() const { return plugin_ != nullptr; }

    // Puts an .fxp chunk into Serum and reads every parameter.
    std::vector<SerumParam> readPreset(const FxpData& fxp);
    // K single cycles across WTPos for oscillator 'A' or 'B' of the current
    // preset, isolated from the rest of the voice. Empty when the osc is off.
    std::vector<float> captureOsc(const FxpData& fxp, char osc, int frames = 64);

private:
    void setChunk(const juce::MemoryBlock& chunk);
    void set(const juce::String& name, float v);
    void setInit(const juce::String& name);
    float get(const juce::String& name) const;
    void render(int samples, std::vector<float>* outL = nullptr);
    void note(int n, bool on);

    juce::AudioPluginFormatManager formats_;
    std::unique_ptr<juce::AudioPluginInstance> plugin_;
    std::map<juce::String, juce::AudioProcessorParameter*> byName_;
    std::vector<float> initValues_;
    double sr_ = 0;
    int block_ = 512;
};

} // namespace serumimport
