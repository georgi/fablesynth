#pragma once
#include "../source/ui/Controls.h"
#include "../source/ui/DeviceParameterBank.h"
#include "../source/seq/ui/DeviceFocusView.h"
#include <cmath>
#include <limits>
#include <memory>

// A hidden ancestor must suppress visual lookups, while a visible headless tree
// must refresh immediately. Modulation routing is cached inside Knob, so adding
// a route while hidden exercises more than the parameter's direct paint path.
inline bool runHiddenVisualPollingChecks() {
    const auto& catalog = fable::paramInfo();
    fui::DeviceParameterBank bank(catalog.data(), catalog.size());
    auto source = bank.source();
    int automationReads = 0, modulationReads = 0;
    source.setLiveAutomationLookup([&](const juce::String&) {
        ++automationReads;
        return std::numeric_limits<float>::quiet_NaN();
    });
    source.setLiveModLookup([&](int) { ++modulationReads; return .1f; });
    juce::Component root, page;
    root.setVisible(true);
    root.addAndMakeVisible(page);
    fui::Knob knob(source, "oscA.pos", fui::Knob::Md, fui::Accent::A, true, 1);
    page.addAndMakeVisible(knob);
    auto set = [&](const char* id, float value) {
        auto* parameter = bank.parameter(id);
        if (!parameter) return false;
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        return true;
    };
    if (!set("oscA.pos", .25f)) return false;
    fui::refreshVisibleControls(root);
    if (automationReads == 0 || modulationReads != 0) return false;
    root.setVisible(false);
    const int beforeHidden = automationReads;
    if (!set("oscA.pos", .5f) || !set("mat1.src", 1) || !set("mat1.dst", 1)
        || !set("mat1.amt", .4f)) return false;
    knob.refreshVisualState();
    fui::refreshVisibleControls(page);
    if (automationReads != beforeHidden || modulationReads != 0) return false;
    root.setVisible(true);
    // Same reveal operation used by device focus and page switching. No OS peer
    // or timer wait is needed to rebuild the new ring and show the live value.
    fui::refreshVisibleControls(root);
    if (modulationReads == 0 || std::abs(knob.effectiveNorm() - .6f) > 1.0e-5f) return false;
    page.setVisible(false);
    const int beforePageHidden = automationReads;
    const int beforeModHidden = modulationReads;
    knob.refreshVisualState();
    fui::refreshVisibleControls(root);
    if (automationReads != beforePageHidden || modulationReads != beforeModHidden) return false;
    page.setVisible(true);
    if (!set("mat1.src", 0)) return false;
    fui::refreshVisibleControls(root);
    if (std::abs(knob.effectiveNorm() - .5f) >= 1.0e-5f) return false;

    // Exercise the production reveal hook, rather than just the leaf helper.
    auto processor = std::make_unique<SeqAudioProcessor>();
    // Hosted clip access requires the runtime conductor created by preparation.
    processor->prepareToPlay(48000, 128);
    auto focus = std::make_unique<fui::DeviceFocusView>(*processor);
    focus->setBounds(0, 0, 1100, 780);
    focus->setVisible(true);
    focus->setTarget(0, 2);
    auto findSlider = [&](auto&& self, juce::Component& c) -> fui::VSlider* {
        if (auto* slider = dynamic_cast<fui::VSlider*>(&c)) return slider;
        for (auto* child : c.getChildren())
            if (auto* slider = self(self, *child)) return slider;
        return nullptr;
    };
    auto* slider = findSlider(findSlider, focus->wt2BodyForTest());
    if (!slider) return false;
    auto hosted = focus->wt2ModelForTest().parameters();
    auto setHosted = [&](const char* id, float value) {
        auto* parameter = hosted.parameter(id);
        if (!parameter) return false;
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        return true;
    };
    for (int slot = 1; slot <= 16; ++slot) {
        const auto id = "mat" + juce::String(slot) + ".src";
        if (auto* p = hosted.parameter(id)) p->setValueNotifyingHost(0);
    }
    slider->refreshVisualState();
    const auto initial = slider->createComponentSnapshot(slider->getLocalBounds());
    focus->setVisible(false);
    if (!setHosted("mat1.src", 1) || !setHosted("mat1.dst", 1) || !setHosted("mat1.amt", .4f)) return false;
    focus->setVisible(true);
    const auto revealed = slider->createComponentSnapshot(slider->getLocalBounds());
    slider->refreshVisualState();
    const auto refreshed = slider->createComponentSnapshot(slider->getLocalBounds());
    bool changed = false;
    for (int y = 0; y < revealed.getHeight(); ++y)
        for (int x = 0; x < revealed.getWidth(); ++x) {
            if (revealed.getPixelAt(x, y) != refreshed.getPixelAt(x, y)) return false;
            changed |= initial.getPixelAt(x, y) != revealed.getPixelAt(x, y);
        }
    return changed;
}
