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
    float cw = area.getWidth() / (float)n;
    for (int i = 0; i < n; ++i)
        ks[i]->setBounds(juce::Rectangle<int>((int)std::round(area.getX() + i * cw), area.getY(),
                                              (int)std::round(cw), area.getHeight()));
}
static juce::Array<juce::Component*> ptrs(juce::OwnedArray<Knob>& a) {
    juce::Array<juce::Component*> v; for (auto* k : a) v.add(k); return v;
}

// ===================== OscPanel =====================
OscPanel::OscPanel(APVTS& s, FableAudioProcessor& proc, int osc, juce::String pre, Accent ac, juce::String t)
    : oscIndex(osc), title(t), prefix(pre), accent(ac),
      power(s, pre + ".on", ac), tableStep(s, pre + ".table", ac),
      wt(proc, osc, accentColour(ac)),
      pos(s, pre + ".pos", ac, [&proc, osc] { return proc.getVizPos(osc); }) {
    addAndMakeVisible(power); addAndMakeVisible(tableStep);
    addAndMakeVisible(wt); addAndMakeVisible(pos);
    // The table stepper cycles only over the procedural + live user tables and
    // shows each table's live name (the param itself reserves fixed USER slots).
    tableStep.countProvider = [&proc] { return proc.numTables(); };
    tableStep.nameProvider  = [&proc](int idx) { return proc.tableName(idx); };
    // ✎ edit button — opens the import / draw editor for this oscillator.
    editBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
    editBtn.setColour(juce::TextButton::textColourOffId, accentColour(ac));
    editBtn.setTooltip("import / draw wavetable");
    editBtn.onClick = [this] { if (onEditTable) onEditTable(oscIndex); };
    addAndMakeVisible(editBtn);
    const char* ids[] = {".oct", ".semi", ".fine", ".unison", ".detune", ".spread", ".level", ".pan"};
    for (int i = 0; i < 8; ++i) {
        auto* k = new Knob(s, pre + ids[i], i == 6 ? Knob::Md : Knob::Sm, ac);
        knobs.add(k); addAndMakeVisible(k);
    }
}
void OscPanel::paint(juce::Graphics& g) {
    paintPanelBg(g, *this);
    paintHeaderTitle(g, titleArea, title, accentColour(accent));
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
    auto posCol = r.removeFromRight(34);
    r.removeFromRight(8);
    wt.setBounds(r);
    pos.setBounds(posCol);
    layoutKnobRow(knobRow, ptrs(knobs));
}

