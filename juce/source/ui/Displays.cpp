#include "Displays.h"
#include "../dsp/Params.h"   // fable::lfoDivFactor
#include <cmath>

namespace fui {

static constexpr double PI = juce::MathConstants<double>::pi;

// ===================== EnvView =====================
EnvView::EnvView(juce::AudioProcessorValueTreeState& s, const juce::String& b, juce::Colour ac)
    : apvts(s), base(b), accent(ac) { startTimerHz(20); }
float EnvView::p(const char* sfx) const {
    auto* prm = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(base + sfx));
    return prm ? prm->convertFrom0to1(prm->getValue()) : 0.0f;
}
void EnvView::timerCallback() {
    float v[4] = {p(".a"), p(".d"), p(".s"), p(".r")};
    if (v[0] != last[0] || v[1] != last[1] || v[2] != last[2] || v[3] != last[3]) {
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
    : apvts(s), shapeId(sh), rateId(ra), syncId(sy), syncRateId(sr), accent(ac),
      transport(std::move(transportProvider)), t0(juce::Time::getMillisecondCounter()) {
    startTimerHz(30);
}
bool LfoView::synced() const {
    auto* syncP = apvts.getParameter(syncId);
    return syncP && syncP->getValue() >= 0.5f;
}
// Effective LFO rate in Hz: synced division * host tempo when sync is on, else
// the free RATE param. Mirrors Engine::lfoHz so the dot tracks the real speed.
float LfoView::currentRate(const HostTransport& tr) const {
    if (synced()) {
        auto* srP = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(syncRateId));
        int idx = srP ? srP->getIndex() : 2;
        return (float)((tr.bpm / 60.0) * fable::lfoDivFactor(idx));
    }
    auto* rp = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(rateId));
    return rp ? rp->convertFrom0to1(rp->getValue()) : 1.0f;
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
    float v = std::sin(s * 78.233f + 12.9898f) * 43758.5453f;
    v = v - std::floor(v);
    return (v - 0.5f) * 2.0f;
}
void LfoView::paint(juce::Graphics& g) {
    drawDisplayBox(g, getLocalBounds().toFloat());
    const HostTransport tr = transport ? transport() : HostTransport{};
    auto* sp = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(shapeId));
    int shape = sp ? sp->getIndex() : 0;
    float rate = currentRate(tr);

    const float w = (float)getWidth(), h = (float)getHeight();
    const float pad = 5, W = w - pad * 2, mid = h / 2, amp = h / 2 - pad;

    juce::Path path;
    if (shape == 4) {
        const int steps = 8;
        for (int s = 0; s < steps; s++) {
            float y = mid - shVal(s) * amp * 0.9f;
            float x0 = pad + (s / (float)steps) * W, x1 = pad + ((s + 1) / (float)steps) * W;
            if (s == 0) path.startNewSubPath(x0, y); else path.lineTo(x0, y);
            path.lineTo(x1, y);
        }
    } else {
        for (int i = 0; i <= 96; i++) {
            float pp = i / 96.0f, x = pad + pp * W, y = mid - lfoFn(shape, pp) * amp * 0.9f;
            if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
        }
    }
    g.setColour(accent.withAlpha(0.9f));
    g.strokePath(path, juce::PathStrokeType(1.5f));

    // Phase dot: when synced and the host is playing, lock to the transport
    // position so the dot sits on the beat grid (matching the audio downbeat);
    // otherwise free-run at the effective rate.
    float phase;
    if (synced() && tr.playing) {
        auto* srP = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(syncRateId));
        int idx = srP ? srP->getIndex() : 2;
        double ph = tr.ppq * fable::lfoDivFactor(idx);
        phase = (float)(ph - std::floor(ph));
    } else {
        phase = std::fmod((juce::Time::getMillisecondCounter() - t0) / 1000.0f * rate, 1.0f);
    }
    float y;
    if (shape == 4) y = mid - shVal((int)std::floor(phase * 8)) * amp * 0.9f;
    else            y = mid - lfoFn(shape, phase) * amp * 0.9f;
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
FilterView::FilterView(juce::AudioProcessorValueTreeState& s, juce::Colour ac) : apvts(s), accent(ac) { startTimerHz(20); }
void FilterView::timerCallback() {
    auto get = [&](const char* id) { auto* p = apvts.getParameter(id); return p ? p->getValue() : 0.0f; };
    float sum = get("filter.on") + get("filter.type") * 1.7f + get("filter.cutoff") * 3.1f + get("filter.res") * 2.3f
              + get("filter2.on") + get("filter2.type") * 1.1f + get("filter2.cutoff") * 0.7f + get("filter2.res") * 1.9f
              + get("filter.route") * 5.0f;
    if (sum != sig) { sig = sum; repaint(); }
}
void FilterView::paint(juce::Graphics& g) {
    drawDisplayBox(g, getLocalBounds().toFloat());
    auto val = [&](const char* id) {
        auto* p = dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
        return p ? p->convertFrom0to1(p->getValue()) : 0.0f;
    };
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
            float x = pad + (i / 120.0f) * (w - pad * 2), y = toY(fn(f));
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
ScopeView::ScopeView(FableAudioProcessor& p, juce::Colour ac) : proc(p), accent(ac) { startTimerHz(30); }
void ScopeView::paint(juce::Graphics& g) {
    const int N = 2048;
    std::array<float, 2048> buf;
    proc.readScope(buf.data(), N);
    const float w = (float)getWidth(), h = (float)getHeight();
    int start = 0;
    for (int i = 1; i < N / 2; i++) if (buf[i - 1] <= 0 && buf[i] > 0) { start = i; break; }
    int M = std::min(900, N - start);
    juce::Path path;
    for (int i = 0; i < M; i++) {
        float x = (i / (float)(M - 1)) * w, y = h / 2 - buf[start + i] * h * 0.46f;
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.setColour(accent.withAlpha(0.95f));
    g.strokePath(path, juce::PathStrokeType(1.2f));
}

// ===================== SpectrumView =====================
SpectrumView::SpectrumView(FableAudioProcessor& p, juce::Colour ac) : proc(p), accent(ac) { startTimerHz(30); }
void SpectrumView::paint(juce::Graphics& g) {
    std::array<float, kFFT * 2> fftData{};
    proc.readScope(fftData.data(), kFFT);
    window.multiplyWithWindowingTable(fftData.data(), kFFT);
    fft.performFrequencyOnlyForwardTransform(fftData.data());

    // byte spectrum with WebAudio-like smoothing (smoothingTimeConstant 0.82).
    const float minDb = -100, maxDb = -30, smoothing = 0.82f;
    std::array<float, kFFT / 2> bytes;
    for (int i = 0; i < kFFT / 2; i++) {
        float mag = fftData[i] / kFFT;
        smoothed[i] = smoothing * smoothed[i] + (1 - smoothing) * mag;
        float db = juce::Decibels::gainToDecibels(smoothed[i] + 1e-9f);
        bytes[i] = juce::jlimit(0.0f, 1.0f, (db - minDb) / (maxDb - minDb)) * 255.0f;
    }

    const float w = (float)getWidth(), h = (float)getHeight();
    const int bars = 48;
    const double sr = proc.getCurrentSr();
    const double fmin = 30, fmax = std::min(18000.0, sr / 2);
    g.setGradientFill(juce::ColourGradient(accent.withAlpha(0.33f), 0, h, accent, 0, 0, false));
    for (int b = 0; b < bars; b++) {
        double f0 = fmin * std::pow(fmax / fmin, b / (double)bars);
        double f1 = fmin * std::pow(fmax / fmin, (b + 1) / (double)bars);
        int i0 = (int)((f0 / (sr / 2)) * (kFFT / 2));
        int i1 = std::max(i0 + 1, (int)((f1 / (sr / 2)) * (kFFT / 2)));
        float m = 0;
        for (int i = i0; i < i1 && i < kFFT / 2; i++) m = std::max(m, bytes[i]);
        float v = m / 255.0f, bh = std::pow(v, 1.4f) * (h - 2), bw = w / bars;
        g.fillRect(b * bw + 0.5f, h - bh, bw - 1.5f, bh);
    }
}

} // namespace fui
