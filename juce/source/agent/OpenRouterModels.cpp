#include "OpenRouterModels.h"

#include <array>

namespace fable {
namespace {
constexpr auto endpoint = "https://openrouter.ai/api/v1/models";
constexpr std::size_t maxResponseBytes = 2 * 1024 * 1024;

bool hasToolsParameter(const juce::var& parameters) {
    if (const auto* values = parameters.getArray())
        for (const auto& value : *values)
            if (value.toString() == "tools") return true;
    return false;
}
}

OpenRouterModels::OpenRouterModels() : Thread("Fable OpenRouter model catalog") {}

OpenRouterModels::~OpenRouterModels() {
    signalThreadShouldExit();
    if (auto* stream = activeStream.load(std::memory_order_acquire)) stream->cancel();
    waitForThreadToExit(-1);
}

void OpenRouterModels::refresh() {
    const juce::ScopedLock guard(lock);
    if (current.loading) return;
    current.loading = true;
    current.error.clear();
    ++current.revision;
    if (!startThread()) {
        current.loading = false;
        current.error = "Could not start live model-list refresh. Enter a model ID manually.";
        ++current.revision;
    }
}

OpenRouterModelList OpenRouterModels::state() const {
    const juce::ScopedLock guard(lock);
    return current;
}

void OpenRouterModels::publish(OpenRouterModelList value) {
    const juce::ScopedLock guard(lock);
    value.revision = current.revision + 1;
    current = std::move(value);
}

juce::StringArray OpenRouterModels::toolCapableIdsFromJson(const juce::String& text,
                                                            juce::String& error) {
    error.clear();
    if (text.getNumBytesAsUTF8() > maxResponseBytes) {
        error = "OpenRouter model list exceeded 2 MiB.";
        return {};
    }
    const auto root = juce::JSON::parse(text);
    const auto* object = root.getDynamicObject();
    const auto data = object != nullptr ? object->getProperty("data") : juce::var();
    const auto* models = data.getArray();
    if (models == nullptr) {
        error = "OpenRouter returned an invalid model list.";
        return {};
    }
    juce::StringArray ids;
    for (const auto& model : *models) {
        const auto* entry = model.getDynamicObject();
        if (entry == nullptr || !hasToolsParameter(entry->getProperty("supported_parameters"))) continue;
        const auto id = entry->getProperty("id").toString().trim();
        if (id.isEmpty() || id.length() > 256 || id.containsAnyOf("\r\n")) continue;
        ids.addIfNotAlreadyThere(id);
    }
    ids.sort(true);
    if (ids.isEmpty()) error = "OpenRouter returned no tool-capable models.";
    return ids;
}

void OpenRouterModels::run() {
    OpenRouterModelList next;
    juce::WebInputStream stream(juce::URL(endpoint), false);
    activeStream.store(&stream, std::memory_order_release);
    stream.withCustomRequestCommand("GET")
          .withExtraHeaders("Accept: application/json\r\n")
          .withConnectionTimeout(10000)
          .withNumRedirectsToFollow(0);
    const bool connected = stream.connect(nullptr);
    const int status = stream.getStatusCode();
    std::array<char, 8192> buffer {};
    std::string bytes;
    while (!threadShouldExit()) {
        const int count = stream.read(buffer.data(), static_cast<int>(buffer.size()));
        if (count <= 0) break;
        if (bytes.size() + static_cast<std::size_t>(count) > maxResponseBytes) {
            next.error = "OpenRouter model list exceeded 2 MiB.";
            break;
        }
        bytes.append(buffer.data(), static_cast<std::size_t>(count));
    }
    activeStream.store(nullptr, std::memory_order_release);
    if (threadShouldExit()) return;
    if (!connected) next.error = "Could not connect to OpenRouter for the live model list.";
    else if (status < 200 || status >= 300)
        next.error = "OpenRouter model list request returned HTTP " + juce::String(status) + ".";
    else if (stream.isError()) next.error = "Network error while reading the OpenRouter model list.";
    else if (next.error.isEmpty()) next.ids = toolCapableIdsFromJson(juce::String::fromUTF8(bytes.data(),
        static_cast<int>(bytes.size())), next.error);
    next.loading = false;
    publish(std::move(next));
}
} // namespace fable
