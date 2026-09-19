#include "AgentPanel.h"
#include "AgentConfig.h"
#include "../ui/Theme.h"
#include <set>

namespace fable {
class AgentTranscript final : public juce::Component {
public:
    explicit AgentTranscript() : content(*this) {
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
    }

    void setConversation(const std::vector<FableAgent::ConversationTurn>& turns, bool busy) {
        const auto atBottom = viewport.getViewPositionY() + viewport.getViewHeight()
            >= content.getHeight() - 8;
        messages.clear();
        for (std::size_t i = 0; i < turns.size(); ++i) {
            const auto& turn = turns[i];
            add("YOU", turn.prompt, Role::user);
            if (turn.activityLog.isNotEmpty())
                addActivity(static_cast<int>(i), turn.activityLog.trimStart(),
                            busy && i + 1 == turns.size());
            if (turn.assistant.isNotEmpty())
                add("FABLE AGENT", turn.assistant, Role::assistant);
            if (turn.error.isNotEmpty())
                add("REQUEST ERROR", turn.error, Role::error);
            if (!turn.changes.empty()) {
                juce::String changes = "Parameter values (" + turn.status + "):\n";
                for (const auto& change : turn.changes)
                    changes += change.id + ": " + juce::String(change.before, 5) + " -> "
                        + juce::String(change.after, 5) + "\n";
                if (turn.changeCount > turn.changes.size())
                    changes += "Showing " + juce::String(static_cast<int>(turn.changes.size()))
                        + " of " + juce::String(static_cast<int>(turn.changeCount))
                        + " parameter changes.";
                add("PROPOSED CHANGES", changes, Role::proposal);
            } else if (turn.status == "failed" || turn.status == "cancelled") {
                add("TURN " + turn.status.toUpperCase(), "You can retry or ask a follow-up.", Role::error);
            }
        }
        if (messages.empty())
            add("FABLE AGENT", "Ask for a parameter change or audio measurement. Tool activity keeps the complete "
                "model-visible transcript, including responses, calls, results, and errors.", Role::assistant);
        layoutMessages();
        if (atBottom) scrollToBottom();
        content.repaint();
    }

    void resized() override {
        viewport.setBounds(getLocalBounds());
        layoutMessages();
    }

private:
    enum class Role { user, assistant, activity, proposal, error };
    struct Message {
        juce::String title, body;
        Role role;
        int activityId = -1;
        bool expanded = true;
        juce::Rectangle<float> bounds;
    };

    class Content final : public juce::Component {
    public:
        explicit Content(AgentTranscript& owner) : transcript(owner) {}
        void paint(juce::Graphics& g) override { transcript.paintMessages(g); }
        void mouseUp(const juce::MouseEvent& event) override { transcript.toggleActivity(event.getPosition()); }
    private:
        AgentTranscript& transcript;
    };

    static juce::Colour fill(Role role) {
        switch (role) {
            case Role::user:     return juce::Colour(0xff173e4c);
            case Role::assistant:return juce::Colour(0xff1b2731);
            case Role::activity: return juce::Colour(0xff121a22);
            case Role::proposal: return juce::Colour(0xff2a2638);
            case Role::error:    return juce::Colour(0xff382126);
        }
        return fui::col::panelLo;
    }

    static juce::Colour accent(Role role) {
        switch (role) {
            case Role::user:     return fui::col::acA;
            case Role::assistant:return fui::col::text;
            case Role::activity: return fui::col::textHint;
            case Role::proposal: return fui::col::acF;
            case Role::error:    return juce::Colour(0xffff8585);
        }
        return fui::col::text;
    }

    static float textHeight(const juce::String& text, float width) {
        juce::AttributedString styled;
        styled.append(text, fui::monoFont(11.5f), fui::col::text);
        juce::TextLayout layout;
        layout.createLayout(styled, juce::jmax(1.0f, width));
        return layout.getHeight();
    }

