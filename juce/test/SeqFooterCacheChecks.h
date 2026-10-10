#pragma once
#include "../source/seq/ui/SeqFooterView.h"

// Use a dedicated processor/view: the check exercises real conductor events,
// rendered meter pixels, fractional backing-store sizes and hidden release.
inline bool checkSeqFooterCache(fui::SeqFooterView& view, SeqAudioProcessor& proc) {
    view.setVisible(true);
    view.setSize(1254, 60);
    auto paint = [&](float scale = 1.0f) {
        juce::Image image(juce::Image::ARGB, 1900, 100, true);
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(scale));
        view.paint(g);
        return image;
    };
    auto unchanged = [&] {
        const int before = view.cacheBuildCountForTesting();
        paint();
        return view.cacheBuildCountForTesting() == before;
    };
    auto changed = [&] {
        const int before = view.cacheBuildCountForTesting();
        paint();
        return view.cacheBuildCountForTesting() == before + 1 && unchanged();
    };
    paint();
    if (!unchanged()) return false;
    auto& cond = proc.conductor();
    const auto saved = cond.session().scenes[0].clips[0];
    if (!cond.session().scenes[0].hasClip[0] || saved.bars <= 1) return false;
    proc.trackBar[0].store(0);
    cond.startTransport();
    cond.onClipStart(0, 0);
    if (!changed()) return false;
    proc.trackBar[0].store(saved.bars);
    if (!unchanged()) return false; // same displayed position after a loop
    proc.trackBar[0].store(1);
    if (!changed()) return false;
    cond.toggleTrackMute(0);
    if (!changed()) return false;
    cond.toggleTrackMute(0);
    if (!changed()) return false;
    auto edited = saved;
    edited.name += " CACHE";
    if (!cond.pasteClip(0, 0, edited) || !changed()) return false;
    cond.pasteClip(0, 0, saved);
    paint();

    proc.trackRms[0].store(0.0f);
    view.tickForTesting();
    const auto silent = paint();
    const int beforeMeter = view.cacheBuildCountForTesting();
    proc.trackRms[0].store(0.25f);
    view.tickForTesting();
    const auto loud = paint();
    if (view.cacheBuildCountForTesting() != beforeMeter || view.meterLevelForTesting(0) != 0.25f) return false;
    // First cell VU is x=403..467, y=28..33. Sample away from the tiny idle fill.
    if (silent.getPixelAt(430, 30) == loud.getPixelAt(430, 30)) return false;
    proc.trackRms[0].store(0.0f);
    view.tickForTesting();
    if (!(view.meterLevelForTesting(0) > 0.0f && view.meterLevelForTesting(0) < 0.25f) || !unchanged()) return false;
    view.setVisible(false);
    if (view.meterLevelForTesting(0) != 0.0f) return false;
    view.setVisible(true);
    view.tickForTesting();
    if (!unchanged()) return false;

    const int beforeScale = view.cacheBuildCountForTesting();
    paint(0.65f);
    if (view.cacheBuildCountForTesting() != beforeScale + 1
        || view.cachePixelSizeForTesting() != juce::Point<int>(816, 39)) return false;
    paint(0.65f);
    if (view.cacheBuildCountForTesting() != beforeScale + 1) return false;
    view.setSize(1254, 61);
    paint(0.65f);
    if (view.cacheBuildCountForTesting() != beforeScale + 2
        || view.cachePixelSizeForTesting() != juce::Point<int>(816, 40)) return false;
    cond.onClipStop(0);
    paint();
    // A document edit to an inactive clip has no visible footer effect.
    if (!cond.pasteClip(0, 0, edited) || !unchanged()) return false;
    cond.pasteClip(0, 0, saved);
    return unchanged();
}
