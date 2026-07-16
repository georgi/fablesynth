#include "Panels.h"
#include "Format.h"

namespace fui {

// ---------- shared helpers ----------
void paintPanelBg(juce::Graphics& g, juce::Component& c) { drawPanel(g, c.getLocalBounds().toFloat()); }

void paintHeaderTitle(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& t, juce::Colour c) {
    g.setColour(c);
    g.setFont(dispFont(10.0f));
    drawSpaced(g, t.toUpperCase(), area, 2.0f, juce::Justification::centredLeft);
}

static void layoutKnobRow(juce::Rectangle<int> area, juce::Array<juce::Component*> ks) {
    int n = ks.size(); if (n == 0) return;
    float cw = static_cast<float>(area.getWidth()) / static_cast<float>(n);
    for (int i = 0; i < n; ++i)
        ks[i]->setBounds(juce::Rectangle<int>((int)std::round(static_cast<float>(area.getX()) + static_cast<float>(i) * cw), area.getY(),
                                              (int)std::round(cw), area.getHeight()));
}
static juce::Array<juce::Component*> ptrs(juce::OwnedArray<Knob>& a) {
    juce::Array<juce::Component*> v; for (auto* k : a) v.add(k); return v;
}

static std::unique_ptr<juce::ParameterAttachment> attachButton(
        const ParameterSource& source, const juce::String& id, juce::Button& button) {
    auto* parameter = source.parameter(id);
    jassert(parameter != nullptr);
    if (parameter == nullptr) return {};
    auto attachment = std::make_unique<juce::ParameterAttachment>(
        *parameter, [&button](float value) {
            button.setToggleState(value >= 0.5f, juce::dontSendNotification);
        }, nullptr);
    button.onClick = [a = attachment.get(), &button] {
        a->setValueAsCompleteGesture(button.getToggleState() ? 1.0f : 0.0f);
    };
    attachment->sendInitialUpdate();
    return attachment;
}

static std::unique_ptr<juce::ParameterAttachment> attachCombo(
        const ParameterSource& source, const juce::String& id, juce::ComboBox& combo) {
    auto* parameter = source.parameter(id);
    jassert(parameter != nullptr);
    if (parameter == nullptr) return {};
    auto attachment = std::make_unique<juce::ParameterAttachment>(
        *parameter, [&combo](float value) {
            combo.setSelectedId((int)std::lround(value) + 1, juce::dontSendNotification);
        }, nullptr);
    combo.onChange = [a = attachment.get(), &combo] {
        a->setValueAsCompleteGesture((float)(combo.getSelectedId() - 1));
    };
    attachment->sendInitialUpdate();
    return attachment;
}

// ===================== OscPanel =====================
OscPanel::OscPanel(WtUiModel& model, int osc, juce::String pre, Accent ac, juce::String t)
    : oscIndex(osc), title(t), prefix(pre), accent(ac),
      power(model.parameters(), pre + ".on", ac), tableStep(model.parameters(), pre + ".table", ac),
      unisonStep(model.parameters(), pre + ".unison", ac), wt(model, osc, accentColour(ac)),
      // POS slider is a mod target: dest 1 (oscA POS) / 2 (oscB POS).
      pos(model.parameters(), pre + ".pos", ac, [&model, osc] { return model.vizPosition(osc); }, osc == 0 ? 1 : 2) {
    addAndMakeVisible(power); addAndMakeVisible(tableStep); addAndMakeVisible(unisonStep);
    addAndMakeVisible(wt); addAndMakeVisible(pos);
    // The table stepper cycles only over the procedural + live user tables and
    // shows each table's live name (the param itself reserves fixed USER slots).
    tableStep.countProvider = [&model] { return model.numTables(); };
    tableStep.nameProvider  = [&model](int idx) { return model.tableName(idx); };
    // ✎ edit button — opens the import / draw editor for this oscillator.
    editBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
    editBtn.setColour(juce::TextButton::textColourOffId, accentColour(ac));
    editBtn.setTooltip("import / draw wavetable");
    editBtn.onClick = [this] { if (onEditTable) onEditTable(oscIndex); };
    addAndMakeVisible(editBtn);
    editBtn.setVisible(model.capabilities().supportsUserTables);
    const char* ids[] = {".oct", ".semi", ".fine", ".detune", ".spread", ".blend", ".level", ".pan"};
    // Continuous knobs are mod targets (A/B per osc): DETUNE (3) → 11/14,
    // SPREAD (4) → 12/15, BLEND (5) → 26/27, LEVEL (6) → 7/8, PAN (7) → 13/16.
    // OCT/SEMI/FINE are discrete steppers → not modulatable (modDest 0);
    // UNISON is the compact stepper in the knob row between FINE and DETUNE
    // (unisonStep — same cell the web UI uses; laid out in resized()).
    for (int i = 0; i < 8; ++i) {
        int modDest = 0;
        switch (i) {
            case 3: modDest = osc == 0 ? 11 : 14; break; // DETUNE → A/B DETUNE
            case 4: modDest = osc == 0 ? 12 : 15; break; // SPREAD → A/B SPREAD
            case 5: modDest = osc == 0 ? 26 : 27; break; // BLEND  → A/B BLEND
            case 6: modDest = osc == 0 ?  7 :  8; break; // LEVEL  → A/B LVL
            case 7: modDest = osc == 0 ? 13 : 16; break; // PAN    → A/B PAN
        }
        auto* k = new Knob(model.parameters(), pre + ids[i], i == 6 ? Knob::Md : Knob::Sm, ac, true, modDest);
        knobs.add(k); addAndMakeVisible(k);
    }
}
void OscPanel::paint(juce::Graphics& g) {
    paintPanelBg(g, *this);
    paintHeaderTitle(g, titleArea, title, accentColour(accent));
    // UNI caption stacked over the unison stepper in the knob row — mirrors the
    // web's .osc-knobs .st-label (label row above the compact ◂ N ▸ strip).
    g.setColour(col::textDim);
    g.setFont(monoFont(8.0f));
    drawSpaced(g, "UNI", uniLabelArea, 1.2f, juce::Justification::centred);
}
void OscPanel::resized() {
    auto r = getLocalBounds().reduced(11, 9);
    auto head = r.removeFromTop(20);
    power.setBounds(head.removeFromLeft(17).withSizeKeepingCentre(15, 15));
    head.removeFromLeft(4);
    auto right = head.removeFromRight(138);
    tableStep.setBounds(right.removeFromRight(100).withSizeKeepingCentre(100, 18));
    right.removeFromRight(4);
    editBtn.setBounds(right.removeFromRight(18).withSizeKeepingCentre(18, 18));
    titleArea = head;
    r.removeFromTop(8);
    auto knobRow = r.removeFromBottom(62);
    // Wide enough for the 7px handle inset + 9px track + 3px gap + up to 6
    // stacked 6px depth bands (7+9+3+36=55), so every routed source's side
    // band paints in-bounds and stays grabbable / right-click-clearable.
    auto posCol = r.removeFromRight(56);
    r.removeFromRight(8);
    wt.setBounds(r);
    pos.setBounds(posCol);
    // 9 cells mirroring the web knob row: OCT SEMI FINE UNI DETUNE SPREAD BLEND
    // LEVEL PAN — the unison stepper takes the cell between FINE and DETUNE.
    auto row = ptrs(knobs);
    row.insert(3, &unisonStep);
    layoutKnobRow(knobRow, row);
    // The stepper is an 18px ◂ N ▸ strip, not a knob: stack the UNI caption
    // (painted in paint()) over a compact strip centred in its cell.
    auto cell = unisonStep.getBounds();
    auto stack = cell.withSizeKeepingCentre(cell.getWidth(), 32);
    uniLabelArea = stack.removeFromTop(12);
    stack.removeFromTop(2);
    unisonStep.setBounds(stack.withSizeKeepingCentre(juce::jmin(56, stack.getWidth()), 18));
}

// ===================== UtilPanel =====================
UtilPanel::UtilPanel(ParameterSource s)
    : subPow(s, "sub.on", Accent::N), noisePow(s, "noise.on", Accent::N),
      subShape(s, "sub.shape", Accent::N), noiseType(s, "noise.type", Accent::N),
      subOct(s, "sub.oct", Knob::Sm, Accent::N),
      // Continuous level knobs are mod targets: SUB LVL → 24, NOISE LVL → 25.
      // sub.oct is a discrete octave knob → not modulatable (modDest 0).
      subLevel(s, "sub.level", Knob::Sm, Accent::N, true, 24),
      noiseLevel(s, "noise.level", Knob::Sm, Accent::N, true, 25) {
    addAndMakeVisible(subPow); addAndMakeVisible(noisePow);
    addAndMakeVisible(subShape); addAndMakeVisible(noiseType);
    addAndMakeVisible(subOct); addAndMakeVisible(subLevel); addAndMakeVisible(noiseLevel);
}
void UtilPanel::paint(juce::Graphics& g) {
    paintPanelBg(g, *this);
    paintHeaderTitle(g, subHead, "SUB", col::text);
    paintHeaderTitle(g, noiseHead, "NOISE", col::text);
}
void UtilPanel::resized() {
    auto r = getLocalBounds().reduced(11, 9);
    auto sub = r.removeFromTop(r.getHeight() / 2);
    {
        auto head = sub.removeFromTop(20);
        subPow.setBounds(head.removeFromLeft(17).withSizeKeepingCentre(15, 15));
        head.removeFromLeft(4);
        subShape.setBounds(head.removeFromRight(86).withSizeKeepingCentre(86, 18));
        subHead = head;
        sub.removeFromTop(6);
        auto row = sub.removeFromTop(60);
        subOct.setBounds(row.removeFromLeft(row.getWidth() / 2).reduced(4, 0));
        subLevel.setBounds(row.reduced(4, 0));
    }
    {
        auto head = r.removeFromTop(20);
        noisePow.setBounds(head.removeFromLeft(17).withSizeKeepingCentre(15, 15));
        head.removeFromLeft(4);
        noiseType.setBounds(head.removeFromRight(86).withSizeKeepingCentre(86, 18));
        noiseHead = head;
        r.removeFromTop(6);
        noiseLevel.setBounds(r.removeFromTop(60).withSizeKeepingCentre(48, 60));
    }
}

// ===================== FilterPanel =====================
FilterPanel::Block::Block(ParameterSource s, juce::String prefix,
                          int cutoffDest, int resDest, int driveDest, int envDest, int keyDest)
    : power(s, prefix + ".on", Accent::F), type(s, prefix + ".type", Accent::F) {
    const char* ids[] = {".cutoff", ".res", ".drive", ".env", ".key"};
    // Every continuous knob is a mod target (per filter): CUTOFF (0) → 3/9,
    // RES (1) → 17/10, DRIVE (2) → 18/21, ENV (3) → 19/22, KEY (4) → 20/23.
    const int dests[] = {cutoffDest, resDest, driveDest, envDest, keyDest};
    for (int i = 0; i < 5; ++i)
        knobs.add(new Knob(s, prefix + ids[i], i == 0 ? Knob::Md : Knob::Sm, Accent::F, true, dests[i]));
}
void FilterPanel::Block::layout(juce::Rectangle<int> r) {
    layoutKnobRow(r, ptrs(knobs));
}

void FilterPanel::Block::setVisible(bool visible) {
    power.setVisible(visible);
    type.setVisible(visible);
    for (auto* knob : knobs) knob->setVisible(visible);
}

FilterPanel::TabButton::TabButton(ParameterSource s, juce::String id,
                                  juce::String text)
    : juce::Button(text), parameters(s), onId(std::move(id)),
      label(std::move(text)) {
    setClickingTogglesState(false);
    setWantsKeyboardFocus(true);
    setTitle(label + " filter controls tab");
    setDescription("Shows the " + label + " controls. Use left and right arrow keys to switch filters.");
    setTooltip("Show " + label + " controls");
    if (auto* p = parameters.parameter(onId)) lastOn = p->getValue() >= 0.5f;
    startTimerHz(8);
}

void FilterPanel::TabButton::paintButton(juce::Graphics& g, bool highlighted, bool down) {
    auto r = getLocalBounds().toFloat();
    const bool selected = getToggleState();
    g.setColour(selected ? col::acF.withAlpha(0.13f) : juce::Colour(0xff0a0d13));
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(selected ? col::acF.withAlpha(0.7f)
                         : (highlighted || down ? col::textDim : col::line));
    g.drawRoundedRectangle(r.reduced(0.5f), 3.0f, 1.0f);

    const float ledX = 10.0f;
    const float ledY = r.getCentreY();
    if (lastOn) {
        g.setColour(col::acF.withAlpha(0.25f));
        g.fillEllipse(ledX - 4.0f, ledY - 4.0f, 8.0f, 8.0f);
        g.setColour(col::acF);
    } else {
        g.setColour(juce::Colour(0xff303645));
    }
    g.fillEllipse(ledX - 2.5f, ledY - 2.5f, 5.0f, 5.0f);

    g.setColour(selected ? col::acF : col::textDim);
    g.setFont(monoFont(9.0f, true));
    drawSpaced(g, label, getLocalBounds().withTrimmedLeft(17), 1.1f,
               juce::Justification::centred);
}

bool FilterPanel::TabButton::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey) {
        if (onNavigate) onNavigate(key == juce::KeyPress::leftKey ? -1 : 1);
        return true;
    }
    return juce::Button::keyPressed(key);
}

