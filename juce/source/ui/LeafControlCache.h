#pragma once
#include "Controls.h"

namespace fui {

// Cache small control rasters across consolidated window repaints. Normal
// repaint() calls invalidate them when values, modulation or interaction change.
inline void cacheLeafControls(juce::Component& component) {
    if (dynamic_cast<Knob*>(&component) || dynamic_cast<Stepper*>(&component)
        || dynamic_cast<juce::TextButton*>(&component))
        component.setBufferedToImage(true);
    for (auto* child : component.getChildren()) cacheLeafControls(*child);
}

inline void enableLeafControlCache(juce::Component& root) {
    root.getProperties().set("fuiLeafControlCache", true);
    cacheLeafControls(root);
}

// Pad selection recreates some controls. Preserve the containing editor's
// opt-in without enabling caching in other standalone instruments.
inline void refreshLeafControlCache(juce::Component& subtree) {
    for (auto* ancestor = &subtree; ancestor; ancestor = ancestor->getParentComponent()) {
        if (static_cast<bool>(ancestor->getProperties()["fuiLeafControlCache"])) {
            cacheLeafControls(subtree);
            return;
        }
    }
}

} // namespace fui
