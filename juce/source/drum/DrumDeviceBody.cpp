#include "DrumDeviceBody.h"

#include <initializer_list>
#include <utility>

DrumDeviceBody::DrumDeviceBody(fui::DrumUiModel& model)
    : model_(model), pads(model), padStrip(model), oscA(model, 0), oscB(model, 1), noise(model),
      pitchEnv(model), ampEnv(model), filter(model), mod(model), selBar(model),
      stepSeq(model), fxRack(model, false), routing(model, true) {
    for (auto* component : std::initializer_list<juce::Component*>{
             &pads, &padStrip, &oscA, &oscB, &noise, &pitchEnv, &ampEnv,
             &filter, &mod, &selBar, &stepSeq, &fxRack, &routing })
        addAndMakeVisible(*component);

    const auto configurePage = [this](juce::TextButton& button, Page page) {
        button.setClickingTogglesState(true);
        button.setRadioGroupId(716);
        button.setColour(juce::TextButton::buttonColourId, fui::col::panelHi);
        button.setColour(juce::TextButton::buttonOnColourId, fui::col::acA.withAlpha(0.18f));
        button.setColour(juce::TextButton::textColourOffId, fui::col::textDim);
        button.setColour(juce::TextButton::textColourOnId, fui::col::text);
        button.onClick = [this, page] { selectPage(page); };
        addAndMakeVisible(button);
    };
    configurePage(editPage_, Page::edit);
    configurePage(padFxPage_, Page::padFx);
    configurePage(groupFxPage_, Page::groupFx);
    configurePage(sequencerPage_, Page::sequencer);

    model_.selectionChanges().addChangeListener(this);
    selectPage(Page::edit);
}

DrumDeviceBody::~DrumDeviceBody() { model_.selectionChanges().removeChangeListener(this); }

void DrumDeviceBody::selectPage(Page page) {
    page_ = page;
    editPage_.setToggleState(page == Page::edit, juce::dontSendNotification);
    padFxPage_.setToggleState(page == Page::padFx, juce::dontSendNotification);
    groupFxPage_.setToggleState(page == Page::groupFx, juce::dontSendNotification);
    sequencerPage_.setToggleState(page == Page::sequencer, juce::dontSendNotification);

    if (page == Page::padFx) {
        const auto pad = model_.selectedPad();
        fxRack.setPad(pad, model_.padName(pad));
    } else if (page == Page::groupFx) {
        fxRack.setPad(-1, "DRUM GROUP");
    }
    resized();
}

void DrumDeviceBody::changeListenerCallback(juce::ChangeBroadcaster*) {
    if (page_ == Page::padFx) {
        const auto pad = model_.selectedPad();
        fxRack.setPad(pad, model_.padName(pad));
    }
}

// Row heights are fixed; the column table is derived from the body's own
// width. The standalone rack is 1460 wide and reproduces the original layout
// exactly, while SQ-4 hosts the same body on a wider canvas
// (DeviceFocusView::kDrumWidth) because its focus slot is much wider relative
// to its height -- at 1460 the body was height-bound and letterboxed with
// ~80px of dead space down each side (DeviceFocusView::layoutBody scales
// uniformly). Widening the canvas rather than compressing the rows keeps
// every panel's internal layout untouched.
void DrumDeviceBody::resized() {
    constexpr int gap = 9, rightX = 379, baseRight = 1063;
    const int w = getWidth() > 0 ? getWidth() : 1460;
    const int rightW = juce::jmax(baseRight / 2, w - 18 - rightX);
    const int fullW = juce::jmax(1, w - 36);

    // Spread a row of panels across the right column, scaling each panel's
    // base width by the column's actual width. The last panel closes the row
    // so rounding can never leave a seam at the right margin.
    auto layRow = [&](std::initializer_list<std::pair<juce::Component*, int>> items,
                      int y, int h) {
        const int n = (int)items.size();
        const int gaps = gap * (n - 1);
        const int avail = rightW - gaps, baseAvail = baseRight - gaps;
        int x = rightX, i = 0;
        for (auto& [component, baseW] : items) {
            const int cw = (++i == n) ? (rightX + rightW - x)
                                      : juce::roundToInt((double)baseW * avail / baseAvail);
            component->setBounds(x, y, cw, h);
            x += cw + gap;
        }
    };

    pads.setBounds(18, 103, 352, 369);
    padStrip.setBounds(18, 481, 352, 119);
    routing.setBounds(18, 609, 352, 45);
    auto pageTabs = juce::Rectangle<int>(rightX, 103, rightW, 26);
    editPage_.setBounds(pageTabs.removeFromLeft(76));
    pageTabs.removeFromLeft(5);
    padFxPage_.setBounds(pageTabs.removeFromLeft(96));
    pageTabs.removeFromLeft(5);
    groupFxPage_.setBounds(pageTabs.removeFromLeft(112));
    pageTabs.removeFromLeft(5);
    sequencerPage_.setBounds(pageTabs.removeFromLeft(126));
    const bool showEdit = page_ == Page::edit;
    const bool showFx = page_ == Page::padFx || page_ == Page::groupFx;
    const bool showSequencer = page_ == Page::sequencer;
    const bool showPadSidebar = !showSequencer;
    for (auto* c : std::initializer_list<juce::Component*>{&pads,&padStrip,&routing}) c->setVisible(showPadSidebar);
    for (auto* c : std::initializer_list<juce::Component*>{&selBar,&oscA,&oscB,&noise,&pitchEnv,&ampEnv,&filter,&mod}) c->setVisible(showEdit);
    fxRack.setVisible(showFx);
    stepSeq.setVisible(showSequencer);
    selBar.setBounds(rightX, 139, rightW, 31);
    layRow({ { &oscA, 424 }, { &oscB, 425 }, { &noise, 196 } }, 179, 243);
    layRow({ { &pitchEnv, 225 }, { &ampEnv, 259 }, { &filter, 259 }, { &mod, 293 } }, 431, 209);
    fxRack.setBounds(rightX,139,rightW,605);
    // The sequencer owns a full workspace rather than permanently occupying
    // the lower third of sound and FX editing. Its own pad labels make the
    // sidebar redundant while it is open.
    stepSeq.setBounds(18, 139, fullW, 605);
}