void FilterPanel::TabButton::timerCallback() {
    auto* p = parameters.parameter(onId);
    const bool nowOn = p != nullptr && p->getValue() >= 0.5f;
    if (nowOn != lastOn) {
        lastOn = nowOn;
        repaint();
    }
}

FilterPanel::FilterPanel(ParameterSource s)
    : route(s, "filter.route", Accent::F), view(s, col::acF),
      // dst indices per §9: F1 cut/res/drive/env/key = 3/17/18/19/20;
      //                      F2 cut/res/drive/env/key = 9/10/21/22/23.
      f1(s, "filter", 3, 17, 18, 19, 20),
      f2(s, "filter2", 9, 10, 21, 22, 23),
      tab1(s, "filter.on", "F1"), tab2(s, "filter2.on", "F2") {
    addAndMakeVisible(route); addAndMakeVisible(view);
    addAndMakeVisible(tab1); addAndMakeVisible(tab2);
    for (auto* b : { &f1, &f2 }) {
        addAndMakeVisible(b->power); addAndMakeVisible(b->type);
        for (auto* k : b->knobs) addAndMakeVisible(k);
    }
    tab1.onClick = [this] { setActiveFilter(0); };
    tab2.onClick = [this] { setActiveFilter(1); };
    tab1.onNavigate = tab2.onNavigate = [this](int) {
        setActiveFilter(1 - activeFilter, true);
    };
    setActiveFilter(0);
}

