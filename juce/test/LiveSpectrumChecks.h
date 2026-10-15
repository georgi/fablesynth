#pragma once
#include "AutoMixUiChecks.h"
#include "../source/ui/LiveSpectrum.h"

// Live spectrum behind the EQ curves and the KIT MIX plot: the path is drawn
// while audio renders and cleared after silence. Each state is rendered to a
// PNG in build/live-spectrum for inspection. Offline only.
namespace livespectrumchecks {

inline double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

inline void writeSnapshot(juce::Component& c, const juce::String& name) {
    auto dir = juce::File::getCurrentWorkingDirectory().getChildFile("build/live-spectrum");
    dir.createDirectory();
    const auto file = dir.getChildFile(name);
    file.deleteFile();
    if (auto stream = file.createOutputStream()) {
        juce::PNGImageFormat png;
        png.writeImageToStream(c.createComponentSnapshot(c.getLocalBounds()), *stream);
        std::printf("  wrote %s\n", file.getFullPathName().toRawUTF8());
    }
}

// The filled path a panel would draw from the current state.
inline bool hasPath(const fui::LiveSpectrum& live) {
    return !fui::LiveSpectrum::path(live.levels(), [](double f) { return f; }, [](double d) { return d; }, -100).isEmpty();
}

// Renders until every display is drawn (update each block).
template <class Render, class Update>
bool untilDrawn(const std::vector<const fui::LiveSpectrum*>& lives, Render render, Update update, int maxBlocks = 400) {
    for (int i = 0; i < maxBlocks; ++i) {
        render();
        update();
        if (std::all_of(lives.begin(), lives.end(), [](auto* l) { return hasPath(*l); })) return true;
    }
    return false;
}
// Renders silence until every display is cleared.
template <class Render, class Update>
bool untilCleared(const std::vector<const fui::LiveSpectrum*>& lives, Render render, Update update, int maxBlocks = 4000) {
    for (int i = 0; i < maxBlocks; ++i) {
        render();
        if (i % 8) continue;
        update();
        if (std::none_of(lives.begin(), lives.end(), [](auto* l) { return hasPath(*l); })) return true;
    }
    return false;
}

// Standalone EQ: drawn while the device renders (via the visual clock), cleared
// when hidden and after silence.
template <class Render, class Silence>
void checkStandaloneEq(automixchecks::Checker& check, fui::FxChain& chain, const juce::String& tag, Render render,
                       Silence silence) {
    auto* eq = chain.module(fui::FxModuleView::Eq);
    const auto* live = eq ? eq->liveSpectrum() : nullptr;
    check(live != nullptr, tag + " EQ has a live spectrum");
    if (!live) return;
    // One visual frame per rendered block: rendering runs much faster than real time.
    double frame = nowMs() / 1000;
    const bool drawn = untilDrawn({live}, render, [&frame] { fui::VisualTimer::tickForTesting(frame += 1.0 / 30); });
    check(drawn, tag + " EQ live path is drawn while the device renders (visual clock)");
    writeSnapshot(chain, tag + "-eq-live.png");
    eq->setVisible(false);
    check(!hasPath(*live), tag + " EQ live path clears when the panel is hidden");
    eq->setVisible(true);
    const auto update = [eq] { eq->updateLiveSpectrum(nowMs()); };
    const bool redrawn = untilDrawn({live}, render, update);
    check(redrawn && untilCleared({live}, silence, update), tag + " EQ live path returns when shown, clears after silence");
}

// Standalone device: the EQ on the FX page (the DR-1 group scope) shows the
// device output. `feed` plays; silence sends all-notes-off and renders tails.
template <class Processor, class Feed>
int runStandalone(const juce::String& machine, int width, int height, bool drum, Feed feed) {
    automixchecks::Checker check{machine};
    std::printf("\n== %s live spectrum ==\n", machine.toRawUTF8());
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
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    int block = 0;
    bool silent = false, released = false;
    auto render = [&] {
        buffer.clear();
        midi.clear();
        if (!silent) feed(*proc, midi, block++);
        else if (!released) {
            for (int ch = 1; ch <= 16; ++ch) midi.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
            released = true;
        }
        proc->processBlock(buffer, midi);
    };
    auto silence = [&] { silent = true; render(); };
    checkStandaloneEq(check, *chain, machine, render, silence);
    return check.fails;
}

} // namespace livespectrumchecks
