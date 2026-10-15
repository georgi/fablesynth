#pragma once
#include "../agent/AgentPanel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include "DrumProcessor.h"
#include "DrumDeviceBody.h"
#include "ui/DrumHeader.h"
#include "ui/DrumBrowserAdapters.h"
#include "ui/PatternBrowserAdapter.h"
#include "ui/DrumStandaloneBackend.h"
#include "../ui/PresetBrowser.h"
#include "../ui/LookAndFeel.h"
#include "../seq/ui/ClipAutomationPanel.h"

// The DR-1 rack: all sections laid out at a fixed logical size matching the
// web CSS grid (src/drum/drum.css). The editor scales it to the window so the
// layout stays pixel-faithful — same scheme as the WT-1 Rack (PluginEditor.h).
class DrumRack : public juce::Component {
public:
    // Reference extent for the editor and FX snapshots. Sequencer expands further.
    static constexpr int LW = 1460, LH = DrumDeviceBody::workspaceHeight;
    DrumRack(fui::DrumUiModel&, DrumAudioProcessor&);
    void resized() override;
    void setDisplayScale(float scale) { body.setDisplayScale(scale); }
    int logicalHeight() const { return body.preferredHeight(); }
    std::function<void()> onPreferredHeightChanged;
    // Kit drawer (header BROWSE) and pad patch drawer (selected-pad bar BROWSE).
    fui::PresetBrowser& kitBrowser() { return kitBrowser_; }
    fui::PresetBrowser& padBrowser() { return padBrowser_; }
    fui::KitBrowserAdapter& kitAdapter() { return kitAdapter_; }
    fui::PadPatchAdapter& padAdapter() { return padAdapter_; }
    // Pattern library drawer (step sequencer PATTERNS).
    fui::PresetBrowser& patternBrowser() { return patternBrowser_; }
    fui::PatternBrowserAdapter& patternAdapter() { return patternAdapter_; }
    void toggleKitBrowser();
    void togglePadBrowser();
    void togglePatternBrowser();

private:
    fui::DrumHeader header;
    DrumDeviceBody body;
    fui::KitBrowserAdapter kitAdapter_;
    fui::PadPatchAdapter padAdapter_;
    fui::PatternBrowserAdapter patternAdapter_;
    fui::PresetBrowser kitBrowser_, padBrowser_, patternBrowser_;
    // Standalone sequence automation (web DrumAutomation.tsx): the panel edits
    // the processor's lanes through a view of the sequence as one clip.
    fui::ClipAutomationPanel automation_;
    fable::ClipData automationClip_;
    void closeDrawers();
public:
    fui::ClipAutomationPanel& automationPanel() { return automation_; }
    void showAutomationPage() { body.showAutomationPage(); }
private:
};

class DrumEditor : public juce::AudioProcessorEditor,
                   public juce::DragAndDropContainer {
public:
    explicit DrumEditor(DrumAudioProcessor&);
    ~DrumEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    DrumRack& getRack() { return rack; }

private:
    void fitWindowToPage();
    fui::DarkLNF lnf;
    std::unique_ptr<fui::DrumUiModel> model;
    DrumRack rack;
    fable::AgentOverlay agentOverlay;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumEditor)
};