void FilterPanel::setActiveFilter(int index, bool moveKeyboardFocus) {
    activeFilter = juce::jlimit(0, 1, index);
    tab1.setToggleState(activeFilter == 0, juce::dontSendNotification);
    tab2.setToggleState(activeFilter == 1, juce::dontSendNotification);
    tab1.setTitle("F1 filter controls tab, " + juce::String(activeFilter == 0 ? "selected" : "not selected"));
    tab2.setTitle("F2 filter controls tab, " + juce::String(activeFilter == 1 ? "selected" : "not selected"));
    f1.setVisible(activeFilter == 0);
    f2.setVisible(activeFilter == 1);
    resized();
    repaint();
    if (moveKeyboardFocus) (activeFilter == 0 ? tab1 : tab2).grabKeyboardFocus();
}
void FilterPanel::paint(juce::Graphics& g) {
    paintPanelBg(g, *this);
    paintHeaderTitle(g, titleArea, "FILTERS", col::acF);
}
void FilterPanel::resized() {
    auto r = getLocalBounds().reduced(11, 9);
    auto head = r.removeFromTop(20);
    route.setBounds(head.removeFromRight(104).withSizeKeepingCentre(104, 18));
    head.removeFromRight(8);
    titleArea = head.removeFromLeft(92);
    head.removeFromLeft(5);
    auto tabs = head.removeFromLeft(84).withSizeKeepingCentre(84, 20);
    tab1.setBounds(tabs.removeFromLeft(41));
    tabs.removeFromLeft(2);
    tab2.setBounds(tabs);
    head.removeFromLeft(8);
    auto& active = activeFilter == 0 ? f1 : f2;
    active.power.setBounds(head.removeFromLeft(17).withSizeKeepingCentre(13, 13));
    head.removeFromLeft(4);
    active.type.setBounds(head.removeFromLeft(86).withSizeKeepingCentre(86, 18));
    r.removeFromTop(6);
    view.setBounds(r.removeFromTop(62));
    r.removeFromTop(8);
    active.layout(r.removeFromTop(60));
}

