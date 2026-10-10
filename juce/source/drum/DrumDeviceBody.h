#pragma once

#include "ui/PadGrid.h"
#include "ui/PadStrip.h"
#include "ui/DrumPanels.h"
#include "ui/StepSeqView.h"
#include "ui/DrumFxRack.h"
#include "ui/KitMixPanel.h"
#include "../ui/FxChain.h"
#include <functional>

// Reusable DR-1 machine surface. It depends only on DrumUiModel and can be
// composed by either the standalone rack or SQ-4 without processor symbols.
class DrumDeviceBody : public juce::Component, private juce::ChangeListener {
public:
    explicit DrumDeviceBody(fui::DrumUiModel&);
    ~DrumDeviceBody() override;
    void resized() override;
    static constexpr int editHeight = 676, workspaceHeight = 766, sequencerHeight = workspaceHeight;
    int preferredHeight() const;
    std::function<void()> onPreferredHeightChanged;
    void setDisplayScale(float scale) { displayScale_ = scale; resized(); }
    fui::SelBarView& selBarView() { return selBar; }   // the pad patch BROWSE button lives here
    fui::StepSeqView& stepSeqView() { return stepSeq; }   // the PATTERNS button lives here
    fui::FxChain& fxChain() { return fxRack; }
    fui::KitMixPanel& kitMixPanel() { return kitMix; }
    // Standalone sequence automation (web DrumAutomation.tsx) as an
    // AUTOMATION page; the rack owns the panel. SQ-4 keeps its clip panel.
    void setAutomationPanel(juce::Component& panel) {
        automationPanel_ = &panel; addChildComponent(panel); addAndMakeVisible(automationPage_); resized();
    }
    void showAutomationPage() { if (automationPanel_) selectPage(Page::automation); }

private:
    enum class Page { edit, padFx, groupFx, sequencer, automation };

    void selectPage(Page page);
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    fui::DrumUiModel& model_;
    fui::PadGrid pads;
    fui::PadStrip padStrip;
    fui::DrumOscPanel oscA, oscB;
    fui::DrumNoisePanel noise;
    fui::DrumPitchEnvPanel pitchEnv;
    fui::DrumAmpEnvPanel ampEnv;
    fui::DrumFilterPanel filter;
    fui::DrumModPanel mod;
    fui::SelBarView selBar;
    fui::StepSeqView stepSeq;
    fui::KitMixPanel kitMix;    // outlives fxRack, which shows it in GROUP FX
    fui::FxChain fxRack;
    fui::DrumFxRack routing;
    juce::TextButton editPage_ { "EDIT" };
    juce::TextButton padFxPage_ { "PAD FX" };
    juce::TextButton groupFxPage_ { "GROUP FX" };
    juce::TextButton sequencerPage_ { "SEQUENCER" };
    juce::TextButton automationPage_ { "AUTOMATION" };
    juce::Component* automationPanel_ = nullptr;
    float displayScale_ = 1.0f;
    Page page_ = Page::edit;
};
