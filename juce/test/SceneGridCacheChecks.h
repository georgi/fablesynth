#pragma once
#include "../source/seq/ui/SceneGridView.h"

// Transport and gestures must redraw overlays without rasterising stationary
// cards and clip previews. Document edits and fractional scale rebuild them.
inline bool checkSceneGridCache(fui::SceneGridView& view, SeqAudioProcessor& proc) {
    view.clearSingleRow();
    view.setSize(1254, 478);
    auto paint = [&](float scale, bool cardOnly = false) {
        juce::Image image(juce::Image::ARGB, 1800, 800, true);
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(scale));
        if (cardOnly) g.reduceClipRegion(juce::Rectangle<int>(0, 0, 218, 73));
        view.paint(g);
    };
    paint(1.0f);
    const int initial = view.cacheBuildCountForTesting();
    paint(1.0f);
    bool ok = view.cacheBuildCountForTesting() == initial;
    const int oldStep = proc.trackStep[0].load(), oldBar = proc.trackBar[0].load();
    proc.trackStep[0].store(7); proc.trackBar[0].store(2);
    view.selectCell(0, 0);
    paint(1.0f);
    ok &= view.cacheBuildCountForTesting() == initial;
    view.clearSelection();
    proc.trackStep[0].store(oldStep); proc.trackBar[0].store(oldBar);

    view.hoverCell(0, 0);
    paint(1.0f);
    ok &= view.cacheBuildCountForTesting() == initial + 1;
    view.hoverCell(-1, -1);
    paint(1.0f);

    auto& cond = proc.conductor();
    const auto saved = cond.session().scenes[0];
    if (saved.hasClip[0]) {
        auto changed = saved.clips[0];
        changed.name += " CACHE CHECK";
        cond.pasteClip(0, 0, changed);
        const int before = view.cacheBuildCountForTesting();
        paint(1.0f, true);
        ok &= view.cacheBuildCountForTesting() == before + 1;
        paint(1.0f);
        ok &= view.cacheBuildCountForTesting() > before + 1;
        cond.pasteClip(0, 0, saved.clips[0]);
        paint(1.0f);
    }
    const int beforeScale = view.cacheBuildCountForTesting();
    paint(0.65f);
    ok &= view.cacheBuildCountForTesting() > beforeScale;
    const int scaled = view.cacheBuildCountForTesting();
    paint(0.65f);
    ok &= view.cacheBuildCountForTesting() == scaled;
    return ok;
}