    void add(juce::String title, juce::String body, Role role) {
        if (body.isNotEmpty()) messages.push_back({ std::move(title), std::move(body), role, -1, true, {} });
    }

    void addActivity(int activityId, juce::String body, bool expandForStreamingTurn) {
        if (body.isEmpty()) return;
        const auto expanded = expandForStreamingTurn || expandedActivities.find(activityId) != expandedActivities.end();
        messages.push_back({ expanded ? "TOOL ACTIVITY  |  CLICK TO COLLAPSE"
                                    : "TOOL ACTIVITY  |  CLICK TO EXPAND",
                             std::move(body), Role::activity, activityId, expanded, {} });
    }

    static const juce::String& collapsedActivityHint() {
        static const juce::String hint { "Model-visible response and tool transcript are retained. Click to show the complete log." };
        return hint;
    }

    static const juce::String& displayedBody(const Message& message) {
        return message.role == Role::activity && !message.expanded ? collapsedActivityHint() : message.body;
    }

    void layoutMessages() {
        const auto width = juce::jmax(1, viewport.getWidth());
        const auto maxBubble = juce::jmax(180.0f, static_cast<float>(width) * 0.82f);
        float y = 8.0f;
        for (auto& message : messages) {
            const auto bubbleWidth = message.role == Role::activity ? (float) width - 16.0f : maxBubble;
            const auto x = message.role == Role::user ? (float) width - bubbleWidth - 8.0f : 8.0f;
            const auto height = 28.0f + textHeight(displayedBody(message), bubbleWidth - 24.0f) + 14.0f;
            message.bounds = { x, y, bubbleWidth, height };
            y += height + 8.0f;
        }
        content.setSize(width, juce::jmax(viewport.getHeight(), (int) std::ceil(y)));
    }

    void paintMessages(juce::Graphics& g) {
        g.fillAll(juce::Colour(0xff10151c));
        for (const auto& message : messages) {
            const auto r = message.bounds;
            const auto colour = fill(message.role);
            g.setColour(colour);
            g.fillRoundedRectangle(r, 10.0f);
            g.setColour(accent(message.role).withAlpha(0.38f));
            g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.0f);
            g.setFont(fui::monoFont(9.5f, true));
            g.setColour(accent(message.role));
            g.drawText(message.title, r.withTrimmedLeft(12.0f).withHeight(24.0f).toNearestInt(),
                       juce::Justification::centredLeft, true);
            juce::AttributedString styled;
            styled.append(displayedBody(message), fui::monoFont(11.5f), fui::col::text);
            juce::TextLayout layout;
            auto body = r.reduced(12.0f).withTrimmedTop(24.0f);
            layout.createLayout(styled, body.getWidth());
            layout.draw(g, body);
        }
    }

    void scrollToBottom() {
        viewport.setViewPosition(0, juce::jmax(0, content.getHeight() - viewport.getViewHeight()));
    }

    void toggleActivity(juce::Point<int> point) {
        for (auto& message : messages) {
            if (message.role != Role::activity || !message.bounds.contains(point.toFloat())) continue;
            message.expanded = !message.expanded;
            if (message.expanded) expandedActivities.insert(message.activityId);
            else expandedActivities.erase(message.activityId);
            message.title = message.expanded ? "TOOL ACTIVITY  |  CLICK TO COLLAPSE"
                                             : "TOOL ACTIVITY  |  CLICK TO EXPAND";
            layoutMessages();
            content.repaint();
            return;
        }
    }

    juce::Viewport viewport;
    Content content;
    std::vector<Message> messages;
    std::set<int> expandedActivities;
};

