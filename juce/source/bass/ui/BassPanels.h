#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "BassUiModel.h"
#include "../../ui/Controls.h"

// BL-1 editor panels — ports of src/bass/components/{OscSection, SubSection,
// FilterSection, EnvPanel, LfoPanel (Lfo + Accent), KeysPanel}.tsx and their
// canvas views. Every control binds to a flat APVTS id (single mono voice —
// no pad rebinding like DR-1).
namespace fui {

// ---- live views -------------------------------------------------------------

// 3D wavetable terrain for the single BL-1 oscillator — same perspective
// drawing as DrumTerrainView, reading BassAudioProcessor::tableAt() and
// highlighting the live frame from getVizPos() (POS param when idle).
class BassTerrainView : public juce::Component, private juce::Timer {
public:
    explicit BassTerrainView(BassUiModel&);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    int   tableIndex() const;
    float knobPos() const;
    BassUiModel& proc;
    float lastShown = -2.0f;
    int   lastTable = -1;
    juce::Image farCache;              // static far-frame pass, re-rendered on table change
    int cacheTable = -1, cacheW = 0, cacheH = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassTerrainView)
};

// Filter magnitude response that tracks the worklet's swept cutoff while the
// voice is sounding — port of FilterSection.tsx FilterView (q = 0.7 + res*9
// prototype, LP24 = squared slope, live vizCut).
class BassFilterView : public juce::Component, private juce::Timer {
public:
    explicit BassFilterView(BassUiModel&);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    BassUiModel& proc;
    float sig = -1.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassFilterView)
};

// Both envelopes in one display — port of EnvPanel.tsx EnvView: filter AD
// (green) with its accent-shortened ghost (dashed amber), amp ADSR (pale).
class BassEnvView : public juce::Component, private juce::Timer {
public:
    explicit BassEnvView(BassUiModel&);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    float val(const char* id) const;
    BassUiModel& proc;
    float last[7] = { -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassEnvView)
};

// LFO waveform preview — port of LfoPanel.tsx LfoView; animates in sync with
// the sequencer tempo while playing.
class BassLfoView : public juce::Component, private juce::Timer {
public:
    explicit BassLfoView(BassUiModel&);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); } // free-running, like the web rAF
    BassUiModel& proc;
    juce::uint32 t0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassLfoView)
};

// ---- panels -----------------------------------------------------------------

// OSC: LED + title + table stepper head, terrain view with live POS, vertical
// POS slider, TUNE/FINE/UNI/DET/SPRD/LVL knobs.
class BassOscPanel : public juce::Component {
public:
    explicit BassOscPanel(BassUiModel&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    Stepper table;
    BassTerrainView wt;
    VSlider pos;
    juce::OwnedArray<Knob> knobs;      // tune fine unison detune spread level
    juce::Rectangle<int> headArea;
};

// SUB: SHAPE + OCT stepper rows, LVL knob.
class BassSubPanel : public juce::Component {
public:
    explicit BassSubPanel(BassUiModel&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    Stepper shape, oct;
    Knob level;
    juce::Rectangle<int> headArea, bodyArea, shapeLabel, octLabel;
};

// FILTER: LED + title + type stepper, live response view, CUT/RES/DRIVE/ENV/
// TRACK knobs.
class BassFilterPanel : public juce::Component {
public:
    explicit BassFilterPanel(BassUiModel&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    Stepper type;
    BassFilterView view;
    juce::OwnedArray<Knob> knobs;      // cut res drive env track
    juce::Rectangle<int> headArea;
};

// ENV: dual-envelope view + F-ATT/F-DEC (green) and ATT/DEC/SUS/REL knobs.
class BassEnvPanel : public juce::Component {
public:
    explicit BassEnvPanel(BassUiModel&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    BassEnvView view;
    juce::OwnedArray<Knob> knobs;      // fenv.att fenv.dec aenv.att/dec/sus/rel
    juce::Rectangle<int> headArea;
};

// LFO: waveform view + RATE/SHAPE steppers, DEPTH knob.
class BassLfoPanel : public juce::Component {
public:
    explicit BassLfoPanel(BassUiModel&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    BassLfoView view;
    Stepper rate, shape;
    Knob depth;
    juce::Rectangle<int> headArea;
};

// ACCENT · SLIDE: ACC AMT (large) + SLD TIME knobs, one-knob hint.
class BassAccentPanel : public juce::Component {
public:
    explicit BassAccentPanel(BassUiModel&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    Knob acc, slide;
    juce::Rectangle<int> headArea, hintArea;
};

// KEYS: two-octave audition keyboard. Click plays when the sequencer is
// stopped; overlapping presses are legato = slide. While playing it just
// mirrors the sequenced note (port of KeysPanel.tsx).
class BassKeysPanel : public juce::Component, private juce::Timer {
public:
    explicit BassKeysPanel(BassUiModel&);
    ~BassKeysPanel() override { stopTimer(); }
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    juce::Rectangle<float> keyBounds(int semi) const;  // public for the host test
    int hitKey(juce::Point<float>) const;              // -1 = none
private:
    void timerCallback() override;
    int  hotKey() const;                               // -100 = none
    BassUiModel& proc;
    juce::Rectangle<int> headArea, keysArea;
    int pressed_ = -1;                                 // key held by the mouse
    int lastHot_ = -100;
};

} // namespace fui
