#pragma once
#include <juce_core/juce_core.h>
#include "JsonSyntax.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

namespace codeact {
using Clock = std::chrono::steady_clock;
using Json = juce::var;

[[noreturn]] inline void fail(const juce::String& message) {
    throw std::runtime_error(message.toStdString());
}
inline Json object(std::initializer_list<std::pair<juce::Identifier, Json>> fields = {}) {
    auto* result = new juce::DynamicObject();
    for (const auto& field : fields) result->setProperty(field.first, field.second);
    return Json(result);
}
inline Json array(std::initializer_list<Json> elements = {}) {
    juce::Array<Json> result;
    for (const auto& element : elements) result.add(element);
    return Json(result);
}
inline juce::String jsonText(const Json& value) { return juce::JSON::toString(value, true); }
inline void put(Json& value, const juce::Identifier& key, const Json& member) {
    if (auto* obj = value.getDynamicObject()) obj->setProperty(key, member);
    else fail("Expected a JSON object");
}
inline Json get(const Json& value, const juce::Identifier& key) {
    if (const auto* obj = value.getDynamicObject()) return obj->getProperty(key);
    return {};
}
inline bool has(const Json& value, const juce::Identifier& key) {
    if (const auto* obj = value.getDynamicObject()) return obj->hasProperty(key);
    return false;
}
inline bool isNumber(const Json& value) {
    return value.isInt() || value.isInt64() || value.isDouble();
}
// JUCE 8 JSON::parse accepts only arrays/objects and permits trailing data.
// Validate a single strict JSON value, then wrap it to handle primitives too.
inline Json parseJson(const juce::String& text, std::size_t maxBytes = 2 * 1024 * 1024) {
    if (text.getNumBytesAsUTF8() > maxBytes) fail("JSON byte limit exceeded");
    JsonSyntax::validate(text.toStdString());
    Json wrapped;
    const auto status = juce::JSON::parse("[" + text + "]", wrapped);
    if (status.failed()) fail("Invalid JSON: " + status.getErrorMessage());
    if (!wrapped.isArray() || wrapped.size() != 1) fail("Expected exactly one JSON value");
    return wrapped[0];
}

struct Limits {
    int maxModelCalls = 12;
    int maxCallsPerResponse = 8;
    int maxOutputTokens = 4096;
    int connectTimeoutMs = 10000;
    int requestTimeoutMs = 90000;
    int turnTimeoutMs = 240000;
    int jsTimeoutMs = 1500;
    int maxPromiseJobs = 10000;
    std::size_t jsHeapBytes = 16 * 1024 * 1024;
    std::size_t jsStackBytes = 256 * 1024;
    std::size_t maxCodeBytes = 32 * 1024;
    std::size_t maxToolOutputBytes = 16 * 1024;
    std::size_t maxRequestBytes = 256 * 1024;
    std::size_t maxResponseBytes = 2 * 1024 * 1024;
    std::size_t maxSseEventBytes = 256 * 1024;
    // Complete, model-visible activity transcript for one turn: streamed
    // assistant text, tool input/output, and provider diagnostics. This is
    // separate from model context and never enters a plugin state document.
    std::size_t maxTurnLogBytes = 32 * 1024 * 1024;
    void validate() const {
        if (maxModelCalls < 1 || maxModelCalls > 100 || maxCallsPerResponse < 1
            || maxCallsPerResponse > 32 || maxOutputTokens < 1 || connectTimeoutMs < 1
            || requestTimeoutMs < 1 || turnTimeoutMs < 1 || jsTimeoutMs < 1
            || maxPromiseJobs < 1 || jsHeapBytes < 1024 * 1024
            || jsStackBytes < 64 * 1024 || maxCodeBytes < 1 || maxToolOutputBytes < 1
            || maxRequestBytes < 1024 || maxResponseBytes < 1024 || maxSseEventBytes < 1024
            || maxTurnLogBytes < 64 * 1024 || maxTurnLogBytes > 64 * 1024 * 1024)
            fail("Invalid resource limits");
    }
};
struct StopState {
    std::atomic<bool> cancelled { false };
    const Clock::time_point deadline;
    explicit StopState(int timeoutMs) : deadline(Clock::now() + std::chrono::milliseconds(timeoutMs)) {}
    bool stopped() const noexcept {
        return cancelled.load(std::memory_order_relaxed) || Clock::now() >= deadline;
    }
    void check() const {
        if (cancelled.load(std::memory_order_relaxed)) fail("Cancelled");
        if (Clock::now() >= deadline) fail("Turn deadline exceeded");
    }
};
struct Endpoint {
    // A base URL, not the full chat/completions URL. No credential is sent to JS.
    juce::String baseUrl = "https://openrouter.ai/api/v1";
    juce::String model, apiKey;
    juce::StringPairArray headers; // e.g. HTTP-Referer / X-Title
    juce::String extraBodyJson = "{}";
    juce::String tokenLimitField = "max_tokens"; // or "max_completion_tokens"
    bool stream = true;
    // Omit by default for router compatibility. The runtime accepts a batch of
    // tool calls and always executes it sequentially, independent of this field.
    bool sendParallelToolCalls = false;
    bool allowLoopbackHttp = false;
};
inline bool sameEndpointContext(const Endpoint& a, const Endpoint& b) {
    return a.baseUrl == b.baseUrl && a.model == b.model && a.apiKey == b.apiKey
        && a.headers == b.headers && a.extraBodyJson == b.extraBodyJson
        && a.tokenLimitField == b.tokenLimitField && a.stream == b.stream
        && a.sendParallelToolCalls == b.sendParallelToolCalls
        && a.allowLoopbackHttp == b.allowLoopbackHttp;
}
struct Parameter {
    juce::String id, name, unit;
    double minimum = 0, maximum = 1, value = 0;
    // Physical-value metadata. step == 0 means continuous. For discrete
    // parameters choices[index] names the value minimum + index * step.
    double step = 0;
    std::vector<juce::String> choices;
};
inline void validateJsonData(const Json& value, int depth = 0) {
    if (depth > 32) fail("Observation JSON nesting limit exceeded");
    if (value.isVoid() || value.isBool() || value.isInt() || value.isInt64()) return;
    if (value.isDouble()) {
        if (!std::isfinite(static_cast<double>(value))) fail("Observation contains a non-finite number");
        return;
    }
    if (value.isString()) {
        if (value.toString().getNumBytesAsUTF8() > 64 * 1024) fail("Observation string is too large");
        return;
    }
    if (value.isArray()) {
        if (value.size() > 8192) fail("Observation array is too large");
        for (const auto& item : *value.getArray()) validateJsonData(item, depth + 1);
        return;
    }
    if (const auto* data = value.getDynamicObject()) {
        const auto& properties = data->getProperties();
        if (properties.size() > 8192) fail("Observation object is too large");
        for (int i = 0; i < properties.size(); ++i) {
            if (properties.getName(i).toString().length() > 256)
                fail("Observation property name is too large");
            validateJsonData(properties.getValueAt(i), depth + 1);
        }
        return;
    }
    fail("Observation contains a non-JSON value");
}
struct Snapshot {
    juce::String pluginName;
    std::vector<Parameter> parameters;
    Json audio = object({{"available", false}, {"reason", "Audio measurement unavailable"}});
    Json meters = object({{"available", false}, {"reason", "Meter data unavailable"}});
    void validate() const {
        if (parameters.size() > 8192 || pluginName.length() > 256) fail("Snapshot too large");
        std::set<juce::String> ids;
        for (const auto& p : parameters) {
            if (p.id.isEmpty() || p.id.length() > 128 || p.name.length() > 256
                || p.unit.length() > 64 || !ids.insert(p.id).second
                || !std::isfinite(p.minimum) || !std::isfinite(p.maximum)
                || !std::isfinite(p.value) || !std::isfinite(p.step) || p.step < 0
                || p.choices.size() > 512 || (!p.choices.empty() && p.step <= 0)
                || p.minimum > p.maximum
                || p.value < p.minimum || p.value > p.maximum)
                fail("Invalid parameter snapshot");
            for (const auto& choice : p.choices)
                if (choice.length() > 256) fail("Invalid parameter snapshot");
        }
        for (const auto* observation : { &audio, &meters }) {
            if (!observation->getDynamicObject()) fail("Snapshot observation must be a JSON object");
            validateJsonData(*observation);
            if (jsonText(*observation).getNumBytesAsUTF8() > 64 * 1024)
                fail("Snapshot observation exceeds 64 KiB");
        }
    }
    Json toJson() const {
        juce::Array<Json> values;
        for (const auto& p : parameters) {
            juce::Array<Json> choices;
            for (const auto& choice : p.choices) choices.add(choice);
            values.add(object({{"id", p.id}, {"name", p.name}, {"unit", p.unit},
                               {"min", p.minimum}, {"max", p.maximum}, {"value", p.value},
                               {"step", p.step}, {"choices", Json(choices)}}));
        }
        return object({{"plugin", pluginName}, {"parameters", Json(values)},
                       {"audio", audio.clone()}, {"meters", meters.clone()}});
    }
};
struct Change {
    juce::String id;
    double before = 0, after = 0;
    Json toJson() const { return object({{"id", id}, {"before", before}, {"after", after}}); }
};
struct JsResult {
    bool ok = false, runtimeReset = false;
    juce::String printed, valueJson = "null", error;
    std::vector<Change> changes;
    Json toJson() const {
        juce::Array<Json> proposed;
        for (const auto& c : changes) proposed.add(c.toJson());
        return object({{"ok", ok}, {"stdout", printed},
                       {"value", ok ? parseJson(valueJson) : Json()},
                       {"error", error}, {"runtimeReset", runtimeReset},
                       {"stagedChanges", Json(proposed)}, {"applied", false}});
    }
};
struct Turn {
    Endpoint endpoint;
    juce::String prompt;
    Snapshot snapshot;
    bool newSession = false;
    // Trusted status supplied by the plugin host (for example whether the last
    // proposal was applied or rejected). It is conversational context, not an
    // instruction and never comes from JavaScript or the model.
    juce::String hostContext;
};
struct TurnResult {
    bool ok = false;
    juce::String text, error;
    std::vector<Change> changes;
    int modelCalls = 0;
};
using TextSink = std::function<void(const juce::String&)>;
struct Progress {
    juce::String activity;
    juce::String textDelta;
    juce::String diagnostic;
    bool beginAssistantMessage = false;
};
using ProgressSink = std::function<void(const Progress&)>;
// Called after a structurally complete turn while Session can still roll its
// transcript and JavaScript state back. Throw to reject the completion.
using CompletionGate = std::function<void()>;
} // namespace codeact
