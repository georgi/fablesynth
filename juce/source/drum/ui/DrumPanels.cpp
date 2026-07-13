#include "DrumPanels.h"
#include "../../ui/Format.h"
#include "../../dsp/Wavetables.h"
#include <cmath>

// Web layout (src/drum/drum.css), rack-relative px:
//   .panel padding 8px 12px 10px; .panel-head 18px + 8px margin-bottom.
//   .dr-osc-body grid: rows 104px / auto, cols 1fr / 36px, gap 8px.
//   .dr-env-view / .dr-filter-view height 58px; knob rows fill the rest.
//   .dr-mod-row height 30px, gap 5px; .dr-mod-head min-height 34px.
namespace fui {

static float parameterValue(DrumUiModel& model, const juce::String& id) {
    auto* p = model.parameters().parameter(id);
    return p ? p->convertFrom0to1(p->getValue()) : 0.0f;
}

// ---- shared bits -------------------------------------------------------------

// .dr-led — 8px accent LED with halo (drum.css .dr-led-a/-b/-f).
static void drawDrLed(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ac) {
    g.setColour(ac.withAlpha(0.55f));
    g.fillEllipse(r.expanded(2.5f));
    g.setGradientFill(juce::ColourGradient(ac.brighter(0.8f), r.getX() + r.getWidth() * 0.38f,
                                           r.getY() + r.getHeight() * 0.32f,
                                           ac.darker(0.4f), r.getRight(), r.getBottom(), true));
    g.fillEllipse(r);
    g.setColour(ac.brighter(0.4f).withAlpha(0.8f));
    g.drawEllipse(r, 1.0f);
}

// .panel-head h2 (Michroma 10px, 0.22em tracking; accent when data-accent set).
static void drawHeadTitle(juce::Graphics& g, juce::Rectangle<int> area,
                          const juce::String& t, juce::Colour c) {
    g.setColour(c);
    g.setFont(dispFont(10.0f));
    drawSpaced(g, t, area, 2.2f);
}

// .st-value-style readout well (e.g. the NOISE panel's "WHITE").
static void drawValueWell(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& s) {
    g.setColour(juce::Colour(0xff0c0f16));
    g.fillRoundedRectangle(r.toFloat(), 5.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 5.0f, 1.0f);
    g.setColour(col::text);
    g.setFont(monoFont(9.0f));
    drawSpaced(g, s, r, 1.0f, juce::Justification::centred);
}

// Panel content box (.panel padding) and head row.
static juce::Rectangle<int> contentBox(const juce::Component& c) {
    auto r = c.getLocalBounds();
    r.removeFromLeft(12);  r.removeFromRight(12);
    r.removeFromTop(8);    r.removeFromBottom(10);
    return r;
}

static void layoutKnobRow(juce::Rectangle<int> area, const juce::OwnedArray<Knob>& knobs,
                          const int* sizesPx) {
    const int n = knobs.size();
    if (n == 0) return;
    const float cw = (float)area.getWidth() / (float)n;
    for (int i = 0; i < n; ++i) {
        juce::Rectangle<int> cell((int)std::round(area.getX() + i * cw), area.getY(),
                                  (int)std::round(cw), area.getHeight());
        const int kh = juce::jmin(area.getHeight(), sizesPx[i] + 13); // dia + label strip
        knobs[i]->setBounds(cell.withSizeKeepingCentre(cell.getWidth(), kh));
    }
}

// ===================== DrumTerrainView =====================
DrumTerrainView::DrumTerrainView(DrumUiModel& p, int pad, int oscIndex, juce::Colour acc)
    : proc(p), osc(oscIndex), accent(acc) {
    const auto pre = "pad" + juce::String(pad) + (osc == 0 ? ".oscA" : ".oscB");
    tableId = pre + ".table";
    posId   = pre + ".pos";
    startTimerHz(30); // animation cadence (matches the web rAF throttle)
}

int DrumTerrainView::tableIndex() const {
    return (int)parameterValue(proc, tableId);
}

float DrumTerrainView::knobPos() const {
    return parameterValue(proc, posId);
}

void DrumTerrainView::timerCallback() {
    // Repaint only when the shown frame or the table actually moves (same
    // throttle as WavetableView.cpp).
    const float mp = proc.vizPosition(osc);
    const float show = mp >= 0 ? mp : knobPos();
    const int idx = tableIndex();
    if (idx == lastTable && std::abs(show - lastShown) < 0.004f) return;
    lastShown = show;
    lastTable = idx;
    repaint();
}

void DrumTerrainView::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth(), h = bounds.getHeight();

