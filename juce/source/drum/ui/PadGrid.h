#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "DrumUiModel.h"
#include "../dsp/DrumEngine.h"
#include "../../ui/Theme.h"
#include <array>
#include <set>

// 4x4 pad grid — port of src/drum/components/PadGrid.tsx (+ drum.css .pad*)
// and hooks/useDrumKeys.ts. Pad 01 is bottom-left (PAD_ORDER (3-row)*4+col).
// Tiles show number, hit LED (180 ms flash fed by consumeHitFlags), name and
// the choke/out tag; the selected pad gets the cyan ring. Click = select +
// audition (web store selectPad triggers at 0.8). QWERTY 1234/qwer/asdf/zxcv
// = pads 12-15/8-11/4-7/0-3 at velocity 0.85, ESC stops the sequencer —
// registered on the top-level component so it works wherever focus sits.
// FileDragAndDropTarget: dropping an audio file on a tile imports it as a
// user wavetable into that pad's OSC A (web dropFile path: mixToMono ->
// detectCycleLength -> sliceToFrames -> buildUserTable).
namespace fui {

class PadGrid : public juce::Component,
                public juce::FileDragAndDropTarget,
                public juce::KeyListener,
                private juce::Timer {
public:
    explicit PadGrid(DrumUiModel&);
    explicit PadGrid(DrumAudioProcessor&); // standalone-test compatibility
    ~PadGrid() override;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    // juce::FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray&, int x, int y) override;
    void fileDragMove(const juce::StringArray&, int x, int y) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // juce::KeyListener
    using juce::Component::keyPressed;
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;

    // Tile geometry (public so the host test can aim a simulated drop).
    juce::Rectangle<int> tileBounds(int padIndex) const;
    int padAt(juce::Point<int>) const;                    // -1 off-tile

private:
    void timerCallback() override;
    void parentHierarchyChanged() override;
    void importFile(const juce::File&, int padIndex);
    void flash(int padIndex);

    std::unique_ptr<DrumUiModel> ownedModel;
    DrumUiModel& proc;
    juce::AudioFormatManager formatMgr;

    std::array<juce::uint32, fable::DR_NPADS> hitMs_{};   // last hit, ms ticks
    std::array<int, fable::DR_NPADS> lastTag_{};          // choke<<8|out cache
    juce::uint32 lastLit_ = 0;
    int lastSel_ = -1;
    int dragOver_ = -1;

    std::set<int> heldKeys_;                              // OS auto-repeat filter
    juce::Component::SafePointer<juce::Component> keyHost_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PadGrid)
};

} // namespace fui
