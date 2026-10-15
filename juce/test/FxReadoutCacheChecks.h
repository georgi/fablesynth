#pragma once
#include "../source/ui/FxChain.h"

inline bool runFxReadoutCacheChecks() {
    fui::FxReadoutCache cache;
    juce::String text = "IN  -12.3 dB";
    auto font = fui::monoFont(8);
    auto colour = fui::col::text;
    juce::Rectangle<float> bounds(7, 9, 117, 24);
    float scale = 1;
    auto alignment = juce::Justification::centred;
    juce::Component owner, parent;
    parent.addChildComponent(owner);
    auto render = [&](bool cached) {
        juce::Image result(juce::Image::ARGB, 300, 100, true);
        juce::Graphics g(result);
        g.addTransform(juce::AffineTransform::scale(scale));
        if (scale == 1.5f)
            g.addTransform(juce::AffineTransform::translation(.37f, .23f));
        if (cached) {
            // Previous plot colour/opacity must not tint the raster blit.
            g.setColour(juce::Colours::red.withAlpha(.15f));
            cache.draw(g, text, font, colour, bounds, alignment, &owner);
        } else {
            g.setFont(font);
            g.setColour(colour);
            g.drawText(text, bounds, alignment);
        }
        return result;
    };
    auto equalPixels = [](const juce::Image& a, const juce::Image& b) {
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return false;
        return true;
    };
    auto changed = [&] {
        const int before = cache.buildCountForTesting();
        const auto first = render(true);
        const auto second = render(true);
        return cache.buildCountForTesting() == before + 1 && equalPixels(first, second);
    };
    if (!changed() || !equalPixels(render(true), render(false))) return false;
    // Raw telemetry can move without changing its displayed rounded string.
    text = "IN  " + juce::String(-12.31, 1) + " dB";
    const int beforeRounded = cache.buildCountForTesting();
    render(true);
    if (cache.buildCountForTesting() != beforeRounded) return false;
    text = "IN  -12.4 dB";
    if (!changed()) return false;
    font = fui::monoFont(9);
    if (!changed()) return false;
    colour = colour.withAlpha(.6f);
    if (!changed()) return false;
    bounds.setWidth(119);
    if (!changed()) return false;
    bounds.setX(8);
    if (!changed()) return false;
    alignment = juce::Justification::centredRight;
    if (!changed()) return false;
    scale = 2;
    parent.setTransform(juce::AffineTransform::translation(.3f, .7f));
    const int beforeAncestor = cache.buildCountForTesting();
    if (!equalPixels(render(true), render(false))
        || cache.buildCountForTesting() != beforeAncestor) return false;
    parent.setTransform({});
    if (!changed()) return false;
    bounds = { 7.25f, 9.5f, 117.5f, 24 };
    if (!changed()) return false;
    // Fractional hosted transforms use the direct painter, including ancestor
    // phase, rather than nearest-blitting a locally phased glyph raster.
    scale = 1.5f;
    const int beforeFractional = cache.buildCountForTesting();
    if (!equalPixels(render(true), render(false))
        || cache.buildCountForTesting() != beforeFractional) return false;
    scale = 2;
    // Changing readings replace one current image; returning to an older
    // string rebuilds instead of retaining an ever-growing numerical bank.
    for (int i = 0; i < 40; ++i) {
        text = juce::String(i);
        if (!changed()) return false;
    }
    text = "0";
    return changed();
}
