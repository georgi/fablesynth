#pragma once
#include "../source/ui/LeafControlCache.h"
#include "../source/seq/ui/HostedDrumModel.h"
#include "../source/drum/ui/PadStrip.h"
#include "../source/ui/FxChain.h"

// Verify cache invalidation with actual JUCE cached painting, and preserve the
// editor's opt-in when pad selection replaces its parameter-bound controls.
inline bool runLeafControlCacheChecks() {
    auto imagesDiffer = [](const juce::Image& a, const juce::Image& b) {
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return true;
        return false;
    };
    auto processor = std::make_unique<SeqAudioProcessor>();
    fui::HostedDrumModel model(*processor);
    juce::Component root;
    root.setVisible(true);
    fui::Knob knob(model.parameters(), "pad0.lvl", fui::Knob::Md, fui::Accent::A);
    root.addAndMakeVisible(knob);
    knob.setBounds(0, 0, 80, 90);
    fui::enableLeafControlCache(root);
    auto* parameter = model.parameters().parameter("pad0.lvl");
    if (!parameter || !knob.getCachedComponentImage() || root.getCachedComponentImage()) return false;
    auto renderCached = [&] {
        juce::Image image(juce::Image::ARGB, knob.getWidth(), knob.getHeight(), true);
        juce::Graphics graphics(image);
        knob.getCachedComponentImage()->paint(graphics);
        return image;
    };
    parameter->setValueNotifyingHost(.2f);
    knob.refreshVisualState();
    const auto before = renderCached();
    const auto unchanged = renderCached();
    parameter->setValueNotifyingHost(.8f);
    knob.refreshVisualState();
    const auto after = renderCached();
    bool ok = !imagesDiffer(before, unchanged) && imagesDiffer(before, after);
    // Turning off caching must produce the updated value as well; compare at
    // native scale so raster scaling differences do not affect this assertion.
    knob.setBufferedToImage(false);
    const auto direct = knob.createComponentSnapshot(knob.getLocalBounds());
    ok &= !imagesDiffer(after, direct);

    fui::PadStrip strip(model);
    fui::FxChain fx(model, false);
    root.addAndMakeVisible(strip);
    root.addAndMakeVisible(fx);
    fui::enableLeafControlCache(root);
    auto checkLeaves = [&](auto&& self, juce::Component& component, int& count) -> bool {
        bool cached = true;
        if (dynamic_cast<fui::Knob*>(&component) || dynamic_cast<fui::Stepper*>(&component)
            || dynamic_cast<juce::TextButton*>(&component)) {
            ++count;
            cached &= component.getCachedComponentImage() != nullptr;
        }
        for (auto* child : component.getChildren()) cached &= self(self, *child, count);
        return cached;
    };
    int initial = 0;
    ok &= checkLeaves(checkLeaves, strip, initial);
    ok &= checkLeaves(checkLeaves, fx, initial);
    model.selectPad(1);
    model.selectionChanges().sendSynchronousChangeMessage();
    fx.setPad(1, model.padName(1));
    int rebuilt = 0;
    ok &= checkLeaves(checkLeaves, strip, rebuilt);
    ok &= checkLeaves(checkLeaves, fx, rebuilt);
    ok &= initial > 0 && rebuilt == initial;
    return ok;
}
