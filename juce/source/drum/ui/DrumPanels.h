#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "DrumUiModel.h"
#include "../../ui/Controls.h"

// Selected-pad editor panels — ports of src/drum/components/{OscSection,
// NoiseSection, PitchEnvPanel, AmpEnvPanel, FilterSection, ModPanel}.tsx and
// their canvas views {WavetableView (shared), NoiseView, DrumEnvView,
// FilterView (shared)}. Every control binds to a "pad<sel>.<field>" APVTS id;
// PadBoundPanel destroys and recreates the children whenever the processor
// broadcasts a selection change, rebinding them to the new pad's ids.
namespace fui {

// ---- live views (pad-relative, recreated on selection change) --------------

// 3D wavetable terrain for one drum oscillator. Mirrors WavetableView.cpp's
// perspective drawing, but that class is hard-bound to FableAudioProcessor —
// this one reads DrumAudioProcessor::tableAt() via the pad's table param and
// highlights the live frame from getVizPos(osc) (POS param when idle).
class DrumTerrainView : public juce::Component, private juce::Timer {
public:
    DrumTerrainView(DrumUiModel&, int pad, int osc, juce::Colour accent);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    int   tableIndex() const;
    float knobPos() const;
    DrumUiModel& proc;
    int osc;
    juce::String tableId, posId;
    juce::Colour accent;
    float lastShown = -2.0f;
    int   lastTable = -1;
    juce::Image farCache;              // static far-frame pass, re-rendered on table change
    int cacheTable = -1, cacheGen = -1, cacheW = 0, cacheH = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumTerrainView)
};

// Animated noise tilt-response curve — port of NoiseView.tsx (seeded random
// walk reseeded every 66 ms, smoothing from the COLOR param).
class DrumNoiseView : public juce::Component, private juce::Timer {
public:
    DrumNoiseView(DrumUiModel&, int pad);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); } // free-running, like the web rAF
    DrumUiModel& proc;
    juce::String colorId;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumNoiseView)
};

// Pitch (exp sweep around a zero line) / AHD envelope shape — port of
// DrumEnvView.tsx, plus a hit pulse from the processor's env viz atomic
// (getVizEnv): the trace glows and the AHD view shows the live level line.
class DrumEnvView : public juce::Component, private juce::Timer {
public:
    enum Mode { Pitch, Ahd };
    DrumEnvView(DrumUiModel&, int pad, Mode, juce::Colour accent);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    float val(const juce::String& id) const;   // real (denormalised) value
    DrumUiModel& proc;
    juce::String base;                          // "pad<i>."
    Mode mode;
    juce::Colour accent;
    float last[5] = { -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumEnvView)
};

// Single-filter magnitude response — the drum FilterSection feeds the shared
// web FilterView one filter + route 0; same analog-prototype math (types 0-4
// = LP12/LP24/BP12/HP12/NOTCH, FilterView.tsx magFor).
class DrumFilterView : public juce::Component, private juce::Timer {
public:
    DrumFilterView(DrumUiModel&, int pad);
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    DrumUiModel& proc;
    juce::String base;                          // "pad<i>.flt."
    float sig = -1.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumFilterView)
};

// ---- rebuild-on-selection plumbing ------------------------------------------

// Base for every pad-bound panel: listens to the processor's selection
// broadcaster and calls rebuild() (which recreates all children with
// "pad<sel>.<field>" ids) on every change. Subclasses call rebuild()
// once at the end of their own constructor.
class PadBoundPanel : public juce::Component, private juce::ChangeListener {
public:
    explicit PadBoundPanel(DrumUiModel&);
    ~PadBoundPanel() override;
protected:
    juce::String pid(const char* field) const;  // "pad<selected>.<field>"
    virtual void rebuild() = 0;
    DrumUiModel& proc;
private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadBoundPanel)
};

// ---- panels -----------------------------------------------------------------

// OSC A (cyan) / SAMPLE (amber): wavetable terrain or one-shot waveform with
// live playhead, vertical POS/START slider and layer-specific controls.
class DrumOscPanel : public PadBoundPanel {
public:
    DrumOscPanel(DrumUiModel&, int osc);   // 0 = A, 1 = B
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void rebuild() override;
    int osc;
    Accent ac;
    std::unique_ptr<Stepper> table;
    std::unique_ptr<DrumTerrainView> wt;
    std::unique_ptr<VSlider> pos;
    juce::OwnedArray<Knob> knobs;                 // tune fine phase uni det lvl
    juce::Rectangle<int> headArea;
};

// NOISE + RING: tilt curve, COLOR/LVL, and fixed-Hz ring carrier FREQ/MIX.
class DrumNoisePanel : public PadBoundPanel {
public:
    explicit DrumNoisePanel(DrumUiModel&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void rebuild() override;
    std::unique_ptr<DrumNoiseView> view;
    juce::OwnedArray<Knob> knobs;                 // color, noise lvl, ring freq, ring mix
    juce::Rectangle<int> headArea;
};

// PITCH ENV: sweep view + AMT / DEC knobs (cyan).
class DrumPitchEnvPanel : public PadBoundPanel {
public:
    explicit DrumPitchEnvPanel(DrumUiModel&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void rebuild() override;
    std::unique_ptr<DrumEnvView> view;
    juce::OwnedArray<Knob> knobs;                 // amt, dec
    juce::Rectangle<int> headArea;
};

// AMP ENV: AHD shape view + ATT / HOLD / DEC / CURVE knobs, "AHD · ONE-SHOT" hint.
class DrumAmpEnvPanel : public PadBoundPanel {
public:
    explicit DrumAmpEnvPanel(DrumUiModel&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void rebuild() override;
    std::unique_ptr<DrumEnvView> view;
    juce::OwnedArray<Knob> knobs;                 // att, hold, dec, curve
    juce::Rectangle<int> headArea;
};

// FILTER: LED + title + power + type stepper, response view, CUT/RES/DRIVE
// knobs. While off, the view + knobs dim to 40% (web .panel.off).
class DrumFilterPanel : public PadBoundPanel, private juce::Timer {
public:
    explicit DrumFilterPanel(DrumUiModel&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void rebuild() override;
    void timerCallback() override;                // apply the .off dimming
    std::unique_ptr<PowerButton> power;
    std::unique_ptr<Stepper> type;
    std::unique_ptr<DrumFilterView> view;
    juce::OwnedArray<Knob> knobs;                 // cut, res, drive
    juce::Rectangle<int> headArea;
    int lastOn = -1;
};

// MOD: 4 src ▸ dst + amount rows (web <select>s become steppers) and the
// MOD ENV DEC knob + readout in the head.
class DrumModPanel : public PadBoundPanel, private juce::Timer {
public:
    explicit DrumModPanel(DrumUiModel&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void rebuild() override;
    void timerCallback() override;                // MOD ENV DEC readout refresh
    struct Row {
        std::unique_ptr<Stepper> src, dst;
        std::unique_ptr<Knob> amt;
    };
    std::array<Row, 4> rows;
    std::unique_ptr<Knob> decKnob;
    juce::Rectangle<int> headArea, decHintArea;
    std::array<juce::Rectangle<int>, 4> arrowAreas;
    float lastDec = -1.0f;
};

} // namespace fui
