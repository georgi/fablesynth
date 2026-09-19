#include "AgentConfig.h"
#include <juce_data_structures/juce_data_structures.h>
#if ! JUCE_WINDOWS
 #include <sys/stat.h>
#endif

namespace fable {
juce::StringPairArray parseAgentEnv(const juce::String& text) {
    juce::StringPairArray result;
    for (auto line : juce::StringArray::fromLines(text)) {
        line = line.trim();
        if (line.startsWith("export ")) line = line.substring(7).trimStart();
        const int equals = line.indexOfChar('=');
        if (equals < 1 || line.startsWithChar('#')) continue;
        const auto key = line.substring(0, equals).trim();
        if (key != "OPENROUTER_API_KEY" && key != "OPENROUTER_MODEL") continue;
        auto value = line.substring(equals + 1).trim();
        if (value.startsWithChar('"') || value.startsWithChar('\'')) {
            const auto quote = value[0];
            const int end = value.indexOfChar(1, quote);
            if (end < 0) continue;
            const auto tail = value.substring(end + 1).trim();
            if (tail.isNotEmpty() && !tail.startsWithChar('#')) continue;
            value = value.substring(1, end);
        } else {
            const int comment = value.indexOf(" #");
            if (comment >= 0) value = value.substring(0, comment).trimEnd();
        }
        result.set(key, value);
    }
    return result;
}

namespace {
constexpr auto apiKeyProperty = "OPENROUTER_API_KEY";
constexpr std::size_t maxKeyBytes = 4096;

juce::PropertiesFile::Options settingsOptions(bool readOnly = false) {
    juce::PropertiesFile::Options options;
    options.applicationName = "Agent";
    options.filenameSuffix = "settings";
   #if JUCE_LINUX || JUCE_BSD
    options.folderName = ".config/FableSynth";
   #else
    options.folderName = "FableSynth";
   #endif
    options.osxLibrarySubFolder = "Application Support";
    options.commonToAllUsers = false;
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.millisecondsBeforeSaving = -1;
    options.doNotSave = readOnly;
    return options;
}

class SettingsLock final {
public:
    explicit SettingsLock(const juce::File& file)
        : lock("FableSynth-agent-settings-" + juce::String::toHexString(file.getFullPathName().hashCode64())),
          acquired(lock.enter(1000)) {}
    ~SettingsLock() { if (acquired) lock.exit(); }
    juce::InterProcessLock lock;
    bool acquired;
};

bool validateKey(const juce::String& original, juce::String& key, juce::String& error) {
    error.clear();
    if (original.containsAnyOf("\r\n")) {
        error = "The API key must be a single line. Remove any line breaks and try again.";
        return false;
    }
    key = original.trim();
    if (key.isEmpty()) {
        error = "Enter an OpenRouter API key before saving.";
        return false;
    }
    if (key.getNumBytesAsUTF8() > maxKeyBytes) {
        error = "The API key is too long (maximum 4096 UTF-8 bytes).";
        return false;
    }
    for (auto character : key)
        if (character < 32 || character == 127) {
            error = "The API key contains an invalid control character.";
            return false;
        }
    return true;
}

bool restrictPermissions(const juce::File& file, bool directory, juce::String& error) {
   #if ! JUCE_WINDOWS
    if (::chmod(file.getFullPathName().toRawUTF8(), directory ? 0700 : 0600) != 0) {
        error = "Could not restrict access to Fable Agent settings at " + file.getFullPathName()
              + ". Check folder ownership and permissions, then try again.";
        return false;
    }
   #else
    juce::ignoreUnused(file, directory, error); // JUCE selects this user's AppData directory.
   #endif
    return true;
}
}

juce::File agentSettingsFile() { return settingsOptions().getDefaultFile(); }

bool saveAgentApiKey(const juce::String& original, juce::String& error, const juce::File& file) {
    juce::String key;
    if (!validateKey(original, key, error)) return false;
    if (file == juce::File() || file.isDirectory() || file.isSymbolicLink()
        || file.getParentDirectory().isSymbolicLink()) {
        error = "The Fable Agent settings location must be a regular file in a dedicated folder.";
        return false;
    }
    SettingsLock lock(file);
    if (!lock.acquired) {
        error = "Another Fable instance is updating the API key. Try saving again.";
        return false;
    }
    const auto folder = file.getParentDirectory();
    if (folder.createDirectory().failed()) {
        error = "Could not create the Fable Agent settings folder at " + folder.getFullPathName()
              + ". Check folder access and try again.";
        return false;
    }
    // Restrict the containing directory before PropertiesFile creates either
    // the destination or its atomic-replacement temporary file.
    if (!restrictPermissions(folder, true, error)) return false;
    if (file.existsAsFile() && !restrictPermissions(file, false, error)) return false;
    if (file.existsAsFile() && file.getSize() > 64 * 1024) {
        error = "The Fable Agent settings file is too large. Remove the saved key and save it again.";
        return false;
    }
    juce::PropertiesFile settings(file, settingsOptions());
    if (!settings.isValidFile()) {
        error = "Could not read Fable Agent settings. Remove the saved key to reset the settings file, then save again.";
        return false;
    }
    settings.setValue(apiKeyProperty, key);
    const bool saved = settings.save();
    // Do not retry implicitly from the destructor after a failed save.
    settings.setNeedsToBeSaved(false);
    if (!saved) {
        error = "Could not save the API key at " + file.getFullPathName()
              + ". Check available disk space and folder permissions, then try again.";
        return false;
    }
    return restrictPermissions(file, false, error);
}

bool removeAgentApiKey(juce::String& error, const juce::File& file) {
    error.clear();
    if (file == juce::File() || file.isDirectory()) {
        error = "The Fable Agent settings location is not a regular file.";
        return false;
    }
    SettingsLock lock(file);
    if (!lock.acquired) {
        error = "Another Fable instance is updating the API key. Try removing it again.";
        return false;
    }
    // This dedicated file stores only the saved credential. Deleting it also
    // makes removal work when an old settings file is damaged or unreadable.
    if ((file.exists() || file.isSymbolicLink()) && !file.deleteFile()) {
        error = "Could not remove the saved API key at " + file.getFullPathName()
              + ". Check folder permissions and try again.";
        return false;
    }
    return true;
}

bool saveAgentApiKey(const juce::String& key, juce::String& error) {
    return saveAgentApiKey(key, error, agentSettingsFile());
}
bool removeAgentApiKey(juce::String& error) { return removeAgentApiKey(error, agentSettingsFile()); }

AgentConfig loadAgentConfig(const juce::File& settingsFile,
    const juce::StringPairArray& environment, const juce::File& developmentEnvFile) {
    AgentConfig config;
    auto key = environment[apiKeyProperty];
    auto model = environment["OPENROUTER_MODEL"].trim();
    if (key.trim().isNotEmpty()) config.keySource = "environment";
    if (settingsFile.existsAsFile()) {
        SettingsLock lock(settingsFile);
        if (!lock.acquired) config.error = "Another Fable instance is updating settings. Try again.";
        else if (settingsFile.getSize() > 64 * 1024 || settingsFile.isSymbolicLink())
            config.error = "Cannot read Fable Agent settings. Remove the saved key and save it again.";
        else {
            juce::PropertiesFile settings(settingsFile, settingsOptions(true));
            if (!settings.isValidFile())
                config.error = "Cannot read Fable Agent settings. Check file access or remove the saved key and save it again.";
            else if (settings.containsKey(apiKeyProperty)) {
                config.hasSavedApiKey = true;
                config.keySource = "saved settings";
                key = settings.getValue(apiKeyProperty);
            }
        }
    } else if (settingsFile.isDirectory()) {
        config.error = "The Fable Agent settings path is a directory. Choose a writable per-user settings location.";
    }
    if ((key.trim().isEmpty() || model.isEmpty()) && developmentEnvFile.existsAsFile()
        && developmentEnvFile.getSize() <= 64 * 1024) {
        const auto values = parseAgentEnv(developmentEnvFile.loadFileAsString());
        if (!config.hasSavedApiKey && key.trim().isEmpty() && values[apiKeyProperty].trim().isNotEmpty()) {
            key = values[apiKeyProperty];
            config.keySource = "development file";
        }
        if (model.isEmpty()) model = values["OPENROUTER_MODEL"].trim();
    }
    config.endpoint.baseUrl = "https://openrouter.ai/api/v1";
    config.endpoint.model = model.isEmpty() ? "openrouter/auto" : model;
    config.endpoint.extraBodyJson = R"({"provider":{"require_parameters":true}})";
    config.endpoint.sendParallelToolCalls = false;
    config.endpoint.headers.set("X-Title", "FableSynth");
    if (config.error.isEmpty()) {
        if (key.trim().isEmpty() && !config.hasSavedApiKey)
            config.error = "Paste your OpenRouter API key above and click Save key.";
        else validateKey(key, config.endpoint.apiKey, config.error);
    }
    return config;
}

AgentConfig loadAgentConfig() {
    juce::StringPairArray environment;
    environment.set(apiKeyProperty, juce::SystemStats::getEnvironmentVariable(apiKeyProperty, {}));
    environment.set("OPENROUTER_MODEL", juce::SystemStats::getEnvironmentVariable("OPENROUTER_MODEL", {}));
    auto path = juce::SystemStats::getEnvironmentVariable("FABLE_AGENT_ENV_FILE", {}).trim();
   #ifdef FABLE_AGENT_DEFAULT_ENV_FILE
    if (path.isEmpty()) path = FABLE_AGENT_DEFAULT_ENV_FILE;
   #endif
    const auto developmentFile = path.isEmpty() ? juce::File::getCurrentWorkingDirectory().getChildFile(".env")
                                              : juce::File::getCurrentWorkingDirectory().getChildFile(path);
    return loadAgentConfig(agentSettingsFile(), environment, developmentFile);
}
} // namespace fable
