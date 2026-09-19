// Opt-in network smoke test: never registered with CTest. Reads runtime config
// without printing it; uses a synthetic snapshot and cannot change an instrument.
#include "../source/agent/AgentConfig.h"
#include <iostream>

int main() {
    try {
        const auto config = fable::loadAgentConfig();
        if (!config.ready()) { std::cerr << config.error << '\n'; return 1; }
        codeact::Limits limits;
        limits.maxModelCalls = 4;
        limits.maxOutputTokens = 1024;
        limits.turnTimeoutMs = 90000;
        limits.requestTimeoutMs = 45000;
        codeact::Session session(limits);
        codeact::OpenAIClient transport;
        codeact::StopState stop(limits.turnTimeoutMs);
        codeact::Turn turn;
        turn.endpoint = config.endpoint;
        turn.snapshot.pluginName = "FableSynth agent smoke test";
        turn.snapshot.parameters.push_back({"filter.cutoff", "Filter cutoff", "Hz", 20, 20000, 1200});
        turn.prompt = "Inspect host.snapshot() using execute_js. Propose setting filter.cutoff "
                      "to exactly 800 Hz using host.proposeParameters. Then give a short final answer.";
        const auto result = session.run(turn, transport, stop);
        if (!result.ok || result.changes.size() != 1 || result.changes[0].id != "filter.cutoff"
            || result.changes[0].after != 800 || result.changes[0].before != 1200) {
            std::cerr << "Live model did not return the expected parameter proposal\n";
            return 1;
        }
        std::cout << "Live OpenRouter loop passed: execute_js → parameter proposal → final answer ("
                  << result.modelCalls << " model calls). No instrument parameters were changed.\n";
        return 0;
    } catch (const std::exception& e) {
        // Provider error bodies are deliberately not logged by the live harness.
        auto error = juce::String::fromUTF8(e.what());
        if (error.startsWith("Provider ")) error = "Provider rejected the request or stream";
        const auto key = fable::loadAgentConfig().endpoint.apiKey;
        if (key.isNotEmpty()) error = error.replace(key, "[redacted]");
        std::cerr << "Live OpenRouter request failed: " << error << '\n';
        return 1;
    }
}
