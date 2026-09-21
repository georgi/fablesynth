#pragma once

#include "../../codeact/src/Agent.h"
#include "../dsp/Params.h"
#include "../dsp/AudioMeter.h"
#include "../dsp/FxTelemetry.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace fable {

// Processor-owned, message-thread-only bridge. Only codeact::Agent runs work on
// a worker; no callback, processor, editor, or APVTS is passed to that worker.
class FableAgent final {
public:
    struct ConversationTurn {
        juce::String prompt, assistant, activityLog, status, error;
        std::size_t activityLogChunks = 0;
        std::vector<codeact::Change> changes;
        std::size_t changeCount = 0; // display includes at most 64 change details
    };
    struct CapturedState {
        codeact::Snapshot snapshot;
        juce::MemoryBlock document;
        std::uint64_t generation = 0;
    };
    using Capture = std::function<CapturedState()>;
    // Commit must check all its destinations/resources before its first write.
    using Commit = std::function<bool(const std::vector<codeact::Change>&, juce::String&)>;

    FableAgent(Capture, Commit, std::unique_ptr<codeact::ChatTransport> = {}, codeact::Limits = {});
    ~FableAgent();
    bool submit(const juce::String& prompt, codeact::Endpoint, bool newSession = false);
    void cancel();
    codeact::View poll();
    const std::vector<ConversationTurn>& conversation();
    std::uint64_t conversationRevision() const { return conversationRevision_; }
    void newConversation();
    const juce::String& selectedModel() const { return selectedModel_; }
    void setSelectedModel(const juce::String&);
    bool canApply() const;
    bool apply(juce::String& error);
    CapturedState capture() const;
    codeact::Snapshot snapshot() const { return capture().snapshot; }

    // The same fully validated transaction boundary used by apply(). Useful to
    // deterministic hosts/tests which prepare a proposal without a network.
    bool applyProposal(const CapturedState&, const std::vector<codeact::Change>&, juce::String& error);

private:
    void synchronize(const codeact::View&);
    void boundConversation();
    void setOutcome(const juce::String& status, const juce::String& error = {});
    juce::String outcomeContext() const;

    std::vector<ConversationTurn> conversation_;
    std::uint64_t conversationRevision_ = 0;
    std::uint64_t workerRevision_ = 0;
    bool awaitingResult_ = false;
    bool resetOnNextSubmit_ = true;
    juce::String selectedModel_;
    // Endpoint identity (including credentials) stays in memory only. Never
    // included in display history, snapshots, host context, or plugin state.
    std::optional<codeact::Endpoint> lastSubmittedEndpoint_;
    std::vector<juce::String> applicationOutcomes_;
    std::size_t outcomesSent_ = 0;
    Capture capture_;
    Commit commit_;
    codeact::Agent worker_;
    std::optional<CapturedState> submitted_;
    bool consumed_ = true;
    bool cancelled_ = false;
    juce::String error_;
};

codeact::Json agentAudioMeasurements(const AudioMeterSnapshot&, const juce::String& tap);
codeact::Json agentFxMeters(const FxTelemetry&, const juce::String& scope,
                           const juce::String& reverbScope = {});
codeact::Json agentMeterObservations(const codeact::Json& audio,
    const juce::Array<codeact::Json>& fx, const juce::Array<codeact::Json>& tracks = {});

juce::MemoryBlock captureAgentDocument(juce::AudioProcessor&);
codeact::Parameter agentParameter(const ParamInfo&, float value, const juce::String& prefix = {});
void appendAgentApvts(codeact::Snapshot&, juce::AudioProcessorValueTreeState&,
                      const ParamInfo*, std::size_t);
bool applyAgentApvts(juce::AudioProcessorValueTreeState&,
                     const std::vector<codeact::Change>&, juce::String& error);
std::unique_ptr<FableAgent> makeApvtsAgent(juce::AudioProcessor&,
    juce::AudioProcessorValueTreeState&, const ParamInfo*, std::size_t,
    const std::atomic<std::uint64_t>& generation,
    std::function<void(codeact::Snapshot&)> measurements = {});

// Compact, immutable factory-bank references for the standalone instrument
// agents. These guide recommendations but are never proposal targets.
codeact::Json agentPresetReferences(const juce::String& instrument,
                                    const juce::StringArray& names,
                                    int currentIndex);

// Compact technique cues distilled from public educational material. They are
// original FableSynth guidance, never third-party preset data or load targets.
codeact::Json agentTechniqueReferences(const juce::String& instrument);

} // namespace fable
