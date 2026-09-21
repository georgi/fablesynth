#include "BassDeviceBody.h"

#include <initializer_list>
#include <utility>

BassDeviceBody::BassDeviceBody(fui::BassUiModel& model)
    : model_(model), osc(model), sub(model), filter(model), env(model), lfo(model), accent(model),
      keys(model), seq(model), fxRack(model, false), arp(model, true) {
    for (auto* component : std::initializer_list<juce::Component*>{
             &osc, &sub, &filter, &env, &lfo, &accent, &keys, &seq, &fxRack })
        addAndMakeVisible(*component);
    addAndMakeVisible(arp);
    const auto configurePage = [this](juce::TextButton& button, Page page) {
        button.setClickingTogglesState(true);
        button.setRadioGroupId(718);
        button.setColour(juce::TextButton::buttonColourId, fui::col::panelHi);
        button.setColour(juce::TextButton::buttonOnColourId, fui::accentA().withAlpha(0.18f));
        button.setColour(juce::TextButton::textColourOffId, fui::col::textDim);
        button.setColour(juce::TextButton::textColourOnId, fui::col::text);
        button.onClick = [this, page] { selectPage(page); };
        addAndMakeVisible(button);
    };
    configurePage(editPage_, Page::edit);
    configurePage(fxPage_, Page::fx);
    configurePage(sequencerPage_, Page::sequencer);
    configurePage(arpPage_, Page::arp);
    selectPage(model_.arpSettings().enabled ? Page::arp : Page::edit);
}

void BassDeviceBody::selectPage(Page page) {
    page_ = page;
    editPage_.setToggleState(page == Page::edit, juce::dontSendNotification);
    fxPage_.setToggleState(page == Page::fx, juce::dontSendNotification);
    sequencerPage_.setToggleState(page == Page::sequencer, juce::dontSendNotification);
    arpPage_.setToggleState(page == Page::arp, juce::dontSendNotification);
    auto settings = model_.arpSettings();
    const bool arpEnabled = page == Page::arp;
    if (settings.enabled != arpEnabled) {
        settings.enabled = arpEnabled;
        model_.setArpSettings(settings);
    }
    resized();
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

    auto tabs = juce::Rectangle<int>(18, 103, fullW, 26);
    editPage_.setBounds(tabs.removeFromLeft(76)); tabs.removeFromLeft(5);
    fxPage_.setBounds(tabs.removeFromLeft(106)); tabs.removeFromLeft(5);
    sequencerPage_.setBounds(tabs.removeFromLeft(126)); tabs.removeFromLeft(5);
    arpPage_.setBounds(tabs.removeFromLeft(70));
    const bool showEdit = page_ == Page::edit;
    const bool showFx = page_ == Page::fx;
    const bool showSequencer = page_ == Page::sequencer;
    const bool showArp = page_ == Page::arp;
    for (auto* c : std::initializer_list<juce::Component*>{&osc,&sub,&filter,&env,&lfo,&accent}) c->setVisible(showEdit);
    fxRack.setVisible(showFx);
    layRow({ { &osc, 464 }, { &sub, 192 }, { &filter, 355 }, { &env, 386 } }, 139, 243);
    layRow({ { &lfo, 290 }, { &accent, 250 } }, 391, 140);
    keys.setVisible(showEdit);
    seq.setVisible(showSequencer);
    arp.setVisible(showArp);
    seq.setBounds(18, 139, fullW, 919);
    arp.setBounds(seq.getBounds());
    fxRack.setBounds(18, 139, fullW, 581);
    keys.setBounds(18, 540, fullW, 140);
}
