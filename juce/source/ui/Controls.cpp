#include "Controls.h"
#include "Format.h"
#include "Modulation.h"
#include <cmath>

namespace fui {

static constexpr double A0 = -135.0, A1 = 135.0; // knob sweep (degrees, 0 = up)
static float clamp01(float x) { return juce::jlimit(0.0f, 1.0f, x); }
static bool floatChanged(float a, float b) { return std::isunordered(a, b) || std::islessgreater(a, b); }

// See Controls.h — pre-WT-1-table metadata resolver installed by the DR-1
// editor. Zero-initialised before any dynamic initialisation, so an installer
// running from a static initialiser in another TU is safe.
static ParamInfoResolver g_infoResolver = nullptr;
void setParamInfoResolver(ParamInfoResolver r) { g_infoResolver = r; }

// Metadata for a control's parameter id: the installed resolver first (DR-1
// ids), then the WT-1 table (unchanged behaviour when no resolver is set).
static const fable::ParamInfo& lookupInfo(const juce::String& id) {
    if (g_infoResolver)
        if (const auto* info = g_infoResolver(id.toStdString()))
            return *info;
    return fable::paramInfo()[(size_t)fable::idFromString(id.toStdString())];
}

static ParameterSource legacySource(juce::AudioProcessorValueTreeState& apvts) {
    return {
        [&apvts](const juce::String& id) {
            return dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
        },
        [](const juce::String& id) { return &lookupInfo(id); }
    };
}

// Cache the 48 mat src/dst/amt RangedAudioParameters into a [16][3] table so a
// mod-target control can read active slots every timer tick without string
// lookups. Shared by Knob and VSlider.
static void cacheMatParams(const ParameterSource& s,
                           juce::RangedAudioParameter* (&table)[16][3]) {
    const char* fields[] = {".src", ".dst", ".amt"};
    for (int slot = 0; slot < 16; ++slot)
        for (int f = 0; f < 3; ++f)
            table[slot][f] = s.parameter("mat" + juce::String(slot + 1) + fields[f]);
}
static float matRealValue(juce::RangedAudioParameter* p) {
    return p ? p->convertFrom0to1(p->getValue()) : 0.0f;
}

// ======================= Knob =======================
Knob::Knob(juce::AudioProcessorValueTreeState& s, const juce::String& paramId,
           Size sz, Accent ac, bool showLbl, int modDest)
    : Knob(legacySource(s), paramId, sz, ac, showLbl, modDest) {}

Knob::Knob(ParameterSource source, const juce::String& paramId,
           Size sz, Accent ac, bool showLbl, int modDest)
    : parameters(std::move(source)), id(paramId), accent(accentColour(ac)), size(sz), showLabel(showLbl),
      modDest_(modDest) {
    param = parameters.parameter(id);
    const auto* sourceInfo = parameters.info(id);
    const auto& info = sourceInfo != nullptr ? *sourceInfo : lookupInfo(id);
    bipolar = info.min < 0;
    midNorm = fable::valueToNorm(info, 0.0f);
    label = juce::String(info.label).toUpperCase();
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    if (modDest_ > 0) cacheMatParams(parameters, matParams_);
    startTimerHz(30);
}

// Cheap signature of this dest's active slots (slot + src + quantized amt), so
// timerCallback only rebuilds rings_ when something actually changed.
juce::uint64 Knob::ringSignature() const {
    juce::uint64 h = 1469598103934665603ull; // FNV-1a basis
    auto mix = [&h](juce::uint64 v) { h = (h ^ v) * 1099511628211ull; };
    for (int slot = 0; slot < 16; ++slot) {
        int src = (int)matRealValue(matParams_[slot][0]);
        int dst = (int)matRealValue(matParams_[slot][1]);
        if (src != 0 && dst == modDest_) {
            int amtq = (int)std::lround(matRealValue(matParams_[slot][2]) * 1000.0f);
            mix((juce::uint64)((slot + 1) & 0xFF));
            mix((juce::uint64)(src & 0xFF));
            mix((juce::uint64)(amtq & 0xFFFF));
        }
    }
    return h;
}

void Knob::rebuildRings() {
    rings_.clear();
    for (int slot = 0; slot < 16; ++slot) {
        int src = (int)matRealValue(matParams_[slot][0]);
        int dst = (int)matRealValue(matParams_[slot][1]);
        if (src != 0 && dst == modDest_)
            rings_.push_back({ slot + 1, src, matRealValue(matParams_[slot][2]) });
    }
}

void Knob::timerCallback() {
    bool dirty = false;
    if (floatChanged(norm(), lastNorm)) { lastNorm = norm(); dirty = true; }
    if (modDest_ > 0) {
        auto sig = ringSignature();
        if (sig != lastRingSig_) { lastRingSig_ = sig; rebuildRings(); dirty = true; }
    }
    if (dirty) repaint();
}

void Knob::nudge(float d) {
    if (!param) return;
    param->setValueNotifyingHost(clamp01(param->getValue() + d));
    repaint();
}

// Knob body / ring geometry — shared by paint and ring hit-test so the two stay
// in lockstep. Returns centre, body radius, the ring gap and the per-ring
// thickness; rings stack outward starting at bodyR + gap.
namespace {
struct KnobGeom { float cx, cy, bodyR, ringGap, ringThk; };
KnobGeom knobGeom(int w, int h, bool showLabel, int sizePx) {
    const float avail = (float)juce::jmin(w, h - (showLabel ? 13 : 0));
    const float dia = juce::jmin(avail, (float)sizePx);
    const float scale = dia / 80.0f;
    return { w * 0.5f, dia * 0.5f + 1.0f, 26.0f * scale, 6.0f * scale, 4.0f * scale };
}
}

void Knob::mouseDown(const juce::MouseEvent& e) {
    grabbedRing_ = -1;
    if (modDest_ > 0 && !rings_.empty()) {
        auto gm = knobGeom(getWidth(), getHeight(), showLabel, svgPx(size));
        float dx = e.position.x - gm.cx, dy = e.position.y - gm.cy;
        float dist = std::sqrt(dx * dx + dy * dy);
        float inner = gm.bodyR + gm.ringGap;
        float outer = inner + (float)rings_.size() * gm.ringThk;
        if (dist >= inner && dist <= outer) {
            int i = (int)std::floor((dist - inner) / gm.ringThk);
            i = juce::jlimit(0, (int)rings_.size() - 1, i);
            grabbedRing_ = i;
            if (e.mods.isRightButtonDown()) {
                int slot = rings_[(size_t)i].slot;
                fui::clearSlot(parameters, slot);
                lastRingSig_ = ~(juce::uint64)0; // force ring rebuild next tick
                grabbedRing_ = -1;
                repaint();
                return;
            }
            grabbedAmt_ = matParams_[rings_[(size_t)i].slot - 1][2];
            if (grabbedAmt_) grabbedAmt_->beginChangeGesture();
            lastY = e.position.y;
            return;
        }
    }
    if (param) param->beginChangeGesture();
    lastY = e.position.y;
}
void Knob::mouseDrag(const juce::MouseEvent& e) {
    if (grabbedRing_ >= 0 && grabbedRing_ < (int)rings_.size()) {
        float dy = lastY - e.position.y;
        lastY = e.position.y;
        if (auto* p = grabbedAmt_) {
            float cur = p->convertFrom0to1(p->getValue());
            float next = juce::jlimit(-1.0f, 1.0f,
                                      cur + dy * (e.mods.isShiftDown() ? 0.001f : 0.005f));
            p->setValueNotifyingHost(p->convertTo0to1(next));
        }
        lastRingSig_ = ~(juce::uint64)0;
        repaint();
        return;
    }
    float dy = lastY - e.position.y;
    lastY = e.position.y;
    nudge(dy * (e.mods.isShiftDown() ? 0.0008f : 0.005f));
}
void Knob::mouseUp(const juce::MouseEvent&) {
    if (grabbedRing_ >= 0) {
        if (grabbedAmt_) { grabbedAmt_->endChangeGesture(); grabbedAmt_ = nullptr; }
        grabbedRing_ = -1;
        return;
    }
    if (param) param->endChangeGesture();
}
void Knob::mouseDoubleClick(const juce::MouseEvent&) {
    if (param) param->setValueNotifyingHost(param->getDefaultValue());
}
void Knob::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    nudge((w.deltaY > 0 ? 1.0f : -1.0f) * (e.mods.isShiftDown() ? 0.005f : 0.03f));
}

