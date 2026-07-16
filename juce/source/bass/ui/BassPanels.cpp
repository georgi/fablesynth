#include "BassPanels.h"
#include "../../dsp/Wavetables.h"
#include "../dsp/BassEngine.h"
#include "../../ui/Format.h"
#include <cmath>

// Web layout (src/bass/bass.css), rack-relative px:
//   .panel padding 8px 12px 10px; .panel-head 18px + 8px margin-bottom.
//   .bl-osc-body grid: rows 104px / auto, cols 1fr / 36px, gap 8px.
//   .bl-display height 104px; .bl-lfo-view height 44px; knob rows fill the rest.
namespace fui {
static bool floatChanged(float a, float b) { return std::isunordered(a, b) || std::islessgreater(a, b); }

// ---- shared bits (drum panel styling, accent-A aware) ------------------------

static void drawBlLed(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ac) {
    g.setColour(ac.withAlpha(0.55f));
    g.fillEllipse(r.expanded(2.5f));
    g.setGradientFill(juce::ColourGradient(ac.brighter(0.8f), r.getX() + r.getWidth() * 0.38f,
                                           r.getY() + r.getHeight() * 0.32f,
                                           ac.darker(0.4f), r.getRight(), r.getBottom(), true));
    g.fillEllipse(r);
    g.setColour(ac.brighter(0.4f).withAlpha(0.8f));
    g.drawEllipse(r, 1.0f);
}

// Dim idle LED (.bl-led without an accent class — the SUB panel).
static void drawIdleLed(juce::Graphics& g, juce::Rectangle<float> r) {
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2a3140),
                                           r.getX() + r.getWidth() * 0.4f,
                                           r.getY() + r.getHeight() * 0.35f,
                                           juce::Colour(0xff12151d), r.getRight(), r.getBottom(), true));
    g.fillEllipse(r);
    g.setColour(col::line);
    g.drawEllipse(r, 1.0f);
}

static void drawHeadTitle(juce::Graphics& g, juce::Rectangle<int> area,
                          const juce::String& t, juce::Colour c) {
    g.setColour(c);
    g.setFont(dispFont(10.0f));
    drawSpaced(g, t, area, 2.2f);
}

// .bl-head-note — dim right-aligned hint in a panel head.
static void drawHeadNote(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& t) {
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    drawSpaced(g, t, area, 1.1f, juce::Justification::right);
}

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
        juce::Rectangle<int> cell((int)std::round(static_cast<float>(area.getX()) + static_cast<float>(i) * cw), area.getY(),
                                  (int)std::round(cw), area.getHeight());
        const int kh = juce::jmin(area.getHeight(), sizesPx[i] + 13); // dia + label strip
        knobs[i]->setBounds(cell.withSizeKeepingCentre(cell.getWidth(), kh));
    }
}

static float rawVal(BassUiModel& proc, const juce::String& id) {
    auto* v = proc.parameters().parameter(id);
    return v ? v->convertFrom0to1(v->getValue()) : 0.0f;
}

// ===================== BassTerrainView =====================
BassTerrainView::BassTerrainView(BassUiModel& p) : proc(p) {
    startTimerHz(30); // animation cadence (matches the web rAF throttle)
}

int BassTerrainView::tableIndex() const { return (int)rawVal(proc, "osc.table"); }
float BassTerrainView::knobPos() const { return rawVal(proc, "osc.pos"); }

void BassTerrainView::timerCallback() {
    const float mp = proc.vizPosition();
    const float show = mp >= 0 ? mp : knobPos();
    const int idx = tableIndex();
    if (idx == lastTable && std::abs(show - lastShown) < 0.004f) return;
    lastShown = show;
    lastTable = idx;
    repaint();
}

