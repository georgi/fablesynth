#include "../source/agent/AgentConfig.h"
#include "../source/agent/OpenRouterModels.h"
#include <juce_events/juce_events.h>
#include <iostream>
#if ! JUCE_WINDOWS
 #include <sys/stat.h>
#endif

namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
struct TemporarySettings {
    juce::File root = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("fable-agent-config-test", {}, false);
    TemporarySettings() { check(root.createDirectory().wasOk(), "create isolated settings fixture"); }
    ~TemporarySettings() { root.deleteRecursively(); }
};
}

int main() {
    juce::ScopedJuceInitialiser_GUI initialise;
    try {
        const auto values = fable::parseAgentEnv(
            "# ignored\nexport OPENROUTER_API_KEY='test-only-token' # local\n"
            "OPENROUTER_MODEL=vendor/model # model\nUNRELATED=secret\n"
            "BROKEN LINE\nOPENROUTER_API_KEY='unterminated\n");
        check(values["OPENROUTER_API_KEY"] == "test-only-token"
                  && values["OPENROUTER_MODEL"] == "vendor/model" && !values.containsKey("UNRELATED"),
              "environment parser changed allowed keys or quoting");
        const auto literal = fable::parseAgentEnv("OPENROUTER_API_KEY=\"$(do-not-execute)\"\n");
        check(literal["OPENROUTER_API_KEY"] == "$(do-not-execute)", "parser evaluated shell text");
        juce::String modelListError;
        const auto toolModels = fable::OpenRouterModels::toolCapableIdsFromJson(
            R"({"data":[
                {"id":"vendor/no-tools","supported_parameters":["temperature"]},
                {"id":"vendor/tool-model","supported_parameters":["tools","tool_choice"]},
                {"id":"vendor/tool-model","supported_parameters":["tools"]},
                {"id":"vendor/invalid\nmodel","supported_parameters":["tools"]}
            ]})", modelListError);
        check(modelListError.isEmpty() && toolModels.size() == 1 && toolModels[0] == "vendor/tool-model",
              "live model parser must include only unique tool-capable IDs");
        check(fable::OpenRouterModels::toolCapableIdsFromJson("{}", modelListError).isEmpty()
                  && modelListError.isNotEmpty(), "invalid model response must be rejected");

        TemporarySettings fixture;
        const auto settingsFile = fixture.root.getChildFile("settings/Agent.settings");
        const auto developmentFile = fixture.root.getChildFile("development.env");
        check(settingsFile != fable::agentSettingsFile(), "fixture must never use real user settings");
        juce::StringPairArray environment;
        auto config = fable::loadAgentConfig(settingsFile, environment, {});
        check(!config.ready() && !config.hasSavedApiKey && config.keySource == "not configured",
              "missing key should report in-plugin setup");
        check(config.error.contains("Save key"), "missing key error should explain settings UI");
        check(!settingsFile.exists(), "read-only configuration load created settings");
        check(developmentFile.replaceWithText("OPENROUTER_API_KEY=file-fixture-key\nOPENROUTER_MODEL=file/model\n"),
              "write isolated development fixture");
        config = fable::loadAgentConfig(settingsFile, environment, developmentFile);
        check(config.ready() && config.endpoint.apiKey == "file-fixture-key"
                  && config.keySource == "development file" && config.endpoint.model == "file/model",
              "development fallback did not load");
        environment.set("OPENROUTER_API_KEY", "environment-fixture-key");
        environment.set("OPENROUTER_MODEL", "environment/model");
        config = fable::loadAgentConfig(settingsFile, environment, developmentFile);
        check(config.ready() && config.endpoint.apiKey == "environment-fixture-key"
                  && config.keySource == "environment" && config.endpoint.model == "environment/model",
              "environment must precede development file");

        juce::String error;
        check(fable::saveAgentApiKey("  saved-fixture-key  ", error, settingsFile) && error.isEmpty(),
              "saving trimmed fixture key failed");
        config = fable::loadAgentConfig(settingsFile, environment, developmentFile);
        check(config.ready() && config.hasSavedApiKey && config.endpoint.apiKey == "saved-fixture-key"
                  && config.keySource == "saved settings", "saved key must precede environment and development file");
        check(config.endpoint.model == "environment/model", "saving key changed model selection source");
        check(config.endpoint.baseUrl == "https://openrouter.ai/api/v1" && !config.endpoint.sendParallelToolCalls,
              "credential storage changed OpenRouter endpoint routing");
        check(!config.keySource.contains("fixture-key") && !config.error.contains("fixture-key"),
              "safe status text exposed fixture credential");
        check(settingsFile.loadFileAsString().contains("saved-fixture-key"),
              "plain-text PropertiesFile did not persist fixture value");
       #if ! JUCE_WINDOWS
        struct stat fileStat {}, directoryStat {};
        check(::stat(settingsFile.getFullPathName().toRawUTF8(), &fileStat) == 0
                  && (fileStat.st_mode & 0777) == 0600, "credential file permissions must be 0600");
        check(::stat(settingsFile.getParentDirectory().getFullPathName().toRawUTF8(), &directoryStat) == 0
                  && (directoryStat.st_mode & 0777) == 0700, "credential directory permissions must be 0700");
       #endif

        const auto persisted = settingsFile.loadFileAsString();
        for (const auto& badKey : juce::StringArray { "", "   ", "key\r\nHeader: injected", "key\n", "bad\tkey",
                                                    juce::String::repeatedString("x", 4097) }) {
            check(!fable::saveAgentApiKey(badKey, error, settingsFile) && error.isNotEmpty(),
                  "invalid credential was accepted");
            check(settingsFile.loadFileAsString() == persisted, "invalid save changed existing credential");
            check(!error.contains("Header: injected") && !error.contains("saved-fixture-key"),
                  "validation error echoed a credential");
        }
        check(fable::saveAgentApiKey("replacement-fixture-key", error, settingsFile), "replacing saved credential failed");
        const auto secondInstance = fable::loadAgentConfig(settingsFile, {}, {});
        check(secondInstance.ready() && secondInstance.endpoint.apiKey == "replacement-fixture-key",
              "another plugin instance did not see updated shared credential");
        check(fable::removeAgentApiKey(error, settingsFile) && error.isEmpty() && !settingsFile.exists(),
              "removing saved credential failed");
        config = fable::loadAgentConfig(settingsFile, environment, developmentFile);
        check(config.ready() && !config.hasSavedApiKey && config.keySource == "environment"
                  && config.endpoint.apiKey == "environment-fixture-key", "removal did not restore environment fallback");
        check(fable::removeAgentApiKey(error, settingsFile), "removing an absent saved credential should succeed");

        check(settingsFile.replaceWithText("damaged settings file"), "write damaged isolated settings");
        config = fable::loadAgentConfig(settingsFile, environment, developmentFile);
        check(!config.ready() && config.error.contains("saved key"), "damaged settings should give actionable error");
        check(!fable::saveAgentApiKey("new-fixture-key", error, settingsFile), "saving silently replaced damaged settings");
        check(fable::removeAgentApiKey(error, settingsFile), "Remove must recover damaged saved settings");
        check(fable::saveAgentApiKey("recovered-fixture-key", error, settingsFile), "save after damaged-settings removal failed");

        const auto blocker = fixture.root.getChildFile("not-a-directory");
        check(blocker.replaceWithText("fixture blocker"), "write blocked path fixture");
        check(!fable::saveAgentApiKey("error-fixture-key", error, blocker.getChildFile("Agent.settings"))
                  && error.isNotEmpty() && !error.contains("error-fixture-key"),
              "unwritable destination did not report a credential-free error");
        check(!fable::removeAgentApiKey(error, settingsFile.getParentDirectory()), "removal must not delete a directory");
        std::cout << "PASS: " << checks << " isolated Agent configuration checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
