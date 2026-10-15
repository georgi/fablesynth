#pragma once
#include "FxUiChecks.h"
#include "../source/ui/AutoMix.h"

#include <cstdio>
#include <map>

// Native auto-mix UI on a standalone device: AUTO EQ and AUTO COMP learn from
// rendered material, APPLY writes the fitted params, UNDO restores them, and
// the side-chain menu writes fx.comp.sc. Each state is rendered to a PNG in
// build/auto-mix-ui for inspection. Offline only: no audio device, no window.
namespace automixchecks {

inline juce::File pngDir() {
    auto dir = juce::File::getCurrentWorkingDirectory().getChildFile("build/auto-mix-ui");
    dir.createDirectory();
    return dir;
}

inline void writeSnapshot(juce::Component& c, const juce::String& name) {
    const auto file = pngDir().getChildFile(name);
    file.deleteFile();
    if (auto stream = file.createOutputStream()) {
        juce::PNGImageFormat png;
        png.writeImageToStream(c.createComponentSnapshot(c.getLocalBounds()), *stream);
        std::printf("  wrote %s\n", file.getFullPathName().toRawUTF8());
    }
}

struct Checker {
    juce::String machine;
    int fails = 0;
    void operator()(bool ok, const juce::String& what) {
        std::printf("  [%s] %s auto-mix: %s\n", ok ? "PASS" : "FAIL", machine.toRawUTF8(), what.toRawUTF8());
        if (!ok) ++fails;
    }
};

// LEARN, then render and poll until the learner stops (8 s of sound) or the
// frame budget runs out. Polls well inside each tap's ring capacity.
template <class Learner, class Render>
void learn(Learner& learner, Render render, int maxBlocks = 4000) {
    learner.learn();
    for (int i = 0; i < maxBlocks && learner.learning(); ++i) {
        render();
        if (i % 4 == 3) learner.poll();
    }
    if (learner.learning()) learner.stop();
}

inline float paramValue(const fui::ParameterSource& source, const juce::String& id) {
    return fui::readParam(source, id, -12345.0f);
}

// Checks the EQ and COMP AUTO paths on one FX chain scope (prefix "" or "pad<i>.").
template <class Render>
void checkChainScope(Checker& check, fui::FxChain& chain, fui::ParameterSource params, const juce::String& prefix,
                     const juce::String& tag, Render render, bool expectEq = true) {
    auto* eq = chain.module(fui::FxModuleView::Eq);
    auto* comp = chain.module(fui::FxModuleView::Comp);
    check(eq && comp, tag + " has EQ and COMP modules");
    if (!eq || !comp) return;

    // ---- AUTO EQ ----
    eq->setAutoOpen(true);
    auto* strip = eq->autoEqStrip();
    check(strip && strip->isVisible() && !strip->getBounds().isEmpty(), tag + " EQ AUTO shows the learn strip");
    if (!strip) return;
    auto& el = strip->learner();
    if (!expectEq) {
        check(!el.available() && el.status() == "USE GROUP FX TO LEARN", tag + " EQ explains why it cannot learn");
        writeSnapshot(chain, tag + "-eq-auto-unavailable.png");
        eq->setAutoOpen(false);
    } else {
        const auto keys = fui::AutoEqLearner::eqKeys();
        std::map<juce::String, float> before;
        for (const auto& k : keys) before[k] = paramValue(params, prefix + k);
        learn(el, render);
        const auto* fit = el.fit();
        check(fit != nullptr && el.status().endsWith("dB OF MOVES"), tag + " EQ LEARN fits rendered material: " + el.status());
        check(el.learnLabel() == "RELEARN" && el.canApply(), tag + " EQ offers RELEARN and APPLY");
        // A different target curve changes the measured deviation (the
        // fitted gains may sit on the same boost/cut caps).
        const auto deviationFor = [&](const char* ref) {
            el.setReference(ref);
            return el.fit() ? el.fit()->deviation : std::vector<double>{};
        };
        const auto bright = deviationFor("bright"), warm = deviationFor("warm");
        double diff = 0;
        for (size_t i = 0; i < bright.size() && i < warm.size(); ++i) diff = std::max(diff, std::abs(bright[i] - warm[i]));
        check(!bright.empty() && diff > 0.1, tag + " EQ reference changes the fit");
        strip->refresh();
        writeSnapshot(chain, tag + "-eq-auto-learned.png");
        if (fit = el.fit(); fit) {
            el.apply();
            const char* gains[] = {"fx.eq.low", "fx.eq.mid", "fx.eq.mid2", "fx.eq.high"};
            const char* freqs[] = {"fx.eq.lfreq", "fx.eq.mfreq", "fx.eq.m2freq", "fx.eq.hfreq"};
            bool written = paramValue(params, prefix + "fx.eq.on") > 0.5f;
            for (int b = 0; b < 4; ++b) {
                written &= std::abs(paramValue(params, prefix + gains[b]) - fit->bands[(size_t)b].gain) < 0.05;
                written &= std::abs(paramValue(params, prefix + freqs[b]) - fit->bands[(size_t)b].freq)
                           < fit->bands[(size_t)b].freq * 0.002 + 0.5;
            }
            check(written, tag + " EQ APPLY writes the four fitted bands and turns the EQ on");
            strip->refresh();
            writeSnapshot(chain, tag + "-eq-auto-applied.png");
            el.undo();
            bool restored = !el.canUndo();
            for (const auto& [k, v] : before) restored &= std::abs(paramValue(params, prefix + k) - v) < 1e-3f * std::max(1.0f, std::abs(v));
            check(restored, tag + " EQ UNDO restores all 21 EQ params");
        }
        eq->setAutoOpen(false);
        check(eq->autoEqStrip() == nullptr, tag + " EQ AUTO closes back to the band controls");
    }

    // ---- AUTO COMP ----
    comp->setAutoOpen(true);
    auto* cstrip = comp->autoCompStrip();
    check(cstrip && cstrip->isVisible(), tag + " COMP AUTO shows the learn strip");
    if (!cstrip) return;
    auto& cl = cstrip->learner();
    std::map<juce::String, float> before;
    for (auto* k : {"on", "thr", "ratio", "att", "rel"}) before[k] = paramValue(params, prefix + "fx.comp." + k);
    learn(cl, render);
    const auto* fit = cl.fit();
    double loudest = -200;
    if (fit) for (double v : fit->levels) loudest = std::max(loudest, v);
    check(fit != nullptr && fit->exact, tag + " COMP LEARN fits the detector trace exactly: " + cl.status()
                                            + " (loudest block " + juce::String(loudest, 1) + " dB)");
    check(cl.histogram().size() == 30, tag + " COMP histogram has 30 bins");
    cstrip->refresh();
    writeSnapshot(chain, tag + "-comp-auto-learned.png");
    if (fit) {
        cl.apply();
        const bool written = paramValue(params, prefix + "fx.comp.on") > 0.5f
            && std::abs(paramValue(params, prefix + "fx.comp.thr") - fit->threshold) < 0.05
            && std::abs(paramValue(params, prefix + "fx.comp.ratio") - fit->ratio) < 0.01
            && std::abs(paramValue(params, prefix + "fx.comp.att") - fit->attack) < fit->attack * 0.01
            && std::abs(paramValue(params, prefix + "fx.comp.rel") - fit->release) < fit->release * 0.01;
        check(written, tag + " COMP APPLY writes threshold, ratio, attack, release and on");
        cl.undo();
        bool restored = true;
        for (const auto& [k, v] : before)
            restored &= std::abs(paramValue(params, prefix + "fx.comp." + k) - v) < 1e-3f * std::max(1.0f, std::abs(v));
        check(restored, tag + " COMP UNDO restores the five comp params");
    }
    comp->setAutoOpen(false);
}

// The side-chain row writes fx.comp.sc; external keys are SQ-4 only.
inline void checkSideChain(Checker& check, fui::FxChain& chain, fui::ParameterSource params, const juce::String& prefix,
                           const juce::String& tag, bool drum) {
    auto* comp = chain.module(fui::FxModuleView::Comp);
    auto* row = comp ? comp->sideChainRow() : nullptr;
    check(row && row->isVisible(), tag + " COMP shows the SIDECHAIN row");
    if (!row) return;
    // DR-1 keys on its own pads ("01 KICK"); the others list KICK first.
    row->menu().setSelectedId(2, juce::sendNotificationSync);
    check(std::abs(paramValue(params, prefix + "fx.comp.sc") - 1) < 1e-3f, tag + " SIDECHAIN menu writes fx.comp.sc");
    check(comp->keyHpKnob() && comp->keyHpKnob()->isVisible(), tag + " keyed COMP shows the KEY HP knob");
    const int external = drum ? 18 : 3;   // TRACK 2 / TRACK 1
    check(!row->menu().isItemEnabled(external), tag + " standalone lists track keys disabled");
    if (drum) check(row->menu().getItemText(1).startsWith("01 "), tag + " pad keys show pad names: " + row->menu().getItemText(1));
    comp->repaint();
    writeSnapshot(chain, tag + "-comp-sidechain.png");
    fui::writeParam(params, prefix + "fx.comp.sc", 0);
    row->poll();
    check(row->menu().getSelectedId() == 1, tag + " SIDECHAIN follows an outside param change");
    check(comp->keyHpKnob() == nullptr, tag + " unkeyed COMP hides KEY HP");
}

// Standalone device: EQ/COMP AUTO + side-chain on the FX page.
template <class Processor, class Feed>
int runStandalone(const juce::String& machine, int width, int height, bool drum, Feed feed) {
    Checker check{machine};
    auto proc = std::make_unique<Processor>();
    proc->prepareToPlay(48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc->createEditor());
    editor->setSize(width, height);
    editor->setVisible(true);
    auto* tab = findFxComponent<juce::TextButton>(*editor, drum ? "GROUP FX" : "FX CHAIN");
    auto* chain = findFxComponent<fui::FxChain>(*editor);
    check(tab && chain, "the editor has an FX page");
    if (!tab || !chain) return check.fails;
    tab->setToggleState(true, juce::dontSendNotification);
    tab->onClick();
    auto params = fui::ParameterSource::fromApvts(proc->apvts);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    int block = 0;
    auto render = [&] {
        buffer.clear();
        midi.clear();
        feed(*proc, midi, block++);
        proc->processBlock(buffer, midi);
    };
    checkChainScope(check, *chain, params, {}, machine, render);
    checkSideChain(check, *chain, params, {}, machine, drum);
    return check.fails;
}

} // namespace automixchecks