void BassTerrainView::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth(), h = bounds.getHeight();

    // display backdrop (.bl-wavetable-view)
    g.setColour(juce::Colour(0xff0d1016));
    g.fillRoundedRectangle(bounds, 9.0f);
    g.setColour(juce::Colour(0xff1c222c));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 9.0f, 1.0f);

    const int idx = tableIndex();
    const auto* tp = proc.tableAt(idx);
    if (!tp) return;
    const auto& t = *tp;
    const int frames = t.frames;
    if (frames < 1 || t.viz.empty()) return;
    const float* viz = t.viz.data();
    const int N = (int)t.viz.size() / frames;

    const float mp = proc.vizPosition();
    const float show = mp >= 0 ? mp : knobPos();

    // perspective layout (mirrors the shared WavetableView canvas math)
    const float depthX = w * 0.22f, depthY = h * 0.42f;
    const float waveW = w * 0.68f, waveAmp = h * 0.17f;
    const float x0 = w * 0.06f, y0 = h * 0.78f;

    const auto buildPath = [&](int f) {
        const float d = frames > 1 ? static_cast<float>(f) / static_cast<float>(frames - 1) : 0.0f;
        const float ox = x0 + d * depthX;
        const float oy = y0 - d * depthY;
        juce::Path path;
        for (int i = 0; i < N; ++i) {
            const float x = ox + (static_cast<float>(i) / static_cast<float>(N - 1)) * waveW;
            const float y = oy - viz[f * N + i] * waveAmp;
            if (i == 0) path.startNewSubPath(x, y);
            else        path.lineTo(x, y);
        }
        return path;
    };

    const int cw = getWidth(), ch = getHeight();
    const bool cacheValid = cacheTable == idx && cacheW == cw && cacheH == ch;
    if (!cacheValid) {
        farCache = (cw > 0 && ch > 0) ? juce::Image(juce::Image::ARGB, cw * 2, ch * 2, true)
                                      : juce::Image();
        if (farCache.isValid()) {
            juce::Graphics cg(farCache);
            cg.addTransform(juce::AffineTransform::scale(2.0f));
            for (int f = frames - 1; f >= 0; --f) {
                const float d = frames > 1 ? static_cast<float>(f) / static_cast<float>(frames - 1) : 0.0f;
                cg.setColour(juce::Colour(0xff8893a8).withAlpha(0.16f + d * 0.10f));
                cg.strokePath(buildPath(f), juce::PathStrokeType(1.0f));
            }
        }
        cacheTable = idx;
        cacheW = cw;
        cacheH = ch;
    }

    if (farCache.isValid())
        g.drawImage(farCache, getLocalBounds().toFloat());

    const juce::Colour accent = accentA();
    const float posF = show * static_cast<float>(frames - 1);
    for (int f = frames - 1; f >= 0; --f) {
        const float near = juce::jmax(0.0f, 1.0f - std::abs(static_cast<float>(f) - posF));
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

// ===================== BassFilterView =====================

// FilterSection.tsx mag(): analog prototype with q = 0.7 + res*9; LP24 is the
// LP12 magnitude squared. Types 0-4 = LP12/LP24/BP12/HP12/NOTCH.
static double bassFltMag(int type, double cutoff, double res, double f) {
    const double q = 0.7 + res * 9.0, wn = f / cutoff;
    const double den = std::sqrt(std::pow(1.0 - wn * wn, 2.0) + std::pow(wn / q, 2.0));
    double m;
    switch (type) {
        case 2:  m = (wn / q) / den; break;
        case 3:  m = (wn * wn) / den; break;
        case 4:  m = std::abs(1.0 - wn * wn) / den; break;
        default: m = 1.0 / den;
    }
    return type == 1 ? m * m : m;
}

BassFilterView::BassFilterView(BassUiModel& p) : proc(p) { startTimerHz(30); }

void BassFilterView::timerCallback() {
    const float vc = proc.vizCutoff();
    const float sum = rawVal(proc, "flt.type") * 1.7f + rawVal(proc, "flt.cut") * 0.001f
                    + rawVal(proc, "flt.res") * 2.3f + (vc > 0 ? vc * 0.003f : 0.0f);
    if (floatChanged(sum, sig)) { sig = sum; repaint(); }
}

void BassFilterView::paint(juce::Graphics& g) {
    drawDisplayBox(g, getLocalBounds().toFloat());
    const int type = (int)std::lround(rawVal(proc, "flt.type"));
    const double res = rawVal(proc, "flt.res");
    const float vizCut = proc.vizCutoff();
    const double cut = vizCut > 0 ? vizCut : rawVal(proc, "flt.cut");

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
    auto plot = [&](juce::Colour stroke, float width) {
        juce::Path pth;
        for (int i = 0; i <= 120; ++i) {
            const double f = fmin * std::pow(fmax / fmin, i / 120.0);
            const float x = pad + (static_cast<float>(i) / 120.0f) * (w - pad * 2), y = toY(bassFltMag(type, cut, res, f));
            if (i == 0) pth.startNewSubPath(x, y);
            else        pth.lineTo(x, y);
        }
        g.setColour(stroke);
        g.strokePath(pth, juce::PathStrokeType(width, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    };
    plot(col::acF.withAlpha(0.28f), 1.0f);   // faint underlay, then the glow line
    plot(col::acF, 1.8f);

    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    g.drawText("LFO + ENV -> CUT", (int)pad + 2, 4, 110, 10, juce::Justification::centredLeft);
}

// ===================== BassEnvView =====================

// EnvView drawing window: envelope segment widths are normalized against this
// many seconds so knob moves read as proportional changes.
static constexpr float kEnvWindowS = 2.5f;

BassEnvView::BassEnvView(BassUiModel& p) : proc(p) { startTimerHz(30); }

float BassEnvView::val(const char* id) const { return rawVal(proc, id); }

void BassEnvView::timerCallback() {
    const float cur[7] = {
        val("fenv.att"), val("fenv.dec"), val("aenv.att"), val("aenv.dec"),
        val("aenv.sus"), val("aenv.rel"), val("acc.amt"),
    };
    bool dirty = false;
    for (int i = 0; i < 7; ++i)
        if (floatChanged(cur[i], last[i])) { last[i] = cur[i]; dirty = true; }
    if (dirty) repaint();
}

void BassEnvView::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth(), h = bounds.getHeight();
    drawDisplayBox(g, bounds);

    const float pad = 6, W = w - 12, H = h - 14;
    auto X = [&](float t) { return pad + juce::jmin(1.0f, t) * W; };
    auto Y = [&](float v) { return pad + (1.0f - v) * H; };
    auto nf = [&](float s) { return juce::jmin(1.0f, s / kEnvWindowS); };

    // filter env: linear att, exp decay — solid green + dashed accent ghost
    const float fA = nf(val("fenv.att")), fD = nf(val("fenv.dec"));
    const float accAmt = val("acc.amt");
    auto fpath = [&](float scale) {
        juce::Path p;
        p.startNewSubPath(X(0), Y(0));
        p.lineTo(X(fA), Y(1));
        for (int i = 0; i <= 80; ++i) {
            const float q = static_cast<float>(i) / 80.0f;
            p.lineTo(X(fA + q * fD * 4 * scale), Y(std::exp(-q * 4.5f)));
        }
        return p;
    };
    const juce::Colour green = accentA();
    auto main = fpath(1.0f);
    g.setColour(green.withAlpha(0.22f));          // glow pass ≈ shadowBlur 6
    g.strokePath(main, juce::PathStrokeType(4.5f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    g.setColour(green);
    g.strokePath(main, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    // accent-boosted ghost (shorter decay)
    juce::Path ghost = fpath(1.0f - 0.35f * accAmt);
    juce::PathStrokeType dashType(1.0f);
    const float dashes[] = { 3.0f, 3.0f };
    juce::Path dashed;
    dashType.createDashedStroke(dashed, ghost, dashes, 2);
    g.setColour(col::acB.withAlpha(0.55f));
    g.fillPath(dashed);

    // amp env: ADSR with a fixed sustain shelf
    const float aA = nf(val("aenv.att")), aD = nf(val("aenv.dec")) * 2,
                aR = nf(val("aenv.rel")) * 2;
    const float sus = juce::jlimit(0.0f, 1.0f, val("aenv.sus"));
    const float x1 = juce::jmin(0.3f, aA);
    const float x2 = juce::jmin(0.62f, x1 + aD);
    const float x3 = 0.78f;
    const float x4 = juce::jmin(1.0f, x3 + aR);
    juce::Path amp;
    amp.startNewSubPath(X(0), Y(0));
    amp.lineTo(X(x1), Y(1));
    amp.quadraticTo(X(x1 + (x2 - x1) * 0.4f), Y(sus + (1 - sus) * 0.25f), X(x2), Y(sus));
    amp.lineTo(X(x3), Y(sus));
    amp.quadraticTo(X(x3 + (x4 - x3) * 0.5f), Y(sus * 0.15f), X(x4), Y(0));
    g.setColour(col::ptr.withAlpha(0.85f));
    g.strokePath(amp, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));

    // captions
    g.setFont(monoFont(7.0f));
    g.setColour(green);
    g.drawText("FILTER", (int)pad + 2, 4, 40, 10, juce::Justification::centredLeft);
    g.setColour(col::acB.withAlpha(0.8f));
    g.drawText("ACCENT", (int)pad + 40, 4, 44, 10, juce::Justification::centredLeft);
    g.setColour(col::acN);
    g.drawText("AMP", (int)pad + 84, 4, 30, 10, juce::Justification::centredLeft);
}

// ===================== BassLfoView =====================

// LfoPanel.tsx shapeValue
static float lfoShapeValue(int shape, float phase) {
    const float p = phase - std::floor(phase);
    switch (shape) {
        case 1: return 1 - 4 * std::abs(p - 0.5f);
        case 2: return 1 - 2 * p;
        case 3: return p < 0.5f ? 1.0f : -1.0f;
        case 4: return std::sin(std::floor(phase * 4) * 999.9f); // pseudo s&h preview
        default: return std::sin(p * 2 * juce::MathConstants<float>::pi);
    }
}

BassLfoView::BassLfoView(BassUiModel& p)
    : proc(p), t0(juce::Time::getMillisecondCounter()) {
    startTimerHz(30);
}

void BassLfoView::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth(), h = bounds.getHeight();
    g.setColour(col::display);
    g.fillRoundedRectangle(bounds, 8.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

    const int shape = (int)std::lround(rawVal(proc, "lfo.shape"));
    const int rate = (int)std::lround(rawVal(proc, "lfo.rate"));
    const double bpm = proc.hostSynced() ? proc.hostBpm() : rawVal(proc, "seq.bpm");
    const double cpb = fable::lfoDivFactor(rate);
    const float ph0 = proc.sequencerPlaying()
        ? (float)std::fmod(((juce::Time::getMillisecondCounter() - t0) / 1000.0)
                               * (bpm / 60.0) * cpb, 1.0)
        : 0.0f;

    juce::Path path;
    for (int i = 0; i <= 100; ++i) {
        const float q = static_cast<float>(i) / 100.0f;
        const float y = h / 2 - lfoShapeValue(shape, q * 2 - ph0) * h * 0.34f;
        const float x = q * w;
        if (i == 0) path.startNewSubPath(x, y);
        else        path.lineTo(x, y);
    }
    g.setColour(accentA().withAlpha(0.18f)); // glow pass ≈ shadowBlur 5
    g.strokePath(path, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    g.setColour(accentA().withAlpha(0.9f));
    g.strokePath(path, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
}

// ===================== BassOscPanel =====================
BassOscPanel::BassOscPanel(BassUiModel& p)
    : table(p.parameters(), "osc.table", Accent::A), wt(p),
      pos(p.parameters(), "osc.pos", Accent::A, [&p] { return p.vizPosition(); }) {
    addAndMakeVisible(table);
    addAndMakeVisible(wt);
    addAndMakeVisible(pos);
    const char* ids[] = { "osc.tune", "osc.fine", "osc.unison",
                          "osc.detune", "osc.spread", "osc.level" };
    for (int i = 0; i < 6; ++i)
        addAndMakeVisible(knobs.add(new Knob(p.parameters(), ids[i],
                                             i == 5 ? Knob::Md : Knob::Sm, Accent::A)));
}

void BassOscPanel::resized() {
    auto r = contentBox(*this);
    headArea = r.removeFromTop(18);
    auto head = headArea;
    table.setBounds(head.removeFromRight(116).withSizeKeepingCentre(116, 18));
    r.removeFromTop(8);
    auto body = r.removeFromTop(104);
    auto posCol = body.removeFromRight(36);
    body.removeFromRight(8);
    wt.setBounds(body);
    pos.setBounds(posCol);
    r.removeFromTop(4);
    static const int sizes[] = { Knob::svgPx(Knob::Sm), Knob::svgPx(Knob::Sm),
                                 Knob::svgPx(Knob::Sm), Knob::svgPx(Knob::Sm),
                                 Knob::svgPx(Knob::Sm), Knob::svgPx(Knob::Md) };
    layoutKnobRow(r, knobs, sizes);
}

void BassOscPanel::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    auto head = headArea;
    drawBlLed(g, head.removeFromLeft(8).withSizeKeepingCentre(8, 8).toFloat(), accentA());
    head.removeFromLeft(8);
    drawHeadTitle(g, head, "OSC", accentA());
}

// ===================== BassSubPanel =====================
BassSubPanel::BassSubPanel(BassUiModel& p)
    : shape(p.parameters(), "sub.shape", Accent::N),
      oct(p.parameters(), "sub.oct", Accent::N),
      level(p.parameters(), "sub.level", Knob::Md, Accent::N) {
    addAndMakeVisible(shape);
    addAndMakeVisible(oct);
    addAndMakeVisible(level);
}

void BassSubPanel::resized() {
    auto r = contentBox(*this);
    headArea = r.removeFromTop(18);
    r.removeFromTop(8);
    auto row1 = r.removeFromTop(20);
    shapeLabel = row1.removeFromLeft(44);
    shape.setBounds(row1.withSizeKeepingCentre(row1.getWidth(), 18));
    r.removeFromTop(7);
    auto row2 = r.removeFromTop(20);
    octLabel = row2.removeFromLeft(44);
    oct.setBounds(row2.withSizeKeepingCentre(row2.getWidth(), 18));
    level.setBounds(r.withSizeKeepingCentre(Knob::svgPx(Knob::Md) + 16,
                                            Knob::svgPx(Knob::Md) + 13));
}

void BassSubPanel::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    auto head = headArea;
    drawIdleLed(g, head.removeFromLeft(8).withSizeKeepingCentre(8, 8).toFloat());
    head.removeFromLeft(8);
    drawHeadTitle(g, head, "SUB", col::text);
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    drawSpaced(g, "SHAPE", shapeLabel, 1.4f);
    drawSpaced(g, "OCT", octLabel, 1.4f);
}

// ===================== BassFilterPanel =====================
BassFilterPanel::BassFilterPanel(BassUiModel& p)
    : type(p.parameters(), "flt.type", Accent::F), view(p) {
    addAndMakeVisible(type);
    addAndMakeVisible(view);
    const char* ids[] = { "flt.cut", "flt.res", "flt.drive", "flt.env", "flt.track" };
    for (int i = 0; i < 5; ++i)
        addAndMakeVisible(knobs.add(new Knob(p.parameters(), ids[i],
                                             i == 0 ? Knob::Md : Knob::Sm, Accent::F)));
}

void BassFilterPanel::resized() {
    auto r = contentBox(*this);
    headArea = r.removeFromTop(18);
    auto head = headArea;
    type.setBounds(head.removeFromRight(92).withSizeKeepingCentre(92, 18));
    r.removeFromTop(8);
    view.setBounds(r.removeFromTop(104));
    r.removeFromTop(4);
    static const int sizes[] = { Knob::svgPx(Knob::Md), Knob::svgPx(Knob::Sm),
                                 Knob::svgPx(Knob::Sm), Knob::svgPx(Knob::Sm),
                                 Knob::svgPx(Knob::Sm) };
    layoutKnobRow(r, knobs, sizes);
}

void BassFilterPanel::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    auto head = headArea;
    drawBlLed(g, head.removeFromLeft(8).withSizeKeepingCentre(8, 8).toFloat(), col::acF);
    head.removeFromLeft(8);
    drawHeadTitle(g, head, "FILTER", col::acF);
}

// ===================== BassEnvPanel =====================
BassEnvPanel::BassEnvPanel(BassUiModel& p) : view(p) {
    addAndMakeVisible(view);
    struct Def { const char* id; Accent ac; };
    const Def defs[] = {
        { "fenv.att", Accent::A }, { "fenv.dec", Accent::A },
        { "aenv.att", Accent::N }, { "aenv.dec", Accent::N },
        { "aenv.sus", Accent::N }, { "aenv.rel", Accent::N },
    };
    for (const auto& d : defs)
        addAndMakeVisible(knobs.add(new Knob(p.parameters(), d.id, Knob::Sm, d.ac)));
}

void BassEnvPanel::resized() {
    auto r = contentBox(*this);
    headArea = r.removeFromTop(18);
    r.removeFromTop(8);
    view.setBounds(r.removeFromTop(104));
    r.removeFromTop(4);
    static const int sizes[] = { Knob::svgPx(Knob::Sm), Knob::svgPx(Knob::Sm),
                                 Knob::svgPx(Knob::Sm), Knob::svgPx(Knob::Sm),
                                 Knob::svgPx(Knob::Sm), Knob::svgPx(Knob::Sm) };
    layoutKnobRow(r, knobs, sizes);
}

void BassEnvPanel::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    auto head = headArea;
    // web "FILTER AD · AMP ADSR" — ASCII middle dot substitute
    drawHeadNote(g, head.removeFromRight(150), "FILTER AD - AMP ADSR");
    drawHeadTitle(g, head, "ENV", col::text);
}

// ===================== BassLfoPanel =====================
BassLfoPanel::BassLfoPanel(BassUiModel& p)
    : view(p),
      rate(p.parameters(), "lfo.rate", Accent::A),
      shape(p.parameters(), "lfo.shape", Accent::N),
      depth(p.parameters(), "lfo.depth", Knob::Md, Accent::A) {
    addAndMakeVisible(view);
    addAndMakeVisible(rate);
    addAndMakeVisible(shape);
    addAndMakeVisible(depth);
}

void BassLfoPanel::resized() {
    auto r = contentBox(*this);
    headArea = r.removeFromTop(18);
    r.removeFromTop(8);
    auto knobCol = r.removeFromRight(64);
    r.removeFromRight(8);
    depth.setBounds(knobCol.withSizeKeepingCentre(64, Knob::svgPx(Knob::Md) + 13));
    view.setBounds(r.removeFromTop(44));
    r.removeFromTop(6);
    auto steppers = r.removeFromTop(18);
    const int half = (steppers.getWidth() - 5) / 2;
    rate.setBounds(steppers.removeFromLeft(half));
    steppers.removeFromLeft(5);
    shape.setBounds(steppers);
}

void BassLfoPanel::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    auto head = headArea;
    drawHeadNote(g, head.removeFromRight(140), "-> CUTOFF - BAR-LOCKED");
    drawHeadTitle(g, head, "LFO", col::text);
}

// ===================== BassAccentPanel =====================
BassAccentPanel::BassAccentPanel(BassUiModel& p)
    : acc(p.parameters(), "acc.amt", Knob::Lg, Accent::A),
      slide(p.parameters(), "slide.time", Knob::Md, Accent::N) {
    addAndMakeVisible(acc);
    addAndMakeVisible(slide);
}

void BassAccentPanel::resized() {
    auto r = contentBox(*this);
    headArea = r.removeFromTop(18);
    r.removeFromTop(4);
    hintArea = r.removeFromBottom(10);
    const float cw = static_cast<float>(r.getWidth()) / 2.0f;
    acc.setBounds(juce::Rectangle<int>(r.getX(), r.getY(), (int)cw, r.getHeight())
                      .withSizeKeepingCentre((int)cw, juce::jmin(r.getHeight(),
                                             Knob::svgPx(Knob::Lg) + 13)));
    slide.setBounds(juce::Rectangle<int>(r.getX() + (int)cw, r.getY(), (int)cw, r.getHeight())
                        .withSizeKeepingCentre((int)cw, juce::jmin(r.getHeight(),
                                               Knob::svgPx(Knob::Md) + 13)));
}

void BassAccentPanel::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    auto head = headArea;
    drawBlLed(g, head.removeFromLeft(8).withSizeKeepingCentre(8, 8).toFloat(), accentA());
    head.removeFromLeft(8);
    drawHeadTitle(g, head, "ACCENT - SLIDE", accentA());
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    drawSpaced(g, "ONE KNOB - LEVEL + ENV + DECAY", hintArea, 1.2f,
               juce::Justification::horizontallyCentred);
}