AgentPanel::AgentPanel(FableAgent& controller) : agent(controller) {
    setOpaque(true);
    title.setText("FABLE AGENT", juce::dontSendNotification);
    title.setFont(fui::monoFont(17.0f));
    modelLabel.setText("OpenRouter model", juce::dontSendNotification);
    modelLabel.setFont(fui::monoFont(11.0f));
    const auto config = loadAgentConfig();
    const auto selectedModel = agent.selectedModel().isEmpty() ? config.endpoint.model : agent.selectedModel();
    model.setEditableText(true);
    populateModels({ selectedModel });
    model.onChange = [this] {
        if (changingModels) return;
        agent.setSelectedModel(model.getText().trim());
        timerCallback();
        if (!agent.conversation().empty())
            status.setText("Changing model starts a new conversation when you Send", juce::dontSendNotification);
    };
    model.setName("OpenRouter model");
    model.setTooltip("Live OpenRouter tool-capable model list. Refreshes when opened; you can also enter a model ID. Changing model starts a new conversation on Send.");
    model.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff10151c));
    model.setColour(juce::ComboBox::textColourId, fui::col::text);
    model.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff34424c));
    addAndMakeVisible(model);
    keyLabel.setText("OpenRouter API key", juce::dontSendNotification);
    keyLabel.setFont(fui::monoFont(11.0f));
    keyStatus.setFont(fui::monoFont(10.0f));
    apiKey.setName("OpenRouter API key");
    apiKey.setPasswordCharacter('*');
    apiKey.setMultiLine(false);
    apiKey.setTooltip("Paste your OpenRouter key and choose Save key. Shared across FableSynth plugins on this computer.");
    removeKeyButton.setTooltip("Remove the saved key from this computer. Developer environment settings, if present, remain available.");
    apiKey.onTextChange = [this] { saveKeyButton.setEnabled(!agent.poll().busy && apiKey.getText().trim().isNotEmpty()); };
    prompt.setMultiLine(true);
    prompt.setReturnKeyStartsNewLine(true);
    prompt.setTextToShowWhenEmpty("Ask for a sound change or a follow-up, e.g. make it a little warmer...",
                                fui::col::textDim);
    for (auto* edit : { &prompt, &apiKey }) {
        edit->setFont(fui::monoFont(12.0f));
        edit->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff10151c));
        edit->setColour(juce::TextEditor::textColourId, fui::col::text);
        edit->setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff34424c));
        addAndMakeVisible(*edit);
    }
    transcript = std::make_unique<AgentTranscript>();
    addAndMakeVisible(*transcript);
    for (auto* label : { &title, &modelLabel, &keyLabel, &keyStatus, &status }) {
        label->setColour(juce::Label::textColourId, fui::col::text);
        addAndMakeVisible(*label);
    }
    status.setFont(fui::monoFont(11.0f));
    status.setText(config.ready() ? "Ready - changes require Apply" : config.error, juce::dontSendNotification);
    for (auto* button : { &sendButton, &cancelButton, &applyButton, &resetButton, &closeButton, &saveKeyButton, &removeKeyButton }) {
        button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff24333d));
        addAndMakeVisible(*button);
    }
    sendButton.onClick = [this] { send(); };
    saveKeyButton.onClick = [this] {
        juce::String error;
        if (!saveAgentApiKey(apiKey.getText(), error)) {
            keyStatus.setText(error, juce::dontSendNotification);
            return;
        }
        apiKey.clear();
        refreshKeyStatus();
        status.setText("API key saved - ready to send", juce::dontSendNotification);
    };
    removeKeyButton.onClick = [this] {
        juce::String error;
        if (!removeAgentApiKey(error)) {
            keyStatus.setText(error, juce::dontSendNotification);
            return;
        }
        apiKey.clear();
        refreshKeyStatus();
        status.setText("Saved API key removed", juce::dontSendNotification);
    };
    cancelButton.onClick = [this] { agent.cancel(); };
    applyButton.onClick = [this] {
        if (beforeStateRead) beforeStateRead();
        juce::String error;
        if (agent.apply(error)) status.setText("Changes applied", juce::dontSendNotification);
        else status.setText(error, juce::dontSendNotification);
        timerCallback();
    };
    resetButton.onClick = [this] {
        agent.newConversation();
        prompt.clear();
        timerCallback();
        status.setText("New conversation - ask for a sound change", juce::dontSendNotification);
        prompt.grabKeyboardFocus();
    };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    applyButton.setEnabled(false);
    cancelButton.setEnabled(false);
    refreshKeyStatus();
    liveModels.refresh();
    timerCallback();
    startTimerHz(10);
}
AgentPanel::~AgentPanel() { stopTimer(); }

