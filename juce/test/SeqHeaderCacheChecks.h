#pragma once
#include "../source/seq/ui/SeqHeader.h"
#include <memory>

// Repaints must reuse text between beats; clock movement changes pixels without
// rebuilding chrome. Geometry and fractional physical scale invalidate all layers.
inline bool runSeqHeaderCacheChecks() {
    auto proc = std::make_unique<SeqAudioProcessor>();
    proc->prepareToPlay(48000, 128);
    fui::SeqHeader view(*proc);
    view.setSize(1254, 44);
    auto paint = [&](float scale = 1.0f, juce::Colour tint = juce::Colours::red.withAlpha(.2f)) {
        juce::Image image(juce::Image::ARGB, 1881, 90, true);
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(scale));
        // The blits must not inherit the caller's tint/opacity.
        g.setColour(tint);
        view.paint(g);
        return image;
    };
    auto samePixels = [](const juce::Image& a, const juce::Image& b, juce::Rectangle<int> r) {
        for (int y = r.getY(); y < r.getBottom(); ++y)
            for (int x = r.getX(); x < r.getRight(); ++x)
                if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return false;
        return true;
    };
    auto first = paint();
    auto again = paint(1.0f, juce::Colours::white);
    const auto initialClockBounds = view.clockPixelBoundsForTesting();
    if (initialClockBounds.getX() != 647 || initialClockBounds.getY() != 8
        || initialClockBounds.getWidth() >= 200 || initialClockBounds.getHeight() != 25) return false;
    if (view.cacheBuildCountForTesting() != 1 || view.clockBuildCountForTesting() != 1
        || view.scopeLabelBuildCountForTesting() != 1
        || !samePixels(first, again, {0, 0, 1254, 44})) return false;
    proc->conductor().startTransport();
    auto beat0 = paint();
    const int chrome = view.cacheBuildCountForTesting();
    const int clock = view.clockBuildCountForTesting();
    const int labels = view.scopeLabelBuildCountForTesting();
    // Use the processor's published conductor timebase without running DSP.
    const double quarter = 48000.0 * 60.0 / proc->conductor().session().bpm;
    proc->currentFrame.store(256.0 + quarter + 8.0);
    auto beat1 = paint();
    if (view.cacheBuildCountForTesting() != chrome || view.clockBuildCountForTesting() != clock + 1
        || view.scopeLabelBuildCountForTesting() != labels
        || samePixels(beat0, beat1, {647, 8, 114, 27})
        || !samePixels(beat0, beat1, {0, 0, 640, 44})) return false;
    paint();
    if (view.clockBuildCountForTesting() != clock + 1) return false;
    proc->currentFrame.store(256.0 + 4.0 * quarter + 8.0);
    auto bar2 = paint();
    if (view.cacheBuildCountForTesting() != chrome || view.clockBuildCountForTesting() != clock + 2
        || samePixels(beat1, bar2, {649, 18, 112, 15})) return false;
    view.quantStep(1);
    paint();
    if (view.cacheBuildCountForTesting() != chrome + 1
        || view.clockBuildCountForTesting() != clock + 2) return false;
    view.setSize(1254, 45);
    paint();
    if (view.cacheBuildCountForTesting() != chrome + 2
        || view.clockBuildCountForTesting() != clock + 3
        || view.scopeLabelBuildCountForTesting() != labels + 1) return false;
    paint(1.5f);
    const auto scaledClockBounds = view.clockPixelBoundsForTesting();
    if (scaledClockBounds.getX() != 970 || scaledClockBounds.getY() != 12
        || scaledClockBounds.getWidth() >= 300 || scaledClockBounds.getHeight() != 38) return false;
    if (view.cacheBuildCountForTesting() != chrome + 3
        || view.clockBuildCountForTesting() != clock + 4
        || view.scopeLabelBuildCountForTesting() != labels + 2) return false;
    const auto compactClock = paint(1.5f);
    if (view.cacheBuildCountForTesting() != chrome + 3
        || view.clockBuildCountForTesting() != clock + 4
        || view.scopeLabelBuildCountForTesting() != labels + 2) return false;
    // Clock text can exceed its nominal slot. Expand the crop and then shrink
    // it again; partial recomposition must erase the old text without ghosts.
    proc->currentFrame.store(256.0 + 4.0 * quarter * 123456789.0 + 8.0);
    paint(1.5f);
    if (view.clockPixelBoundsForTesting().getWidth() <= scaledClockBounds.getWidth()) return false;
    proc->currentFrame.store(256.0 + 4.0 * quarter + 8.0);
    const auto returned = paint(1.5f);
    if (!samePixels(returned, compactClock, {0, 0, 1881, 67})) return false;
    proc->conductor().stopTransport();
    auto stopped = paint(1.5f);
    const int stoppedChrome = view.cacheBuildCountForTesting();
    const int stoppedClock = view.clockBuildCountForTesting();
    if (!proc->conductor().setBpm(137.0)) return false;
    auto tempo = paint(1.5f);
    if (view.cacheBuildCountForTesting() != stoppedChrome
        || view.clockBuildCountForTesting() != stoppedClock + 1
        || samePixels(stopped, tempo, {974, 27, 168, 23})) return false;
    proc->releaseResources();
    return true;
}
