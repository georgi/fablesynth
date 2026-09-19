#include "OpenAIClient.h"
#include "SseFramer.h"
#include <algorithm>
#include <array>

namespace codeact {
namespace {
void appendString(Json& target, const juce::Identifier& field, const Json& fragment) {
    if (fragment.isVoid()) return;
    if (!fragment.isString()) fail("Expected a string delta: " + field.toString());
    put(target, field, get(target, field).toString() + fragment.toString());
}
void mergeMetadata(Json& target, const Json& fragment) {
    if (fragment.isVoid()) return;
    if (target.isVoid()) { target = fragment.clone(); return; }
    const auto* incoming = fragment.getDynamicObject();
    if (incoming && target.getDynamicObject()) {
        const auto& properties = incoming->getProperties();
        for (int i = 0; i < properties.size(); ++i) {
            const auto key = properties.getName(i);
            auto member = get(target, key);
            mergeMetadata(member, properties.getValueAt(i));
            put(target, key, member);
        }
    } else if (jsonText(target) != jsonText(fragment)) {
        // Never silently corrupt an opaque provider signature. Such an endpoint
        // can use stream=false, which preserves the complete assistant object.
        fail("Unsupported fragmented provider metadata; set Endpoint::stream=false");
    }
}
void checkEndpoint(const Endpoint& e) {
    const auto url = juce::URL(e.baseUrl);
    if (!url.isWellFormed() || e.baseUrl.containsAnyOf("\r\n?#@")
        || e.baseUrl.length() > 2048 || e.model.isEmpty() || e.model.length() > 256)
        fail("Invalid endpoint base URL or model ID");
    const auto domain = url.getDomain().toLowerCase();
    const bool loopback = domain == "localhost" || domain == "127.0.0.1"
                       || domain == "[::1]" || domain == "::1";
    if (!e.baseUrl.startsWithIgnoreCase("https://")
        && !(e.allowLoopbackHttp && loopback && e.baseUrl.startsWithIgnoreCase("http://")))
        fail("Use HTTPS, or explicitly allow HTTP for a literal loopback endpoint");
    if (e.apiKey.containsAnyOf("\r\n") || e.apiKey.length() > 8192)
        fail("Invalid API key header");
    if (e.tokenLimitField != "max_tokens" && e.tokenLimitField != "max_completion_tokens")
        fail("tokenLimitField must be max_tokens or max_completion_tokens");
}
juce::String requestHeaders(const Endpoint& e) {
    juce::String result = "Content-Type: application/json\r\nAccept: ";
    result += e.stream ? "text/event-stream\r\n" : "application/json\r\n";
    if (e.apiKey.isNotEmpty()) result += "Authorization: Bearer " + e.apiKey + "\r\n";
    if (e.headers.size() > 32) fail("Too many custom headers");
    const auto& keys = e.headers.getAllKeys();
    const auto& values = e.headers.getAllValues();
    for (int i = 0; i < keys.size(); ++i) {
        const auto& key = keys[i]; const auto& value = values[i];
        if (key.isEmpty() || key.length() > 128
            || key.containsAnyOf("\r\n: \t") || value.containsAnyOf("\r\n") || value.length() > 4096)
            fail("Invalid custom header");
        for (const auto* reserved : {"authorization", "host", "content-type", "content-length",
                                    "transfer-encoding", "connection", "accept"})
            if (key.equalsIgnoreCase(reserved)) fail("Reserved header: " + key);
        result += key + ": " + value + "\r\n";
    }
    return result;
}
// Separate watchdog: a flag checked only by the reader cannot interrupt a
// blocking read. Both the stream and StopState outlive this joined watchdog.
class RequestWatchdog final : private juce::Thread {
public:
    RequestWatchdog(juce::WebInputStream& s, const StopState& token, int timeoutMs)
        : Thread("CodeAct HTTP deadline"), stream(s), stop(token),
          deadline(std::min(token.deadline, Clock::now() + std::chrono::milliseconds(timeoutMs))) {
        if (!startThread()) fail("Cannot start HTTP watchdog");
    }
    ~RequestWatchdog() override {
        signalThreadShouldExit(); notify(); waitForThreadToExit(-1);
    }
    void check() const {
        stop.check();
        if (Clock::now() >= deadline) fail("HTTP request deadline exceeded");
    }
private:
    void run() override {
        while (!threadShouldExit()) {
            if (stop.stopped() || Clock::now() >= deadline) { stream.cancel(); return; }
            wait(20);
        }
    }
    juce::WebInputStream& stream;
    const StopState& stop;
    Clock::time_point deadline;
};
juce::String decodeUtf8(const std::string& bytes) {
    if (!juce::CharPointer_UTF8::isValidString(bytes.data(), static_cast<int>(bytes.size())))
        fail("Endpoint returned invalid UTF-8");
    return juce::String::fromUTF8(bytes.data(), static_cast<int>(bytes.size()));
}
}
Json executeJsTool() {
    return object({{"type", "function"}, {"function", object({
        {"name", "execute_js"},
        {"description", "Execute JavaScript in the persistent bounded plugin workspace. "
                        "Use print(), memory, host.snapshot(), and host.proposeParameters(). "
                        "Filter or page large snapshots inside JavaScript and return only a small batch. "
                        "Return a JSON-serializable value. Proposals are NOT applied."},
        {"parameters", object({{"type", "object"},
            {"properties", object({{"code", object({{"type", "string"}})}})},
            {"required", array({"code"})}, {"additionalProperties", false}})}
    })}});
}
Json makeRequest(const Endpoint& e, const juce::Array<Json>& messages, const Limits& limits) {
    checkEndpoint(e);
    auto body = parseJson(e.extraBodyJson, 16 * 1024);
    if (!body.getDynamicObject()) fail("extraBodyJson must be a JSON object");
    for (const auto* reserved : {"model", "messages", "tools", "stream", "tool_choice",
                                "parallel_tool_calls", "n", "max_tokens", "max_completion_tokens"})
        if (has(body, reserved)) fail("Reserved request body field: " + juce::String(reserved));
    put(body, "model", e.model);
    put(body, "messages", Json(messages));
    put(body, "tools", array({executeJsTool()}));
    put(body, "tool_choice", "auto");
    put(body, "stream", e.stream);
    if (e.sendParallelToolCalls) put(body, "parallel_tool_calls", false);
    put(body, juce::Identifier(e.tokenLimitField), limits.maxOutputTokens);
    return body;
}
void validateCompletion(const Completion& c, const Limits& limits) {
    if (!c.message.getDynamicObject() || get(c.message, "role").toString() != "assistant")
        fail("Expected an assistant message");
    const auto content = get(c.message, "content");
    if (!content.isVoid() && !content.isString()) fail("Only text assistant content is supported");
    if (has(c.message, "function_call")) fail("Legacy function_call is unsupported; use tools/tool_calls");
    const auto calls = get(c.message, "tool_calls");
    if (!calls.isVoid() && !calls.isArray()) fail("tool_calls must be an array");
    const auto count = calls.isArray() ? calls.size() : 0;
    if (count > limits.maxCallsPerResponse) fail("Too many tool calls in one response");
    if (c.finishReason != (count > 0 ? "tool_calls" : "stop"))
        fail("Incomplete or unsuccessful completion: " + c.finishReason);
    std::set<juce::String> ids;
    for (int i = 0; i < count; ++i) {
        const auto call = calls[i];
        const auto id = get(call, "id"), type = get(call, "type"), fn = get(call, "function");
        if (!id.isString() || id.toString().isEmpty() || id.toString().length() > 256
            || !ids.insert(id.toString()).second || type.toString() != "function"
            || !fn.getDynamicObject() || !get(fn, "name").isString()
            || get(fn, "name").toString().isEmpty() || get(fn, "name").toString().length() > 128
            || !get(fn, "arguments").isString()
            || get(fn, "arguments").toString().getNumBytesAsUTF8() > limits.maxCodeBytes * 6 + 1024)
            fail("Invalid tool call envelope");
    }
}
ChatStreamAssembler::ChatStreamAssembler(const Limits& l, TextSink sink)
    : limits(l), onText(std::move(sink)) {}
void ChatStreamAssembler::feed(const juce::String& event) {
    if (sawDone) fail("SSE data after [DONE]");
    bytesSeen += event.getNumBytesAsUTF8();
    if (bytesSeen > limits.maxResponseBytes) fail("Stream response byte limit exceeded");
    if (event == "[DONE]") { sawDone = true; return; }
    const auto chunk = parseJson(event, limits.maxSseEventBytes);
    if (has(chunk, "error")) fail("Provider reported a streaming error: " + jsonText(get(chunk, "error")).substring(0, 2048));
    const auto choices = get(chunk, "choices");
    if (!choices.isArray()) fail("Missing streaming choices array");
    if (choices.size() == 0) return; // OpenAI's usage-only accounting frame.
    if (choices.size() != 1) fail("Only one completion choice is supported");
    const auto choice = choices[0];
    if (has(choice, "index") && (!get(choice, "index").isInt() || static_cast<int>(get(choice, "index")) != 0))
        fail("Unexpected completion choice index");
    const auto delta = get(choice, "delta");
    if (!delta.getDynamicObject()) fail("Invalid streaming delta");
    if (has(delta, "function_call")) fail("Legacy function_call delta is unsupported");
    if (has(delta, "role") && get(delta, "role").toString() != "assistant") fail("Invalid streaming role");
    for (const auto* key : {"content", "refusal", "reasoning", "reasoning_content"}) {
        const auto fragment = get(delta, key);
        if (!finishReason.isEmpty() && !fragment.isVoid() && fragment.toString().isNotEmpty())
            fail("Content arrived after the completion finished");
        appendString(message, key, fragment);
        if (juce::String(key) == "content" && fragment.isString() && fragment.toString().isNotEmpty() && onText)
            onText(fragment.toString());
    }
    if (has(delta, "reasoning_details") && !get(delta, "reasoning_details").isVoid()) {
        if (!finishReason.isEmpty()) fail("Reasoning metadata arrived after completion");
        const auto fragments = get(delta, "reasoning_details");
        if (!fragments.isArray()) fail("Invalid reasoning_details");
        auto existing = get(message, "reasoning_details");
        if (existing.isVoid()) existing = array();
        // OpenRouter documents concatenating these blocks in received order.
        // Keep every opaque block, signature, index, and format unmodified.
        for (const auto& detail : *fragments.getArray()) existing.append(detail.clone());
        put(message, "reasoning_details", existing);
    }
    const auto toolDeltas = get(delta, "tool_calls");
    if (!toolDeltas.isVoid()) {
        if (!toolDeltas.isArray()) fail("Invalid tool_calls delta");
        if (!finishReason.isEmpty() && toolDeltas.size() > 0) fail("Tool delta after completion");
        for (const auto& part : *toolDeltas.getArray()) {
            const auto indexValue = get(part, "index");
            if (!indexValue.isInt()) fail("Missing tool-call index");
            const auto index = static_cast<int>(indexValue);
            if (index < 0 || index >= limits.maxCallsPerResponse) fail("Invalid tool-call index");
            auto found = calls.find(index);
            if (found == calls.end())
                found = calls.emplace(index, object({{"id", ""}, {"type", "function"},
                    {"function", object({{"name", ""}, {"arguments", ""}})}})).first;
            auto& call = found->second;
            if (has(part, "type") && get(part, "type").toString() != "function") fail("Invalid tool type");
            appendString(call, "id", get(part, "id"));
            const auto fnDelta = get(part, "function");
            if (!fnDelta.isVoid()) {
                if (!fnDelta.getDynamicObject()) fail("Invalid function delta");
                auto fn = get(call, "function");
                appendString(fn, "name", get(fnDelta, "name"));
                appendString(fn, "arguments", get(fnDelta, "arguments"));
                put(call, "function", fn);
            }
            if (has(part, "extra_content")) {
                auto metadata = get(call, "extra_content");
                mergeMetadata(metadata, get(part, "extra_content"));
                put(call, "extra_content", metadata);
            }
            const auto* fields = part.getDynamicObject();
            if (!fields) fail("Invalid tool fragment");
            const auto& props = fields->getProperties();
            for (int i = 0; i < props.size(); ++i) {
                const auto key = props.getName(i).toString();
                if (key != "index" && key != "id" && key != "type" && key != "function" && key != "extra_content")
                    fail("Unknown tool delta field; use non-streaming mode: " + key);
            }
        }
    }
    // Preserve provider extensions instead of silently dropping signatures.
    const auto& fields = delta.getDynamicObject()->getProperties();
    for (int i = 0; i < fields.size(); ++i) {
        const auto key = fields.getName(i);
        const auto k = key.toString();
        if (k == "role" || k == "content" || k == "refusal" || k == "reasoning" || k == "reasoning_content"
            || k == "reasoning_details" || k == "tool_calls") continue;
        auto metadata = get(message, key);
        mergeMetadata(metadata, fields.getValueAt(i)); put(message, key, metadata);
    }
    const auto reason = get(choice, "finish_reason");
    if (!reason.isVoid()) {
        if (!reason.isString() || reason.toString().isEmpty()) fail("Invalid finish_reason");
        if (finishReason.isNotEmpty() && finishReason != reason.toString()) fail("Conflicting finish reasons");
        finishReason = reason.toString(); // A repeated usage-frame reason is valid.
    }
}
Completion ChatStreamAssembler::finish() const {
    if (!sawDone) fail("Truncated SSE stream: missing [DONE]");
    auto result = message.clone();
    if (!calls.empty()) {
        juce::Array<Json> sorted;
        int expected = 0;
        for (const auto& item : calls) {
            if (item.first != expected++) fail("Non-contiguous tool-call indices");
            sorted.add(item.second.clone());
        }
        put(result, "tool_calls", Json(sorted));
    }
    Completion completion{ result, finishReason };
    validateCompletion(completion, limits);
    return completion;
}
Completion OpenAIClient::complete(const Endpoint& endpoint, const juce::Array<Json>& messages,
                                  const Limits& limits, const StopState& stop, const TextSink& onText) {
    stop.check();
    const auto payload = jsonText(makeRequest(endpoint, messages, limits));
    if (payload.getNumBytesAsUTF8() > limits.maxRequestBytes)
        fail("Conversation/request limit exceeded. Start a new session; history was not partially truncated.");
    auto base = endpoint.baseUrl;
    while (base.endsWithChar('/')) base = base.dropLastCharacters(1);
    juce::WebInputStream stream(juce::URL(base + "/chat/completions").withPOSTData(payload), true);
    stream.withCustomRequestCommand("POST")
          .withExtraHeaders(requestHeaders(endpoint))
          .withConnectionTimeout(limits.connectTimeoutMs)
          .withNumRedirectsToFollow(0); // Do not forward bearer credentials through redirects.
    RequestWatchdog watchdog(stream, stop, limits.requestTimeoutMs);
    const auto connected = stream.connect(nullptr);
    watchdog.check();
    const int status = stream.getStatusCode();
    if (status < 200 || status >= 300) {
        // Keep the complete provider error body within the ordinary HTTP
        // response limit so the native activity log can explain routing and
        // validation failures. The configured credential is always redacted.
        juce::String detail;
        std::string bytes;
        std::array<char, 4096> buffer {};
        while (connected) {
            const int count = stream.read(buffer.data(), static_cast<int>(buffer.size()));
            watchdog.check();
            if (count <= 0) break;
            if (bytes.size() + static_cast<std::size_t>(count) > limits.maxResponseBytes) {
                detail = "Provider error body exceeded the configured response limit.";
                break;
            }
            bytes.append(buffer.data(), static_cast<std::size_t>(count));
        }
        if (!bytes.empty()) detail = decodeUtf8(bytes);
        if (endpoint.apiKey.isNotEmpty()) detail = detail.replace(endpoint.apiKey, "[redacted]");
        fail("HTTP " + juce::String(status) + " from the model endpoint"
             + (detail.isEmpty() ? " (check URL, key, model, and provider limits)" : ": " + detail));
    }
    if (!connected) fail("Could not connect to the model endpoint");
    if (endpoint.stream) {
        ChatStreamAssembler assembler(limits, onText);
        SseFramer framer([&](const std::string& data) { assembler.feed(decodeUtf8(data)); }, limits.maxSseEventBytes);
        std::size_t total = 0;
        while (!assembler.done()) {
            watchdog.check();
            char byte;
            // JUCE documents read(n) as waiting for n bytes. A large n can delay
            // tiny SSE deltas until another chunk arrives; read(1) is deliberate.
            const int count = stream.read(&byte, 1);
            if (count <= 0) break;
            if (++total > limits.maxResponseBytes) fail("HTTP response byte limit exceeded");
            framer.feed(std::string_view(&byte, 1));
        }
        watchdog.check();
        if (stream.isError()) fail("Network error while reading stream");
        return assembler.finish(); // No tool execution before full validation.
    }
    std::string bytes;
    std::array<char, 4096> buffer{};
    for (;;) {
        watchdog.check();
        const int count = stream.read(buffer.data(), static_cast<int>(buffer.size()));
        if (count <= 0) break;
        if (bytes.size() + static_cast<std::size_t>(count) > limits.maxResponseBytes) fail("HTTP response byte limit exceeded");
        bytes.append(buffer.data(), static_cast<std::size_t>(count));
    }
    watchdog.check();
    if (stream.isError()) fail("Network error while reading response");
    const auto body = parseJson(decodeUtf8(bytes), limits.maxResponseBytes);
    if (has(body, "error")) fail("Provider error: " + jsonText(get(body, "error")).substring(0, 2048));
    const auto choices = get(body, "choices");
    if (!choices.isArray() || choices.size() != 1) fail("Expected exactly one completion choice");
    Completion result{ get(choices[0], "message").clone(), get(choices[0], "finish_reason").toString() };
    validateCompletion(result, limits);
    if (onText && get(result.message, "content").isString()) onText(get(result.message, "content").toString());
    return result;
}
} // namespace codeact
