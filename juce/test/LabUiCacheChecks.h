#pragma once
#include "../source/ui/LabPanel.h"
#include "../source/ui/DeviceParameterBank.h"

namespace fui {
struct LabModuleTestAccess {
    static int tick(LabModule& module) { return module.advanceVisualState(); }
    static float phase(const LabModule& module) { return module.phase_; }
    static juce::Image cache(const LabModule& module) { return module.staticCache_; }
    static juce::Image staticReference(const LabModule& module, float scale) {
        juce::Image image(juce::Image::ARGB, (int)std::ceil(module.getWidth() * scale),
                          (int)std::ceil(module.getHeight() * scale), true);
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(scale));
        module.drawStatic(g, module.power_.isOn() ? module.caption() : juce::String("BYPASS"));
        return image;
    }
};
}

inline bool runLabUiCacheChecks() {
    const auto& catalog = fable::paramInfo();
    fui::DeviceParameterBank bank(catalog.data(), catalog.size());
    auto set = [&](const char* id, float value) {
        auto* p = bank.parameter(id);
        if (!p) return false;
        p->setValueNotifyingHost(p->convertTo0to1(value));
        return true;
    };
    if (!set("fx.crush.on", 0)) return false;
    fui::LabModule module(bank.source(), fui::LabModule::Crush);
    module.setBounds(0, 0, 220, 300);
    module.setVisible(true);
    auto paint = [&](float scale) {
        juce::Image image(juce::Image::ARGB, (int)std::ceil(module.getWidth() * scale),
                          (int)std::ceil(module.getHeight() * scale), true);
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(scale));
        module.paint(g);
    };
    using Access = fui::LabModuleTestAccess;
    paint(1);
    if (Access::tick(module) != 2 || Access::tick(module) != 0) return false;
    if (!set("fx.crush.chaos", .75f) || Access::tick(module) != 1 || Access::tick(module) != 0) return false;
    const float stoppedPhase = Access::phase(module);
    module.setVisible(false);
    if (!set("fx.crush.on", 1) || Access::tick(module) != 0 || Access::phase(module) != stoppedPhase) return false;
    module.setVisible(true);
    if (Access::tick(module) != 2) return false;
    paint(1);
    for (int i = 1; i < fui::kAnimationHz; ++i) Access::tick(module);
    if (std::abs(Access::phase(module) - stoppedPhase - 1.0f) > 1.e-5f) return false;
    auto samePixels = [](const juce::Image& a, const juce::Image& b) {
        if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return false;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return false;
        return true;
    };
    for (float scale : {0.75f, 1.0f, 2.0f}) {
        paint(scale);
        if (!samePixels(Access::cache(module), Access::staticReference(module, scale))) return false;
        const auto cached = Access::cache(module);
        paint(scale);
        if (cached != Access::cache(module)) return false;
        if (!set("fx.crush.bits", 8.0f + scale)) return false;
        paint(scale);
        if (cached == Access::cache(module) || !samePixels(Access::cache(module), Access::staticReference(module, scale))) return false;
        module.setSize(module.getWidth() + 1, module.getHeight() + 1);
        paint(scale);
        if (!samePixels(Access::cache(module), Access::staticReference(module, scale))) return false;
    }
    if (!set("fx.crush.on", 0) || Access::tick(module) != 2) return false;
    paint(1);
    return Access::tick(module) == 0 && samePixels(Access::cache(module), Access::staticReference(module, 1));
}
