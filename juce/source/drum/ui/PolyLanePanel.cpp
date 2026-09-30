#include "PolyLanePanel.h"

namespace fui {
namespace {
const juce::Colour well{0xff0a0d13}, face{0xff11141c};
void wellOutline(juce::Graphics& g, juce::Rectangle<float> r, float radius = 5.0f) {
    g.setColour(well); g.fillRoundedRectangle(r, radius);
    g.setColour(col::line); g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
}
void miniLabel(juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r) {
    g.setColour(col::textDim); g.setFont(monoFont(8.0f)); drawSpaced(g, s, r, 1.1f);
}
}

void PolyButton::paintButton(juce::Graphics& g, bool hovered, bool down) {
    auto b = getLocalBounds().toFloat();
    const bool selected = getToggleState();
    if (style == Style::power) {
        wellOutline(g, b);
        if (selected) {
            g.setColour(col::acB.withAlpha(.45f)); g.drawRoundedRectangle(b.reduced(.5f), 5.0f, 1.0f);
            g.setColour(col::acB.withAlpha(.25f)); g.fillEllipse(7.0f, b.getCentreY() - 5.0f, 10.0f, 10.0f);
        }
        g.setColour(selected ? col::acB : juce::Colour(0xff262d3a));
        g.fillEllipse(9.0f, b.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setColour(selected ? col::acB : hovered ? col::text : col::textDim);
        g.setFont(monoFont(8.0f, true));
        drawSpaced(g, getButtonText(), getLocalBounds().withTrimmedLeft(21), 1.0f, juce::Justification::centred);
        return;
    }
    if (style == Style::view) {
        wellOutline(g, b);
        g.setColour(col::acA.withAlpha(.45f)); g.drawRoundedRectangle(b.reduced(.5f), 5.0f, 1.0f);
    } else {
        g.setColour(face); g.fillRect(b);
        if (selected && style == Style::segment) {
            g.setColour(col::acA.withAlpha(.12f)); g.fillRect(b);
            g.setColour(col::acA); g.fillRect(1.0f, b.getBottom() - 2.0f, b.getWidth() - 2.0f, 2.0f);
        }
        if (!first) { g.setColour(col::line); g.drawVerticalLine(0, 1.0f, b.getBottom() - 1.0f); }
    }
    g.setColour(selected ? col::acA : hovered || down ? col::text : col::textDim);
    g.setFont(monoFont(style == Style::history ? 14.0f : style == Style::stepper ? 15.0f : 8.0f,
                       style == Style::stepper));
    drawSpaced(g, getButtonText(), getLocalBounds(), style == Style::segment ? .7f : 0.0f, juce::Justification::centred);
    if (hasKeyboardFocus(true)) { g.setColour(col::acA); g.drawRect(getLocalBounds().reduced(2), 1); }
}

SequenceNumberInput::SequenceNumberInput(const juce::String& name) : caption(name) {
    input.setTitle(name); input.setJustification(juce::Justification::centred);
    input.setFont(monoFont(12)); input.setSelectAllWhenFocused(true);
    input.setColour(juce::TextEditor::backgroundColourId, well);
    input.setColour(juce::TextEditor::textColourId, col::acA);
    input.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    input.setColour(juce::TextEditor::focusedOutlineColourId, col::acA);
    input.onReturnKey = [this] { commit(); };
    input.onFocusLost = [this] { commit(); };
    input.onEscapeKey = [this] { input.setText(juce::String(value), false); if (onError) onError({}); };
    addAndMakeVisible(input);
    for (auto* b : { &minus, &plus }) {
        addAndMakeVisible(b); b->setRepeatSpeed(400, 100); b->setTriggeredOnMouseDown(true);
        const int delta = b == &minus ? -1 : 1;
        b->setTitle((delta < 0 ? "Decrease " : "Increase ") + name);
        b->onStateChange = [this, b] { if (b->isDown()) { if (onBegin) onBegin(); } else if (onEnd) onEnd(); };
        b->onClick = [this, delta] {
            int v = value + delta;
            if (wrap) v = v < minimum ? maximum : v > maximum ? minimum : v;
            else v = juce::jlimit(minimum, maximum, v);
            if (v != value && onChange) onChange(v);
        };
    }
}
void SequenceNumberInput::commit() {
    const auto draft = input.getText().trim();
    if (draft.isEmpty() || !draft.containsOnly("0123456789") || draft.getIntValue() < minimum || draft.getIntValue() > maximum) {
        if (onError) onError(caption + ": enter " + juce::String(minimum) + " to " + juce::String(maximum));
        return;
    }
    if (onError) onError({});
    if (draft.getIntValue() != value && onChange) onChange(draft.getIntValue());
}
void SequenceNumberInput::setValue(int v, int hi) {
    maximum = hi;
    if (v != value || !input.hasKeyboardFocus(true)) input.setText(juce::String(v), false);
    value = v;
}
void SequenceNumberInput::paint(juce::Graphics& g) { wellOutline(g, getLocalBounds().toFloat()); }
void SequenceNumberInput::resized() {
    auto b = getLocalBounds().reduced(1);
    minus.setBounds(b.removeFromLeft(29)); plus.setBounds(b.removeFromRight(29)); input.setBounds(b);
}

PolyLanePanel::PolyLanePanel(DrumUiModel& m) : model(m) {
    setWantsKeyboardFocus(true);
    for (auto* c : std::initializer_list<juce::Component*>{ &error, &undo, &redo, &enabled, &grid, &fit, &one, &two, &view, &steps, &rotation }) addAndMakeVisible(c);
    for (auto& b : source) addAndMakeVisible(b);
    error.setFont(monoFont(8)); error.setColour(juce::Label::textColourId, col::acB);
    grid.first = one.first = source[0].first = undo.first = true;
    fit.last = two.last = source[3].last = redo.last = true;
    grid.setRadioGroupId(810); fit.setRadioGroupId(810); one.setRadioGroupId(811); two.setRadioGroupId(811);
    grid.setTitle("GRID timing mode"); fit.setTitle("FIT timing mode"); enabled.setTitle("Enable lane POLY");
    one.setTitle("FIT cycle one bar"); two.setTitle("FIT cycle two bars");
    undo.setTitle("Undo sequence edit"); redo.setTitle("Redo sequence edit"); view.setTitle("View source bar");
    undo.onClick = [this] { if (onUndo) onUndo(); refresh(); };
    redo.onClick = [this] { if (onRedo) onRedo(); refresh(); };
    enabled.onClick = [this] { edit([](auto&, auto& lane) { lane.enabled = !lane.enabled; }, false); };
    grid.onClick = [this] { edit([](auto&, auto& lane) { lane.mode = fable::DrumRhythmMode::grid; }); };
    fit.onClick = [this] { edit([](auto&, auto& lane) { lane.mode = fable::DrumRhythmMode::fit; }); };
    one.onClick = [this] { edit([](auto&, auto& lane) { lane.cycleBeats = 4; }); };
    two.onClick = [this] { edit([](auto&, auto& lane) { lane.cycleBeats = 8; }); };
    steps.minimum = 1; rotation.wrap = true;
    for (auto* number : { &steps, &rotation }) {
        number->onBegin = [this] { if (!gesture) { gesture = true; gestureSaved = false; } };
        number->onEnd = [this] { gesture = false; };
        number->onError = [this](auto text) { error.setText(text, juce::dontSendNotification); };
    }
    steps.onChange = [this](int n) { edit([n](auto&, auto& lane) { lane.steps = n; lane.rotation %= n; }); };
    rotation.onChange = [this](int n) { edit([n](auto&, auto& lane) { lane.rotation = n; }); };
    for (int i = 0; i < 4; ++i) {
        source[(size_t)i].setTitle("Source bar " + juce::String(i + 1));
        source[(size_t)i].onClick = [this, i] { edit([i](auto&, auto& lane) { lane.sourceBar = i; }); };
    }
    view.onClick = [this] {
        const auto s = model.sequence(); const int p = model.selectedPad();
        if (s.configuredLanes & (1 << p)) model.setEditPattern(s.rhythm.lanes[(size_t)p].sourceBar);
    };
    refresh(); startTimerHz(30);
}
void PolyLanePanel::edit(const std::function<void(DrumSequence&, fable::DrumLaneRhythm&)>& action, bool activate) {
    auto s = model.sequence(); const int p = model.selectedPad();
    auto& lane = s.rhythm.lanes[(size_t)p];
    if (!(s.configuredLanes & (1 << p))) { lane = {}; lane.sourceBar = model.editPattern(); }
    if (activate) lane.enabled = true;
    action(s, lane); s.hasRhythm = true; s.configuredLanes |= (uint16_t)(1 << p);
    if (!gesture || !gestureSaved) { if (beforeEdit) beforeEdit(); gestureSaved = true; }
    model.commitSequence(s); refresh();
}
void PolyLanePanel::refresh() {
    refreshing = true;
    const auto s = model.sequence(); const int p = model.selectedPad();
    auto lane = s.rhythm.lanes[(size_t)p];
    if (!(s.configuredLanes & (1 << p))) { lane = {}; lane.sourceBar = model.editPattern(); }
    if (lastPad != p) { error.setText({}, juce::dontSendNotification); lastPad = p; }
    on = lane.enabled; fitMode = lane.mode == fable::DrumRhythmMode::fit;
    laneSteps = lane.steps; sourceBar = lane.sourceBar; cycleBeats = lane.cycleBeats; selectedPad = p;
    enabled.setButtonText(on ? "ON" : "OFF"); enabled.setToggleState(on, juce::dontSendNotification);
    grid.setToggleState(!fitMode, juce::dontSendNotification); fit.setToggleState(fitMode, juce::dontSendNotification);
    one.setVisible(fitMode); two.setVisible(fitMode);
    one.setToggleState(cycleBeats == 4, juce::dontSendNotification); two.setToggleState(cycleBeats == 8, juce::dontSendNotification);
    steps.setValue(lane.steps, 16); rotation.setValue(lane.rotation, lane.steps - 1);
    const int bars = juce::jlimit(1, 4, model.capabilities().hosted ? model.clipBars() : 4);
    for (int i = 0; i < 4; ++i) { source[(size_t)i].setVisible(i < bars); source[(size_t)i].setToggleState(i == lane.sourceBar, juce::dontSendNotification); }
    view.setEnabled(model.editPattern() != lane.sourceBar);
    for (auto* button : { &grid, &fit, &one, &two, &view })
        button->setAlpha(on ? 1.0f : .6f);
    steps.setAlpha(on ? 1.0f : .6f); rotation.setAlpha(on ? 1.0f : .6f);
    for (auto& button : source) button.setAlpha(on ? 1.0f : .6f);
    view.setAlpha((model.editPattern() == lane.sourceBar ? .35f : 1.0f) * (on ? 1.0f : .6f));
    resized(); repaint(); refreshing = false;
}
void PolyLanePanel::timerCallback() { if (isShowing()) refresh(); }
void PolyLanePanel::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff0f131b), 0, 0, juce::Colour(0xff0b0e14), 0, r.getBottom(), false));
    g.fillRoundedRectangle(r, 9.0f);
    g.setColour(juce::Colours::white.withAlpha(.03f)); g.drawHorizontalLine(1, 9.0f, r.getRight() - 9.0f);
    g.setColour(col::line); g.drawRoundedRectangle(r.reduced(.5f), 9.0f, 1.0f);
    g.setColour(col::text); g.setFont(dispFont(11)); drawSpaced(g, "POLY", titleBounds, 1.7f);
    g.setColour(col::acA); g.setFont(monoFont(9, true));
    drawSpaced(g, juce::String(selectedPad + 1).paddedLeft('0', 2) + " " + model.padName(selectedPad), padBounds, .9f);
    g.setColour(juce::Colour(0xff07090e)); g.fillRoundedRectangle(meterBounds.toFloat(), 5.0f);
    g.setColour(juce::Colours::black.withAlpha(.45f)); g.drawHorizontalLine(meterBounds.getY() + 1, (float)meterBounds.getX() + 4, (float)meterBounds.getRight() - 4);
    const int play = on && model.sequencerPlaying() ? model.lanePosition(selectedPad) - sourceBar * 16 : -1;
    const int gap = 3, usable = meterBounds.getWidth() - 14, width = juce::jmax(2, (usable - (laneSteps - 1) * gap) / laneSteps);
    for (int i = 0; i < laneSteps; ++i) {
        const int value = model.step(sourceBar, selectedPad, i);
        auto c = i == play ? value ? col::acB : col::text : value ? juce::Colour(0xff73513c) : juce::Colour(0xff1a202b);
        c = c.withMultipliedAlpha(on ? 1.0f : .45f);
        const float x = (float)(meterBounds.getX() + 7 + i * (width + gap));
        const float h = value == 2 ? 10.0f : 5.0f, y = (float)meterBounds.getCentreY() - h * .5f;
        if (i == play) { g.setColour(c.withAlpha(.2f)); g.fillRoundedRectangle(x - 2, y - 2, (float)width + 4, h + 4, 3.0f); }
        g.setColour(c); g.fillRoundedRectangle(x, y, (float)width, h, 2.0f);
    }
    const juce::String readout = !on ? juce::String::fromUTF8(u8"OFF · SET STEPS TO START") : fitMode
        ? juce::String(laneSteps) + " IN " + (cycleBeats == 4 ? "1 BAR" : "2 BARS") + juce::String::fromUTF8(u8" · STRAIGHT")
        : "LOOP " + juce::String(laneSteps) + juce::String::fromUTF8(u8" × 1/16 · SWING");
    g.setColour(on ? col::acB.interpolatedWith(col::textHint, .25f) : col::textHint);
    g.setFont(monoFont(8)); drawSpaced(g, readout, readoutBounds, .7f);
    const int dividerY = 51;
    g.setColour(col::line); g.drawHorizontalLine(dividerY, 12.0f, r.getRight() - 12.0f);
    juce::Graphics::ScopedSaveState scope(g);
    if (!on) g.setOpacity(.6f);
    auto segmentWell = [&g](juce::Rectangle<int> a, juce::Rectangle<int> b) {
        if (!a.isEmpty() && !b.isEmpty()) wellOutline(g, a.getUnion(b).toFloat());
    };
    segmentWell(grid.getBounds(), fit.getBounds());
    if (fitMode) segmentWell(one.getBounds(), two.getBounds());
    segmentWell(source[0].getBounds(), source[(size_t)juce::jlimit(0, 3, model.hasTargetClip() ? model.clipBars() - 1 : 3)].getBounds());
    miniLabel(g, "MODE", modeLabel); miniLabel(g, "STEPS", stepsLabel);
    if (fitMode) miniLabel(g, "CYCLE", cycleLabel);
    miniLabel(g, "ROTATE", rotationLabel); miniLabel(g, "SOURCE", sourceLabel);
}
void PolyLanePanel::resized() {
    const int w = getWidth(), right = w - 12;
    titleBounds = {12, 11, 53, 30}; padBounds = {76, 11, 125, 30};
    enabled.setBounds(205, 13, 56, 24);
    undo.setBounds(right - 56, 13, 28, 24); redo.setBounds(right - 28, 13, 28, 24);
    readoutBounds = { right - 56 - 183, 13, 175, 24 };
    const int meterRight = readoutBounds.getX() - 10;
    const int meterWidth = juce::jmin(260, laneSteps * 16 + 14, juce::jmax(60, meterRight - 274));
    meterBounds = { meterRight - meterWidth, 13, meterWidth, 24 };
    const int bars = juce::jlimit(1, 4, model.capabilities().hosted ? model.clipBars() : 4);
    const int sourceWidth = bars * 31;
    auto place = [this](int& x, int y, juce::Rectangle<int>& label, int lw, juce::Component& control, int cw) {
        label = { x, y, lw, 30 }; x += lw + 7; control.setBounds(x, y, cw, 30); x += cw + 18;
    };
    int x = 12, y = 67;
    if (w < 740) { y = 64; }
    modeLabel = {x, y, 31, 30}; x += 38;
    grid.setBounds(x, y, 52, 30); fit.setBounds(x + 52, y, 46, 30); x += 98 + 18;
    place(x, y, stepsLabel, 39, steps, 100);
    if (fitMode) {
        if (w < 740 && x + 155 > right) { x = 12; y += 42; }
        cycleLabel = {x, y, 37, 30}; x += 44;
        one.setBounds(x, y, 57, 30); two.setBounds(x + 57, y, 65, 30); x += 122 + 18;
    } else cycleLabel = {};
    if (x + 158 > right) { x = 12; y += 42; }
    place(x, y, rotationLabel, 46, rotation, 100);
    if (x + 50 + sourceWidth + 8 + 96 > right) { x = 12; y += 42; }
    sourceLabel = {x, y, 43, 30}; x += 50;
    for (int i = 0; i < 4; ++i) source[(size_t)i].setBounds(x + i * 31, y, 31, 30);
    x += sourceWidth + 8; view.setBounds(x, y, 96, 30);
    error.setBounds(12, getHeight() - 18, getWidth() - 24, 16);
}
}