// ===================== EnvPanel =====================
EnvPanel::EnvPanel(ParameterSource s, juce::String base, juce::String t, juce::Colour viewAccent, Accent knobAccent, int modSrc)
    : title(t), view(s, base, viewAccent) {
    addAndMakeVisible(view);
    const char* ids[] = {".a", ".d", ".s", ".r"};
    for (int i = 0; i < 4; ++i) knobs.add(new Knob(s, base + ids[i], Knob::Sm, knobAccent));
    for (auto* k : knobs) addAndMakeVisible(k);
    if (modSrc > 0) {
        srcChip = std::make_unique<ModSourceChip>(modSrc, juce::String(fable::MOD_SOURCES[(size_t)modSrc]), true);
        addAndMakeVisible(*srcChip);
    }
}
void EnvPanel::paint(juce::Graphics& g) {
    paintPanelBg(g, *this);
    paintHeaderTitle(g, titleArea, title, col::text);
}
void EnvPanel::resized() {
    auto r = getLocalBounds().reduced(11, 9);
    titleArea = r.removeFromTop(20);
    if (srcChip) srcChip->setBounds(titleArea.removeFromRight(22).withSizeKeepingCentre(20, 16));
    r.removeFromTop(6);
    view.setBounds(r.removeFromTop(62));
    r.removeFromTop(8);
    layoutKnobRow(r.removeFromTop(60), ptrs(knobs));
}