// ---- Knob drag-target (mod-source chips) ----
static int parseModSrc(const juce::var& d) {
    auto s = d.toString();
    if (!s.startsWith("mod-src:")) return 0;
    return s.fromFirstOccurrenceOf(":", false, false).getIntValue();
}
bool Knob::isInterestedInDragSource(const SourceDetails& d) {
    return modDest_ > 0 && d.description.toString().startsWith("mod-src:");
}
void Knob::itemDragEnter(const SourceDetails&) { dragHover_ = true; repaint(); }
void Knob::itemDragExit(const SourceDetails&)  { dragHover_ = false; repaint(); }
void Knob::itemDropped(const SourceDetails& d) {
    dragHover_ = false;
    int src = parseModSrc(d.description);
    if (src > 0 && modDest_ > 0) {
        fui::addRoute(parameters, src, modDest_);
        lastRingSig_ = ~(juce::uint64)0; // force rebuild next tick
    }
    repaint();
}

void Knob::paint(juce::Graphics& g) {
    const float avail = (float)juce::jmin(getWidth(), getHeight() - (showLabel ? 13 : 0));
    const float dia = juce::jmin(avail, (float)svgPx(size));
    const float cx = static_cast<float>(getWidth()) * 0.5f, cy = dia * 0.5f + 1.0f;
    const float scale = dia / 80.0f;
    const float bodyR = 26 * scale, arcR = 33 * scale, ptrLen = 23 * scale;
    const float n = clamp01(norm());

    auto degToRad = [](double d) { return (float)(d * juce::MathConstants<double>::pi / 180.0); };

    // body
    g.setColour(col::knobBody);
    g.fillEllipse(cx - bodyR, cy - bodyR, bodyR * 2, bodyR * 2);
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.drawEllipse(cx - bodyR, cy - bodyR, bodyR * 2, bodyR * 2, 1.5f * scale);

    // track
    juce::Path track;
    track.addCentredArc(cx, cy, arcR, arcR, 0, degToRad(A0), degToRad(A1), true);
    g.setColour(juce::Colours::white.withAlpha(0.09f));
    g.strokePath(track, juce::PathStrokeType(5 * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    double deg = A0 + (A1 - A0) * n;
    double from = bipolar ? A0 + (A1 - A0) * midNorm : A0;
    if (std::abs(deg - from) > 0.01) {
        juce::Path arc;
        arc.addCentredArc(cx, cy, arcR, arcR, 0, degToRad(from), degToRad(deg), true);
        g.setColour(accent.withAlpha(0.35f));
        g.strokePath(arc, juce::PathStrokeType(8 * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(accent);
        g.strokePath(arc, juce::PathStrokeType(5 * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // pointer
    float rad = degToRad(deg);
    juce::Point<float> tip(cx + ptrLen * std::sin(rad), cy - ptrLen * std::cos(rad));
    g.setColour(col::ptr);
    g.drawLine({ {cx, cy}, tip }, 4 * scale);

    // modulation rings: one arc per active slot whose dst == modDest, from the
    // current value angle to the angle at clamp(currentNorm + amt) (bipolar),
    // stacked at increasing radii just outside the body. Source-colored.
    if (modDest_ > 0) {
        const float ringGap = 6.0f * scale, ringThk = 4.0f * scale;
        for (size_t i = 0; i < rings_.size(); ++i) {
            const auto& ring = rings_[i];
            float toNorm = clamp01(n + ring.amt);
            double dFrom = A0 + (A1 - A0) * n;
            double dTo   = A0 + (A1 - A0) * toNorm;
            float rr = bodyR + ringGap + (float)i * ringThk + ringThk * 0.5f;
            auto c = modSourceColour(ring.src);
            if (std::abs(dTo - dFrom) < 0.01) { // zero-depth: a tick so it's visible
                dTo = dFrom + (ring.amt >= 0 ? 0.8 : -0.8);
                c = c.withAlpha(0.4f);
            }
            juce::Path ringArc;
            ringArc.addCentredArc(cx, cy, rr, rr, 0, degToRad(dFrom), degToRad(dTo), true);
            g.setColour(c);
            g.strokePath(ringArc, juce::PathStrokeType(ringThk * 0.7f,
                         juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }
    }

    // drop-target highlight while a compatible source chip hovers
    if (dragHover_) {
        float hr = bodyR + 6.0f * scale + (float)juce::jmax((size_t)1, rings_.size()) * 4.0f * scale;
        g.setColour(accent.withAlpha(0.7f));
        g.drawEllipse(cx - hr, cy - hr, hr * 2, hr * 2, 1.6f * scale);
    }

    // label / value
    if (showLabel) {
        auto labelArea = juce::Rectangle<int>(0, (int)(dia) - 1, getWidth(), 12);
        g.setFont(monoFont(8.0f));
        if (dragging) {
            g.setColour(col::text);
            g.drawText(formatParam(id, param ? param->convertFrom0to1(n) : 0.0f), labelArea,
                       juce::Justification::centred);
        } else {
            g.setColour(col::textDim);
            drawSpaced(g, label, labelArea, 1.2f, juce::Justification::centred);
        }
    }
}

// ======================= Stepper =======================
Stepper::Stepper(juce::AudioProcessorValueTreeState& s, const juce::String& paramId, Accent ac)
    : Stepper(legacySource(s), paramId, ac) {}

Stepper::Stepper(ParameterSource source, const juce::String& paramId, Accent ac)
    : parameters(std::move(source)), id(paramId), accent(accentColour(ac)) {
    choice = dynamic_cast<juce::AudioParameterChoice*>(parameters.parameter(id));
    if (!choice)
        ranged = parameters.parameter(id);
    auto styleBtn = [this](juce::TextButton& b, int dir) {
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
        b.setColour(juce::TextButton::textColourOffId, col::textDim);
        b.onClick = [this, dir] { step(dir); };
        addAndMakeVisible(b);
    };
    styleBtn(prev, -1);
    styleBtn(next, +1);
    startTimerHz(20);
}
int Stepper::choiceCount() const {
    if (countProvider) return juce::jmax(1, countProvider());
    return choice ? choice->choices.size() : 1;
}
juce::String Stepper::displayName() const {
    if (choice) {
        if (nameProvider) return nameProvider(choice->getIndex());
        return choice->getCurrentChoiceName();
    }
    if (ranged) {
        const int v = (int)std::lround(ranged->convertFrom0to1(ranged->getValue()));
        // Integer-range steppers can format the raw value (e.g. seq.root shows
        // a note name via params.ts fmtNote); default is the plain number.
        if (nameProvider) return nameProvider(v);
        return juce::String(v);
    }
    return "-";
}
void Stepper::timerCallback() {
    if (choice) {
        if (choice->getIndex() != lastIndex) { lastIndex = choice->getIndex(); repaint(); }
        return;
    }
    if (ranged) {
        float v = ranged->getValue();
        if (floatChanged(v, lastValue)) { lastValue = v; repaint(); }
    }
}
void Stepper::step(int d) {
    if (choice) {
        int n = choiceCount();
        int idx = ((choice->getIndex() % n) + d + n) % n;
        choice->setValueNotifyingHost(choice->convertTo0to1((float)idx));
        repaint();
        return;
    }
    if (ranged) {
        const auto& r = ranged->getNormalisableRange();
        float stepBy = r.interval > 0.0f ? r.interval : 1.0f;
        float cur  = ranged->convertFrom0to1(ranged->getValue());
        float nextValue = juce::jlimit(r.start, r.end, cur + static_cast<float>(d) * stepBy);
        ranged->setValueNotifyingHost(ranged->convertTo0to1(nextValue));
        repaint();
    }
}
void Stepper::resized() {
    auto r = getLocalBounds();
    prev.setBounds(r.removeFromLeft(18));
    next.setBounds(r.removeFromRight(18));
}
void Stepper::paint(juce::Graphics& g) {
    auto r = getLocalBounds().reduced(20, 0);
    g.setColour(juce::Colour(0xff0a0d13));
    g.fillRoundedRectangle(r.toFloat(), 4.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 4.0f, 1.0f);
    g.setColour(accent);
    g.setFont(monoFont(10.0f));
    drawSpaced(g, displayName(), r, 0.8f, juce::Justification::centred);
}

// ======================= PowerButton =======================
PowerButton::PowerButton(juce::AudioProcessorValueTreeState& s, const juce::String& paramId, Accent ac)
    : PowerButton(legacySource(s), paramId, ac) {}

PowerButton::PowerButton(ParameterSource source, const juce::String& paramId, Accent ac)
    : parameters(std::move(source)), id(paramId), accent(accentColour(ac)) {
    param = parameters.parameter(id);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    startTimerHz(20);
}
bool PowerButton::isOn() const { return param && param->getValue() > 0.5f; }
void PowerButton::timerCallback() { if (isOn() != last) { last = isOn(); repaint(); } }
void PowerButton::mouseDown(const juce::MouseEvent&) {
    if (param) param->setValueNotifyingHost(isOn() ? 0.0f : 1.0f);
    repaint();
}
void PowerButton::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat().withSizeKeepingCentre(15, 15);
    if (isOn()) {
        g.setColour(accent.withAlpha(0.5f));
        g.fillEllipse(b.expanded(3));
        g.setGradientFill(juce::ColourGradient(accent.brighter(0.5f), b.getCentreX(), b.getY(),
                                               accent.darker(0.4f), b.getCentreX(), b.getBottom(), false));
        g.fillEllipse(b);
        g.setColour(accent.withAlpha(0.6f));
        g.drawEllipse(b, 1.0f);
    } else {
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2a3140), b.getCentreX(), b.getY(),
                                               juce::Colour(0xff12151d), b.getCentreX(), b.getBottom(), false));
        g.fillEllipse(b);
        g.setColour(juce::Colours::white.withAlpha(0.14f));
        g.drawEllipse(b, 1.0f);
    }
}

// ======================= VSlider =======================
VSlider::VSlider(juce::AudioProcessorValueTreeState& s, const juce::String& paramId, Accent ac,
                 std::function<float()> ghostFn, int modDest)
    : VSlider(legacySource(s), paramId, ac, std::move(ghostFn), modDest) {}

VSlider::VSlider(ParameterSource source, const juce::String& paramId, Accent ac,
                 std::function<float()> ghostFn, int modDest)
    : parameters(std::move(source)), id(paramId), accent(accentColour(ac)), ghost(std::move(ghostFn)),
      modDest_(modDest) {
    param = parameters.parameter(id);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    if (modDest_ > 0) cacheMatParams(parameters, matParams_);
    startTimerHz(30);
}
juce::uint64 VSlider::ringSignature() const {
    juce::uint64 h = 1469598103934665603ull;
    auto mix = [&h](juce::uint64 v) { h = (h ^ v) * 1099511628211ull; };
    for (int slot = 0; slot < 16; ++slot) {
        int src = (int)matRealValue(matParams_[slot][0]);
        int dst = (int)matRealValue(matParams_[slot][1]);
        if (src != 0 && dst == modDest_) {
            int amtq = (int)std::lround(matRealValue(matParams_[slot][2]) * 1000.0f);
            mix((juce::uint64)((slot + 1) & 0xFF));
            mix((juce::uint64)(src & 0xFF));
            mix((juce::uint64)(amtq & 0xFFFF));
        }
    }
    return h;
}
void VSlider::rebuildRings() {
    rings_.clear();
    for (int slot = 0; slot < 16; ++slot) {
        int src = (int)matRealValue(matParams_[slot][0]);
        int dst = (int)matRealValue(matParams_[slot][1]);
        if (src != 0 && dst == modDest_)
            rings_.push_back({ slot + 1, src, matRealValue(matParams_[slot][2]) });
    }
}
void VSlider::timerCallback() {
    float n = param ? param->getValue() : 0.0f, gh = ghost ? ghost() : -1.0f;
    bool dirty = false;
    if (floatChanged(n, lastNorm) || floatChanged(gh, lastGhost)) { lastNorm = n; lastGhost = gh; dirty = true; }
    if (modDest_ > 0) {
        auto sig = ringSignature();
        if (sig != lastRingSig_) { lastRingSig_ = sig; rebuildRings(); dirty = true; }
    }
    if (dirty) repaint();
}
juce::Rectangle<float> VSlider::trackArea() const {
    // Left-anchor the 9px track (with a small inset for the wider handle/ghost
    // overhang) so depth bands grow rightward into the column's spare width,
    // keeping every band inside getLocalBounds() and therefore grabbable.
    constexpr float kTrackInset = 7.0f;
    auto b = getLocalBounds().toFloat().withTrimmedBottom(14);
    return { b.getX() + kTrackInset, b.getY(), 9.0f, b.getHeight() };
}
void VSlider::moveTo(float y) {
    auto t = trackArea();
    if (param) param->setValueNotifyingHost(clamp01(1.0f - (y - t.getY()) / t.getHeight()));
    repaint();
}
// Depth-band layout: each active slot gets a vertical band just to the right of
// the track, stacked outward. Width matches the ring-thickness idea on the knob.
namespace { constexpr float kBandGap = 3.0f, kBandThk = 6.0f; }

void VSlider::mouseDown(const juce::MouseEvent& e) {
    grabbedRing_ = -1;
    if (modDest_ > 0 && !rings_.empty()) {
        auto t = trackArea();
        float inner = t.getRight() + kBandGap;
        float outer = inner + (float)rings_.size() * kBandThk;
        if (e.position.x >= inner && e.position.x <= outer
            && e.position.y >= t.getY() && e.position.y <= t.getBottom()) {
            int i = (int)std::floor((e.position.x - inner) / kBandThk);
            i = juce::jlimit(0, (int)rings_.size() - 1, i);
            grabbedRing_ = i;
            if (e.mods.isRightButtonDown()) {
                int slot = rings_[(size_t)i].slot;
                fui::clearSlot(parameters, slot);
                lastRingSig_ = ~(juce::uint64)0;
                grabbedRing_ = -1;
                repaint();
                return;
            }
            grabbedAmt_ = matParams_[rings_[(size_t)i].slot - 1][2];
            if (grabbedAmt_) grabbedAmt_->beginChangeGesture();
            lastY = e.position.y; // dedicated last-Y for the band drag (lastNorm is the timer's)
            return;
        }
    }
    if (param) param->beginChangeGesture();
    moveTo(e.position.y);
}
void VSlider::mouseDrag(const juce::MouseEvent& e) {
    if (grabbedRing_ >= 0 && grabbedRing_ < (int)rings_.size()) {
        float dy = lastY - e.position.y; // dedicated last-Y for the band drag
        lastY = e.position.y;
        if (auto* p = grabbedAmt_) {
            float cur = p->convertFrom0to1(p->getValue());
            float next = juce::jlimit(-1.0f, 1.0f,
                                      cur + dy * (e.mods.isShiftDown() ? 0.001f : 0.005f));
            p->setValueNotifyingHost(p->convertTo0to1(next));
        }
        lastRingSig_ = ~(juce::uint64)0;
        repaint();
        return;
    }
    moveTo(e.position.y);
}
void VSlider::mouseUp(const juce::MouseEvent&) {
    if (grabbedRing_ >= 0) {
        if (grabbedAmt_) { grabbedAmt_->endChangeGesture(); grabbedAmt_ = nullptr; }
        grabbedRing_ = -1;
        return;
    }
    if (param) param->endChangeGesture();
}
void VSlider::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    if (param) { param->setValueNotifyingHost(clamp01(param->getValue() + (w.deltaY > 0 ? 0.03f : -0.03f))); repaint(); }
}
void VSlider::paint(juce::Graphics& g) {
    auto t = trackArea();
    g.setColour(juce::Colour(0xff0a0d13));
    g.fillRoundedRectangle(t, 5.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(t.reduced(0.5f), 5.0f, 1.0f);

    float n = param ? param->getValue() : 0.0f;
    float fillTop = t.getBottom() - n * t.getHeight();
    g.setColour(accent.withAlpha(0.55f));
    g.fillRoundedRectangle({ t.getX(), fillTop, t.getWidth(), t.getBottom() - fillTop }, 4.0f);

    // modulated ghost marker
    float gh = ghost ? ghost() : -1.0f;
    if (gh >= 0) {
        float gy = t.getBottom() - gh * t.getHeight();
        g.setColour(accent);
        g.fillRect(t.getX() - 4, gy - 1, t.getWidth() + 8, 2.0f);
    }

    // handle
    float hy = t.getBottom() - n * t.getHeight();
    juce::Rectangle<float> handle(t.getCentreX() - 11, hy - 4.5f, 22, 9);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2b3344), handle.getCentreX(), handle.getY(),
                                           juce::Colour(0xff161b25), handle.getCentreX(), handle.getBottom(), false));
    g.fillRoundedRectangle(handle, 3.0f);
    g.setColour(accent.withAlpha(0.55f));
    g.drawRoundedRectangle(handle, 3.0f, 1.0f);

    // modulation depth bands: one vertical band per active slot beside the
    // track, from the current pos to current + amt, source-colored, stacked.
    if (modDest_ > 0) {
        for (size_t i = 0; i < rings_.size(); ++i) {
            const auto& ring = rings_[i];
            float toNorm = clamp01(n + ring.amt);
            float y0 = t.getBottom() - n * t.getHeight();
            float y1 = t.getBottom() - toNorm * t.getHeight();
            float bx = t.getRight() + kBandGap + (float)i * kBandThk;
            auto top = juce::jmin(y0, y1), bot = juce::jmax(y0, y1);
            if (bot - top < 1.5f) { top -= 1.0f; bot += 1.0f; } // visible tick at zero
            g.setColour(modSourceColour(ring.src).withAlpha(0.85f));
            g.fillRect(juce::Rectangle<float>(bx, top, kBandThk - 1.0f, bot - top));
        }
    }

    // drop-target highlight while a compatible source chip hovers
    if (dragHover_) {
        g.setColour(accent.withAlpha(0.7f));
        g.drawRoundedRectangle(t.expanded(3.0f), 6.0f, 1.6f);
    }

    // POS label
    g.setColour(col::textDim);
    g.setFont(monoFont(8.0f));
    drawSpaced(g, "POS", getLocalBounds().removeFromBottom(12), 1.0f, juce::Justification::centred);
}

// ---- VSlider drag-target (mod-source chips) ----
bool VSlider::isInterestedInDragSource(const SourceDetails& d) {
    return modDest_ > 0 && d.description.toString().startsWith("mod-src:");
}
void VSlider::itemDragEnter(const SourceDetails&) { dragHover_ = true; repaint(); }
void VSlider::itemDragExit(const SourceDetails&)  { dragHover_ = false; repaint(); }
void VSlider::itemDropped(const SourceDetails& d) {
    dragHover_ = false;
    int src = parseModSrc(d.description);
    if (src > 0 && modDest_ > 0) {
        fui::addRoute(parameters, src, modDest_);
        lastRingSig_ = ~(juce::uint64)0;
    }
    repaint();
}

} // namespace fui