// ===================== UtilPanel =====================
UtilPanel::UtilPanel(APVTS& s)
    : subPow(s, "sub.on", Accent::N), noisePow(s, "noise.on", Accent::N),
      subShape(s, "sub.shape", Accent::N), noiseType(s, "noise.type", Accent::N),
      subOct(s, "sub.oct", Knob::Sm, Accent::N), subLevel(s, "sub.level", Knob::Sm, Accent::N),
      noiseLevel(s, "noise.level", Knob::Sm, Accent::N) {
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
FilterPanel::Block::Block(APVTS& s, juce::String prefix, juce::String lbl)
    : label(lbl), power(s, prefix + ".on", Accent::F), type(s, prefix + ".type", Accent::F) {
    const char* ids[] = {".cutoff", ".res", ".drive", ".env", ".key"};
    for (int i = 0; i < 5; ++i)
        knobs.add(new Knob(s, prefix + ids[i], i == 0 ? Knob::Md : Knob::Sm, Accent::F));
}
void FilterPanel::Block::layout(juce::Rectangle<int> r) {
    auto head = r.removeFromTop(18);
    power.setBounds(head.removeFromLeft(17).withSizeKeepingCentre(13, 13));
    head.removeFromLeft(4);
    type.setBounds(head.removeFromRight(86).withSizeKeepingCentre(86, 18));
    labelArea = head;
    r.removeFromTop(4);
    layoutKnobRow(r, ptrs(knobs));
}
void FilterPanel::Block::paintLabel(juce::Graphics& g) {
    g.setColour(col::acF);
    g.setFont(monoFont(10.0f));
    drawSpaced(g, label, labelArea, 1.2f, juce::Justification::centredLeft);
}

FilterPanel::FilterPanel(APVTS& s)
    : route(s, "filter.route", Accent::F), view(s, col::acF),
      f1(s, "filter", "F1"), f2(s, "filter2", "F2") {
    addAndMakeVisible(route); addAndMakeVisible(view);
    for (auto* b : { &f1, &f2 }) {
        addAndMakeVisible(b->power); addAndMakeVisible(b->type);
        for (auto* k : b->knobs) addAndMakeVisible(k);
    }
}
void FilterPanel::paint(juce::Graphics& g) {
    paintPanelBg(g, *this);
    paintHeaderTitle(g, titleArea, "FILTERS", col::acF);
    f1.paintLabel(g); f2.paintLabel(g);
}
void FilterPanel::resized() {
    auto r = getLocalBounds().reduced(11, 9);
    auto head = r.removeFromTop(20);
    route.setBounds(head.removeFromRight(104).withSizeKeepingCentre(104, 18));
    titleArea = head;
    r.removeFromTop(6);
    view.setBounds(r.removeFromTop(62));
    r.removeFromTop(6);
    auto h = r.getHeight();
    f1.layout(r.removeFromTop(h / 2));
    r.removeFromTop(4);
    f2.layout(r);
}

// ===================== EnvPanel =====================
EnvPanel::EnvPanel(APVTS& s, juce::String base, juce::String t, juce::Colour viewAccent, Accent knobAccent)
    : title(t), view(s, base, viewAccent) {
    addAndMakeVisible(view);
    const char* ids[] = {".a", ".d", ".s", ".r"};
    for (int i = 0; i < 4; ++i) knobs.add(new Knob(s, base + ids[i], Knob::Sm, knobAccent));
    for (auto* k : knobs) addAndMakeVisible(k);
}
void EnvPanel::paint(juce::Graphics& g) {
    paintPanelBg(g, *this);
    paintHeaderTitle(g, titleArea, title, col::text);
}
void EnvPanel::resized() {
    auto r = getLocalBounds().reduced(11, 9);
    titleArea = r.removeFromTop(20);
    r.removeFromTop(6);
    view.setBounds(r.removeFromTop(62));
    r.removeFromTop(8);
    // ATK DEC SUS on top, REL centred below (matches the web wrap)
    auto top = r.removeFromTop(60);
    layoutKnobRow(top, { knobs[0], knobs[1], knobs[2] });
    r.removeFromTop(2);
    knobs[3]->setBounds(r.removeFromTop(60).withSizeKeepingCentre(48, 60));
}

// ===================== LfoPanel =====================
LfoPanel::Block::Block(APVTS& s, juce::String id, juce::String t, Accent ac)
    : title(t), shape(s, id + ".shape", Accent::N), view(s, id + ".shape", id + ".rate", accentColour(ac)),
      rate(s, id + ".rate", Knob::Md, ac) {}
void LfoPanel::Block::layout(juce::Rectangle<int> r) {
    auto head = r.removeFromTop(20);
    shape.setBounds(head.removeFromRight(90).withSizeKeepingCentre(90, 18));
    titleArea = head;
    r.removeFromTop(6);
    view.setBounds(r.removeFromTop(46));
    r.removeFromTop(6);
    rate.setBounds(r.removeFromTop(64).withSizeKeepingCentre(60, 64));
}
void LfoPanel::Block::paintTitle(juce::Graphics& g) { paintHeaderTitle(g, titleArea, title, col::text); }

LfoPanel::LfoPanel(APVTS& s) : l1(s, "lfo1", "LFO 1", Accent::A), l2(s, "lfo2", "LFO 2", Accent::B) {
    for (auto* b : { &l1, &l2 }) { addAndMakeVisible(b->shape); addAndMakeVisible(b->view); addAndMakeVisible(b->rate); }
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
MatrixPanel::Row::Row(APVTS& s, int slot) {
    juce::String base = "mat" + juce::String(slot);
    for (int i = 0; i < (int)fable::MOD_SOURCES.size(); ++i) src.addItem(fable::MOD_SOURCES[i], i + 1);
    for (int i = 0; i < (int)fable::MOD_DESTS.size(); ++i) dst.addItem(fable::MOD_DESTS[i], i + 1);
    srcAtt = std::make_unique<APVTS::ComboBoxAttachment>(s, base + ".src", src);
    dstAtt = std::make_unique<APVTS::ComboBoxAttachment>(s, base + ".dst", dst);
    amt = std::make_unique<Knob>(s, base + ".amt", Knob::Xs, Accent::N, false);
}
MatrixPanel::MatrixPanel(APVTS& s) {
    for (int i = 1; i <= 4; ++i) {
        auto* row = new Row(s, i);
        rows.add(row);
        addAndMakeVisible(row->src); addAndMakeVisible(row->dst); addAndMakeVisible(*row->amt);
    }
}
void MatrixPanel::paint(juce::Graphics& g) {
    paintPanelBg(g, *this);
    paintHeaderTitle(g, titleArea, "MOD MATRIX", col::text);
    g.setColour(col::textDim);
    g.setFont(monoFont(11.0f));
    for (auto* row : rows) {
        auto a = row->src.getBounds();
        g.drawText(">", row->src.getRight(), a.getY(), 14, a.getHeight(), juce::Justification::centred);
    }
}
void MatrixPanel::resized() {
    auto r = getLocalBounds().reduced(11, 9);
    titleArea = r.removeFromTop(20);
    r.removeFromTop(6);
    int n = rows.size();
    int rowH = juce::jmin(28, (r.getHeight() - (n - 1) * 7) / n);
    for (int i = 0; i < n; ++i) {
        auto* row = rows[i];
        auto rr = r.removeFromTop(rowH);
        if (i < n - 1) r.removeFromTop(7);
        auto amtA = rr.removeFromRight(34);
        row->amt->setBounds(amtA.withSizeKeepingCentre(34, rowH));
        rr.removeFromRight(6);
        int half = (rr.getWidth() - 14) / 2;
        row->src.setBounds(rr.removeFromLeft(half).withSizeKeepingCentre(half, 26));
        rr.removeFromLeft(14);
        row->dst.setBounds(rr.withSizeKeepingCentre(rr.getWidth(), 26));
    }
}

// ===================== FxPanel =====================
FxPanel::Module::Module(APVTS& s, juce::String fx, juce::String t, juce::StringArray knobIds)
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
FxPanel::FxPanel(APVTS& s) {
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
    float cw = (r.getWidth() - gap * (n - 1)) / (float)n;
    for (int i = 0; i < n; ++i) {
        auto cell = juce::Rectangle<int>((int)std::round(r.getX() + i * (cw + gap)), r.getY(),
                                         (int)std::round(cw), r.getHeight());
        modules[i]->layout(cell);
    }
}

// ===================== TopBar =====================
TopBar::TopBar(APVTS& s, FableAudioProcessor& p)
    : proc(p), scope(p, col::acA), spectrum(p, col::acB), master(s, "master.volume", Knob::Md, Accent::N) {
    addAndMakeVisible(scope); addAndMakeVisible(spectrum); addAndMakeVisible(master);
    for (int i = 0; i < proc.getNumPrograms(); ++i) presets.addItem(proc.getProgramName(i), i + 1);
    presets.setSelectedId(proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presets.onChange = [this] { proc.setCurrentProgram(presets.getSelectedId() - 1); };
    addAndMakeVisible(presets);
    prev.onClick = [this] {
        int n = proc.getNumPrograms(), i = (proc.getCurrentProgram() - 1 + n) % n;
        proc.setCurrentProgram(i); presets.setSelectedId(i + 1, juce::dontSendNotification);
    };
    next.onClick = [this] {
        int n = proc.getNumPrograms(), i = (proc.getCurrentProgram() + 1) % n;
        proc.setCurrentProgram(i); presets.setSelectedId(i + 1, juce::dontSendNotification);
    };
    addAndMakeVisible(prev); addAndMakeVisible(next); addAndMakeVisible(save);
    startTimerHz(15);
}
void TopBar::timerCallback() {
    if (proc.getVoiceCount() != lastVoices || proc.getMidiActive() != lastMidi) {
        lastVoices = proc.getVoiceCount(); lastMidi = proc.getMidiActive();
        repaint(statusArea);
    }
}
void TopBar::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());

    // brand: FABLE (white) SYNTH (cyan)  WT-1 (dim)
    g.setFont(dispFont(17.0f));
    int bx = brandArea.getX(), by = brandArea.getY();
    g.setColour(col::text);
    drawSpaced(g, "FABLE", { bx, by, 70, brandArea.getHeight() }, 1.5f);
    int fableW = (int)g.getCurrentFont().getStringWidthFloat("FABLE") + 5 * 5;
    g.setColour(col::acA);
    drawSpaced(g, "SYNTH", { bx + fableW, by, 80, brandArea.getHeight() }, 1.5f);
    g.setColour(col::textDim);
    g.setFont(monoFont(9.0f));
    drawSpaced(g, "WT-1", { bx + fableW + 88, by + 4, 50, brandArea.getHeight() }, 2.0f);

    // scope / spectrum boxes
    for (auto& bx2 : { std::make_pair(scopeBox, juce::String("SCOPE")),
                       std::make_pair(specBox, juce::String("SPECTRUM")) }) {
        drawDisplayBox(g, bx2.first.toFloat(), 8.0f);
        g.setColour(col::textDim);
        g.setFont(monoFont(7.0f));
        drawSpaced(g, bx2.second, bx2.first.reduced(6, 3).removeFromTop(10), 2.0f, juce::Justification::right);
    }

    // status: MIDI led + voices
    auto st = statusArea;
    auto midiRow = st.removeFromTop(st.getHeight() / 2);
    auto led = midiRow.removeFromLeft(14).withSizeKeepingCentre(7, 7).toFloat();
    g.setColour(proc.getMidiActive() ? col::acA : juce::Colour(0xff232936));
    g.fillEllipse(led);
    if (proc.getMidiActive()) { g.setColour(col::acA.withAlpha(0.5f)); g.fillEllipse(led.expanded(2)); }
    g.setColour(col::textDim); g.setFont(monoFont(9.0f));
    g.drawText("MIDI", midiRow, juce::Justification::centredLeft);
    g.setColour(col::acB);
    g.drawText(juce::String(proc.getVoiceCount()), st.removeFromLeft(14), juce::Justification::centred);
    g.setColour(col::textDim);
    g.drawText("VOICES", st, juce::Justification::centredLeft);
}
void TopBar::resized() {
    auto r = getLocalBounds().reduced(16, 10);
    brandArea = r.removeFromLeft(230).withSizeKeepingCentre(230, 22);

    auto pb = r.removeFromLeft(330);
    pb = pb.withSizeKeepingCentre(330, 28);
    prev.setBounds(pb.removeFromLeft(28));
    pb.removeFromLeft(6);
    save.setBounds(pb.removeFromRight(46));
    pb.removeFromRight(6);
    next.setBounds(pb.removeFromRight(28));
    pb.removeFromRight(6);
    presets.setBounds(pb);

    master.setBounds(r.removeFromRight(70));
    r.removeFromRight(10);
    statusArea = r.removeFromRight(96);
    r.removeFromRight(10);
    // scope + spectrum fill the middle-right
    specBox = r.removeFromRight(168).withSizeKeepingCentre(168, 46);
    r.removeFromRight(10);
    scopeBox = r.removeFromRight(168).withSizeKeepingCentre(168, 46);
    scope.setBounds(scopeBox.reduced(1));
    spectrum.setBounds(specBox.reduced(1));
}

} // namespace fui
