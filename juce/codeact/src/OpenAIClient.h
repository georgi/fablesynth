#pragma once
#include "Types.h"
namespace codeact {
struct Completion { Json message; juce::String finishReason; };
Json executeJsTool();
void validateCompletion(const Completion&, const Limits&);
Json makeRequest(const Endpoint&, const juce::Array<Json>& messages, const Limits&);

// Injectable so protocol and agent-loop tests never need a real API key.
class ChatTransport {
public:
    virtual ~ChatTransport() = default;
    virtual Completion complete(const Endpoint&, const juce::Array<Json>& messages,
                                const Limits&, const StopState&, const TextSink&) = 0;
};
class OpenAIClient final : public ChatTransport {
public:
    Completion complete(const Endpoint&, const juce::Array<Json>&,
                        const Limits&, const StopState&, const TextSink&) override;
};
// Public for deterministic streaming fixtures. feed() takes one complete SSE
// data payload, not arbitrary network chunks. SseFramer handles byte framing.
class ChatStreamAssembler {
public:
    explicit ChatStreamAssembler(const Limits&, TextSink sink = {});
    void feed(const juce::String& event);
    bool done() const noexcept { return sawDone; }
    Completion finish() const;
private:
    Limits limits;
    TextSink onText;
    Json message = object({{"role", "assistant"}, {"content", Json()}});
    std::map<int, Json> calls;
    juce::String finishReason;
    bool sawDone = false;
    std::size_t bytesSeen = 0;
};
} // namespace codeact
