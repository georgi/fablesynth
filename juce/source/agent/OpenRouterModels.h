#pragma once

#include <juce_core/juce_core.h>
#include <atomic>

namespace fable {

struct OpenRouterModelList {
    bool loading = false;
    juce::String error;
    juce::StringArray ids;
    std::uint64_t revision = 0;
};

// Fetches OpenRouter's public model catalog off the message and audio threads.
// Only models declaring the "tools" parameter are offered to the agent picker.
class OpenRouterModels final : private juce::Thread {
public:
    OpenRouterModels();
    ~OpenRouterModels() override;

    void refresh();
    OpenRouterModelList state() const;

    // Public so parsing/filtering stays deterministic in tests.
    static juce::StringArray toolCapableIdsFromJson(const juce::String&, juce::String& error);

private:
    void run() override;
    void publish(OpenRouterModelList);

    mutable juce::CriticalSection lock;
    OpenRouterModelList current;
    std::atomic<juce::WebInputStream*> activeStream { nullptr };
};

} // namespace fable