// ===================== LfoPanel =====================
LfoPanel::Block::Block(ParameterSource s, juce::String id, juce::String t, Accent ac,
                       std::function<HostTransport()> transportProvider, int modSrc)
    : title(t),
      srcChip(modSrc, juce::String(fable::MOD_SOURCES[(size_t)modSrc]), true),
      shape(s, id + ".shape", Accent::N),
      view(s, id + ".shape", id + ".rate", id + ".sync", id + ".syncrate", accentColour(ac), std::move(transportProvider)),
      syncRate(s, id + ".syncrate", ac),
      rate(s, id + ".rate", Knob::Sm, ac),
      rise(s, id + ".rise", Knob::Sm, ac),
      phase(s, id + ".phase", Knob::Sm, ac) {
    syncBtn.setClickingTogglesState(true);
    retrigBtn.setClickingTogglesState(true);
    for (auto* b : { &syncBtn, &retrigBtn }) {
        b->setColour(juce::TextButton::buttonOnColourId, accentColour(ac));
        b->setColour(juce::TextButton::textColourOnId, col::bg);
    }
    syncAtt   = attachButton(s, id + ".sync", syncBtn);
    retrigAtt = attachButton(s, id + ".retrig", retrigBtn);
}

void LfoPanel::Block::applySync(bool sync) {
    lastSync = sync;
    rate.setVisible(!sync);
    syncRate.setVisible(sync);
}

void LfoPanel::Block::layout(juce::Rectangle<int> r) {
    auto head = r.removeFromTop(20);
    shape.setBounds(head.removeFromRight(78).withSizeKeepingCentre(78, 18));
    head.removeFromRight(3);
    retrigBtn.setBounds(head.removeFromRight(30).withSizeKeepingCentre(30, 18));
    head.removeFromRight(2);
    syncBtn.setBounds(head.removeFromRight(30).withSizeKeepingCentre(30, 18));
    head.removeFromRight(3);
    srcChip.setBounds(head.removeFromLeft(20).withSizeKeepingCentre(20, 16));
    head.removeFromLeft(4);
    titleArea = head;
    r.removeFromTop(6);
    view.setBounds(r.removeFromTop(62));
    r.removeFromTop(8);
    auto row = r.removeFromTop(60);
    int w = row.getWidth() / 3;
    slot0 = row.removeFromLeft(w);
    rate.setBounds(slot0);
    syncRate.setBounds(slot0.withSizeKeepingCentre(w - 4, 18));
    rise.setBounds(row.removeFromLeft(w));
    phase.setBounds(row);
    applySync(lastSync);
}

