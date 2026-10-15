#pragma once
#include "../../ui/VisualTimer.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include "../SeqProcessor.h"
#include "../../ui/Theme.h"

#include <functional>
#include <tuple>

// SQ-4 top bar — port of src/seq/components/Header.tsx + Scope.tsx: logo,
// combined play/stop transport, launch quantize stepper,
// beat dots + bar/BPM readout from the shared timebase, a master scope, and
// the SWING/VOL master knobs. Everything is drawn directly in paint() and hit
// -tested in mouseDown/Drag — no juce::Button/Knob children — since the whole
// slot is a single 1254x44 strip and every element is either static text or a
// small drag/click target.
namespace fui {

class SeqHeader : public juce::Component, private fui::VisualTimer {
public:
    explicit SeqHeader(SeqAudioProcessor&);
    ~SeqHeader() override { stopTimer(); library_.setLookAndFeel(nullptr); }

    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    // Test handles (also the real click targets, wired from mouseDown).
    void playClick();
    void quantStep(int d);

    // Session import/export — web-compatible SessionDoc v:1 JSON (no web
    // equivalent; JUCE-only surface). Test handle == real click target: the
    // async juce::FileChooser lambda calls straight into
    // proc.applySessionJson()/currentSessionJson(), so tests exercise those
    // directly without opening an OS dialog.
    void loadClick();
    void saveClick();
    void selectLibrarySession(int index);

    // Exact text painted in the quant value pill (extracted from paintQuant()
    // so HintBar's session-mode copy can quote the live quant setting).
    juce::String quantLabel() const;

    std::function<void()> onLibrarySessionChanged;
    std::function<void()> onMasterFxToggle;
    std::function<bool()> isMasterFxOpen; // lights MASTER FX while its view shows
    juce::ComboBox& libraryForTest() { return library_; }
    void syncLibraryForTest() { refreshLibrarySelection(); }
    int cacheBuildCountForTesting() const { return cacheBuildCount_; }
    int clockBuildCountForTesting() const { return clockBuildCount_; }
    juce::Rectangle<int> clockPixelBoundsForTesting() const { return clockPixelBounds_; }
    int scopeLabelBuildCountForTesting() const { return scopeLabelBuildCount_; }

private:
    void timerCallback() override;
    void refreshLibrarySelection();
    // Stationary artwork, clock, and scope have independent damage paths.
    juce::uint32 paintSignature() const;
    juce::Rectangle<int> clockDamage() const;

    float swingValue() const;                  // 0..1, from the conductor
    float volValue() const;                    // 0..1, from the "master" APVTS param
    juce::RangedAudioParameter* volParam() const;

    void paintHeader(juce::Graphics&);
    void paintHeaderForeground(juce::Graphics&);
    void paintScopeLabel(juce::Graphics&);
    juce::Image headerLayer_, headerForegroundLayer_, clockLayer_, composedHeaderLayer_, scopeLabelLayer_;
    float headerScale_ = 0, clockScale_ = 0, scopeLabelScale_ = 0;
    juce::Colour headerAccent_;
    std::tuple<bool, int, float, float> cachedState_;
    std::tuple<bool, int, int, int> lastClockState_, cachedClockState_;
    bool lastScopeLive_ = false;
    juce::Rectangle<int> lastClockDamage_, clockPixelBounds_;
    int cacheBuildCount_ = 0, clockBuildCount_ = 0, scopeLabelBuildCount_ = 0;

    void paintButtons(juce::Graphics&);
    void paintQuant(juce::Graphics&);
    void paintClock(juce::Graphics&);
    void paintScope(juce::Graphics&);
    void paintKnob(juce::Graphics&, juce::Rectangle<int>, const juce::String& label, float v);

    SeqAudioProcessor& proc;

    juce::Rectangle<int> logoArea, playBtn, quantTagArea, quantPrevBtn, quantValArea,
        quantNextBtn, beatsArea, clockLineArea, libraryLabelArea, loadBtn, saveBtn,
        scopeArea, swingKnob, volKnob, masterFxBtn;

    juce::ComboBox library_;
    int shownLibrarySession_ = -2;
    int libraryPollTick_ = 0;

    std::unique_ptr<juce::FileChooser> chooser_;

    enum class Drag { None, Swing, Vol };
    Drag dragging_ = Drag::None;
    float lastY_ = 0;

    juce::uint32 lastSig_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SeqHeader)
};

} // namespace fui
