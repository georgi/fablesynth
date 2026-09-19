#pragma once
#include "../source/agent/FableAgent.h"
#include <cstdio>
#include <limits>

// Real processor checks, instantiated separately by each existing host binary.
// No network, credentials, or model nondeterminism is involved.
template <typename Processor>
bool runAgentProcessorChecks(const char* label, const fable::ParamInfo* catalog, std::size_t count,
                             const juce::String& continuous, const juce::String& discrete) {
    bool ok = true;
    auto expect = [&](bool condition, const char* description) {
        if (!condition) { std::fprintf(stderr, "AGENT %s FAIL: %s\n", label, description); ok = false; }
    };
    Processor processor;
    auto& agent = processor.getAgent();
    const auto original = agent.capture();
    expect(!static_cast<bool>(codeact::get(original.snapshot.audio, "available")), "audio unavailable before rendering");
    expect(original.snapshot.parameters.size() == count, "complete canonical catalog");
    for (std::size_t i = 0; i < count && i < original.snapshot.parameters.size(); ++i) {
        const auto& p = original.snapshot.parameters[i]; const auto& d = catalog[i];
        expect(p.id == juce::String(d.pid) && p.minimum == d.min && p.maximum == d.max,
               "canonical identity and physical range");
        if (d.kind == fable::Kind::Enum) expect(p.choices.size() == d.options->size(), "enumerated choices");
        if (d.kind == fable::Kind::Bool || d.curve == fable::Curve::Int) expect(p.step == 1, "discrete metadata");
    }
    auto parameter = [&](const juce::String& id) -> codeact::Parameter {
        for (const auto& p : original.snapshot.parameters) if (p.id == id) return p;
        expect(false, "test parameter exists"); return {};
    };
    const auto p = parameter(continuous), d = parameter(discrete);
    const auto alternate = p.minimum + (p.maximum - p.minimum) * 0.37;
    const codeact::Change good { p.id, p.value, alternate };
    juce::String error;
    for (const auto& bad : std::vector<codeact::Change> {
            { "unknown", 0, 0 }, { d.id, d.value, d.minimum + 0.5 },
            { p.id, p.value, p.maximum + 1 }, { p.id, p.value, std::numeric_limits<double>::infinity() } }) {
        expect(!agent.applyProposal(original, { good, bad }, error), "reject whole invalid batch");
        expect(agent.capture().document == original.document, "invalid batch performs no writes");
    }
    expect(agent.applyProposal(original, { good }, error), "valid parameter proposal applies");
    const auto* raw = processor.apvts.getRawParameterValue(p.id);
    expect(raw && std::abs(raw->load() - alternate) < std::max(1.0e-5, std::abs(alternate) * 1.0e-5),
           "APVTS holds requested physical value");
    expect(!agent.applyProposal(original, { good }, error), "stale whole-state proposal rejected");
    juce::MemoryBlock saved;
    processor.getStateInformation(saved);
    Processor restored;
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    expect(restored.apvts.getRawParameterValue(p.id)->load() == raw->load(), "agent value survives state reload");
    const auto identical = agent.capture();
    processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    expect(!agent.applyProposal(identical, { {p.id, raw->load(), p.minimum} }, error),
           "identical document reload invalidates prior proposal");
    processor.prepareToPlay(48000, 128);
    juce::AudioBuffer<float> rendered(2, 128);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.8f), 0);
    double energy = 0, peak = 0;
    int measuredFrames = 0;
    for (int block = 0; block < 38; ++block) {
        rendered.clear();
        processor.processBlock(rendered, midi);
        midi.clear();
        for (int frame = 0; frame < rendered.getNumSamples() && measuredFrames < 4800; ++frame, ++measuredFrames)
            for (int channel = 0; channel < 2; ++channel) {
                const double value = rendered.getSample(channel, frame);
                energy += value * value;
                peak = std::max(peak, std::abs(value));
            }
    }
    const auto measured = agent.snapshot();
    const auto combined = codeact::get(measured.audio, "combined");
    const auto measuredRms = static_cast<double>(codeact::get(combined, "rmsLinear"));
    expect(static_cast<bool>(codeact::get(measured.audio, "available")), "actual output meter becomes available");
    expect(static_cast<double>(codeact::get(measured.audio, "windowFrames")) == 4800,
           "measurement uses a fixed 100 ms sample window");
    expect(std::isfinite(measuredRms) && measuredRms > 1.0e-7,
           "rendered note produces finite nonzero output measurement");
    expect(std::abs(measuredRms - std::sqrt(energy / (4800 * 2))) < 1.0e-6,
           "meter RMS matches exact final host output samples");
    expect(std::abs(static_cast<double>(codeact::get(combined, "samplePeakLinear")) - peak) < 1.0e-6,
           "meter sample peak matches exact final host output samples");
    expect(codeact::get(measured.audio, "audioWindowAgeMs").isVoid()
               && static_cast<bool>(codeact::get(measured.audio, "frozenAtTurnStart")),
           "measurement labels unknown age and frozen capture");
    expect(codeact::get(measured.meters, "fx").isArray(), "FX meter taps are discoverable");
    processor.releaseResources();
    expect(!static_cast<bool>(codeact::get(agent.snapshot().audio, "available")),
           "released processor invalidates audio measurements");
    std::printf("AGENT %s processor checks %s\n", label, ok ? "PASS" : "FAIL");
    return ok;
}