void LfoPanel::Block::paintTitle(juce::Graphics& g) { paintHeaderTitle(g, titleArea, title, col::text); }

LfoPanel::LfoPanel(ParameterSource s, std::function<HostTransport()> transportProvider)
    : parameters(s),
      l1(s, "lfo1", "LFO 1", Accent::A, transportProvider, 1),
      l2(s, "lfo2", "LFO 2", Accent::B, transportProvider, 2) {
    for (auto* b : { &l1, &l2 }) {
        addAndMakeVisible(b->srcChip);
        addAndMakeVisible(b->shape);
        addAndMakeVisible(b->view);
        addAndMakeVisible(b->syncBtn);
        addAndMakeVisible(b->retrigBtn);
        addAndMakeVisible(b->syncRate);
        addAndMakeVisible(b->rate);
        addAndMakeVisible(b->rise);
        addAndMakeVisible(b->phase);
    }
    // Initialise swap state from current param values.
    l1.applySync(s.parameter("lfo1.sync")->getValue() != 0.0f);
    l2.applySync(s.parameter("lfo2.sync")->getValue() != 0.0f);
    startTimerHz(15);
}

void LfoPanel::timerCallback() {
    // Poll sync params and re-layout on change.
    bool s1 = parameters.parameter("lfo1.sync")->getValue() != 0.0f;
    bool s2 = parameters.parameter("lfo2.sync")->getValue() != 0.0f;
    if (s1 != l1.lastSync) l1.applySync(s1);
    if (s2 != l2.lastSync) l2.applySync(s2);
}

void LfoPanel::paint(juce::Graphics& g) {
    paintPanelBg(g, *this);
    l1.paintTitle(g); l2.paintTitle(g);
}

void LfoPanel::resized() {
    auto r = getLocalBounds().reduced(11, 9);
    auto left = r.removeFromLeft(r.getWidth() / 2);
    left.removeFromRight(7);
    l1.layout(left);
    r.removeFromLeft(7);
    l2.layout(r);
}

// ===================== MatrixPanel =====================
MatrixPanel::Row::Row(ParameterSource s, int sl) : slot(sl) {
    juce::String base = "mat" + juce::String(slot);
    for (size_t i = 0; i < fable::MOD_SOURCES.size(); ++i) src.addItem(fable::MOD_SOURCES[i], static_cast<int>(i) + 1);
    for (size_t i = 0; i < fable::MOD_DESTS.size(); ++i) dst.addItem(fable::MOD_DESTS[i], static_cast<int>(i) + 1);
    srcAtt = attachCombo(s, base + ".src", src);
    dstAtt = attachCombo(s, base + ".dst", dst);
    amt = std::make_unique<Knob>(s, base + ".amt", Knob::Xs, Accent::N, false);
    remove.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
    remove.setColour(juce::TextButton::textColourOffId, col::textDim);
    remove.setTooltip("remove route");
    remove.onClick = [s, sl] { fui::clearSlot(s, sl); };
    addAndMakeVisible(src); addAndMakeVisible(dst); addAndMakeVisible(*amt); addAndMakeVisible(remove);
}
void MatrixPanel::Row::resized() {
    auto rr = getLocalBounds();
    int rowH = rr.getHeight();
    amt->setBounds(rr.removeFromLeft(22).withSizeKeepingCentre(22, rowH));
    rr.removeFromLeft(4);
    remove.setBounds(rr.removeFromRight(18).withSizeKeepingCentre(18, juce::jmin(18, rowH)));
    rr.removeFromRight(4);
    int half = (rr.getWidth() - 14) / 2;
    src.setBounds(rr.removeFromLeft(half).withSizeKeepingCentre(half, juce::jmin(22, rowH)));
    rr.removeFromLeft(14); // arrow gutter
    dst.setBounds(rr.withSizeKeepingCentre(rr.getWidth(), juce::jmin(22, rowH)));
}
void MatrixPanel::Row::paint(juce::Graphics& g) {
    g.setColour(col::textDim);
    g.setFont(monoFont(11.0f));
    auto sb = src.getBounds();
    g.drawText(juce::String::fromUTF8("\xe2\x96\xb8"), // ▸
               sb.getRight(), sb.getY(), 14, sb.getHeight(), juce::Justification::centred);
}

