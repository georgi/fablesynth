#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace fui {
// Default editor width: 20 % above an editor's base width, reduced when the
// window (height = width * heightPerWidth) would not fit the main display's
// work area. Users can still resize within the 840..2100 px limits.
inline int defaultEditorWidth(int baseWidth, double heightPerWidth) {
    int width = juce::roundToInt(baseWidth * 1.2);
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
        const auto area = display->userArea;
        width = juce::jmin(width, area.getWidth() - 40, juce::roundToInt((area.getHeight() - 60) / heightPerWidth));
    }
    return juce::jlimit(840, 2100, width);
}
} // namespace fui
