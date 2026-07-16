#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include "ParameterSource.h"
#include <functional>

// Custom rack controls — faithful ports of the web components:
//   Knob.tsx, Stepper.tsx, PowerButton.tsx, VSlider.tsx
// All bind directly to an APVTS parameter by id.
namespace fui {

// Optional ParamInfo resolver consulted before the WT-1 fable::paramInfo()
// table when a control looks up its parameter metadata. The DR-1 editor
// installs a fable::drumParamInfo() lookup so the shared controls resolve its
// parameter ids (e.g. "master.swing", "pad3.lvl"); it is never installed in
// WT-1 binaries, so WT-1 behaviour is untouched. Return nullptr for unknown
// ids to fall through to the WT-1 table.
using ParamInfoResolver = const fable::ParamInfo* (*)(const std::string&);
void setParamInfoResolver(ParamInfoResolver);

// ---- rotary knob ----------------------------------------------------------
// modDest > 0 turns the knob into a modulation target: it accepts source-chip
// drops (juce::DragAndDropTarget), paints one depth ring per slot whose dst ==
// modDest, and lets the rings be depth-dragged / right-click-cleared.
class Knob : public juce::Component, public juce::DragAndDropTarget, private juce::Timer {
public:
    enum Size { Lg, Md, Sm, Xs };
    Knob(juce::AudioProcessorValueTreeState& s, const juce::String& paramId,
         Size sz, Accent accent, bool showLabel = true, int modDest = 0);
    Knob(ParameterSource, const juce::String& paramId,
         Size sz, Accent accent, bool showLabel = true, int modDest = 0);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // juce::DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

    static int svgPx(Size s) { return s == Lg ? 74 : s == Md ? 56 : s == Sm ? 44 : 34; }

private:
    void  timerCallback() override;
    void  nudge(float deltaNorm);
    float norm() const { return param ? param->getValue() : 0.0f; }
    void  rebuildRings();                 // refresh rings_ from active slots
    juce::uint64 ringSignature() const;   // cheap hash of this dest's active slots

    ParameterSource parameters;
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

    // ---- modulation target state ----
    int  modDest_ = 0;
    bool dragHover_ = false;
    struct Ring { int slot, src; float amt; };
    std::vector<Ring> rings_;
    int  grabbedRing_ = -1;
    juce::RangedAudioParameter* grabbedAmt_ = nullptr; // amt param being depth-dragged
    juce::uint64 lastRingSig_ = ~(juce::uint64)0;
    // Cached mat src/dst/amt params (16 slots x 3 = 48), indexed [slot-1][field].
    juce::RangedAudioParameter* matParams_[16][3] = {};
};

// ---- enum stepper  (◂ VALUE ▸) -------------------------------------------
class Stepper : public juce::Component, private juce::Timer {
public:
    Stepper(juce::AudioProcessorValueTreeState&, const juce::String& paramId, Accent accent);
    Stepper(ParameterSource, const juce::String& paramId, Accent accent);
    void paint(juce::Graphics&) override;
    void resized() override;

    // Optional dynamic overrides for table steppers: cycle only over the live
    // count of selectable tables and show the live (user) table name. Default
    // (unset) uses the param's fixed choice list. In the integer-range mode
    // nameProvider formats the raw value instead (e.g. seq.root -> note name).
    std::function<int()>               countProvider;
    std::function<juce::String(int)>   nameProvider;
private:
    void timerCallback() override;
    void step(int d);
    int  choiceCount() const;
    juce::String displayName() const;
    ParameterSource parameters;
    juce::String id;
    juce::AudioParameterChoice* choice = nullptr;
    // Set when the param is not a choice (e.g. unison: AudioParameterFloat with
    // Int curve, range 1-16). Drives an integer-range stepping mode.
    juce::RangedAudioParameter* ranged = nullptr;
    juce::Colour accent;
    juce::TextButton prev{"<"}, next{">"};
    int lastIndex = -1;
    float lastValue = -1.0f;   // normalized value cache for the numeric mode
};

// ---- power LED toggle -----------------------------------------------------
class PowerButton : public juce::Component, private juce::Timer {
public:
    PowerButton(juce::AudioProcessorValueTreeState&, const juce::String& paramId, Accent accent);
    PowerButton(ParameterSource, const juce::String& paramId, Accent accent);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool isOn() const;
private:
    void timerCallback() override;
    ParameterSource parameters;
    juce::String id;
    juce::RangedAudioParameter* param = nullptr;
    juce::Colour accent;
    bool last = false;
};

// ---- vertical slider (wavetable POS, with modulated ghost) -----------------
// modDest > 0 turns the slider into a modulation target: it accepts source-chip
// drops, paints a side depth band per slot whose dst == modDest, and lets each
// band be depth-dragged / right-click-cleared.
class VSlider : public juce::Component, public juce::DragAndDropTarget, private juce::Timer {
public:
    VSlider(juce::AudioProcessorValueTreeState&, const juce::String& paramId, Accent accent,
            std::function<float()> ghostProvider, int modDest = 0);
    VSlider(ParameterSource, const juce::String& paramId, Accent accent,
            std::function<float()> ghostProvider, int modDest = 0);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // juce::DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;
private:
    void timerCallback() override;
    void moveTo(float y);
    juce::Rectangle<float> trackArea() const;
    void rebuildRings();
    juce::uint64 ringSignature() const;

    ParameterSource parameters;
    juce::String id;
    juce::RangedAudioParameter* param = nullptr;
    juce::Colour accent;
    std::function<float()> ghost;
    float lastNorm = -1, lastGhost = -2, lastY = 0;

    // ---- modulation target state ----
    int  modDest_ = 0;
    bool dragHover_ = false;
    struct Ring { int slot, src; float amt; };
    std::vector<Ring> rings_;
    int  grabbedRing_ = -1;
    juce::RangedAudioParameter* grabbedAmt_ = nullptr; // amt param being depth-dragged
    juce::uint64 lastRingSig_ = ~(juce::uint64)0;
    juce::RangedAudioParameter* matParams_[16][3] = {};
};

} // namespace fui
