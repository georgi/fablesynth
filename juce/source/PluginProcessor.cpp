#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace fable;

FableAudioProcessor::FableAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createLayout()) {
    // Cache raw atomic pointers, indexed by the engine's flat parameter id.
    const auto& info = paramInfo();
    for (int i = 0; i < NUM_PARAMS; ++i)
        rawParams[i] = apvts.getRawParameterValue(info[i].pid);
}

// Build the APVTS layout from the single canonical descriptor table, using the
// exact same value<->norm mapping as the web app so curves are identical.
juce::AudioProcessorValueTreeState::ParameterLayout FableAudioProcessor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& d : paramInfo()) {
        juce::ParameterID pid(d.pid, 1);
        if (d.kind == Kind::Bool) {
            layout.add(std::make_unique<juce::AudioParameterBool>(pid, d.label, d.def != 0.0f));
        } else if (d.kind == Kind::Enum) {
            juce::StringArray choices;
            for (const auto& s : *d.options) choices.add(s);
            layout.add(std::make_unique<juce::AudioParameterChoice>(pid, d.label, choices, (int)d.def));
        } else {
            // Custom range whose 0..1 mapping matches normToValue / valueToNorm.
            ParamInfo info = d;
            juce::NormalisableRange<float> range(
                d.min, d.max,
                [info](float, float, float n) { return normToValue(info, n); },
                [info](float, float, float v) { return valueToNorm(info, v); });
            if (d.curve == Curve::Int) range.interval = 1.0f;
            layout.add(std::make_unique<juce::AudioParameterFloat>(pid, d.label, range, d.def));
        }
    }
    return layout;
}

void FableAudioProcessor::prepareToPlay(double sampleRate, int) {
    engine.prepare(sampleRate);
    engine.setTables(generateTables());
    fx.prepare(sampleRate);
    prepared = true;
}

bool FableAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void FableAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    // Pull current parameter values into the engine's flat array (audio-thread safe).
    auto& p = engine.params();
    for (int i = 0; i < NUM_PARAMS; ++i)
        if (rawParams[i]) p[i] = rawParams[i]->load();
    fx.setParams(p);

    // MIDI -> note / bend events.
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        if (m.isNoteOn())        engine.noteOn(m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff())  engine.noteOff(m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.panic();
        else if (m.isPitchWheel()) engine.pitchBend((m.getPitchWheelValue() - 8192) / 8192.0 * 2.0);
    }

    if (buffer.getNumChannels() > 1) {
        float* L = buffer.getWritePointer(0);
        float* R = buffer.getWritePointer(1);
        engine.render(L, R, n);     // fills L/R with the summed (pre-FX) voice mix
        fx.process(L, R, n);
    } else {
        // Mono out: render to a stereo scratch then downmix (engine is stereo).
        scratchR.setSize(1, n, false, false, true);
        float* L = buffer.getWritePointer(0);
        float* R = scratchR.getWritePointer(0);
        engine.render(L, R, n);
        fx.process(L, R, n);
        juce::FloatVectorOperations::add(L, R, n);
        juce::FloatVectorOperations::multiply(L, 0.5f, n);
    }
}

void FableAudioProcessor::setCurrentProgram(int index) {
    if (index < 0 || index >= (int)factoryPresets().size()) return;
    currentProgram = index;
    // Apply preset values onto the APVTS so the host + UI reflect them.
    ParamArray pv = applyPreset(factoryPresets()[index]);
    const auto& info = paramInfo();
    for (int i = 0; i < NUM_PARAMS; ++i) {
        if (auto* param = apvts.getParameter(info[i].pid)) {
            const auto& d = info[i];
            float norm = (d.kind == Kind::Bool) ? (pv[i] != 0.0f ? 1.0f : 0.0f)
                       : (d.kind == Kind::Enum) ? param->convertTo0to1(pv[i])
                       : valueToNorm(d, pv[i]);
            param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm));
        }
    }
}

const juce::String FableAudioProcessor::getProgramName(int index) {
    if (index < 0 || index >= (int)factoryPresets().size()) return {};
    return factoryPresets()[index].name;
}

void FableAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto state = apvts.copyState(); state.isValid())
        if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void FableAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* FableAudioProcessor::createEditor() {
    return new FableAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new FableAudioProcessor();
}
