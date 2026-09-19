#pragma once
#include <Agent.h>

namespace fable {
// Runtime-only configuration. Never placed in APVTS, session JSON, or JS.
struct AgentConfig {
    codeact::Endpoint endpoint;
    juce::String error;
    juce::String keySource = "not configured";
    bool hasSavedApiKey = false;
    bool ready() const { return error.isEmpty(); }
};

AgentConfig loadAgentConfig();
bool saveAgentApiKey(const juce::String& key, juce::String& error);
bool removeAgentApiKey(juce::String& error);
// Shared by all four instruments, per OS user. Plain-text JUCE settings, not
// encrypted storage. Never included in plugin/preset/session serialization.
juce::File agentSettingsFile();

// Injected paths and environment for deterministic tests. settingsFile must be
// inside a dedicated settings directory: saving restricts that directory's
// POSIX permissions. These overloads never read the real environment or .env.
AgentConfig loadAgentConfig(const juce::File& settingsFile,
    const juce::StringPairArray& environment, const juce::File& developmentEnvFile);
bool saveAgentApiKey(const juce::String& key, juce::String& error, const juce::File& settingsFile);
bool removeAgentApiKey(juce::String& error, const juce::File& settingsFile);
// Public for deterministic tests. Parses data only; never evaluates shell code.
juce::StringPairArray parseAgentEnv(const juce::String& text);
} // namespace fable
