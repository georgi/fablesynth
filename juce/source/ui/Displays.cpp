#include "Displays.h"
#include "../dsp/Params.h"   // fable::lfoDivFactor
#include <cmath>

namespace fui {

static constexpr double PI = juce::MathConstants<double>::pi;
static bool floatChanged(float a, float b) { return std::isunordered(a, b) || std::islessgreater(a, b); }
static ParameterSource wtSource(juce::AudioProcessorValueTreeState& apvts) {
    const auto& catalog = fable::paramInfo();
    return ParameterSource::fromApvts(apvts, catalog.data(), catalog.size());
}

// ===================== EnvView =====================
EnvView::EnvView(juce::AudioProcessorValueTreeState& s, const juce::String& b, juce::Colour ac)
    : EnvView(wtSource(s), b, ac) {}
EnvView::EnvView(ParameterSource source, const juce::String& b, juce::Colour ac)
    : parameters(std::move(source)), base(b), accent(ac) { startVisualTimer(*this, fui::kAnimationHz); }
float EnvView::p(const char* sfx) const {
    return parameters.effectiveValue(base + sfx);
}
void EnvView::timerCallback() {
    float v[4] = {p(".a"), p(".d"), p(".s"), p(".r")};
    if (floatChanged(v[0], last[0]) || floatChanged(v[1], last[1])
        || floatChanged(v[2], last[2]) || floatChanged(v[3], last[3])) {
        last[0] = v[0]; last[1] = v[1]; last[2] = v[2]; last[3] = v[3]; repaint();
    }
}
void EnvView::paint(juce::Graphics& g) {
    drawDisplayBox(g, getLocalBounds().toFloat());
    const float a = p(".a"), d = p(".d"), s = p(".s"), r = p(".r");
    const float w = (float)getWidth(), h = (float)getHeight();
    const float pad = 6, W = w - pad * 2, H = h - pad * 2;
    auto seg = [](float t) { return std::pow(t / 12.0f, 0.4f); };
    const float ta = seg(a), td = seg(d), tr = seg(r), hold = 0.22f;
    const float total = ta + td + tr + hold;
    auto X = [&](float t) { return pad + (t / total) * W; };
    auto Y = [&](float v) { return pad + (1 - v) * H; };

    juce::Path path;
    path.startNewSubPath(X(0), Y(0));
    path.quadraticTo(X(ta * 0.55f), Y(0.96f), X(ta), Y(1));            // attack
    path.quadraticTo(X(ta + td * 0.45f), Y(s + (1 - s) * 0.5f), X(ta + td), Y(s)); // decay
    path.lineTo(X(ta + td + hold), Y(s));                              // hold
    path.quadraticTo(X(ta + td + hold + tr * 0.45f), Y(s * 0.5f), X(total), Y(0)); // release

    // fill under the curve
    juce::Path fill = path;
    fill.lineTo(X(total), Y(0));
    fill.lineTo(X(0), Y(0));
    fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(accent.withAlpha(0.27f), 0, pad,
                                           accent.withAlpha(0.0f), 0, h, false));
    g.fillPath(fill);