// ===================== BassKeysPanel =====================

// KeysPanel.tsx key geometry: 25 keys (2 octaves + top C), 15 whites; blacks
// sit at fractional white-key offsets.
static constexpr int kWhiteCount = 15;
static bool keyIsBlack(int pc) { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
static float blackOffset(int pc) {
    switch (pc) { case 1: return 0.68f; case 3: return 1.72f; case 6: return 3.68f;
                  case 8: return 4.7f; default: return 5.72f; }
}
// White index of key semi within its octave.
static int whiteIndex(int pc) {
    static const int idx[12] = { 0, 0, 1, 0, 2, 3, 0, 4, 0, 5, 0, 6 };
    return idx[pc];
}

BassKeysPanel::BassKeysPanel(BassUiModel& p) : proc(p) { startTimerHz(20); }

juce::Rectangle<float> BassKeysPanel::keyBounds(int semi) const {
    const auto area = keysArea.toFloat();
    const float ww = (area.getWidth() - 2.0f * (kWhiteCount - 1)) / kWhiteCount;
    const int pc = semi % 12, oct = semi / 12;
    if (keyIsBlack(pc)) {
        const float x = area.getX()
                      + (static_cast<float>(oct * 7) + blackOffset(pc)) * (area.getWidth() / kWhiteCount);
        return { x, area.getY(), area.getWidth() * 0.041f, area.getHeight() * 0.58f };
    }
    const int wi = oct * 7 + whiteIndex(pc);
    return { static_cast<float>(area.getX()) + static_cast<float>(wi) * (ww + 2.0f),
             static_cast<float>(area.getY()), ww, static_cast<float>(area.getHeight()) };
}

int BassKeysPanel::hitKey(juce::Point<float> pos) const {
    // black keys sit on top, so hit-test them first
    for (int s = 0; s < fable::BL_KEY_COUNT; ++s)
        if (keyIsBlack(s % 12) && keyBounds(s).contains(pos)) return s;
    for (int s = 0; s < fable::BL_KEY_COUNT; ++s)
        if (!keyIsBlack(s % 12) && keyBounds(s).contains(pos)) return s;
    return -1;
}

int BassKeysPanel::hotKey() const {
    int semi = proc.currentSemitone();
    if (semi <= -100) return -100;
    // sequencer semis are root-relative (-12..23): show them an octave up so
    // the lane octave lands mid-keyboard; audition semis are already key ids.
    int hot = proc.sequencerPlaying() ? semi + 12 : semi;
    while (hot > fable::BL_KEY_COUNT - 1) hot -= 12;
    while (hot < 0) hot += 12;
    return hot;
}

void BassKeysPanel::timerCallback() {
    const int hot = hotKey();
    if (hot != lastHot_) { lastHot_ = hot; repaint(keysArea); }
}

void BassKeysPanel::mouseDown(const juce::MouseEvent& e) {
    const int k = hitKey(e.position);
    if (k < 0) return;
    pressed_ = k;
    proc.noteOn(k, 0.85f);
    repaint(keysArea);
}

void BassKeysPanel::mouseUp(const juce::MouseEvent&) {
    if (pressed_ < 0) return;
    proc.noteOff(pressed_);
    pressed_ = -1;
    repaint(keysArea);
}

void BassKeysPanel::resized() {
    auto r = contentBox(*this);
    headArea = r.removeFromTop(18);
    r.removeFromTop(8);
    keysArea = r;
}

void BassKeysPanel::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());
    auto head = headArea;
    drawHeadNote(g, head.removeFromRight(240), "AUDITION WHEN STOPPED - LEGATO = SLIDE");
    drawHeadTitle(g, head, "KEYS", col::text);

    const int hot = hotKey();
    const juce::Colour green = accentA();

    // whites first, blacks on top (web z-order)
    for (int pass = 0; pass < 2; ++pass) {
        for (int s = 0; s < fable::BL_KEY_COUNT; ++s) {
            const bool black = keyIsBlack(s % 12);
            if ((pass == 1) != black) continue;
            const auto b = keyBounds(s);
            const bool isHot = s == hot;
            if (black) {
                g.setGradientFill(juce::ColourGradient(
                    isHot ? juce::Colour(0xff2fbf76) : juce::Colour(0xff222835),
                    b.getX(), b.getY(),
                    isHot ? juce::Colour(0xff14522f) : juce::Colour(0xff0c0f15),
                    b.getX(), b.getBottom(), false));
                g.fillRoundedRectangle(b, 3.0f);
                g.setColour(juce::Colours::black.withAlpha(0.7f));
                g.drawRoundedRectangle(b.reduced(0.5f), 3.0f, 1.0f);
                if (isHot) {
                    g.setColour(green.withAlpha(0.5f));
                    g.drawRoundedRectangle(b.expanded(1.0f), 4.0f, 2.0f);
                }
            } else {
                g.setGradientFill(juce::ColourGradient(
                    isHot ? juce::Colour(0xff7dffb8) : juce::Colour(0xffd7dee9),
                    b.getX(), b.getY(),
                    isHot ? juce::Colour(0xff2fbf76) : juce::Colour(0xffa9b3c4),
                    b.getX(), b.getBottom(), false));
                g.fillRoundedRectangle(b, 4.0f);
                g.setColour(juce::Colours::white.withAlpha(0.1f));
                g.drawRoundedRectangle(b.reduced(0.5f), 4.0f, 1.0f);
                if (isHot) {
                    g.setColour(green.withAlpha(0.6f));
                    g.drawRoundedRectangle(b.expanded(1.0f), 5.0f, 2.0f);
                }
            }
        }
    }
}

} // namespace fui
