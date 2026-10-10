#pragma once
#include <cstdio>
#include "../source/drum/DrumProcessor.h"
#include "../source/drum/ui/StepSeqView.h"

// Reuse must survive cursor advances, while direct edits invalidate immediately
// (without waiting for the visual timer). Also exercise undo and scaled blits.
inline bool runDrumSeqCacheChecks() {
    DrumAudioProcessor proc;
    proc.prepareToPlay(48000, 4096);
    fui::StepSeqView view(proc);
    view.setSize(1424, 605);
    auto paint = [&](float scale = 1.0f, juce::Colour tint = juce::Colours::white) {
        juce::Image image(juce::Image::ARGB, 2136, 908, true);
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(scale));
        g.reduceClipRegion(view.getLocalBounds());
        g.setColour(tint);
        view.paint(g);
        return image;
    };
    auto fail = [](int line) { std::fprintf(stderr, "DrumSeqCacheChecks failed at line %d\n", line); return false; };
    auto same = [](const juce::Image& a, const juce::Image& b) {
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return false;
        return true;
    };
    const auto initial = paint();
    if (!same(initial, paint(1, juce::Colours::red.withAlpha(.2f)))
        || view.cacheBuildCountForTesting() != 1 || view.overlayBuildCountForTesting() != 1) return fail(__LINE__);
    view.toggleStep(0, 0);
    const auto edited = paint();
    if (same(initial, edited) || view.cacheBuildCountForTesting() != 2) return fail(__LINE__);
    view.undo();
    if (!same(initial, paint()) || view.cacheBuildCountForTesting() != 3) return fail(__LINE__);
    view.setSelection({0, 2, 0, 1});
    const auto selected = paint();
    if (same(initial, selected)) return fail(__LINE__);
    view.clearSelection();
    if (!same(initial, paint())) return fail(__LINE__);
    proc.setSeqPlaying(true);
    paint();
    const int artwork = view.cacheBuildCountForTesting();
    const int overlays = view.overlayBuildCountForTesting();
    juce::AudioBuffer<float> buffer(2, 4096);
    juce::MidiBuffer midi;
    for (int i = 0; i < 8; ++i) { buffer.clear(); proc.processBlock(buffer, midi); }
    paint();
    if (view.cacheBuildCountForTesting() != artwork
        || view.overlayBuildCountForTesting() <= overlays) return fail(__LINE__);
    // Compare actual playback composition to the original painter at both
    // integer and fractional raster scales, including panel rounded edges.
    for (float scale : {1.0f, .65f}) {
        const auto cached = paint(scale);
        const bool fractional = scale != std::round(scale);
        juce::Image direct(juce::Image::ARGB, fractional ? 2136 : view.getWidth(), fractional ? 908 : view.getHeight(), true);
        juce::Graphics dg(direct);
        dg.addTransform(juce::AffineTransform::scale(scale));
        dg.reduceClipRegion(view.getLocalBounds());
        view.paintUncachedForTesting(dg);
        bool parity = true;
        for (int y = 0; y < direct.getHeight(); ++y)
            for (int x = 0; x < direct.getWidth(); ++x)
                if (cached.getPixelAt(x, y) != direct.getPixelAt(x, y)) parity = false;
        if (!parity) {
            for (int y = 0; y < direct.getHeight(); ++y) {
                bool found = false;
                for (int x = 0; x < direct.getWidth(); ++x) {
                    if (cached.getPixelAt(x, y) != direct.getPixelAt(x, y)) {
                        std::fprintf(stderr, "DR seq parity scale %.2f first diff (%d,%d): %08x / %08x\n", scale, x, y,
                            cached.getPixelAt(x, y).getARGB(), direct.getPixelAt(x, y).getARGB());
                        found = true; break;
                    }
                }
                if (found) break;
            }
            return fail(__LINE__);
        }
    }
    for (float scale : {.65f, .8f}) {
        juce::Image cached(juce::Image::ARGB, 1200, 600, true), direct(juce::Image::ARGB, 1200, 600, true);
        auto draw = [&](juce::Image& image, bool original) {
            juce::Graphics g(image);
            g.addTransform(juce::AffineTransform::scale(scale).translated(37.2f, 19.4f));
            g.reduceClipRegion(view.getLocalBounds());
            if (original) view.paintUncachedForTesting(g); else view.paint(g);
        };
        draw(cached, false); draw(direct, true);
        if (!same(cached, direct)) return fail(__LINE__);
    }
    const int scaledArtwork = view.cacheBuildCountForTesting();
    paint(2.0f);
    if (view.cacheBuildCountForTesting() != scaledArtwork + 1) return fail(__LINE__);
    paint(2.0f);
    if (view.cacheBuildCountForTesting() != scaledArtwork + 1) return fail(__LINE__);
    view.setSize(1000, 605);
    paint(2.0f);
    if (view.cacheBuildCountForTesting() != scaledArtwork + 2) return fail(__LINE__);
    juce::Component parent;
    parent.addAndMakeVisible(view);
    parent.setTransform(juce::AffineTransform::translation(.3f, .7f));
    const int beforeParent = view.cacheBuildCountForTesting();
    const auto translated = paint();
    if (view.cacheBuildCountForTesting() != beforeParent) return fail(__LINE__);
    juce::Image directParent(juce::Image::ARGB, 2136, 908, true);
    juce::Graphics parentGraphics(directParent);
    parentGraphics.reduceClipRegion(view.getLocalBounds());
    view.paintUncachedForTesting(parentGraphics);
    if (!same(translated, directParent)) return fail(__LINE__);
    parent.removeChildComponent(&view);
    proc.releaseResources();
    return true;
}
