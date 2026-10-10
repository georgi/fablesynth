#pragma once
#include "BassSeqCacheChecks.h"
#include "../source/bass/ui/BassPanels.h"

inline bool runBassKeysCacheChecks() {
    BassAudioProcessor proc;
    proc.prepareToPlay(48000, 512);
    bassSeqCacheChecks::BassSeqCacheTestModel model(proc);
    fui::BassKeysPanel view(model);
    view.setSize(1100, 180);
    auto fail = [](int line) { std::fprintf(stderr, "BassKeysCacheChecks failed at line %d\n", line); return false; };
    auto same = [](const juce::Image& a, const juce::Image& b, int tolerance = 3) {
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
    auto paint = [&](bool direct = false, float scale = 1, bool clipped = false) {
        juce::Image image(juce::Image::ARGB, (int)std::ceil(view.getWidth() * scale),
                          (int)std::ceil(view.getHeight() * scale), true);
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(scale));
        if (clipped) g.reduceClipRegion(view.keyBounds(0).getSmallestIntegerContainer());
        if (direct) view.paintDirectForTesting(g); else view.paint(g);
        return image;
    };
    if (!same(paint(), paint(true))) return fail(__LINE__);
    const int first = view.cacheBuildCountForTesting();
    if (first != fable::BL_KEY_COUNT + 1) return fail(__LINE__);
    paint();
    if (view.cacheBuildCountForTesting() != first) return fail(__LINE__);
    // Render real processor notes: both key colours and changed returns retain
    // their z-order without invalidating the cold artwork.
    proc.setSeqPlaying(false);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    for (int key : {0, 1, 7, 13, 24, 0}) {
        proc.noteOn(key, .85f);
        buffer.clear(); proc.processBlock(buffer, midi);
        if (!same(paint(), paint(true)) || view.cacheBuildCountForTesting() != first) return fail(__LINE__);
        proc.noteOff(key);
    }
    auto arp = model.arpSettings(); arp.enabled = true; arp.keys = true;
    model.setArpSettings(arp);
    if (!same(paint(), paint(true)) || view.cacheBuildCountForTesting() != first + 1) return fail(__LINE__);
    arp.keys = false; model.setArpSettings(arp);
    paint(false, 1, true);
    if (!same(paint(), paint(true)) || view.cacheBuildCountForTesting() != first + 2) return fail(__LINE__);
    view.setSize(1101, 181); paint(false, 1, true);
    if (!same(paint(), paint(true))) return fail(__LINE__);
    const int beforeFractional = view.cacheBuildCountForTesting();
    if (!same(paint(false, .8f), paint(true, .8f), 0)
        || view.cacheBuildCountForTesting() != beforeFractional) return fail(__LINE__);
    juce::Component parent; parent.addAndMakeVisible(view);
    parent.setTransform(juce::AffineTransform::translation(.25f, .5f));
    if (!same(paint(), paint(true), 0) || view.cacheBuildCountForTesting() != beforeFractional) return fail(__LINE__);
    parent.setTransform({});
    if (!same(paint(false, 2), paint(true, 2))) return fail(__LINE__);
    const int scaled = view.cacheBuildCountForTesting(); paint(false, 2);
    if (view.cacheBuildCountForTesting() != scaled) return fail(__LINE__);
    auto wrapper = [&](auto& panel) {
        panel.setSize(350, 230);
        auto draw = [&](bool direct, float scale) {
            juce::Image image(juce::Image::ARGB, 700, 460, true);
            juce::Graphics g(image); g.addTransform(juce::AffineTransform::scale(scale));
            g.reduceClipRegion(panel.getLocalBounds());
            if (direct) panel.paintDirectForTesting(g); else panel.paint(g);
            return image;
        };
        if (!same(draw(false, 1), draw(true, 1))) return fail(__LINE__);
        const int count = panel.cacheBuildCountForTesting(); draw(false, 1);
        if (panel.cacheBuildCountForTesting() != count || !same(draw(false, .8f), draw(true, .8f), 0)) return fail(__LINE__);
        panel.setSize(351, 231);
        if (!same(draw(false, 1), draw(true, 1)) || panel.cacheBuildCountForTesting() != count + 1) return fail(__LINE__);
        return true;
    };
    fui::BassOscPanel osc(model); fui::BassSubPanel sub(model); fui::BassFilterPanel filter(model);
    fui::BassEnvPanel env(model); fui::BassLfoPanel lfo(model); fui::BassAccentPanel accent(model);
    return wrapper(osc) && wrapper(sub) && wrapper(filter) && wrapper(env) && wrapper(lfo) && wrapper(accent);
}
