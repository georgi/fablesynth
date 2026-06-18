#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include <functional>

// Custom rack controls — faithful ports of the web components:
//   Knob.tsx, Stepper.tsx, PowerButton.tsx, VSlider.tsx
// All bind directly to an APVTS parameter by id.
namespace fui {

// ---- rotary knob ----------------------------------------------------------
class Knob : public juce::Component, private juce::Timer {
public:
    enum Size { Lg, Md, Sm, Xs };
    Knob(juce::AudioProcessorValueTreeState& s, const juce::String& paramId,
         Size sz, Accent accent, bool showLabel = true);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static int svgPx(Size s) { return s == Lg ? 74 : s == Md ? 56 : s == Sm ? 44 : 34; }

private:
    void  timerCallback() override;
    void  nudge(float deltaNorm);
    float norm() const { return param ? param->getValue() : 0.0f; }

    juce::AudioProcessorValueTreeState& apvts;
    juce::String id;
    juce::RangedAudioParameter* param = nullptr;
    juce::Colour accent;
    Size  size;
    bool  showLabel;
    bool  bipolar = false;
    float midNorm = 0.0f;
    juce::String label;
    bool  dragging = false;
    float lastY = 0, lastNorm = -1;
};

// ---- enum stepper  (◂ VALUE ▸) -------------------------------------------
class Stepper : public juce::Component, private juce::Timer {
public:
    Stepper(juce::AudioProcessorValueTreeState&, const juce::String& paramId, Accent accent);
    void paint(juce::Graphics&) override;
    void resized() override;

    // Optional dynamic overrides for table steppers: cycle only over the live
    // count of selectable tables and show the live (user) table name. Default
    // (unset) uses the param's fixed choice list.
    std::function<int()>               countProvider;
    std::function<juce::String(int)>   nameProvider;
private:
    void timerCallback() override;
    void step(int d);
    int  choiceCount() const;
    juce::String displayName() const;
    juce::AudioProcessorValueTreeState& apvts;
    juce::String id;
    juce::AudioParameterChoice* choice = nullptr;
    juce::Colour accent;
    juce::TextButton prev{"<"}, next{">"};
    int lastIndex = -1;
};

// ---- power LED toggle -----------------------------------------------------
class PowerButton : public juce::Component, private juce::Timer {
public:
    PowerButton(juce::AudioProcessorValueTreeState&, const juce::String& paramId, Accent accent);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool isOn() const;
private:
    void timerCallback() override;
    juce::AudioProcessorValueTreeState& apvts;
    juce::String id;
    juce::RangedAudioParameter* param = nullptr;
    juce::Colour accent;
    bool last = false;
};

// ---- vertical slider (wavetable POS, with modulated ghost) -----------------
class VSlider : public juce::Component, private juce::Timer {
public:
    VSlider(juce::AudioProcessorValueTreeState&, const juce::String& paramId, Accent accent,
            std::function<float()> ghostProvider);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    void timerCallback() override;
    void moveTo(float y);
    juce::Rectangle<float> trackArea() const;
    juce::AudioProcessorValueTreeState& apvts;
    juce::String id;
    juce::RangedAudioParameter* param = nullptr;
    juce::Colour accent;
    std::function<float()> ghost;
    float lastNorm = -1, lastGhost = -2;
};

} // namespace fui
