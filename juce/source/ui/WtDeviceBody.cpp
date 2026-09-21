#include "WtDeviceBody.h"

WtDeviceBody::WtDeviceBody(fui::WtUiModel& model,
                           std::function<HostTransport()> transportProvider)
    : model_(model), oscA(model, 0, "oscA", fui::Accent::A, "OSC A"),
      oscB(model, 1, "oscB", fui::Accent::B, "OSC B"),
      util(model.parameters()), filter(model.parameters()),
      env1(model.parameters(), "env1", "AMP ENV", juce::Colour(0xffe8edf7), fui::Accent::N),
      env2(model.parameters(), "env2", "MOD ENV", juce::Colour(0xffb18cff), fui::Accent::F, 3),
      lfos(model.parameters(), transportProvider ? std::move(transportProvider) : [&model] {
          return HostTransport{ model.hostBpm(), 0.0, model.sequencerPlaying() };
      }),
      matrix(model.parameters()), fx(model, true), seq(model), arp(model, false) {
    addAndMakeVisible(oscA); addAndMakeVisible(oscB); addAndMakeVisible(util);
    addAndMakeVisible(filter); addAndMakeVisible(env1); addAndMakeVisible(env2);
    addAndMakeVisible(lfos); addAndMakeVisible(matrix); addAndMakeVisible(fx);
    addAndMakeVisible(seq);
    addAndMakeVisible(arp);
    const auto configurePage = [this](juce::TextButton& button, Page page) {
        button.setClickingTogglesState(true);
        button.setRadioGroupId(717);
        button.setColour(juce::TextButton::buttonColourId, fui::col::panelHi);
        button.setColour(juce::TextButton::buttonOnColourId, fui::col::acA.withAlpha(0.18f));
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
    oscA.onEditTable = [this](int osc) { if (onEditTable) onEditTable(osc); };
    oscB.onEditTable = [this](int osc) { if (onEditTable) onEditTable(osc); };
}

void WtDeviceBody::selectPage(Page page) {
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

juce::Rectangle<int> WtDeviceBody::colArea(int c0, int span, int y, int h) const {
    const int padX = 14, gap = 9;
    const float colUnit = (LW - padX * 2 - 11 * gap) / 12.0f;
    int x = (int)std::round(static_cast<float>(padX) + static_cast<float>(c0) * (colUnit + static_cast<float>(gap)));
    int w = (int)std::round(static_cast<float>(span) * colUnit + static_cast<float>((span - 1) * gap));
    return { x, y, w, h };
}

void WtDeviceBody::resized() {
    const int gap = 9;
    const int row1 = 250, row2 = 206, row3 = 90, row4 = 270;
    const int y1 = 36;
    const int y2 = y1 + row1 + gap;
    const int y3 = y2 + row2 + gap;
    const int y4 = y3 + row3 + gap;
    oscA.setBounds(colArea(0, 5, y1, row1));
    oscB.setBounds(colArea(5, 5, y1, row1));
    util.setBounds(colArea(10, 2, y1, row1));
    filter.setBounds(colArea(0, 4, y2, row2));
    env1.setBounds(colArea(4, 2, y2, row2));
    env2.setBounds(colArea(6, 2, y2, row2));
    lfos.setBounds(colArea(8, 4, y2, row2));
    auto tabs = juce::Rectangle<int>(14, 2, LW - 28, 26);
    editPage_.setBounds(tabs.removeFromLeft(76)); tabs.removeFromLeft(5);
    fxPage_.setBounds(tabs.removeFromLeft(106)); tabs.removeFromLeft(5);
    sequencerPage_.setBounds(tabs.removeFromLeft(126)); tabs.removeFromLeft(5);
    arpPage_.setBounds(tabs.removeFromLeft(70));
    const bool showEdit = page_ == Page::edit;
    const bool showFx = page_ == Page::fx;
    const bool showSequencer = page_ == Page::sequencer;
    const bool showArp = page_ == Page::arp;
    for (auto* c : std::initializer_list<juce::Component*>{&oscA,&oscB,&util,&filter,&env1,&env2,&lfos,&matrix}) c->setVisible(showEdit);
    fx.setVisible(showFx);
    seq.setVisible(showSequencer);
    arp.setVisible(showArp);
    matrix.setBounds(colArea(0, 12, y3, row3));
    fx.setBounds(colArea(0, 12, y1, y4-y1-gap));
    seq.setBounds(colArea(0, 12, y1, LH - y1 - 14));
    arp.setBounds(seq.getBounds());
}
