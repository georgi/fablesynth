#pragma once
#include "FableAgent.h"
#include "OpenRouterModels.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace fable {
class AgentTranscript;

class AgentPanel final : public juce::Component, private juce::Timer {
public:
    explicit AgentPanel(FableAgent&);
    ~AgentPanel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    std::function<void()> onClose;
    std::function<void()> beforeStateRead;
private:
    void timerCallback() override;
    void send();
    void refreshKeyStatus();
    void updateLiveModels();
    void populateModels(const juce::StringArray&);
    FableAgent& agent;
    juce::Label title, modelLabel, keyLabel, keyStatus, status;
    juce::ComboBox model;
    juce::TextEditor prompt, apiKey;
    std::unique_ptr<AgentTranscript> transcript;
    juce::TextButton saveKeyButton { "Save key" }, removeKeyButton { "Remove" };
    bool hasSavedKey = false;
    bool changingModels = false;
    std::uint64_t modelCatalogRevision = ~std::uint64_t(0);
    OpenRouterModels liveModels;
    juce::TextButton sendButton { "Send" }, cancelButton { "Cancel" },
        applyButton { "Apply changes" }, resetButton { "New conversation" }, closeButton { "Close" };
    std::uint64_t revision = ~std::uint64_t(0);
    std::uint64_t conversationRevision = ~std::uint64_t(0);
};

// Owned by the editor; the controller stays in the processor. The full-size
// overlay passes clicks through except at its launcher and the open panel.
class AgentOverlay final : public juce::Component {
public:
    explicit AgentOverlay(std::function<FableAgent&()>);
    void resized() override;
    bool isPanelOpen() const { return panel && panel->isVisible(); }
    std::function<void()> beforeStateRead;
private:
    std::function<FableAgent&()> getAgent;
    juce::TextButton launcher { "AGENT" };
    std::unique_ptr<AgentPanel> panel;
};
} // namespace fable
