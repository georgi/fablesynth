#pragma once
#include "../source/drum/DrumProcessor.h"
#include "../source/drum/ui/PadGrid.h"
#include <memory>

inline bool runPadGridCacheChecks() {
    auto processor = std::make_unique<DrumAudioProcessor>();
    auto model = fui::makeStandaloneDrumUiModel(*processor);
    fui::PadGrid grid(*model);
    grid.setBounds(0, 0, 352, 369);
    auto paint = [&](float scale = 1.0f, bool direct = false,
                     juce::Rectangle<int> clip = {}) {
        juce::Image image(juce::Image::ARGB, (int)std::ceil(grid.getWidth() * scale),
                          (int)std::ceil(grid.getHeight() * scale), true);
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(scale));
        if (!clip.isEmpty()) g.reduceClipRegion(clip);
        if (direct) grid.paintDirectForTesting(g); else grid.paint(g);
        return image;
    };
    auto equal = [](const juce::Image& a, const juce::Image& b, int tolerance = 0) {
        if (a.getBounds() != b.getBounds()) return false;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x) {
                const auto p = a.getPixelAt(x, y), q = b.getPixelAt(x, y);
                if (std::abs((int)p.getRed() - q.getRed()) > tolerance
                    || std::abs((int)p.getGreen() - q.getGreen()) > tolerance
                    || std::abs((int)p.getBlue() - q.getBlue()) > tolerance
                    || std::abs((int)p.getAlpha() - q.getAlpha()) > tolerance) return false;
            }
        return true;
    };
    // Cached and uncached composition can differ by a few rounding levels at
    // translucent edges. A small channel tolerance still detects stale rings,
    // text, missing layers, or accidental cyan image tinting.
    auto parity = [&](float scale = 1.0f) { return equal(paint(scale), paint(scale, true), 3); };
    auto unchanged = [&] {
        const int count = grid.cacheBuildCountForTesting();
        paint();
        return grid.cacheBuildCountForTesting() == count;
    };
    paint();
    if (grid.cacheBuildCountForTesting() != 33 || !unchanged() || !parity()) return false;
    const int beforeSelection = grid.cacheBuildCountForTesting();
    model->selectPad(1);
    if (!parity() || grid.cacheBuildCountForTesting() != beforeSelection + 2 || !unchanged()) return false;
    const int beforeName = grid.cacheBuildCountForTesting();
    processor->setPadName(2, "NEW NAME");
    if (!parity() || grid.cacheBuildCountForTesting() != beforeName + 1 || !unchanged()) return false;
    const int beforeTag = grid.cacheBuildCountForTesting();
    auto* choke = processor->apvts.getParameter("pad2.choke");
    if (!choke) return false;
    choke->setValueNotifyingHost(choke->convertTo0to1(1.0f));
    if (!parity() || grid.cacheBuildCountForTesting() != beforeTag + 1 || !unchanged()) return false;
    const auto unlit = paint();
    const int beforeHit = grid.cacheBuildCountForTesting();
    grid.flashForTesting(3);
    const auto lit = paint();
    if (equal(unlit, lit) || !parity() || grid.cacheBuildCountForTesting() != beforeHit) return false;
    const auto point = grid.tileBounds(4).getCentre();
    grid.fileDragEnter({}, point.x, point.y);
    if (!parity() || grid.cacheBuildCountForTesting() != beforeHit + 1) return false;
    grid.fileDragExit({});
    if (!parity() || grid.cacheBuildCountForTesting() != beforeHit + 2) return false;
    // A clipped first paint must not bake that clip into any cached tile.
    grid.setSize(353, 370);
    paint(1.0f, false, grid.tileBounds(0).reduced(12));
    if (!parity() || !unchanged()) return false;
    const int beforeScale = grid.cacheBuildCountForTesting();
    const auto scaled = paint(1.5f);
    if (!equal(scaled, paint(1.5f, true)) || grid.cacheBuildCountForTesting() != beforeScale) return false;
    if (!equal(scaled, paint(1.5f)) || grid.cacheBuildCountForTesting() != beforeScale) return false;
    // Fractional ancestor translations also bypass the phase-zero cache even
    // at an integer physical scale.
    juce::Component parent;
    parent.addAndMakeVisible(grid);
    parent.setTransform(juce::AffineTransform::translation(0.25f, 0.5f));
    if (!equal(paint(), paint(1.0f, true)) || grid.cacheBuildCountForTesting() != beforeScale) return false;
    parent.setTransform({});
    // Moving selection while a tile is clipped out must update it on exposure.
    model->selectPad(5);
    paint(1.0f, false, grid.tileBounds(0).reduced(12));
    paint();
    if (grid.cacheBuildCountForTesting() != beforeScale + 2 || !parity()) return false;
    // Small tiles make metadata overlap the LED; preserve original draw order.
    grid.setSize(352, 170);
    if (!parity()) return false;
    return true;
}