MatrixPanel::MatrixPanel(ParameterSource s) : parameters(s) {
    // All 16 rows constructed up front so the combobox attachments stay stable;
    // visibility is toggled per slot (rowVisible) by the diffing timer.
    for (int i = 1; i <= fable::MOD_MATRIX_SIZE; ++i) {
        auto* row = new Row(s, i);
        rows.add(row);
        rowsHolder.addChildComponent(row); // hidden until rowVisible
    }
    // Scrollable row list: rows can exceed the panel height once many slots fill.
    viewport.setViewedComponent(&rowsHolder, false);
    viewport.setScrollBarsShown(true, false); // vertical only
    addAndMakeVisible(viewport);
    // Header source chips: LFO1=1, LFO2=2, MOD ENV=3, VELO=4, NOTE=5.
    for (int srcIdx = 1; srcIdx <= 5; ++srcIdx) {
        auto* chip = new ModSourceChip(srcIdx, juce::String(fable::MOD_SOURCES[(size_t)srcIdx]), false);
        chips.add(chip);
        addAndMakeVisible(chip);
    }
    addBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
    addBtn.setColour(juce::TextButton::textColourOffId, col::text);
    addBtn.onClick = [this] { fui::addRoute(parameters, 1, 0); }; // src=LFO 1, dst=— (inactive)
    addAndMakeVisible(addBtn);
    lastVisible = visibleSlots();
    startTimerHz(3); // low rate: diff visible slots before rebuilding to avoid flicker
}

std::vector<int> MatrixPanel::visibleSlots() const {
    std::vector<int> out;
    for (int slot = 1; slot <= fable::MOD_MATRIX_SIZE; ++slot) {
        auto rv = [&](const char* f) {
            auto* p = dynamic_cast<juce::RangedAudioParameter*>(
                parameters.parameter("mat" + juce::String(slot) + f));
            return p ? (int)p->convertFrom0to1(p->getValue()) : 0;
        };
        if (rv(".src") != 0 || rv(".dst") != 0) out.push_back(slot); // rowVisible (§9)
    }
    return out;
}

void MatrixPanel::timerCallback() {
    addBtn.setEnabled(fui::findFreeSlot(parameters) > 0); // grey out ADD ROUTE when the 16-slot pool is full
    auto cur = visibleSlots();
    if (cur != lastVisible) {
        lastVisible = cur;
        relayoutRows();
        repaint();
    }
}

void MatrixPanel::relayoutRows() {
    // Fixed-height rows stacked in the holder; the viewport scrolls vertically
    // once their total height exceeds the visible area.
    for (auto* row : rows) row->setVisible(false);
    const int rowH = 24, gap = 2;
    int n = (int)lastVisible.size();
    int vpW = viewport.getWidth(), vpH = viewport.getHeight();
    int neededH = n > 0 ? n * rowH + (n - 1) * gap : 0;
    bool vscroll = neededH > vpH;
    int holderW = vpW - (vscroll ? viewport.getScrollBarThickness() : 0);
    rowsHolder.setSize(holderW, juce::jmax(neededH, vpH));
    for (int i = 0; i < n; ++i) {
        auto* row = rows[lastVisible[(size_t)i] - 1];
        row->setBounds(0, i * (rowH + gap), holderW, rowH);
        row->setVisible(true);
    }
}

void MatrixPanel::paint(juce::Graphics& g) {
    paintPanelBg(g, *this);
    paintHeaderTitle(g, titleArea, "MOD MATRIX", col::text);
    g.setColour(col::textDim);
    g.setFont(monoFont(7.5f));
    g.drawFittedText("Drag to a control, or add a route.", hintArea,
                     juce::Justification::centredLeft, 2);
    // (each Row paints its own ▸ arrow between src and dst, so it scrolls correctly)
    if (lastVisible.empty()) {
        g.setColour(col::textDim);
        g.setFont(monoFont(10.0f));
        g.drawText("Drag a source onto a control, or ADD ROUTE.",
                   rowsArea, juce::Justification::centredTop);
    }
}

