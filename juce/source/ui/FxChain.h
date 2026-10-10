#pragma once
#include "VisualTimer.h"
#include "LiveSpectrum.h"
#include "Controls.h"
#include "DeviceUiModel.h"
#include "LabPanel.h"
#include "AutoMix.h"
#include <deque>

namespace fui {

// One current raster per readout, never a history of telemetry strings.
class FxReadoutCache {
  public:
    void draw(juce::Graphics&, const juce::String&, const juce::Font&, juce::Colour,
              juce::Rectangle<float>, juce::Justification = juce::Justification::centred,
              const juce::Component* owner = nullptr);
    int buildCountForTesting() const { return builds_; }
  private:
    juce::Image image_;
    juce::String text_;
    juce::Font font_;
    juce::Colour colour_;
    juce::Rectangle<float> bounds_;
    juce::Point<float> origin_;
    float scale_ = 0;
    int alignment_ = 0, builds_ = 0;
};

// Shared native ports of the web EQ, dynamics, Drive, Chorus, Echo and Reverb panels.
// The web's drawing coordinates are retained; only the surrounding rack reflows.
class FxModuleView : public juce::Component, private fui::VisualTimer {
  public:
    enum Kind { Eq, Ott, Comp, Drive, Chorus, Echo, Reverb };
    FxModuleView(DeviceUiModel &, Kind, bool tape, juce::String prefix = {}, int pad = 0);
    ~FxModuleView() override;
    void paint(juce::Graphics &) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent &) override;
    void mouseDrag(const juce::MouseEvent &) override;
    void mouseUp(const juce::MouseEvent &) override;
    void mouseDoubleClick(const juce::MouseEvent &) override;
    void mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &) override;
    bool keyPressed(const juce::KeyPress &) override;
    Kind kind() const { return kind_; }
    // AUTO (EQ and COMP): swaps the controls for the learn strip.
    void setAutoOpen(bool);
    bool autoOpen() const { return autoOpen_; }
    AutoEqStrip* autoEqStrip() { return autoEq_.get(); }
    // EQ only: the live spectrum behind the curve (null for other kinds).
    const LiveSpectrum* liveSpectrum() const { return liveTick_ ? &live_ : nullptr; }
    void updateLiveSpectrum(double nowMs);
    AutoCompStrip* autoCompStrip() { return autoComp_.get(); }
    SideChainRow* sideChainRow() { return sideChain_.get(); }
    Knob* keyHpKnob() { return keyHp_.get(); }

  private:
    using Meter = fable::FxTelemetry;
    void timerCallback() override;
    void visualVisibilityChanged(bool visible) override {
        if (!visible) { history_.clear(); lastReceived_ = 0; }
    }
    juce::String headerCaption() const;
    void drawStaticLayer(juce::Graphics&, const juce::String&);
    juce::Image staticLayer_;
    juce::String cachedCaption_;
    float staticScale_ = 0;
    bool staticLayerDirty_ = true;
    std::array<float, 24> lastPreviewState_{};
    bool previewStateValid_ = false;
    std::array<FxReadoutCache, 4> readoutCaches_;
    std::array<FxReadoutCache, 3> eqValueCaches_;
    FxReadoutCache correlationCache_;
    float displayValue(const juce::String&) const;
    float value(const juce::String &) const;
    void write(const juce::String &, float, bool gesture = true);
    void selectBand(int);
    void resetBand();
    void finishDrag();
    void drawEq(juce::Graphics &, float width, float height);
    void drawDynamics(juce::Graphics &, float w, float h);
    void drawDrive(juce::Graphics &, float w, float h);
    void drawChorus(juce::Graphics &, float w, float h);
    void drawEcho(juce::Graphics &, float w, float h);
    void drawReverb(juce::Graphics &, float w, float h);
    void applyAutoVisibility();
    int scopePad() const { return prefix_.isEmpty() ? -1 : pad_; }
    int hitBand(juce::Point<float>) const;
    juce::String bandKey(const char *field) const;
    float echoTime() const;
    DeviceUiModel &model_;
    ParameterSource source_;
    Kind kind_;
    bool tape_;
    juce::String prefix_, effect_;
    int pad_ = 0, bus_ = 0, selected_ = 1;
    PowerButton power_;
    juce::OwnedArray<Knob> knobs_;
    std::array<juce::TextButton, 4> bands_;
    std::array<juce::TextButton, 3> driveTypes_;
    juce::TextButton bandOn_{"BAND 2"}, reset_{"RESET"}, sync_{"SYNC"};
    juce::ComboBox shape_, mode_, division_;
    juce::Rectangle<int> plot_, readouts_, controls_, footer_, caption_;
    Meter data_;
    std::deque<Meter> history_;
    uint64_t serial_ = 0;
    double lastReceived_ = 0;
    bool dragging_ = false;
    juce::Point<float> dragPoint_;
    juce::TextButton auto_{"AUTO"};
    bool autoOpen_ = false, cachedKeyed_ = false;
    std::unique_ptr<AutoEqStrip> autoEq_;
    std::unique_ptr<AutoCompStrip> autoComp_;
    std::unique_ptr<SideChainRow> sideChain_;
    std::unique_ptr<Knob> keyHp_;   // the fifth COMP knob while keyed
    juce::Rectangle<int> autoArea_, sideChainArea_;
    LiveSpectrum live_;
    std::unique_ptr<VisualTick> liveTick_;
};

class FxChain : public juce::Component {
  public:
    FxChain(DeviceUiModel &, bool tape);
    void setPad(int pad, const juce::String &name);
    // An extra panel for the unscoped (group) strip, toggled by its own head
    // button. DR-1 passes KIT MIX; it replaces the lower module row when on.
    void setGroupPanel(juce::Component *panel, const juce::String &buttonText);
    bool groupPanelVisible() const { return groupPanel_ && groupPanelOn_ && pad_ < 0; }
    void showGroupPanel(bool);
    FxModuleView *module(FxModuleView::Kind);
    void resized() override;
    void paint(juce::Graphics &) override;

  private:
    void rebuild();
    DeviceUiModel &model_;
    bool tape_;
    int pad_ = -1;
    juce::String padName_;
    juce::OwnedArray<FxModuleView> modules_;
    std::unique_ptr<LabPanel> lab_;
    juce::TextButton labButton_ { "LAB" };
    bool labVisible_ = false;
    juce::Component *groupPanel_ = nullptr;
    juce::TextButton groupButton_;
    bool groupPanelOn_ = false;
};

// Compact, keyboard-operable navigation shared by standalone and hosted bodies.
class DevicePageTabs : public juce::Component {
  public:
    DevicePageTabs();
    void resized() override;
    bool fxSelected() const { return fx_.getToggleState(); }
    std::function<void()> onChange;

  private:
    juce::TextButton sound_{"SOUND"}, fx_{"FX CHAIN"};
};
} // namespace fui