    // display backdrop (.dr-wavetable-view)
    g.setColour(juce::Colour(0xff0d1016));
    g.fillRoundedRectangle(bounds, 9.0f);
    g.setColour(juce::Colour(0xff1c222c));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 9.0f, 1.0f);

    const int idx = tableIndex();
    const auto* tp = proc.tableAt(idx);
    if (!tp) return; // empty user slot
    const auto& t = *tp;
    const int frames = t.frames;
    if (frames < 1 || t.viz.empty()) return;
    const float* viz = t.viz.data();
    const int N = (int)t.viz.size() / frames;

    const float mp = proc.vizPosition(osc);
    const float show = mp >= 0 ? mp : knobPos();

    // perspective layout (mirrors the web canvas math / WavetableView.cpp)
    const float depthX = w * 0.22f, depthY = h * 0.42f;
    const float waveW = w * 0.68f, waveAmp = h * 0.17f;
    const float x0 = w * 0.06f, y0 = h * 0.78f;

    const auto buildPath = [&](int f) {
        const float d = frames > 1 ? (float)f / (frames - 1) : 0.0f; // 1-frame guard
        const float ox = x0 + d * depthX;
        const float oy = y0 - d * depthY;
        juce::Path path;
        for (int i = 0; i < N; ++i) {
            const float x = ox + (i / (float)(N - 1)) * waveW;
            const float y = oy - viz[f * N + i] * waveAmp;
            if (i == 0) path.startNewSubPath(x, y);
            else        path.lineTo(x, y);
        }
        return path;
    };

    const int cw = getWidth(), ch = getHeight();
    const int gen = proc.tablesGeneration();
    const bool cacheValid = cacheTable == idx && cacheGen == gen && cacheW == cw && cacheH == ch;
    if (!cacheValid) {
        farCache = (cw > 0 && ch > 0) ? juce::Image(juce::Image::ARGB, cw * 2, ch * 2, true)
                                      : juce::Image();
        if (farCache.isValid()) {
            juce::Graphics cg(farCache);
            cg.addTransform(juce::AffineTransform::scale(2.0f));
            for (int f = frames - 1; f >= 0; --f) {
                const float d = frames > 1 ? (float)f / (frames - 1) : 0.0f;
                cg.setColour(juce::Colour(0xff8893a8).withAlpha(0.16f + d * 0.10f));
                cg.strokePath(buildPath(f), juce::PathStrokeType(1.0f));
            }
        }
        cacheTable = idx;
        cacheGen = gen;
        cacheW = cw;
        cacheH = ch;
    }

    if (farCache.isValid())
        g.drawImage(farCache, getLocalBounds().toFloat());

    const float posF = show * (frames - 1);
    for (int f = frames - 1; f >= 0; --f) {
        const float near = juce::jmax(0.0f, 1.0f - std::abs(f - posF));
        if (near <= 0.02f) continue;
        auto path = buildPath(f);
        g.setColour(accent.withAlpha(near * 0.22f)); // bloom ≈ canvas shadowBlur
        g.strokePath(path, juce::PathStrokeType(3.0f + near * 5.0f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(accent.withAlpha(0.25f + near * 0.75f));
        g.strokePath(path, juce::PathStrokeType(1.0f + near * 1.4f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

// ===================== DrumNoiseView =====================

// NoiseView.tsx seededRandom — JS int32/uint32 bit ops transcribed exactly.
static float noiseRand(uint32_t& s) {
    s = (s ^ (s >> 15)) * (1u | s);
    s ^= s + (s ^ (s >> 7)) * (61u | s);
    return (float)((double)(s ^ (s >> 14)) / 4294967296.0);
}

DrumNoiseView::DrumNoiseView(DrumUiModel& p, int pad) : proc(p) {
    colorId = "pad" + juce::String(pad) + ".noise.color";
    startTimerHz(15); // web reseeds the walk every 66 ms
}

void DrumNoiseView::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth(), h = bounds.getHeight();
    g.setColour(col::display);
    g.fillRoundedRectangle(bounds, 9.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 9.0f, 1.0f);

    const float color = parameterValue(proc, colorId);
    uint32_t seed = (uint32_t)(juce::Time::getMillisecondCounter() / 66);
    const float normColor = juce::jlimit(0.0f, 1.0f, (color + 1.0f) / 2.0f);
    const float smoothing = 0.15f + normColor * 0.7f;
    const int points = 90;
    const float padX = 7.0f;
    float y = 0.0f;

    juce::Path path;
    for (int i = 0; i < points; ++i) {
        y += (noiseRand(seed) * 2.0f - 1.0f - y) * smoothing;
        const float x = padX + (i / (float)(points - 1)) * (w - padX * 2.0f);
        const float py = h * 0.5f + y * h * 0.38f;
        if (i == 0) path.startNewSubPath(x, py);
        else        path.lineTo(x, py);
    }
    g.setColour(col::acB.withAlpha(0.18f)); // glow pass ≈ shadowBlur 5
    g.strokePath(path, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    g.setColour(col::acB.withAlpha(0.8f));
    g.strokePath(path, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
}

// ===================== DrumEnvView =====================
DrumEnvView::DrumEnvView(DrumUiModel& p, int pad, Mode m, juce::Colour acc)
    : proc(p), base("pad" + juce::String(pad) + "."), mode(m), accent(acc) {
    startTimerHz(30);
}

float DrumEnvView::val(const juce::String& id) const {
    return parameterValue(proc, id);
}

void DrumEnvView::timerCallback() {
    float cur[5];
    if (mode == Pitch) {
        cur[0] = val(base + "penv.amt"); cur[1] = val(base + "penv.dec");
        cur[2] = cur[3] = 0;
    } else {
        cur[0] = val(base + "aenv.att");  cur[1] = val(base + "aenv.hold");
        cur[2] = val(base + "aenv.dec");  cur[3] = val(base + "aenv.curve");
    }
    cur[4] = std::round(proc.vizEnvelope() * 100.0f); // hit pulse, quantised
    bool dirty = false;
    for (int i = 0; i < 5; ++i)
        if (cur[i] != last[i]) { last[i] = cur[i]; dirty = true; }
    if (dirty) repaint();
}

void DrumEnvView::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth(), h = bounds.getHeight();
    g.setColour(col::display);
    g.fillRoundedRectangle(bounds, 9.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 9.0f, 1.0f);

    const float pad = 6.0f;
    const float width = w - pad * 2.0f;
    const float env = juce::jlimit(0.0f, 1.0f, proc.vizEnvelope());

    if (mode == Pitch) {
        // DrumEnvView.tsx pitch branch: exp(-7p) sweep around a zero line.
        const float amt = val(base + "penv.amt");
        const float zeroY = h * 0.72f;
        const float magnitude = juce::jmin(1.0f, std::abs(amt) / 48.0f);
        const float availableHeight = amt >= 0 ? zeroY - pad : h - pad - zeroY;
        const float sign = amt >= 0 ? 1.0f : -1.0f;
        auto yFor = [&](float p) { return zeroY - sign * magnitude * availableHeight
                                          * std::exp(-7.0f * p); };

        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.drawLine(pad, zeroY, w - pad, zeroY, 1.0f);

        juce::Path trace;
        for (int i = 0; i <= 60; ++i) {
            const float p = i / 60.0f;
            const float x = pad + p * width, y = yFor(p);
            if (i == 0) trace.startNewSubPath(x, y);
            else        trace.lineTo(x, y);
        }
        juce::Path fill(trace);
        fill.lineTo(w - pad, zeroY);
        fill.lineTo(pad, zeroY);
        fill.closeSubPath();
        juce::ColourGradient grad(accent.withAlpha(0.25f), 0, juce::jmin(pad, zeroY),
                                  accent.withAlpha(0.21f), 0, juce::jmax(h - pad, zeroY), false);
        grad.addColour(0.72, accent.withAlpha(0.07f));
        g.setGradientFill(grad);
        g.fillPath(fill);

        g.setColour(accent.withAlpha(0.20f + 0.30f * env)); // glow, pulsing on hits
        g.strokePath(trace, juce::PathStrokeType(4.5f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        g.setColour(accent);
        g.strokePath(trace, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        g.setColour(juce::Colour(0xff6b768c));
        g.setFont(monoFont(7.0f));
        g.drawText((amt > 0 ? "+" : "") + juce::String((int)std::lround(amt)) + " ST",
                   (int)pad, 5, 60, 10, juce::Justification::centredLeft);
        return;
    }

    // AHD branch: attack ramp, hold plateau, curve-morphed decay.
    const float att = val(base + "aenv.att"), hold = val(base + "aenv.hold");
    const float dec = val(base + "aenv.dec");
    const float curve = juce::jlimit(0.0f, 1.0f, val(base + "aenv.curve"));
    const float height = h - pad * 2.0f;
    auto seg = [](float time) { return std::pow(juce::jmax(0.0f, time) / 4.0f, 0.4f); };
    const float attackW = seg(att), holdW = seg(hold), decayW = seg(dec);
    const float total = juce::jmax(0.0001f, attackW + holdW + decayW);
    auto xFor = [&](float time) { return pad + (time / total) * width; };
    auto yFor = [&](float value) { return pad + (1.0f - value) * height; };
    const float attackEnd = attackW, holdEnd = attackEnd + holdW, decayEnd = holdEnd + decayW;

    juce::Path trace;
    trace.startNewSubPath(xFor(0), yFor(0));
    trace.lineTo(xFor(attackEnd), yFor(1));
    trace.lineTo(xFor(holdEnd), yFor(1));
    for (int i = 1; i <= 60; ++i) {
        const float progress = i / 60.0f;
        const float linear = 1.0f - progress;
        const float exponential = std::exp(-4.5f * progress);
        const float value = linear + (exponential - linear) * curve;
        trace.lineTo(xFor(holdEnd + decayW * progress), yFor(i == 60 ? 0.0f : value));
    }
    juce::Path fill(trace);
    fill.lineTo(xFor(decayEnd), yFor(0));
    fill.lineTo(xFor(0), yFor(0));
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(accent.withAlpha(0.24f), 0, pad,
                                           accent.withAlpha(0.0f), 0, h - pad, false));
    g.fillPath(fill);

    g.setColour(accent.withAlpha(0.20f + 0.30f * env)); // glow, pulsing on hits
    g.strokePath(trace, juce::PathStrokeType(4.5f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
    g.setColour(accent);
    g.strokePath(trace, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // live envelope level from the audio thread (selected pad's last hit)
    if (env > 0.004f) {
        g.setColour(accent.withAlpha(0.55f));
        g.drawLine(pad, yFor(env), w - pad, yFor(env), 1.0f);
    }
}

// ===================== DrumFilterView =====================

// FilterView.tsx magFor, types 0-4 (LP12/LP24/BP12/HP12/NOTCH) — the analog
// prototypes the drum filter list maps onto.
static double drumFltMag(int type, double cutoff, double res, double f) {
    const double k = 2.0 - 1.93 * res, wn = f / cutoff;
    const double den = std::sqrt(std::pow(1.0 - wn * wn, 2.0) + std::pow(k * wn, 2.0));
    switch (type) {
        case 0:  return 1.0 / den;
        case 1:  return 1.0 / (den * den);
        case 2:  return (k * wn) / den;
        case 3:  return (wn * wn) / den;
        default: return std::abs(1.0 - wn * wn) / den;
    }
}

DrumFilterView::DrumFilterView(DrumUiModel& p, int pad)
    : proc(p), base("pad" + juce::String(pad) + ".flt.") {
    startTimerHz(20);
}

void DrumFilterView::timerCallback() {
    auto get = [&](const char* sfx) {
        return parameterValue(proc, base + sfx);
    };
    const float sum = get("on") + get("type") * 1.7f + get("cut") * 0.001f + get("res") * 2.3f;
    if (sum != sig) { sig = sum; repaint(); }
}

void DrumFilterView::paint(juce::Graphics& g) {
    drawDisplayBox(g, getLocalBounds().toFloat());
    auto get = [&](const char* sfx) {
        return parameterValue(proc, base + sfx);
    };
    const bool on = get("on") > 0.5f;
    const int type = (int)std::lround(get("type"));
    const double cut = get("cut"), res = get("res");

    const float w = (float)getWidth(), h = (float)getHeight(), pad = 6;
    const double fmin = 20, fmax = 20000;

    g.setColour(juce::Colours::white.withAlpha(0.05f));
    for (double fg : { 100.0, 1000.0, 10000.0 }) {
        const float x = pad + (float)(std::log(fg / fmin) / std::log(fmax / fmin)) * (w - pad * 2);
        g.drawVerticalLine((int)x, 0, h);
    }
    auto toY = [&](double mag) {
        const double db = juce::jlimit(-30.0, 24.0, 20.0 * std::log10(juce::jmax(1e-6, mag)));
        return (float)(h * 0.45 - (db / 30.0) * h * 0.42);
    };
    auto plot = [&](std::function<double(double)> fn, juce::Colour stroke, float width) {
        juce::Path pth;
        for (int i = 0; i <= 120; ++i) {
            const double f = fmin * std::pow(fmax / fmin, i / 120.0);
            const float x = pad + (i / 120.0f) * (w - pad * 2), y = toY(fn(f));
            if (i == 0) pth.startNewSubPath(x, y);
            else        pth.lineTo(x, y);
        }
        g.setColour(stroke);
        g.strokePath(pth, juce::PathStrokeType(width, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    };

    // faint individual response (web plots each enabled filter), then combined
    if (on) plot([&](double f) { return drumFltMag(type, cut, res, f); },
                 col::acF.withAlpha(0.28f), 1.0f);
    plot([&](double f) { return on ? drumFltMag(type, cut, res, f) : 1.0; },
         on ? col::acF : juce::Colour(0xff5a6275), 1.8f);
}

// ===================== PadBoundPanel =====================
PadBoundPanel::PadBoundPanel(DrumUiModel& p) : proc(p) {
    proc.selectionChanges().addChangeListener(this);
}

PadBoundPanel::~PadBoundPanel() {
    proc.selectionChanges().removeChangeListener(this);
}

juce::String PadBoundPanel::pid(const char* field) const {
    return "pad" + juce::String(proc.selectedPad()) + "." + field;
}

void PadBoundPanel::changeListenerCallback(juce::ChangeBroadcaster*) {
    rebuild();   // new pad selected -> recreate every child with the new ids
    resized();
    repaint();
}

// ===================== DrumOscPanel =====================
DrumOscPanel::DrumOscPanel(DrumUiModel& p, int oscIndex)
    : PadBoundPanel(p), osc(oscIndex), ac(oscIndex == 0 ? Accent::A : Accent::B) {
    rebuild();
}

void DrumOscPanel::rebuild() {
    const int sel = proc.selectedPad();
    const auto pre = juce::String(osc == 0 ? "oscA." : "oscB.");
    table = std::make_unique<Stepper>(proc.parameters(), pid((pre + "table").toRawUTF8()), ac);
    // Cycle only over the live tables and show live (user) names — WT-1 scheme.
    table->countProvider = [this] { return proc.numTables(); };
    table->nameProvider  = [this](int idx) { return proc.tableName(idx); };
    addAndMakeVisible(*table);
    wt = std::make_unique<DrumTerrainView>(proc, sel, osc, accentColour(ac));
    addAndMakeVisible(*wt);
    auto& pr = proc;
    const int o = osc;
    pos = std::make_unique<VSlider>(proc.parameters(), pid((pre + "pos").toRawUTF8()), ac,
                                    [&pr, o] { return pr.vizPosition(o); });
    addAndMakeVisible(*pos);
    knobs.clear();
    const char* ids[] = { "tune", "fine", "phase", "unison", "detune", "level" };
    for (int i = 0; i < 6; ++i)
        addAndMakeVisible(knobs.add(new Knob(proc.parameters(), pid((pre + ids[i]).toRawUTF8()),
                                             i == 5 ? Knob::Md : Knob::Sm, ac)));
}

void DrumOscPanel::resized() {
    auto r = contentBox(*this);
    headArea = r.removeFromTop(18);
    auto head = headArea;
    if (table) table->setBounds(head.removeFromRight(116).withSizeKeepingCentre(116, 18));
    r.removeFromTop(8);
    auto body = r.removeFromTop(104);
    auto posCol = body.removeFromRight(36);
    body.removeFromRight(8);
    if (wt)  wt->setBounds(body);
    if (pos) pos->setBounds(posCol);
    r.removeFromTop(4);
    static const int sizes[] = { Knob::svgPx(Knob::Sm), Knob::svgPx(Knob::Sm),
                                 Knob::svgPx(Knob::Sm), Knob::svgPx(Knob::Sm),
                                 Knob::svgPx(Knob::Sm), Knob::svgPx(Knob::Md) };
    layoutKnobRow(r, knobs, sizes);
}

void DrumOscPanel::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    auto head = headArea;
    drawDrLed(g, head.removeFromLeft(8).withSizeKeepingCentre(8, 8).toFloat(),
              accentColour(ac));
    head.removeFromLeft(8);
    drawHeadTitle(g, head, osc == 0 ? "OSC A" : "OSC B", accentColour(ac));
}

// ===================== DrumNoisePanel =====================
DrumNoisePanel::DrumNoisePanel(DrumUiModel& p) : PadBoundPanel(p) { rebuild(); }

void DrumNoisePanel::rebuild() {
    view = std::make_unique<DrumNoiseView>(proc, proc.selectedPad());
    addAndMakeVisible(*view);
    knobs.clear();
    addAndMakeVisible(knobs.add(new Knob(proc.parameters(), pid("noise.color"), Knob::Sm, Accent::B)));
    addAndMakeVisible(knobs.add(new Knob(proc.parameters(), pid("noise.level"), Knob::Md, Accent::B)));
}

void DrumNoisePanel::resized() {
    auto r = contentBox(*this);
    headArea = r.removeFromTop(18);
    r.removeFromTop(8);
    if (view) view->setBounds(r.removeFromTop(104));
    r.removeFromTop(8);
    static const int sizes[] = { Knob::svgPx(Knob::Sm), Knob::svgPx(Knob::Md) };
    layoutKnobRow(r, knobs, sizes);
}

void DrumNoisePanel::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    auto head = headArea;
    drawDrLed(g, head.removeFromLeft(8).withSizeKeepingCentre(8, 8).toFloat(), col::acB);
    head.removeFromLeft(8);
    drawValueWell(g, head.removeFromRight(65).withSizeKeepingCentre(65, 18), "WHITE");
    drawHeadTitle(g, head, "NOISE", col::acB);
}

// ===================== DrumPitchEnvPanel =====================
DrumPitchEnvPanel::DrumPitchEnvPanel(DrumUiModel& p) : PadBoundPanel(p) { rebuild(); }

void DrumPitchEnvPanel::rebuild() {
    view = std::make_unique<DrumEnvView>(proc, proc.selectedPad(),
                                         DrumEnvView::Pitch, col::acA);
    addAndMakeVisible(*view);
    knobs.clear();
    addAndMakeVisible(knobs.add(new Knob(proc.parameters(), pid("penv.amt"), Knob::Md, Accent::A)));
    addAndMakeVisible(knobs.add(new Knob(proc.parameters(), pid("penv.dec"), Knob::Md, Accent::A)));
}

void DrumPitchEnvPanel::resized() {
    auto r = contentBox(*this);
    headArea = r.removeFromTop(18);
    r.removeFromTop(8);
    if (view) view->setBounds(r.removeFromTop(58));
    r.removeFromTop(7);
    static const int sizes[] = { Knob::svgPx(Knob::Md), Knob::svgPx(Knob::Md) };
    layoutKnobRow(r, knobs, sizes);
}

void DrumPitchEnvPanel::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    drawHeadTitle(g, headArea, "PITCH ENV", col::acA);
}

// ===================== DrumAmpEnvPanel =====================
DrumAmpEnvPanel::DrumAmpEnvPanel(DrumUiModel& p) : PadBoundPanel(p) { rebuild(); }

void DrumAmpEnvPanel::rebuild() {
    view = std::make_unique<DrumEnvView>(proc, proc.selectedPad(),
                                         DrumEnvView::Ahd, col::ptr);
    addAndMakeVisible(*view);
    knobs.clear();
    for (const char* id : { "aenv.att", "aenv.hold", "aenv.dec", "aenv.curve" })
        addAndMakeVisible(knobs.add(new Knob(proc.parameters(), pid(id), Knob::Sm, Accent::N)));
}

void DrumAmpEnvPanel::resized() {
    auto r = contentBox(*this);
    headArea = r.removeFromTop(18);
    r.removeFromTop(8);
    if (view) view->setBounds(r.removeFromTop(58));
    r.removeFromTop(7);
    static const int sizes[] = { Knob::svgPx(Knob::Sm), Knob::svgPx(Knob::Sm),
                                 Knob::svgPx(Knob::Sm), Knob::svgPx(Knob::Sm) };
    layoutKnobRow(r, knobs, sizes);
}

void DrumAmpEnvPanel::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    auto head = headArea;
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    // web hint "AHD · ONE-SHOT" — ASCII middle dot substitute for the mono font
    drawSpaced(g, "AHD - ONE-SHOT", head.removeFromRight(92), 1.0f,
               juce::Justification::right);
    drawHeadTitle(g, head, "AMP ENV", col::text);
}

// ===================== DrumFilterPanel =====================
DrumFilterPanel::DrumFilterPanel(DrumUiModel& p) : PadBoundPanel(p) {
    rebuild();
    startTimerHz(10);
}

void DrumFilterPanel::rebuild() {
    power = std::make_unique<PowerButton>(proc.parameters(), pid("flt.on"), Accent::F);
    addAndMakeVisible(*power);
    type = std::make_unique<Stepper>(proc.parameters(), pid("flt.type"), Accent::F);
    addAndMakeVisible(*type);
    view = std::make_unique<DrumFilterView>(proc, proc.selectedPad());
    addAndMakeVisible(*view);
    knobs.clear();
    addAndMakeVisible(knobs.add(new Knob(proc.parameters(), pid("flt.cut"),   Knob::Md, Accent::F)));
    addAndMakeVisible(knobs.add(new Knob(proc.parameters(), pid("flt.res"),   Knob::Sm, Accent::F)));
    addAndMakeVisible(knobs.add(new Knob(proc.parameters(), pid("flt.drive"), Knob::Sm, Accent::F)));
    lastOn = -1; // re-apply the dimming to the fresh children
}

// .panel.off dims the body (view + knobs) to 40%; head controls stay lit.
void DrumFilterPanel::timerCallback() {
    const int on = power && power->isOn() ? 1 : 0;
    if (on == lastOn) return;
    lastOn = on;
    const float a = on != 0 ? 1.0f : 0.4f;
    if (view) view->setAlpha(a);
    for (auto* k : knobs) k->setAlpha(a);
}

void DrumFilterPanel::resized() {
    auto r = contentBox(*this);
    headArea = r.removeFromTop(18);
    auto head = headArea;
    if (type)  type->setBounds(head.removeFromRight(92).withSizeKeepingCentre(92, 18));
    r.removeFromTop(8);
    if (view) view->setBounds(r.removeFromTop(58));
    r.removeFromTop(7);
    static const int sizes[] = { Knob::svgPx(Knob::Md), Knob::svgPx(Knob::Sm),
                                 Knob::svgPx(Knob::Sm) };
    layoutKnobRow(r, knobs, sizes);
    // power button sits after the title (web flex order: led, h2, power, stepper)
    if (power) {
        auto h2 = headArea;
        h2.removeFromLeft(8 + 8);
        power->setBounds(h2.removeFromLeft(64).removeFromRight(17)
                             .withSizeKeepingCentre(17, 17));
    }
}

void DrumFilterPanel::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    auto head = headArea;
    drawDrLed(g, head.removeFromLeft(8).withSizeKeepingCentre(8, 8).toFloat(), col::acF);
    head.removeFromLeft(8);
    drawHeadTitle(g, head, "FILTER", col::acF);
}

// ===================== DrumModPanel =====================
DrumModPanel::DrumModPanel(DrumUiModel& p) : PadBoundPanel(p) {
    rebuild();
    startTimerHz(10);
}

void DrumModPanel::rebuild() {
    for (int n = 0; n < 4; ++n) {
        const auto pre = "mod" + juce::String(n + 1) + ".";
        auto& row = rows[(size_t)n];
        row.src = std::make_unique<Stepper>(proc.parameters(), pid((pre + "src").toRawUTF8()), Accent::N);
        row.dst = std::make_unique<Stepper>(proc.parameters(), pid((pre + "dst").toRawUTF8()), Accent::N);
        row.amt = std::make_unique<Knob>(proc.parameters(), pid((pre + "amt").toRawUTF8()),
                                         Knob::Xs, Accent::N, false);
        addAndMakeVisible(*row.src);
        addAndMakeVisible(*row.dst);
        addAndMakeVisible(*row.amt);
    }
    decKnob = std::make_unique<Knob>(proc.parameters(), pid("modenv.dec"), Knob::Xs, Accent::N, false);
    addAndMakeVisible(*decKnob);
    lastDec = -1.0f;
}

void DrumModPanel::timerCallback() {
    const float dec = parameterValue(proc, pid("modenv.dec"));
    if (dec != lastDec) { lastDec = dec; repaint(headArea); }
}

void DrumModPanel::resized() {
    auto r = contentBox(*this);
    headArea = r.removeFromTop(34);
    auto head = headArea;
    if (decKnob) decKnob->setBounds(head.removeFromRight(34).withSizeKeepingCentre(34, 34));
    head.removeFromRight(5);
    decHintArea = head.removeFromRight(110);
    for (int n = 0; n < 4; ++n) {
        auto rowArea = r.removeFromTop(30);
        r.removeFromTop(5);
        auto& row = rows[(size_t)n];
        if (!row.src) continue;
        row.amt->setBounds(rowArea.removeFromRight(34));
        rowArea.removeFromRight(5);
        arrowAreas[(size_t)n] = juce::Rectangle<int>(rowArea.getCentreX() - 5, rowArea.getY(), 10,
                                                     rowArea.getHeight());
        const int half = (rowArea.getWidth() - 10) / 2;
        row.src->setBounds(rowArea.removeFromLeft(half).withSizeKeepingCentre(half, 18));
        row.dst->setBounds(rowArea.removeFromRight(half).withSizeKeepingCentre(half, 18));
    }
}

void DrumModPanel::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    drawHeadTitle(g, headArea.withTrimmedRight(160), "MOD", col::text);
    // .dr-mod-env hint: "MOD ENV DEC <fmtSec>" next to the compact decay knob
    const float v = parameterValue(proc, pid("modenv.dec"));
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    drawSpaced(g, "MOD ENV DEC " + fmtSec(v).toUpperCase(),
               decHintArea, 0.9f, juce::Justification::right);
    // ▸ arrows between src and dst (ASCII: default mono font lacks U+25B8)
    g.setColour(col::textDim);
    g.setFont(monoFont(8.0f));
    for (const auto& a : arrowAreas)
        g.drawText(">", a, juce::Justification::centred);
}

} // namespace fui