void AgentPanel::refreshKeyStatus() {
    const auto config = loadAgentConfig();
    hasSavedKey = config.hasSavedApiKey;
    apiKey.setTextToShowWhenEmpty(hasSavedKey ? "Saved key - paste to replace" : "Paste your OpenRouter API key", fui::col::textDim);
    keyStatus.setText(config.ready() ? "Using " + config.keySource + ". Shared across FableSynth plugins."
                                    : config.error, juce::dontSendNotification);
    keyStatus.setTooltip(config.error);
    const bool busy = agent.poll().busy;
    apiKey.setEnabled(!busy);
    saveKeyButton.setEnabled(!busy && apiKey.getText().trim().isNotEmpty());
    removeKeyButton.setEnabled(!busy);
}

void AgentPanel::populateModels(const juce::StringArray& liveIds) {
    const auto current = model.getText().trim();
    const auto selected = current.isNotEmpty() ? current
        : (agent.selectedModel().isNotEmpty() ? agent.selectedModel() : juce::String("openrouter/auto"));
    juce::StringArray ids;
    ids.add("openrouter/auto");
    for (const auto& id : liveIds) if (id != "openrouter/auto") ids.addIfNotAlreadyThere(id);
    if (!ids.contains(selected)) ids.add(selected);
    changingModels = true;
    model.clear(juce::dontSendNotification);
    for (int i = 0; i < ids.size(); ++i) model.addItem(ids[i], i + 1);
    model.setText(selected, juce::dontSendNotification);
    changingModels = false;
}

void AgentPanel::updateLiveModels() {
    const auto state = liveModels.state();
    if (state.revision == modelCatalogRevision) return;
    modelCatalogRevision = state.revision;
    if (!state.ids.isEmpty()) populateModels(state.ids);
    if (state.loading) status.setText("Refreshing live OpenRouter models…", juce::dontSendNotification);
    else if (state.error.isNotEmpty()) status.setText(state.error + " You can enter a model ID manually.", juce::dontSendNotification);
}

void AgentPanel::visibilityChanged() {
    if (isVisible()) { refreshKeyStatus(); liveModels.refresh(); }
    else apiKey.clear();
}

void AgentPanel::send() {
    auto config = loadAgentConfig();
    config.endpoint.model = model.getText().trim();
    if (!config.ready()) { status.setText(config.error, juce::dontSendNotification); return; }
    if (config.endpoint.model.isEmpty() || prompt.getText().trim().isEmpty()) {
        status.setText("Enter a model and a prompt", juce::dontSendNotification); return;
    }
    try {
        if (beforeStateRead) beforeStateRead();
        if (!agent.submit(prompt.getText().trim(), std::move(config.endpoint))) {
            const auto diagnostic = agent.poll().diagnostics;
            status.setText(diagnostic.isEmpty() ? "An agent turn is already running" : diagnostic,
                           juce::dontSendNotification); return;
        }
        prompt.clear();
        revision = ~std::uint64_t(0);
        timerCallback();
    } catch (const std::exception& e) {
        status.setText(juce::String::fromUTF8(e.what()), juce::dontSendNotification);
    }
}

