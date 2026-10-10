#pragma once
#include "../source/seq/ui/TrackHeadsView.h"
#include <memory>

inline bool runTrackHeadsCacheChecks() {
    auto processor = std::make_unique<SeqAudioProcessor>();
    processor->prepareToPlay(48000, 128);
    fui::TrackHeadsView view(*processor);
    view.setBounds(0, 0, 1254, 60);
    juce::Image image(juce::Image::ARGB, 1881, 90, true);
    auto paint = [&](float scale = 1.0f) {
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(scale));
        view.paint(g);
    };
    auto unchanged = [&] {
        const int count = view.cacheBuildCountForTesting();
        paint();
        return view.cacheBuildCountForTesting() == count;
    };
    auto changed = [&] {
        const int count = view.cacheBuildCountForTesting();
        paint();
        return view.cacheBuildCountForTesting() == count + 1 && unchanged();
    };
    paint();
    if (view.cacheBuildCountForTesting() != 1 || !unchanged()) return false;
    view.setFocusTrack(1);
    if (!changed()) return false;
    view.muteClick(0);
    if (!changed()) return false;
    view.soloClick(1);
    if (!changed()) return false;
    processor->conductor().startTransport();
    processor->conductor().onClipStart(0, 0);
    if (!changed()) return false;
    auto* volume = processor->apvts.getParameter("vol0");
    if (!volume) return false;
    volume->setValueNotifyingHost(.321234f);
    if (!changed()) return false;
    // Factory -> custom changes the label, while subsequent inline parameter
    // edits keep the same CUSTOM subtitle and should reuse the artwork.
    processor->conductor().setTrackPatch(0, { false, 0, {{"test", .2f}} });
    if (!changed()) return false;
    processor->conductor().setTrackPatch(0, { false, 0, {{"test", .3f}} });
    if (!unchanged()) return false;
    view.setBounds(0, 0, 1254, 61);
    if (!changed()) return false;
    const int beforeScale = view.cacheBuildCountForTesting();
    paint(1.5f);
    if (view.cacheBuildCountForTesting() != beforeScale + 1) return false;
    paint(1.5f);
    if (view.cacheBuildCountForTesting() != beforeScale + 1) return false;
    processor->releaseResources();
    return true;
}
