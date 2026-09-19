#include "FableAgent.h"
#include <algorithm>

namespace fable {
namespace {
bool onMessageThread() {
    const auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    return manager != nullptr && manager->isThisTheMessageThread();
}

// The canonical descriptors predate units. These suffix conventions are shared
// by all Fable engines; values remain physical (seconds, Hz, dB), never host
// normalised values. Dimensionless amounts are explicitly labelled as such.
juce::String parameterUnit(const ParamInfo& d) {
    if (d.kind == Kind::Bool) return "boolean";
    if (d.kind == Kind::Enum) return "choice index";
    const juce::String id(d.pid);
    const auto leaf = id.containsChar('.') ? id.fromLastOccurrenceOf(".", false, false) : id;
    if (leaf == "bpm") return "BPM";
    if (leaf == "oct") return "octaves";
    if (leaf == "fine") return "cents";
    if (leaf == "semi" || leaf == "tune" || id.endsWith("penv.amt")) return "semitones";
    if (leaf == "root") return "MIDI note";
    if (leaf == "unison") return "voices";
    if (leaf == "cutoff" || leaf == "cut" || leaf.endsWith("freq") || leaf == "rate") return "Hz";
    if (id.contains("fx.eq.") && (leaf == "low" || leaf == "mid" || leaf == "mid2" || leaf == "high")) return "dB";
    if (id.contains("fx.comp.") && (leaf == "thr" || leaf == "gain")) return "dB";
    if (id.endsWith("fx.ott.time")) return "multiplier";
    if (leaf == "time" || leaf == "glide" || leaf == "rise" || leaf == "att"
        || leaf == "dec" || leaf == "rel" || leaf == "hold"
        || (id.contains("env") && (leaf == "a" || leaf == "d" || leaf == "r"))) return "seconds";
    return "ratio";
}

bool sameSnapshot(const codeact::Snapshot& a, const codeact::Snapshot& b) {
    if (a.pluginName != b.pluginName || a.parameters.size() != b.parameters.size()) return false;
    for (std::size_t i = 0; i < a.parameters.size(); ++i) {
        const auto& x = a.parameters[i]; const auto& y = b.parameters[i];
        if (x.id != y.id || x.name != y.name || x.unit != y.unit || x.value != y.value
            || x.minimum != y.minimum || x.maximum != y.maximum || x.step != y.step
            || x.choices != y.choices) return false;
    }
    return true;
}
}

FableAgent::FableAgent(Capture capture, Commit commit,
                     std::unique_ptr<codeact::ChatTransport> transport, codeact::Limits limits)
    : capture_(std::move(capture)), commit_(std::move(commit)), worker_(limits, std::move(transport)) {}
FableAgent::~FableAgent() { worker_.shutdown(); }

FableAgent::CapturedState FableAgent::capture() const {
    if (!onMessageThread()) codeact::fail("Capture parameters on the message thread");
    auto state = capture_();
    state.snapshot.validate();
    return state;
}

namespace {
juce::String boundedText(juce::String text, std::size_t bytes) {
    while (text.getNumBytesAsUTF8() > bytes) {
        const auto length = static_cast<int>(static_cast<std::uint64_t>(text.length()) * bytes
                                             / text.getNumBytesAsUTF8());
        text = text.substring(0, length);
    }
    return text;
}
}

void FableAgent::boundConversation() {
    auto bytes = [](const ConversationTurn& turn) {
        auto size = turn.prompt.getNumBytesAsUTF8() + turn.assistant.getNumBytesAsUTF8()
                  + turn.activityLog.getNumBytesAsUTF8() + turn.error.getNumBytesAsUTF8()
                  + turn.status.getNumBytesAsUTF8();
        for (const auto& change : turn.changes) size += change.id.getNumBytesAsUTF8() + 2 * sizeof(double);
        return size;
    };
    std::size_t total = 0;
    for (const auto& turn : conversation_) total += bytes(turn);
    while (conversation_.size() > 32 || (total > 32 * 1024 * 1024 && conversation_.size() > 1)) {
        total -= bytes(conversation_.front());
        conversation_.erase(conversation_.begin());
    }
}

void FableAgent::synchronize(const codeact::View& view) {
    if (!awaitingResult_ || conversation_.empty() || workerRevision_ == view.revision) return;
    workerRevision_ = view.revision;
    auto& turn = conversation_.back();
    if (view.trace) {
        while (turn.activityLogChunks < view.trace->size())
            turn.activityLog += (*view.trace)[turn.activityLogChunks++];
    }
    turn.assistant = view.text;
    if (!view.busy && view.result) {
        turn.assistant = view.result->text;
        turn.error = view.result->error;
        turn.changeCount = view.result->changes.size();
        turn.changes.assign(view.result->changes.begin(), view.result->changes.begin()
                            + std::min<std::size_t>(64, view.result->changes.size()));
        turn.status = cancelled_ ? "cancelled" : !view.result->ok ? "failed"
                      : turn.changes.empty() ? "complete" : "pending";
        awaitingResult_ = false;
        if (view.result->ok && !cancelled_) {
            const auto delivered = std::min(outcomesSent_, applicationOutcomes_.size());
            applicationOutcomes_.erase(applicationOutcomes_.begin(), applicationOutcomes_.begin() + delivered);
        }
    }
    ++conversationRevision_;
    boundConversation();
}

const std::vector<FableAgent::ConversationTurn>& FableAgent::conversation() {
    poll();
    return conversation_;
}

void FableAgent::setSelectedModel(const juce::String& model) {
    jassert(onMessageThread());
    const auto value = boundedText(model.trim(), 256);
    if (value != selectedModel_) { selectedModel_ = value; ++conversationRevision_; }
}

void FableAgent::newConversation() {
    jassert(onMessageThread());
    if (worker_.poll().busy) worker_.cancel();
    awaitingResult_ = false;
    conversation_.clear();
    applicationOutcomes_.clear();
    outcomesSent_ = 0;
    submitted_.reset();
    consumed_ = true;
    cancelled_ = true;
    resetOnNextSubmit_ = true;
    lastSubmittedEndpoint_.reset();
    error_.clear();
    ++conversationRevision_;
}

void FableAgent::setOutcome(const juce::String& status, const juce::String& error) {
    if (conversation_.empty()) return;
    auto& turn = conversation_.back();
    turn.status = status;
    turn.error = error;
    if (error.isNotEmpty()) turn.activityLog += "\nAPPLICATION ERROR\n" + error + "\n";
    applicationOutcomes_.push_back(codeact::jsonText(codeact::object({
        { "status", status }, { "applied", status == "applied" },
        { "error", boundedText(error, 256) },
        { "changeCount", static_cast<int>(turn.changeCount) }
    })));
    while (applicationOutcomes_.size() > 4) applicationOutcomes_.erase(applicationOutcomes_.begin());
    ++conversationRevision_;
    boundConversation();
}

juce::String FableAgent::outcomeContext() const {
    if (conversation_.empty()) return {};
    juce::Array<codeact::Json> outcomes;
    for (const auto& outcome : applicationOutcomes_) outcomes.add(codeact::parseJson(outcome));
    const auto& previous = conversation_.back();
    return boundedText(codeact::jsonText(codeact::object({
        { "previousPrompt", boundedText(previous.prompt, 1024) },
        { "previousTurnStatus", previous.status },
        { "previousTurnError", boundedText(previous.error, 256) },
        { "applicationOutcomes", codeact::Json(outcomes) },
        { "instruction", "The current host.snapshot() is authoritative. Only status applied means a proposal was committed; other statuses made no parameter changes." }
    })), 16 * 1024);
}

bool FableAgent::submit(const juce::String& prompt, codeact::Endpoint endpoint, bool newSession) {
    if (!onMessageThread() || poll().busy) return false;
    const bool endpointChanged = lastSubmittedEndpoint_
        && !codeact::sameEndpointContext(*lastSubmittedEndpoint_, endpoint);
    if (newSession || endpointChanged) newConversation();
    try {
        auto state = capture();
        if (!conversation_.empty() && conversation_.back().status == "pending") {
            consumed_ = true;
            setOutcome("not-applied", "A follow-up was submitted before this proposal was applied.");
        }
        codeact::Turn turn { endpoint, prompt, state.snapshot, resetOnNextSubmit_ };
        turn.hostContext = resetOnNextSubmit_ ? juce::String() : outcomeContext();
        if (!worker_.submit(std::move(turn))) return false;
        outcomesSent_ = applicationOutcomes_.size();
        setSelectedModel(endpoint.model);
        lastSubmittedEndpoint_ = endpoint;
        conversation_.push_back({ boundedText(prompt, 16 * 1024), {}, {}, "running", {}, 0, {}, 0 });
        ++conversationRevision_;
        boundConversation();
        workerRevision_ = 0;
        awaitingResult_ = true;
        resetOnNextSubmit_ = false;
        submitted_ = std::move(state);
        consumed_ = false;
        cancelled_ = false;
        error_.clear();
        return true;
    } catch (const std::exception& e) {
        error_ = e.what();
        if (!conversation_.empty() && conversation_.back().status == "pending")
            setOutcome("not-applied", "The next request could not capture valid state; this proposal was discarded.");
        conversation_.push_back({ boundedText(prompt, 16 * 1024), {}, {}, "failed", error_, 0, {}, 0 });
        consumed_ = true;
        ++conversationRevision_;
        boundConversation();
        return false;
    }
}

void FableAgent::cancel() {
    jassert(onMessageThread());
    poll();
    cancelled_ = true;
    consumed_ = true;
    worker_.cancel();
    if (awaitingResult_ || (!conversation_.empty() && conversation_.back().status == "pending"))
        setOutcome("cancelled");
}
codeact::View FableAgent::poll() {
    jassert(onMessageThread());
    auto view = worker_.poll();
    synchronize(view);
    if (error_.isNotEmpty()) view.diagnostics = error_;
    return view;
}
bool FableAgent::canApply() const {
    const auto view = worker_.poll();
    return !view.busy && !consumed_ && !cancelled_ && submitted_.has_value()
        && view.result && view.result->ok && !view.result->changes.empty();
}
bool FableAgent::apply(juce::String& error) {
    if (!onMessageThread()) { error = "Apply changes on the message thread"; return false; }
    poll();
    if (!canApply()) { error = "No unapplied successful proposal"; return false; }
    const auto result = worker_.poll().result;
    // Consumed on every apply attempt. Failed/stale proposals require a new
    // request; editor reopening cannot resurrect a completed transaction.
    consumed_ = true;
    const bool applied = applyProposal(*submitted_, result->changes, error);
    setOutcome(applied ? "applied" : "rejected", error);
    return applied;
}

bool FableAgent::applyProposal(const CapturedState& before,
                             const std::vector<codeact::Change>& changes, juce::String& error) {
    error.clear();
    if (!onMessageThread()) { error = "Apply changes on the message thread"; return false; }
    try {
        const auto now = capture();
        if (before.generation != now.generation || before.document != now.document
            || !sameSnapshot(before.snapshot, now.snapshot))
            codeact::fail("Sound or session changed since this request. Submit again.");
        if (changes.empty()) codeact::fail("Proposal contains no changes");
        std::map<juce::String, const codeact::Parameter*> byId;
        for (const auto& p : now.snapshot.parameters) byId.emplace(p.id, &p);
        std::set<juce::String> seen;
        for (const auto& c : changes) {
            const auto it = byId.find(c.id);
            if (it == byId.end() || !seen.insert(c.id).second)
                codeact::fail("Unknown or duplicate parameter: " + c.id);
            const auto& p = *it->second;
            if (!std::isfinite(c.before) || !std::isfinite(c.after) || c.before != p.value)
                codeact::fail("Invalid or stale value: " + c.id);
            if (c.after < p.minimum || c.after > p.maximum)
                codeact::fail("Value outside parameter range: " + c.id);
            if (p.step > 0) {
                const auto steps = (c.after - p.minimum) / p.step;
                if (std::abs(steps - std::round(steps)) > 1.0e-7)
                    codeact::fail("Value must use a discrete step: " + c.id);
            }
        }
        return commit_(changes, error);
    } catch (const std::exception& e) { error = e.what(); return false; }
}

codeact::Json agentAudioMeasurements(const AudioMeterSnapshot& meter, const juce::String& tap) {
    auto result = codeact::object({
        { "available", meter.available && meter.coherent }, { "coherent", meter.coherent },
        { "tap", tap }, { "frozenAtTurnStart", true },
        { "snapshotCapturedAtMs", juce::Time::currentTimeMillis() },
        { "audioWindowAgeMs", codeact::Json() },
        { "freshness", "Latest completed audio window; wall-clock age unknown. Capture time is retrieval time, not sample time." },
        { "generation", static_cast<juce::int64>(meter.generation) },
        { "serial", static_cast<juce::int64>(meter.serial) },
        { "sampleRateHz", meter.sampleRate }, { "channels", meter.channelCount },
        { "windowFrames", static_cast<juce::int64>(meter.windowFrames) },
        { "windowSeconds", meter.sampleRate > 0 ? meter.windowFrames / meter.sampleRate : 0.0 },
        { "windowEndFrame", static_cast<juce::int64>(meter.windowEndFrame) },
        { "dbfsFloor", -120 },
        { "definition", "RMS and sample peak of the actual output, not true peak or perceived loudness. Full-scale counts include finite samples with abs(value) >= 1, counted across output channels. Non-finite input samples are counted and treated as zero for level/DC statistics. Measurements are not listening." }
    });
    if (!meter.available || !meter.coherent) {
        codeact::put(result, "reason", !meter.coherent ? "A coherent audio window was not available during capture."
                                                      : "No completed audio window since prepare, or audio resources were released.");
        return result;
    }
    auto channel = [](const auto& value) {
        return codeact::object({ { "rmsLinear", value.rms }, { "samplePeakLinear", value.samplePeak },
            { "rmsDbfs", value.rmsDbfs }, { "samplePeakDbfs", value.samplePeakDbfs }, { "dcLinear", value.dc } });
    };
    codeact::put(result, "left", channel(meter.left));
    codeact::put(result, "right", meter.channelCount > 1 ? channel(meter.right) : codeact::Json());
    codeact::put(result, "combined", channel(meter.combined));
    codeact::put(result, "stereoAvailable", meter.channelCount > 1);
    codeact::put(result, "stereoCorrelation", meter.channelCount > 1 ? codeact::Json(meter.stereoCorrelation) : codeact::Json());
    codeact::put(result, "fullScaleSampleCount", static_cast<juce::int64>(meter.fullScaleSampleCount));
    codeact::put(result, "nonFiniteSampleCount", static_cast<juce::int64>(meter.nonFiniteSampleCount));
    return result;
}

codeact::Json agentFxMeters(const FxTelemetry& telemetry, const juce::String& scope,
                            const juce::String& reverbScope) {
    static const char* names[] = {
        "ottInputRmsDbfs", "ottOutputRmsDbfs", "compressorInputRmsDbfs", "compressorOutputRmsDbfs",
        "echoLeftRmsDbfs", "echoRightRmsDbfs", "reverbLeftRmsDbfs", "reverbRightRmsDbfs",
        "ottLowEnvelopeDbfs", "ottMidEnvelopeDbfs", "ottHighEnvelopeDbfs",
        "ottLowGainDb", "ottMidGainDb", "ottHighGainDb", "ottAutoGainDb", "compressorAutoGainDb",
        "compressorReductionDb", "delayTimeSeconds", "delayLeftDriftSeconds", "delayRightDriftSeconds", "reverbCorrelation"
    };
    static_assert(std::size(names) == FxTelemetry::count);
    auto values = codeact::object();
    for (int i = 0; i < FxTelemetry::count; ++i)
        codeact::put(values, names[i], std::isfinite(telemetry.values[(size_t)i])
                     ? codeact::Json(telemetry.values[(size_t)i]) : codeact::Json());
    return codeact::object({
        { "scope", scope }, { "available", telemetry.seconds > 0 || telemetry.reverbSeconds > 0 },
        { "serial", static_cast<juce::int64>(telemetry.serial) },
        { "processedSeconds", telemetry.seconds }, { "sampleRateHz", telemetry.sampleRate },
        { "approximateWindowSeconds", 1.0 / 30.0 }, { "dbfsFloor", -90 },
        { "reverbScope", reverbScope.isEmpty() ? scope : reverbScope },
        { "reverbSerial", static_cast<juce::int64>(telemetry.reverbSerial) },
        { "reverbProcessedSeconds", telemetry.reverbSeconds },
        { "values", values },
        { "note", "FX taps, not final output. Bypassed/inactive effects may retain earlier/default readings; inspect corresponding on/off parameters. Atomic fields may span adjacent telemetry publications." }
    });
}

codeact::Json agentMeterObservations(const codeact::Json& audio,
    const juce::Array<codeact::Json>& fx, const juce::Array<codeact::Json>& tracks) {
    juce::Array<codeact::Json> gatedFx;
    for (const auto& effect : fx) {
        auto copy = effect.clone();
        codeact::put(copy, "available", static_cast<bool>(codeact::get(audio, "available"))
                                       && static_cast<bool>(codeact::get(effect, "available")));
        gatedFx.add(std::move(copy));
    }
    return codeact::object({ { "available", codeact::get(audio, "available") },
        { "frozenAtTurnStart", true }, { "snapshotCapturedAtMs", codeact::get(audio, "snapshotCapturedAtMs") },
        { "audioWindowAgeMs", codeact::Json() },
        { "freshness", "Frozen telemetry read at turn capture; audio-window wall-clock age is unknown. No live polling or listening." },
        { "fx", codeact::Json(gatedFx) }, { "tracks", codeact::Json(tracks) } });
}

juce::MemoryBlock captureAgentDocument(juce::AudioProcessor& processor) {
    juce::MemoryBlock document;
    processor.getStateInformation(document);
    if (auto xml = juce::AudioProcessor::getXmlFromBinary(document.getData(), (int)document.getSize())) {
        // Editor selection is persisted for convenience but cannot change the
        // sound. It should not invalidate a proposal while reviewing controls.
        for (const auto* childName : { "NOTESEQ", "BASS", "DRUM" })
            if (auto* child = xml->getChildByName(childName)) {
                child->removeAttribute("editPattern");
                child->removeAttribute("selectedPad");
            }
        juce::AudioProcessor::copyXmlToBinary(*xml, document);
    }
    return document;
}

codeact::Parameter agentParameter(const ParamInfo& d, float value, const juce::String& prefix) {
    codeact::Parameter p;
    p.id = prefix + d.pid;
    p.name = p.id.replaceCharacter('.', ' ').toUpperCase() + " (" + d.label + ")";
    p.unit = parameterUnit(d);
    p.minimum = d.min; p.maximum = d.max; p.value = value;
    p.step = d.curve == Curve::Int || d.kind != Kind::Float ? 1.0 : 0.0;
    if (d.kind == Kind::Bool) p.choices = { "OFF", "ON" };
    else if (d.options) for (const auto& option : *d.options) p.choices.emplace_back(option);
    return p;
}
void appendAgentApvts(codeact::Snapshot& snapshot, juce::AudioProcessorValueTreeState& apvts,
                      const ParamInfo* info, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        const auto* raw = apvts.getRawParameterValue(info[i].pid);
        if (!raw) codeact::fail("Missing declared parameter: " + juce::String(info[i].pid));
        snapshot.parameters.push_back(agentParameter(info[i], raw->load()));
    }
}
bool applyAgentApvts(juce::AudioProcessorValueTreeState& apvts,
                     const std::vector<codeact::Change>& changes, juce::String& error) {
    std::vector<std::pair<juce::RangedAudioParameter*, float>> writes;
    writes.reserve(changes.size());
    for (const auto& c : changes) {
        auto* p = apvts.getParameter(c.id);
        if (!p) { error = "Parameter is no longer available: " + c.id; return false; }
        writes.emplace_back(p, p->convertTo0to1(static_cast<float>(c.after)));
    }
    for (const auto& write : writes) {
        write.first->beginChangeGesture();
        write.first->setValueNotifyingHost(write.second);
        write.first->endChangeGesture();
    }
    return true;
}
std::unique_ptr<FableAgent> makeApvtsAgent(juce::AudioProcessor& processor,
    juce::AudioProcessorValueTreeState& apvts, const ParamInfo* info, std::size_t size,
    const std::atomic<std::uint64_t>& generation,
    std::function<void(codeact::Snapshot&)> measurements) {
    return std::make_unique<FableAgent>([&processor, &apvts, info, size, &generation, measurements = std::move(measurements)] {
        FableAgent::CapturedState state;
        state.snapshot.pluginName = processor.getName();
        state.generation = generation.load();
        appendAgentApvts(state.snapshot, apvts, info, size);
        state.document = captureAgentDocument(processor);
        if (measurements) measurements(state.snapshot);
        return state;
    }, [&apvts](const auto& changes, auto& error) { return applyAgentApvts(apvts, changes, error); });
}
} // namespace fable