void AgentPanel::timerCallback() {
    updateLiveModels();
    const auto view = agent.poll();
    const auto& turns = agent.conversation();
    const auto currentConversationRevision = agent.conversationRevision();
    if (view.revision == revision && currentConversationRevision == conversationRevision) return;
    revision = view.revision;
    conversationRevision = currentConversationRevision;
    sendButton.setEnabled(!view.busy);
    model.setEnabled(!view.busy);
    apiKey.setEnabled(!view.busy);
    saveKeyButton.setEnabled(!view.busy && apiKey.getText().trim().isNotEmpty());
    removeKeyButton.setEnabled(!view.busy);
    resetButton.setEnabled(!view.busy);
    cancelButton.setEnabled(view.busy);
    applyButton.setEnabled(agent.canApply());
    if (view.busy) status.setText(view.activity, juce::dontSendNotification);
    if (!view.busy && !turns.empty()) {
        const auto& last = turns.back();
        juce::String message = "Ready for a follow-up";
        if (agent.canApply()) message = "Review the values, then Apply changes or ask a follow-up";
        else if (last.status == "applied") message = "Changes applied - ready for a follow-up";
        else if (last.status == "rejected") message = "Changes could not be applied - ask again using the current sound";
        else if (last.status == "failed" || last.status == "cancelled") message = "Turn " + last.status + " - you can retry or ask a follow-up";
        status.setText(message, juce::dontSendNotification);
    }
    transcript->setConversation(turns, view.busy);
}

void AgentPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1b232d));
    g.setColour(juce::Colour(0xff586f7b));
    g.drawRect(getLocalBounds(), 1);
}
void AgentPanel::resized() {
    auto area = getLocalBounds().reduced(16);
    auto heading = area.removeFromTop(28);
    closeButton.setBounds(heading.removeFromRight(64));
    title.setBounds(heading);
    area.removeFromTop(8);
    auto modelRow = area.removeFromTop(28);
    modelLabel.setBounds(modelRow.removeFromLeft(145));
    model.setBounds(modelRow);
    area.removeFromTop(8);
    auto keyRow = area.removeFromTop(28);
    keyLabel.setBounds(keyRow.removeFromLeft(145));
    removeKeyButton.setBounds(keyRow.removeFromRight(68));
    keyRow.removeFromRight(6);
    saveKeyButton.setBounds(keyRow.removeFromRight(76));
    keyRow.removeFromRight(6);
    apiKey.setBounds(keyRow);
    keyStatus.setBounds(area.removeFromTop(24));
    area.removeFromTop(10);
    auto buttons = area.removeFromBottom(30);
    sendButton.setBounds(buttons.removeFromLeft(66)); buttons.removeFromLeft(8);
    cancelButton.setBounds(buttons.removeFromLeft(70)); buttons.removeFromLeft(8);
    resetButton.setBounds(buttons.removeFromLeft(150));
    applyButton.setBounds(buttons.removeFromRight(130));
    area.removeFromBottom(6);
    status.setBounds(area.removeFromBottom(40));
    prompt.setBounds(area.removeFromBottom(65));
    area.removeFromBottom(8);
    transcript->setBounds(area);
}

AgentOverlay::AgentOverlay(std::function<FableAgent&()> factory) : getAgent(std::move(factory)) {
    setInterceptsMouseClicks(false, true);
    launcher.setTooltip("Open the parameter agent");
    launcher.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff263e49));
    addAndMakeVisible(launcher);
    launcher.onClick = [this] {
        if (!panel) {
            panel = std::make_unique<AgentPanel>(getAgent());
            panel->onClose = [this] { panel->setVisible(false); };
            panel->beforeStateRead = [this] { if (beforeStateRead) beforeStateRead(); };
            addChildComponent(*panel);
        }
        panel->setVisible(!panel->isVisible());
        resized();
        if (panel->isVisible()) panel->toFront(true);
    };
}
void AgentOverlay::resized() {
    launcher.setBounds(getWidth() - 88, getHeight() - 29, 78, 23);
    if (panel) {
        const int w = juce::jmin(650, getWidth() - 20), h = juce::jmin(530, getHeight() - 42);
        panel->setBounds(getWidth() - w - 10, getHeight() - h - 36, w, h);
    }
}
} // namespace fable
