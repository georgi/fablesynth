#pragma once

#include "Panels.h"
#include "NoteSeqView.h"
#include "WtUiModel.h"
#include "FxChain.h"
#include "ArpPanel.h"
#include "LabPanel.h"

// Reusable WT-1 sound-design and note-sequencer surface. The standalone
// editor adds its own header around this body; SQ-4 embeds the body directly.
class WtDeviceBody : public juce::Component {
public:
    static constexpr int LW = 1520, LH = 614; // tab pages end at y = 600 + 14 px padding

    explicit WtDeviceBody(fui::WtUiModel&,
                          std::function<HostTransport()> transportProvider = {});
    void resized() override;

    void setAutomationPanel(juce::Component& panel) {
        automationPanel_=&panel; addChildComponent(panel); addAndMakeVisible(automationPage_); resized();
    }
    void showAutomationPage() { if (automationPanel_) selectPage(Page::automation); }
    // SQ-4 scales the body to its width; the ARP page may then extend down to
    // logical y (the visible bottom) instead of ending at the fixed y = 600.
    // SQ-4 hosts DR-1/BL-1 on narrower logical canvases, so at the same
    // on-screen width their tab rows render larger; this scales the tab row
    // and voice controls (about their outer corners) to match.
    void setHostedChromeScale(float k) {
        if (k == chromeScale_) return;
        chromeScale_ = k;
        resized();
    }
    void setHostedPageBottom(int y) {
        if (y == hostedPageBottom_) return;
        hostedPageBottom_ = y;
        const int h = std::max(LH, y + 14);
        if (h != getHeight()) setSize(LW, h); else resized();
    }
    std::function<void(int)> onEditTable;
    fui::NoteSeqView& noteSeq() { return seq; }

private:
    enum class Page { edit, fx, lab, sequencer, arp, chords, automation };

    juce::Rectangle<int> colArea(int c0, int span, int y, int h) const;
    void selectPage(Page page);

    fui::WtUiModel& model_;
    fui::OscPanel oscA, oscB;
    fui::UtilPanel util;
    fui::FilterPanel filter;
    fui::EnvPanel env1, env2;
    fui::LfoPanel lfos;
    fui::MatrixPanel matrix;
    fui::FxChain fx;
    fui::NoteSeqView seq;
    fui::ArpPanel arp;
    int hostedPageBottom_ = 0;
    float chromeScale_ = 1.0f;
    fui::LabPanel lab;
    juce::TextButton editPage_ { "EDIT" };
    juce::TextButton fxPage_ { "FX CHAIN" };
    juce::TextButton labPage_ { "LAB" };
    juce::TextButton sequencerPage_ { "SEQUENCER" };
    juce::TextButton arpPage_ { "ARP" };
    juce::TextButton chordsPage_ { "CHORDS" };
    fui::PlaybackToggles playback_;
    juce::TextButton automationPage_{"AUTOMATION"};
    juce::Component* automationPanel_=nullptr;
    Page page_ = Page::edit;
    // Voice mode beside the page tabs (web keyboard bar): GLIDE, MONO, and
    // LEGATO, which only applies in mono. Here so hosted SQ-4 tracks get it too.
    fui::Knob glide_;
    juce::Label glideLabel_ { {}, "GLIDE" };
    fui::PillButton mono_ { "MONO", fui::PillButton::Tag, fui::col::acN }, legato_ { "LEGATO", fui::PillButton::Tag, fui::col::acN };
    std::unique_ptr<juce::ParameterAttachment> monoAtt_, legatoAtt_;
};
