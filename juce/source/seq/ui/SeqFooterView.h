#pragma once
#include "../../ui/VisualTimer.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>
#include "../SeqProcessor.h"
#include "../../ui/Theme.h"

// SQ-4 now-playing footer — port of src/seq/components/FooterRow.tsx: a
// STOP ALL button + live-scene chips on the left, then one cell per track
// showing its owning scene/clip/bar position and a VU meter driven by the
// audio thread's trackRms atomics. Painted directly and hit-tested in
// mouseDown, same scheme as the other SQ-4 strips.
namespace fui {

class SeqFooterView : public juce::Component, private fui::VisualTimer {
public:
    explicit SeqFooterView(SeqAudioProcessor&);
    ~SeqFooterView() override { stopTimer(); }

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

    // Test handles (also the real click targets, wired from mouseDown).
    void stopAllClick();
    void trackStopClick(int t);
    int cacheBuildCountForTesting() const { return cacheBuildCount_; }
    juce::Point<int> cachePixelSizeForTesting() const { return { artwork_.getWidth(), artwork_.getHeight() }; }
    void tickForTesting() { timerCallback(); }
    float meterLevelForTesting(int t) const { return vuLevel_[t]; }

private:
    void visualVisibilityChanged(bool showing) override { if (!showing) for (auto& level : vuLevel_) level = 0.0f; }
    void timerCallback() override;
    struct PaintState {
        struct Chip {
            juce::String text;
            int width = 0;
            bool operator==(const Chip& other) const { return text == other.text && width == other.width; }
        };
        std::vector<Chip> chips;
        bool anyLive = false;
        int tracks = 0;
        std::array<juce::String, 4> labels;
        std::array<bool, 4> live {}, audible {};
        std::array<juce::uint32, 4> colours {};
        bool operator==(const PaintState& other) const {
            return chips == other.chips && anyLive == other.anyLive && tracks == other.tracks
                && labels == other.labels && live == other.live && audible == other.audible
                && colours == other.colours;
        }
        bool operator!=(const PaintState& other) const { return !(*this == other); }
    };
    PaintState paintState() const;
    void paintArtwork(juce::Graphics&, const PaintState&);
    void paintMeters(juce::Graphics&, const PaintState&);

    SeqAudioProcessor& proc;

    juce::Rectangle<int> masterArea, stopAllBtn, chipsArea;
    juce::Rectangle<int> cellArea[4], cellStopBtn[4], nowArea[4], vuArea[4];

    // slow-fall VU state (message-thread copy, ticked at the animation cadence).
    float vuLevel_[4] {};

    PaintState lastState_, cachedState_;
    juce::Image artwork_;
    float artworkScale_ = 0.0f;
    int artworkWidth_ = 0, artworkHeight_ = 0, cacheBuildCount_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SeqFooterView)
};

} // namespace fui
