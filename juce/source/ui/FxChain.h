#pragma once
#include "Controls.h"
#include "DeviceUiModel.h"
#include <deque>

namespace fui {

// Shared native ports of EqPanel, DynamicsView, TapeEchoPanel and ReverbPanel.
// The web's drawing coordinates are retained; only the surrounding rack reflows.
class FxModuleView : public juce::Component, private juce::Timer {
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

  private:
    using Meter = fable::FxTelemetry;
    void timerCallback() override;
    float value(const juce::String &) const;
    void write(const juce::String &, float, bool gesture = true);
    void selectBand(int);
    void resetBand();
    void finishDrag();
    void drawEq(juce::Graphics &, float width, float height);
    void drawDynamics(juce::Graphics &, float w, float h);
    void drawEcho(juce::Graphics &, float w, float h);
    void drawReverb(juce::Graphics &, float w, float h);
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
    juce::TextButton bandOn_{"BAND 2"}, reset_{"RESET"}, sync_{"SYNC"};
    juce::ComboBox shape_, mode_, division_;
    juce::Rectangle<int> plot_, readouts_, controls_, footer_, caption_;
    Meter data_;
    std::deque<Meter> history_;
    uint64_t serial_ = 0;
    double lastReceived_ = 0;
    bool dragging_ = false;
    juce::Point<float> dragPoint_;
};

class FxChain : public juce::Component {
  public:
    FxChain(DeviceUiModel &, bool tape);
    void setPad(int pad, const juce::String &name);
    void resized() override;
    void paint(juce::Graphics &) override;

  private:
    void rebuild();
    DeviceUiModel &model_;
    bool tape_;
    int pad_ = -1;
    juce::String padName_;
    juce::OwnedArray<FxModuleView> modules_;
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
