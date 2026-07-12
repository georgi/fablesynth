#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../SeqProcessor.h"
#include "../../ui/Theme.h"

// SQ-4 now-playing footer — port of src/seq/components/FooterRow.tsx: a
// STOP ALL button + live-scene chips on the left, then one cell per track
// showing its owning scene/clip/bar position and a VU meter driven by the
// audio thread's trackRms atomics. Painted directly and hit-tested in
// mouseDown, same scheme as the other SQ-4 strips.
namespace fui {

class SeqFooterView : public juce::Component, private juce::Timer {
public:
    explicit SeqFooterView(SeqAudioProcessor&);
    ~SeqFooterView() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

    // Test handles (also the real click targets, wired from mouseDown).
    void stopAllClick();
    void trackStopClick(int t);

private:
    void timerCallback() override;

    SeqAudioProcessor& proc;

    juce::Rectangle<int> masterArea, stopAllBtn, chipsArea;
    juce::Rectangle<int> cellArea[4], cellStopBtn[4], nowArea[4], vuArea[4];

    // slow-fall VU state (message-thread copy, ticked at 30 Hz).
    float vuLevel_[4] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SeqFooterView)
};

} // namespace fui
