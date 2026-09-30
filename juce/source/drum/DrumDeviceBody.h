#pragma once

#include "ui/PadGrid.h"
#include "ui/PadStrip.h"
#include "ui/DrumPanels.h"
#include "ui/StepSeqView.h"
#include "ui/DrumFxRack.h"
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

private:
    enum class Page { edit, padFx, groupFx, sequencer };

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
    fui::FxChain fxRack;
    fui::DrumFxRack routing;
    juce::TextButton editPage_ { "EDIT" };
    juce::TextButton padFxPage_ { "PAD FX" };
    juce::TextButton groupFxPage_ { "GROUP FX" };
    juce::TextButton sequencerPage_ { "SEQUENCER" };
    float displayScale_ = 1.0f;
    Page page_ = Page::edit;
};