void MatrixPanel::resized() {
    auto r = getLocalBounds().reduced(11, 9);
    auto rail = r.removeFromLeft(172);
    r.removeFromLeft(9);

    titleArea = rail.removeFromTop(18);
    rail.removeFromTop(3);
    chipArea = rail.removeFromTop(38);
    {
        auto top = chipArea.removeFromTop(18);
        auto bottom = chipArea.removeFromBottom(18);
        const int gap = 3;
        const int topW = (top.getWidth() - gap * 2) / 3;
        for (int i = 0; i < 3; ++i) {
            chips[i]->setBounds(top.removeFromLeft(topW).withSizeKeepingCentre(topW, 16));
            if (i < 2) top.removeFromLeft(gap);
        }
        const int bottomW = (bottom.getWidth() - gap) / 2;
        chips[3]->setBounds(bottom.removeFromLeft(bottomW).withSizeKeepingCentre(bottomW, 16));
        bottom.removeFromLeft(gap);
        chips[4]->setBounds(bottom.withSizeKeepingCentre(bottomW, 16));
    }
    rail.removeFromTop(2);
    hintArea = rail.removeFromTop(15);
    addArea = rail.removeFromBottom(22);
    addBtn.setBounds(addArea);

    rowsArea = r.withSizeKeepingCentre(r.getWidth(), juce::jmin(102, r.getHeight()));
    viewport.setBounds(rowsArea);
    relayoutRows();
}

// ===================== FxPanel =====================
FxPanel::Module::Module(ParameterSource s, juce::String fx, juce::String t, juce::StringArray knobIds)
    : title(t), power(s, "fx." + fx + ".on", Accent::N) {
    for (auto& k : knobIds) knobs.add(new Knob(s, "fx." + fx + "." + k, Knob::Sm, Accent::N));
}
void FxPanel::Module::layout(juce::Rectangle<int> r) {
    bounds = r;
    auto inner = r.reduced(8, 7);
    auto head = inner.removeFromTop(18);
    power.setBounds(head.removeFromLeft(17).withSizeKeepingCentre(13, 13));
    head.removeFromLeft(4);
    titleArea = head;
    inner.removeFromTop(4);
    layoutKnobRow(inner.removeFromTop(60), ptrs(knobs));
}
void FxPanel::Module::paintModule(juce::Graphics& g) {
    g.setColour(juce::Colours::black.withAlpha(0.18f));
    g.fillRoundedRectangle(bounds.toFloat(), 9.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 9.0f, 1.0f);
    paintHeaderTitle(g, titleArea, title, col::text);
}
FxPanel::FxPanel(ParameterSource s) {
    struct Def { const char* fx; const char* title; juce::StringArray k; };
    std::vector<Def> defs = {
        {"drive", "DRIVE", {"amt", "mix"}},
        {"chorus", "CHORUS", {"rate", "depth", "mix"}},
        {"delay", "DELAY", {"time", "fb", "mix"}},
        {"reverb", "REVERB", {"size", "mix"}},
    };
    for (auto& d : defs) {
        auto* m = new Module(s, d.fx, d.title, d.k);
        modules.add(m);
        addAndMakeVisible(m->power);
        for (auto* k : m->knobs) addAndMakeVisible(k);
    }
}
void FxPanel::paint(juce::Graphics& g) {
    paintPanelBg(g, *this);
    for (auto* m : modules) m->paintModule(g);
}
void FxPanel::resized() {
    auto r = getLocalBounds().reduced(11, 9);
    int n = modules.size();
    int gap = 10;
    float cw = static_cast<float>(r.getWidth() - gap * (n - 1)) / static_cast<float>(n);
    for (int i = 0; i < n; ++i) {
        auto cell = juce::Rectangle<int>((int)std::round(static_cast<float>(r.getX())
                                         + static_cast<float>(i) * (cw + static_cast<float>(gap))), r.getY(),
                                         (int)std::round(cw), r.getHeight());
        modules[i]->layout(cell);
    }
}

} // namespace fui
