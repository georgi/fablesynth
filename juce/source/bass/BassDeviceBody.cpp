#include "BassDeviceBody.h"

#include <initializer_list>
#include <utility>

BassDeviceBody::BassDeviceBody(fui::BassUiModel& model)
    : osc(model), sub(model), filter(model), env(model), lfo(model), accent(model),
      keys(model), seq(model), fxRack(model, false) {
    for (auto* component : std::initializer_list<juce::Component*>{
             &osc, &sub, &filter, &env, &lfo, &accent, &keys, &seq, &fxRack })
        addAndMakeVisible(*component);
    addAndMakeVisible(pages);
    pages.onChange = [this] { resized(); };
}

// Keyboard last, the way every hardware and soft synth puts it: KEYS is a
// full-width bottom row rather than a third column of the mod row, and LFO /
// ACCENT spread across the width it used to take. Row heights are fixed; the
// column table is derived from the body's own width so the standalone rack
// (1460) and SQ-4's wider focus canvas (DeviceFocusView::kBassWidth) share
// this one layout -- see DrumDeviceBody::resized() for the same scheme.
void BassDeviceBody::resized() {
    constexpr int gap = 9;
    const int w = getWidth() > 0 ? getWidth() : 1460;
    const int fullW = juce::jmax(1, w - 36);

    // Spread a row across the full width, scaling each panel by its base
    // width. The last panel closes the row so rounding leaves no seam.
    auto layRow = [&](std::initializer_list<std::pair<juce::Component*, int>> items,
                      int y, int h) {
        const int n = (int)items.size();
        const int avail = fullW - gap * (n - 1);
        int baseTotal = 0;
        for (auto& [component, baseW] : items) { juce::ignoreUnused(component); baseTotal += baseW; }
        int x = 18, i = 0;
        for (auto& [component, baseW] : items) {
            const int cw = (++i == n) ? (18 + fullW - x)
                                      : juce::roundToInt((double)baseW * avail / baseTotal);
            component->setBounds(x, y, cw, h);
            x += cw + gap;
        }
    };

    pages.setBounds(18,103,fullW,26);
    const bool showFx = pages.fxSelected();
    for (auto* c : std::initializer_list<juce::Component*>{&osc,&sub,&filter,&env,&lfo,&accent}) c->setVisible(!showFx);
    fxRack.setVisible(showFx);
    layRow({ { &osc, 464 }, { &sub, 192 }, { &filter, 355 }, { &env, 386 } }, 139, 243);
    layRow({ { &lfo, 290 }, { &accent, 250 } }, 391, 140);
    keys.setVisible(!showFx);
    seq.setBounds(18, showFx ? 729 : 540, fullW, showFx ? 329 : 369);
    fxRack.setBounds(18, 139, fullW, 581);
    keys.setBounds(18, 918, fullW, 140);
}
