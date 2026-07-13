#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../SeqProcessor.h"
#include "../../ui/Theme.h"

#include <functional>

// SQ-4 top bar — port of src/seq/components/Header.tsx + Scope.tsx: logo,
// transport (play/pause = ctx.suspend, stop-all), launch quantize stepper,
// beat dots + bar/BPM readout from the shared timebase, a master scope, and
// the SWING/VOL master knobs. Everything is drawn directly in paint() and hit
// -tested in mouseDown/Drag — no juce::Button/Knob children — since the whole
// slot is a single 1424x66 strip and every element is either static text or a
// small drag/click target.
namespace fui {

class SeqHeader : public juce::Component, private juce::Timer {
public:
    explicit SeqHeader(SeqAudioProcessor&);
    ~SeqHeader() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

    // Test handles (also the real click targets, wired from mouseDown).
    void playClick();
    void stopAllClick();
    void quantStep(int d);

    // Session import/export — web-compatible SessionDoc v:1 JSON (no web
    // equivalent; JUCE-only surface). Test handle == real click target: the
    // async juce::FileChooser lambda calls straight into
    // proc.applySessionJson()/currentSessionJson(), so tests exercise those
    // directly without opening an OS dialog.
    void loadClick();
    void saveClick();
    void selectLibrarySession(int index);

    std::function<void()> onLibrarySessionChanged;
    juce::ComboBox& libraryForTest() { return library_; }
    void syncLibraryForTest() { refreshLibrarySelection(); }

private:
    void timerCallback() override;
    void refreshLibrarySelection();

    float swingValue() const;                  // 0..1, from the conductor
    float volValue() const;                    // 0..1, from the "master" APVTS param
    juce::RangedAudioParameter* volParam() const;

    void paintButtons(juce::Graphics&);
    void paintQuant(juce::Graphics&);
    void paintClock(juce::Graphics&);
    void paintScope(juce::Graphics&);
    void paintKnob(juce::Graphics&, juce::Rectangle<int>, const juce::String& label, float v);

    SeqAudioProcessor& proc;

    juce::Rectangle<int> logoArea, playBtn, stopBtn, quantTagArea, quantPrevBtn, quantValArea,
        quantNextBtn, beatsArea, clockLineArea, libraryLabelArea, loadBtn, saveBtn,
        scopeArea, swingKnob, volKnob;

    juce::ComboBox library_;
    int shownLibrarySession_ = -2;

    std::unique_ptr<juce::FileChooser> chooser_;

    enum class Drag { None, Swing, Vol };
    Drag dragging_ = Drag::None;
    float lastY_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SeqHeader)
};

} // namespace fui