    g.setColour(accent);
    g.strokePath(path, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ===================== LfoView =====================
LfoView::LfoView(juce::AudioProcessorValueTreeState& s, const juce::String& sh,
                 const juce::String& ra, const juce::String& sy, const juce::String& sr,
                 juce::Colour ac, std::function<HostTransport()> transportProvider)
    : LfoView(wtSource(s), sh, ra, sy, sr, ac, std::move(transportProvider)) {}
LfoView::LfoView(ParameterSource source, const juce::String& sh,
                 const juce::String& ra, const juce::String& sy, const juce::String& sr,
                 juce::Colour ac, std::function<HostTransport()> transportProvider)
    : parameters(std::move(source)), shapeId(sh), rateId(ra), syncId(sy), syncRateId(sr), accent(ac),
      transport(std::move(transportProvider)), t0(juce::Time::getMillisecondCounter()),
      shapeParams_(parameters, sh.upToLastOccurrenceOf(".", false, false)) {
    oneshot_ = parameters.parameter(sh.upToLastOccurrenceOf(".", false, false) + ".oneshot");
    startVisualTimer(*this, fui::kAnimationHz);
}
bool LfoView::isCustom() const {
    return parameters.parameter(shapeId) && (int)parameters.effectiveValue(shapeId) == fable::LFO_CUSTOM;
}

// ---- drawn-shape editing (CUSTOM only); mirrors src/components/displays/LFOView.tsx ----
static constexpr float kLfoPad = 5, kLfoHit = 9;
static constexpr int kLfoSteps = 16, kLfoSnap = 32;
LfoView::At LfoView::locate(juce::Point<float> p) const {
    const float W = (float)getWidth() - kLfoPad * 2, mid = (float)getHeight() / 2, amp = (mid - kLfoPad) * 0.9f;
    return {juce::jlimit(0.0, 1.0, (double)((p.x - kLfoPad) / W)),
            juce::jlimit(0.0, 1.0, (double)(((mid - p.y) / amp + 1) / 2)), p.x, p.y};
}
int LfoView::hitPoint(const lfoshape::Points& points, juce::Point<float> p) const {
    const float W = (float)getWidth() - kLfoPad * 2, mid = (float)getHeight() / 2, amp = (mid - kLfoPad) * 0.9f;
    int best = -1; float bestD = kLfoHit;
    for (int i = 0; i < (int)points.size(); ++i) {
        const auto& pt = points[(size_t)i];
        const float d = p.getDistanceFrom({kLfoPad + (float)(pt.t / lfoshape::kLane) * W, mid - (float)(pt.v * 2 - 1) * amp});
        if (d < bestD) { best = i; bestD = d; }
    }
    return best;
}
void LfoView::mouseDown(const juce::MouseEvent& e) {
    drag_ = Drag::None;
    if (!isCustom() || !shapeParams_.valid()) return;
    if (e.mods.isPopupMenu()) { removeAt(e.position); return; }
    const auto points = lfoshape::read(shapeParams_);
    const At at = locate(e.position);
    auto snap = [&](double x) { return e.mods.isShiftDown() ? x : std::round(x * kLfoSnap) / kLfoSnap; };
    if (paintMode_) {
        lfoshape::write(shapeParams_, lfoshape::paintStep(points, juce::jmin(at.x, 0.999), at.y, kLfoSteps));
        drag_ = Drag::Paint; paintX_ = at.x; paintY_ = at.y;
        return;
    }
    const int hit = hitPoint(points, e.position);
    if (e.mods.isAltDown() && points.size() > 1) {
        dragIndex_ = lfoshape::segmentAt(points, at.x);
        const auto& a = points[(size_t)dragIndex_];
        const auto& b = points[(size_t)((dragIndex_ + 1) % (int)points.size())];
        dragC0_ = a.hold ? 0 : a.c; dragDir_ = b.v < a.v ? -1 : 1;
        dragBase_ = points; drag_ = Drag::Bend;
    } else if (hit >= 0) {
        dragIndex_ = hit; dragBase_ = points; drag_ = Drag::Move;
    } else {
        auto added = lfoshape::addPoint(points, snap(at.x), at.y);
        if (added.second < 0) return; // the shape is full
        lfoshape::write(shapeParams_, added.first);
        dragIndex_ = added.second; dragBase_ = added.first; drag_ = Drag::Move;
    }
}
void LfoView::mouseDrag(const juce::MouseEvent& e) {
    if (drag_ == Drag::None) return;
    const At at = locate(e.position);
    if (drag_ == Drag::Paint) {
        // Fill every cell between the previous and the current pointer sample.
        auto points = lfoshape::read(shapeParams_);
        const int cells = juce::jmax(1, (int)std::ceil(std::abs(at.x - paintX_) * kLfoSteps));
        for (int k = 1; k <= cells; ++k) {
            const double f = (double)k / cells;
            points = lfoshape::paintStep(points, juce::jmin(0.999, paintX_ + (at.x - paintX_) * f), paintY_ + (at.y - paintY_) * f, kLfoSteps);
        }
        lfoshape::write(shapeParams_, points);
        paintX_ = at.x; paintY_ = at.y;
    } else if (drag_ == Drag::Move) {
        const double x = e.mods.isShiftDown() ? at.x : std::round(at.x * kLfoSnap) / kLfoSnap;
        lfoshape::write(shapeParams_, lfoshape::movePoint(dragBase_, dragIndex_, x, at.y));
    } else {
        const double dy = (double)(e.getMouseDownY() - e.y) / 40.0;
        lfoshape::write(shapeParams_, lfoshape::bendSegment(dragBase_, dragIndex_, dragC0_ - dy * dragDir_));
    }
    repaint();
}
void LfoView::mouseUp(const juce::MouseEvent&) { drag_ = Drag::None; }
void LfoView::mouseDoubleClick(const juce::MouseEvent& e) { if (isCustom()) removeAt(e.position); }
void LfoView::removeAt(juce::Point<float> p) {
    const auto points = lfoshape::read(shapeParams_);
    const int hit = hitPoint(points, p);
    if (hit < 0) return;
    lfoshape::write(shapeParams_, lfoshape::removePoint(points, hit));
    repaint();
}
bool LfoView::synced() const {
    auto* syncP = parameters.parameter(syncId);
    return syncP && parameters.effectiveValue(syncId) >= 0.5f;
}
// Effective LFO rate in Hz: synced division * host tempo when sync is on, else
// the free RATE param. Mirrors Engine::lfoHz so the dot tracks the real speed.
float LfoView::currentRate(const HostTransport& tr) const {
    if (synced()) {
        auto* srP = dynamic_cast<juce::AudioParameterChoice*>(parameters.parameter(syncRateId));
        int idx = srP ? (int)parameters.effectiveValue(syncRateId) : 2;
        return (float)((tr.bpm / 60.0) * fable::lfoDivFactor(idx));
    }
    auto* rp = parameters.parameter(rateId);
    return rp ? parameters.effectiveValue(rateId) : 1.0f;
}
static float lfoFn(int shape, float p) {
    switch (shape) {
        case 0: return std::sin(2 * (float)PI * p);
        case 1: return 1 - 4 * std::abs(p - 0.5f);
        case 2: return 1 - 2 * p;
        case 3: return p < 0.5f ? 1.0f : -1.0f;
        default: return 0;
    }
}
static float shVal(int s) {
    float v = std::sin(static_cast<float>(s) * 78.233f + 12.9898f) * 43758.5453f;
    v = v - std::floor(v);
    return (v - 0.5f) * 2.0f;
}
void LfoView::paint(juce::Graphics& g) {
    drawDisplayBox(g, getLocalBounds().toFloat());
    const HostTransport tr = transport ? transport() : HostTransport{};
    auto* sp = dynamic_cast<juce::AudioParameterChoice*>(parameters.parameter(shapeId));
    int shape = sp ? (int)parameters.effectiveValue(shapeId) : 0;
    float rate = currentRate(tr);

    const float w = (float)getWidth(), h = (float)getHeight();
    const float pad = 5, W = w - pad * 2, mid = h / 2, amp = h / 2 - pad;

    // CUSTOM: the drawn shape, its beat grid and its point handles.
    const bool custom = shape == fable::LFO_CUSTOM;
    const lfoshape::Points points = custom ? lfoshape::read(shapeParams_) : lfoshape::Points{};
    if (custom) {
        for (int s = 1; s < kLfoSteps; ++s) {
            g.setColour(juce::Colour(0xff8893a8).withAlpha(s % 4 ? 0.07f : 0.2f));
            const float x = pad + (float)s / kLfoSteps * W;
            g.drawLine(x, pad, x, h - pad, 1.0f);
        }
        g.setColour(juce::Colour(0xff8893a8).withAlpha(0.14f));
        g.drawLine(pad, mid, w - pad, mid, 1.0f);
    }
    const auto valueAt = [&](float p) {
        return custom ? (float)lfoshape::value(points, p) : shape == 4 ? shVal((int)std::floor(p * 8)) : lfoFn(shape, p);
    };

    juce::Path path;
    if (custom) {
        // A drawn shape can step: sample finely so the steps stay vertical.
        const int n = juce::jmax(96, (int)W);
        for (int i = 0; i <= n; i++) {
            const float pp = juce::jmin((float)i / (float)n, 0.999999f);
            const float x = pad + (float)i / (float)n * W, y = mid - valueAt(pp) * amp * 0.9f;
            if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
        }
    } else if (shape == 4) {
        const int steps = 8;
        for (int s = 0; s < steps; s++) {
            float y = mid - shVal(s) * amp * 0.9f;
            float x0 = pad + (static_cast<float>(s) / static_cast<float>(steps)) * W;
            float x1 = pad + (static_cast<float>(s + 1) / static_cast<float>(steps)) * W;
            if (s == 0) path.startNewSubPath(x0, y); else path.lineTo(x0, y);
            path.lineTo(x1, y);
        }
    } else {
        for (int i = 0; i <= 96; i++) {
            float pp = static_cast<float>(i) / 96.0f, x = pad + pp * W, y = mid - lfoFn(shape, pp) * amp * 0.9f;
            if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
        }
    }
    g.setColour(accent.withAlpha(0.9f));
    g.strokePath(path, juce::PathStrokeType(1.5f));

    if (custom) {
        // Point handles: round for a ramp, square for a held step.
        for (const auto& pt : points) {
            const float x = pad + (float)(pt.t / lfoshape::kLane) * W, y = mid - (float)(pt.v * 2 - 1) * amp * 0.9f;
            const juce::Rectangle<float> box(x - 3, y - 3, 6, 6);
            g.setColour(juce::Colour(0xff07090e));
            if (pt.hold) g.fillRect(box.reduced(0.5f)); else g.fillEllipse(box);
            g.setColour(accent);
            if (pt.hold) g.drawRect(box.reduced(0.5f), 1.2f); else g.drawEllipse(box, 1.2f);
        }
        if (points.empty()) {
            g.setColour(juce::Colour(0xff8893a8).withAlpha(0.8f));
            g.setFont(monoFont(8.0f));
            g.drawText("CLICK TO PLACE POINTS - ALT-DRAG TO BEND", getLocalBounds(), juce::Justification::centred);
        }
    }

    // Phase dot: when synced and the host is playing, lock to the transport
    // position so the dot sits on the beat grid (matching the audio downbeat);
    // otherwise free-run at the effective rate.
    const bool locked = synced() && tr.playing;
    if (locked != wasSynced_) { phaseClock_.reset(); wasSynced_ = locked; }
    const double position = locked ? tr.ppq
        : (double)(juce::Time::getMillisecondCounter() - t0) / 1000.0;
    const double speed = locked ? fable::lfoDivFactor((int)parameters.effectiveValue(syncRateId)) : rate;
    float phase = (float)phaseClock_.phase(position, speed);
    if (oneshot_ && oneshot_->getValue() > 0.5f && !locked) {
        // A one-shot stops at the end of its cycle: one cycle of motion, one of rest.
        const double lap = std::fmod(position * speed, 2.0);
        phase = lap < 1.0 ? (float)lap : 0.999999f;
    }
    const float y = mid - valueAt(phase) * amp * 0.9f;
    float dx = pad + phase * W;
    g.setColour(accent.withAlpha(0.5f));
    g.fillEllipse(dx - 4, y - 4, 8, 8);
    g.setColour(juce::Colours::white);
    g.fillEllipse(dx - 2.5f, y - 2.5f, 5, 5);
}

// ===================== FilterView =====================
static const double VOWELS[5][3] = {
    {730, 1090, 2440}, {530, 1840, 2480}, {390, 1990, 2550}, {570, 840, 2410}, {440, 1020, 2240}};
static const double F_AMPS[3] = {1, 0.55, 0.32};
static double magFor(int type, double cutoff, double res, double f) {
    if (type <= 4) {
        double k = 2 - 1.93 * res, wn = f / cutoff;
        double den = std::sqrt(std::pow(1 - wn * wn, 2) + std::pow(k * wn, 2));
        switch (type) {
            case 0: return 1 / den;
            case 1: return 1 / (den * den);
            case 2: return (k * wn) / den;
            case 3: return (wn * wn) / den;
            default: return std::abs(1 - wn * wn) / den;
        }
    }
    if (type == 5) {
        double fb = res * 0.97, c = std::cos((2 * PI * f) / cutoff);
        return (1 - fb) / std::sqrt(1 + fb * fb - 2 * fb * c);
    }
    double norm = std::min(0.999, std::max(0.0, std::log(cutoff / 20) / std::log(1000.0)));
    double pos = norm * 4; int vi = std::min(3, (int)pos); double fr = pos - vi;
    double q = 2 + res * 22, acc = 0.04;
    for (int j = 0; j < 3; j++) {
        double f0 = VOWELS[vi][j] + (VOWELS[vi + 1][j] - VOWELS[vi][j]) * fr;
        double rr = f / f0 - f0 / f;
        acc += F_AMPS[j] / std::sqrt(1 + q * q * rr * rr);
    }
    return acc * 0.8;
}
FilterView::FilterView(juce::AudioProcessorValueTreeState& s, juce::Colour ac)
    : FilterView(wtSource(s), ac) {}
FilterView::FilterView(ParameterSource source, juce::Colour ac)
    : parameters(std::move(source)), accent(ac) { startVisualTimer(*this, fui::kAnimationHz); }
void FilterView::timerCallback() {
    auto get = [&](const char* id) { auto* p = parameters.parameter(id); return p ? p->convertTo0to1(parameters.effectiveValue(id)) : 0.0f; };
    float sum = get("filter.on") + get("filter.type") * 1.7f + get("filter.cutoff") * 3.1f + get("filter.res") * 2.3f
              + get("filter2.on") + get("filter2.type") * 1.1f + get("filter2.cutoff") * 0.7f + get("filter2.res") * 1.9f
              + get("filter.route") * 5.0f;
    if (floatChanged(sum, sig)) { sig = sum; repaint(); }
}
void FilterView::paint(juce::Graphics& g) {
    drawDisplayBox(g, getLocalBounds().toFloat());
    auto val = [&](const char* id) { return parameters.effectiveValue(id); };
    struct F { int type; double cut, res; bool on; };
    F f0{(int)val("filter.type"), val("filter.cutoff"), val("filter.res"), val("filter.on") > 0.5f};
    F f1{(int)val("filter2.type"), val("filter2.cutoff"), val("filter2.res"), val("filter2.on") > 0.5f};
    int route = (int)val("filter.route");

    const float w = (float)getWidth(), h = (float)getHeight(), pad = 6;
    const double fmin = 20, fmax = 20000;
    bool anyOn = f0.on || f1.on;

    g.setColour(juce::Colours::white.withAlpha(0.05f));
    for (double fg : {100.0, 1000.0, 10000.0}) {
        float x = pad + (float)(std::log(fg / fmin) / std::log(fmax / fmin)) * (w - pad * 2);
        g.drawVerticalLine((int)x, 0, h);
    }
    auto toY = [&](double mag) {
        double db = std::max(-30.0, std::min(24.0, 20 * std::log10(std::max(1e-6, mag))));
        return (float)(h * 0.45 - (db / 30) * h * 0.42);
    };
    auto plot = [&](std::function<double(double)> fn, juce::Colour stroke, float width) {
        juce::Path pth;
        for (int i = 0; i <= 120; i++) {
            double f = fmin * std::pow(fmax / fmin, i / 120.0);
            float x = pad + (static_cast<float>(i) / 120.0f) * (w - pad * 2), y = toY(fn(f));
            if (i == 0) pth.startNewSubPath(x, y); else pth.lineTo(x, y);
        }
        g.setColour(stroke);
        g.strokePath(pth, juce::PathStrokeType(width));
    };
    if (f0.on) plot([&](double f) { return magFor(f0.type, f0.cut, f0.res, f); }, accent.withAlpha(0.28f), 1.0f);
    if (f1.on) plot([&](double f) { return magFor(f1.type, f1.cut, f1.res, f); }, accent.withAlpha(0.28f), 1.0f);
    plot([&](double f) {
        double m1 = f0.on ? magFor(f0.type, f0.cut, f0.res, f) : -1;
        double m2 = f1.on ? magFor(f1.type, f1.cut, f1.res, f) : -1;
        if (m1 < 0 && m2 < 0) return 1.0;
        if (route == 0) return (m1 < 0 ? 1 : m1) * (m2 < 0 ? 1 : m2);
        return (m1 < 0 ? 0 : m1) + (m2 < 0 ? 0 : m2);
    }, anyOn ? accent : juce::Colour(0xff5a6275), 1.8f);
}

// ===================== ScopeView =====================
ScopeView::ScopeView(std::function<void(float*, int)> reader, juce::Colour ac)
    : readScope(std::move(reader)), accent(ac) { startVisualTimer(*this, fui::kAnimationHz); }
void ScopeView::timerCallback() {
    // Silent output draws the same flat line every frame: skip the repaint until
    // audio returns. One extra paint after the transition draws the final flat line.
    std::array<float, 2048> buf;
    readScope(buf.data(), static_cast<int>(buf.size()));
    float peak = 0;
    for (float v : buf) peak = std::max(peak, std::abs(v));
    bool active = peak > 1.0e-5f;
    if (active || wasActive_) repaint();
    wasActive_ = active;
}
void ScopeView::paint(juce::Graphics& g) {
    const int N = 2048;
    std::array<float, 2048> buf;
    readScope(buf.data(), N);
    const float w = (float)getWidth(), h = (float)getHeight();
    // Idle: a faint neutral centre line instead of a full-brightness flat trace,
    // so a silent scope reads as "no signal", not a stuck display (web parity:
    // ScopeView.tsx drawIdle).
    float peak = 0;
    for (float v : buf) peak = std::max(peak, std::abs(v));
    if (peak <= 1.0e-5f) {
        g.setColour(col::textHint.withAlpha(0.22f));
        g.drawLine(0, h / 2, w, h / 2, 1.0f);
        return;
    }
    int start = 0;
    for (int i = 1; i < N / 2; i++) if (buf[static_cast<size_t>(i - 1)] <= 0 && buf[static_cast<size_t>(i)] > 0) { start = i; break; }
    int M = std::min(900, N - start);
    juce::Path path;
    for (int i = 0; i < M; i++) {
        float x = (static_cast<float>(i) / static_cast<float>(M - 1)) * w;
        float y = h / 2 - buf[static_cast<size_t>(start + i)] * h * 0.46f;
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.setColour(accent.withAlpha(0.95f));
    g.strokePath(path, juce::PathStrokeType(1.2f));
}

// ===================== SpectrumView =====================
SpectrumView::SpectrumView(std::function<void(float*, int)> reader,
                           std::function<double()> sampleRateProvider,
                           juce::Colour ac)
    : readScope(std::move(reader)), sampleRate(std::move(sampleRateProvider)), accent(ac) { startVisualTimer(*this, fui::kAnimationHz); }
void SpectrumView::timerCallback() {
    std::array<float, kFFT> buf;
    readScope(buf.data(), kFFT);
    float peak = 0;
    for (float v : buf) peak = std::max(peak, std::abs(v));
    bool active = peak > 1.0e-5f;
    // decaying_: the smoothing runs in paint, so keep painting after the input
    // stops until every bar has fallen below the -100 dB display floor.
    if (active || wasActive_ || decaying_) repaint();
    wasActive_ = active;
}
void SpectrumView::paint(juce::Graphics& g) {
    std::array<float, kFFT * 2> fftData{};
    readScope(fftData.data(), kFFT);
    window.multiplyWithWindowingTable(fftData.data(), kFFT);
    fft.performFrequencyOnlyForwardTransform(fftData.data());

    // byte spectrum with WebAudio-like smoothing (smoothingTimeConstant 0.82).
    // Preserve the former 30 Hz smoothing time at the animation cadence.
    const float minDb = -100, maxDb = -30;
    const float smoothing = std::pow(0.82f, 30.0f / kAnimationHz);
    std::array<float, kFFT / 2> bytes;
    float maxSm = 0;
    for (int i = 0; i < kFFT / 2; i++) {
        const auto index = static_cast<size_t>(i);
        float mag = fftData[index] / static_cast<float>(kFFT);
        smoothed[index] = smoothing * smoothed[index] + (1 - smoothing) * mag;
        maxSm = std::max(maxSm, smoothed[index]);
        float db = juce::Decibels::gainToDecibels(smoothed[index] + 1e-9f);
        bytes[index] = juce::jlimit(0.0f, 1.0f, (db - minDb) / (maxDb - minDb)) * 255.0f;
    }
    decaying_ = maxSm > 1.0e-5f; // 1e-5 == the -100 dB floor where bars are zero

    const float w = (float)getWidth(), h = (float)getHeight();
    const int bars = 48;
    // Idle: faint baseline stubs where the bars would rise, so a silent
    // analyser still reads as an analyser (web parity: SpectrumView.tsx drawIdle).
    if (!decaying_) {
        g.setColour(col::textHint.withAlpha(0.14f));
        for (int b = 0; b < bars; b++) {
            float bw = w / static_cast<float>(bars);
            g.fillRect(static_cast<float>(b) * bw + 0.5f, h - 2, bw - 1.5f, 2.0f);
        }
        return;
    }
    const double sr = sampleRate();
    const double fmin = 30, fmax = std::min(18000.0, sr / 2);
    g.setGradientFill(juce::ColourGradient(accent.withAlpha(0.33f), 0, h, accent, 0, 0, false));
    for (int b = 0; b < bars; b++) {
        double f0 = fmin * std::pow(fmax / fmin, b / (double)bars);
        double f1 = fmin * std::pow(fmax / fmin, (b + 1) / (double)bars);
        int i0 = (int)((f0 / (sr / 2)) * (kFFT / 2));
        int i1 = std::max(i0 + 1, (int)((f1 / (sr / 2)) * (kFFT / 2)));
        float m = 0;
        for (int i = i0; i < i1 && i < kFFT / 2; i++) m = std::max(m, bytes[static_cast<size_t>(i)]);
        float v = m / 255.0f, bh = std::pow(v, 1.4f) * (h - 2), bw = w / static_cast<float>(bars);
        g.fillRect(static_cast<float>(b) * bw + 0.5f, h - bh, bw - 1.5f, bh);
    }
}

} // namespace fui
