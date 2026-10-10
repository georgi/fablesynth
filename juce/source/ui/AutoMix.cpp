#include "AutoMix.h"
#include "Controls.h"
#include "Theme.h"

#include <cmath>

namespace fui {
namespace am = fable::automix;

namespace {
double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

// JavaScript number-to-string for the short values the status lines print.
juce::String jsNumber(double v) {
    if (std::abs(v - std::round(v)) < 1e-9) return juce::String((long long)std::llround(v));
    return juce::String(v, 6).trimCharactersAtEnd("0");
}

const char* const kEqKeys[] = {"fx.eq.on",
    "fx.eq.lfreq", "fx.eq.low", "fx.eq.lq", "fx.eq.ltype", "fx.eq.lon",
    "fx.eq.mfreq", "fx.eq.mid", "fx.eq.mq", "fx.eq.mtype", "fx.eq.mon",
    "fx.eq.m2freq", "fx.eq.mid2", "fx.eq.m2q", "fx.eq.m2type", "fx.eq.m2on",
    "fx.eq.hfreq", "fx.eq.high", "fx.eq.hq", "fx.eq.htype", "fx.eq.hon"};

void styleBox(juce::ComboBox& box) {
    box.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0a0d14));
    box.setColour(juce::ComboBox::outlineColourId, col::line);
}
} // namespace

juce::String toFixed(double value, int digits) { return am::toFixed(value, digits); }

juce::String SideChainInfo::label(int index) const {
    if (index < 0 || index >= (int)sources.size()) return {};
    const auto& s = sources[(size_t)index];
    auto it = labels.find(s);
    return it != labels.end() ? it->second : juce::String(s);
}
bool SideChainInfo::external(int index) const {
    return index > 0 && index < (int)sources.size() && !juce::String(sources[(size_t)index]).startsWith("PAD");
}

std::map<std::string, juce::String> trackKeyLabels(const std::vector<std::string>& names) {
    std::map<std::string, juce::String> out;
    for (size_t i = 0; i < names.size(); ++i)
        out["TRACK " + std::to_string(i + 1)] = juce::String((int)i + 1) + " " + juce::String(names[i]);
    return out;
}

float readParam(const ParameterSource& source, const juce::String& id, float fallback) {
    if (auto* p = source.parameter(id)) return p->convertFrom0to1(p->getValue());
    return fallback;
}
void writeParam(const ParameterSource& source, const juce::String& id, float value) {
    if (auto* p = source.parameter(id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, p->convertTo0to1(value)));
        p->endChangeGesture();
    }
}

// ===================== AutoEqLearner =====================
AutoEqLearner::AutoEqLearner(AutoEqSource source, ParameterSource params, juce::String prefix, double sampleRate)
    : source_(std::move(source)), params_(std::move(params)), prefix_(std::move(prefix)),
      sampleRate_(sampleRate > 0 ? sampleRate : 48000), reference_(source_.reference) {}
AutoEqLearner::~AutoEqLearner() = default;

juce::StringArray AutoEqLearner::eqKeys() {
    juce::StringArray keys;
    for (auto* k : kEqKeys) keys.add(k);
    return keys;
}

void AutoEqLearner::learn() {
    if (learning_) { stop(); return; }
    if (!source_.tap) return;
    capture_ = std::make_unique<am::SpectrumCapture>(sampleRate_, kFftSize);
    pending_.assign((size_t)kFftSize, 0.0f);
    spectrum_.assign((size_t)kFftSize / 2, 0.0f);
    filled_ = 0;
    cursor_ = source_.tap->frames();
    // The measurement is coloured by the EQ that was active while it ran.
    removeColour_ = !source_.preEq && readParam(params_, id("fx.eq.on")) > 0.5f;
    colour_.clear();
    if (removeColour_) {
        const char* bands[4][5] = {{"lfreq", "low", "lq", "ltype", "lon"}, {"mfreq", "mid", "mq", "mtype", "mon"},
                                   {"m2freq", "mid2", "m2q", "m2type", "m2on"}, {"hfreq", "high", "hq", "htype", "hon"}};
        for (auto& b : bands) {
            am::EqBand band;
            band.freq = readParam(params_, id(juce::String("fx.eq.") + b[0]));
            band.gain = readParam(params_, id(juce::String("fx.eq.") + b[1]));
            band.q = readParam(params_, id(juce::String("fx.eq.") + b[2]));
            band.type = (int)std::lround(readParam(params_, id(juce::String("fx.eq.") + b[3])));
            band.on = readParam(params_, id(juce::String("fx.eq.") + b[4])) > 0.5f;
            colour_.push_back(band);
        }
    }
    started_ = nowMs();
    progress_ = 0;
    learning_ = true;
}

void AutoEqLearner::poll() {
    if (!learning_ || !source_.tap) return;
    for (;;) {
        uint64_t dropped = 0;
        const int got = source_.tap->pull(cursor_, pending_.data() + filled_, nullptr, kFftSize - filled_, &dropped);
        // A gap breaks the frame; start a new one from here.
        if (dropped > 0) filled_ = 0;
        if (got <= 0) break;
        filled_ += got;
        if (filled_ < kFftSize) continue;
        am::analyserSpectrumDb(pending_.data(), kFftSize, spectrum_.data());
        capture_->add(spectrum_.data(), pending_.data(), kFftSize);
        filled_ = 0;
    }
    progress_ = std::min(1.0, capture_->seconds() / kLearnSeconds);
    if (capture_->seconds() >= kLearnSeconds || nowMs() - started_ > kTimeoutSeconds * 1000) stop();
}

void AutoEqLearner::stop() {
    if (!learning_) return;
    learning_ = false;
    progress_ = 0;
    if (capture_ && capture_->seconds() >= 2) {
        auto measured = capture_->levels();
        levels_ = removeColour_ ? am::removeEq(measured, colour_, sampleRate_) : measured;
        refit();
    }
    capture_.reset();
}

void AutoEqLearner::setReference(const std::string& r) { reference_ = r; refit(); }
void AutoEqLearner::setAmount(double a) { amount_ = a; refit(); }
void AutoEqLearner::refit() {
    if (levels_) fit_ = am::fitEq(*levels_, am::referenceById(reference_), amount_, sampleRate_);
    else fit_.reset();
}

void AutoEqLearner::apply() {
    if (!canApply()) return;
    if (undo_.empty())
        for (auto* k : kEqKeys) undo_[id(k)] = readParam(params_, id(k));
    const char* bands[4][5] = {{"lfreq", "low", "lq", "ltype", "lon"}, {"mfreq", "mid", "mq", "mtype", "mon"},
                               {"m2freq", "mid2", "m2q", "m2type", "m2on"}, {"hfreq", "high", "hq", "htype", "hon"}};
    for (size_t i = 0; i < fit_->bands.size() && i < 4; ++i) {
        const auto& b = fit_->bands[i];
        const auto key = [&](int f) { return id(juce::String("fx.eq.") + bands[i][f]); };
        writeParam(params_, key(3), (float)b.type);
        writeParam(params_, key(0), (float)b.freq);
        writeParam(params_, key(2), (float)b.q);
        writeParam(params_, key(1), (float)b.gain);
        writeParam(params_, key(4), 1);
    }
    writeParam(params_, id("fx.eq.on"), 1);
}

void AutoEqLearner::undo() {
    for (const auto& [k, v] : undo_) writeParam(params_, k, v);
    undo_.clear();
}

juce::String AutoEqLearner::status() const {
    if (!source_.tap) return source_.note;
    if (learning_) return "LISTENING " + juce::String((int)am::jsRound(progress_ * 100)) + "%";
    if (fit_) return toFixed(fit_->change, 1) + " dB OF MOVES";
    return "PLAY, THEN LEARN";
}
juce::String AutoEqLearner::learnLabel() const { return learning_ ? "STOP" : levels_ ? "RELEARN" : "LEARN"; }

// ===================== AutoCompLearner =====================
AutoCompLearner::AutoCompLearner(AutoCompSource source, ParameterSource params, juce::String prefix,
                                 double sampleRate, std::function<std::optional<double>()> tempo)
    : source_(std::move(source)), params_(std::move(params)), prefix_(std::move(prefix)),
      sampleRate_(sampleRate > 0 ? sampleRate : 48000), tempo_(std::move(tempo)), style_(source_.style) {}
AutoCompLearner::~AutoCompLearner() = default;

void AutoCompLearner::learn() {
    if (learning_) { stop(); return; }
    if (!source_.trace) return;
    blocks_.clear();
    traces_.clear();
    pending_.clear();
    audible_ = 0;
    cursor_ = source_.trace->frames();
    started_ = nowMs();
    progress_ = 0;
    learning_ = true;
}

void AutoCompLearner::poll() {
    if (!learning_ || !source_.trace) return;
    // ~33 ms blocks: the RMS of the detector input and its per-sample trace.
    const int n = (int)am::jsRound(sampleRate_ / am::BLOCK_RATE);
    std::vector<float> chunk(4096);
    for (;;) {
        const int got = source_.trace->pull(cursor_, chunk.data(), nullptr, (int)chunk.size());
        if (got <= 0) break;
        pending_.insert(pending_.end(), chunk.begin(), chunk.begin() + got);
    }
    size_t at = 0;
    const double elapsed = (nowMs() - started_) / 1000;
    bool done = false;
    while (!done && pending_.size() - at >= (size_t)n) {
        std::vector<float> trace(pending_.begin() + (long)at, pending_.begin() + (long)at + n);
        at += (size_t)n;
        double s = 0;
        for (float v : trace) s += (double)v * v;
        const double db = 10 * std::log10(std::max(1e-12, s / n));
        blocks_.push_back(db);
        traces_.push_back(std::move(trace));
        if (db > am::SILENCE_DB) ++audible_;
        progress_ = std::min(1.0, audible_ / am::BLOCK_RATE / kLearnSeconds);
        // A sparse part (a single pad) may never fill the window; take what
        // twelve seconds gave once there is at least a second of sound.
        done = audible_ >= kLearnSeconds * am::BLOCK_RATE
            || (elapsed > kSparseSeconds && audible_ >= am::BLOCK_RATE);
    }
    pending_.erase(pending_.begin(), pending_.begin() + (long)at);
    if (done || elapsed > kTimeoutSeconds) stop();
}

void AutoCompLearner::stop() {
    if (!learning_) return;
    learning_ = false;
    progress_ = 0;
    if (blocks_.size() >= (size_t)(am::BLOCK_RATE * 2)) {
        learnedBlocks_ = std::move(blocks_);
        learnedTraces_ = std::move(traces_);
        learned_ = true;
        refit();
    }
    blocks_.clear();
    traces_.clear();
}

void AutoCompLearner::setStyle(const std::string& s) { style_ = s; refit(); }
void AutoCompLearner::setDepth(double d) { depth_ = d; refit(); }
void AutoCompLearner::refit() {
    fit_.reset();
    if (!learned_) return;
    am::FitOptions options;
    if (tempo_) options.bpm = tempo_();
    options.sampleRate = sampleRate_;
    for (const auto& t : learnedTraces_) options.traces.push_back({t.data(), (int)t.size()});
    if (auto fit = am::fitComp(learnedBlocks_, am::compStyleById(style_), depth_, options)) fit_ = *fit;
}

void AutoCompLearner::apply() {
    if (!canApply()) return;
    if (undo_.empty())
        for (auto* k : {"on", "thr", "ratio", "att", "rel"}) undo_[id(k)] = readParam(params_, id(k));
    writeParam(params_, id("thr"), (float)fit_->threshold);
    writeParam(params_, id("ratio"), (float)fit_->ratio);
    writeParam(params_, id("att"), (float)fit_->attack);
    writeParam(params_, id("rel"), (float)fit_->release);
    writeParam(params_, id("on"), 1);
}
void AutoCompLearner::undo() {
    for (const auto& [k, v] : undo_) writeParam(params_, k, v);
    undo_.clear();
}

juce::String AutoCompLearner::status() const {
    if (!source_.trace) return source_.note;
    if (learning_) return "LISTENING " + juce::String((int)am::jsRound(progress_ * 100)) + "%";
    if (!fit_) return "PLAY, THEN LEARN";
    const auto& f = *fit_;
    const auto& style = am::compStyleById(style_);
    juce::String s = toFixed(f.threshold, 1) + juce::String::fromUTF8(" dB · ") + jsNumber(f.ratio)
        + juce::String::fromUTF8(":1 · ") + toFixed(f.attack * 1000, f.attack < 0.001 ? 1 : 0) + "/"
        + juce::String((long long)am::jsRound(f.release * 1000)) + juce::String::fromUTF8(" ms · ≈")
        + toFixed(f.loudGr, 1) + " dB GR";
    if (f.bpm && am::truthy(*f.bpm)) s += juce::String::fromUTF8(" · ") + jsNumber(*f.bpm) + " BPM";
    if (!f.exact) s += juce::String::fromUTF8(" · EST");
    if (f.threshold <= -40 && f.loudGr < style.targetGr * depth_ - 0.5)
        s += juce::String::fromUTF8(" · TOO QUIET FOR THIS STYLE");
    return s;
}
juce::String AutoCompLearner::learnLabel() const { return learning_ ? "STOP" : learned_ ? "RELEARN" : "LEARN"; }

std::vector<float> AutoCompLearner::histogram() const {
    std::vector<float> bins(30, 0.0f);
    if (!fit_) return {};
    for (double v : fit_->levels) bins[(size_t)juce::jlimit(0, 29, (int)std::floor((v + 60) / 2))] += 1;
    const float max = *std::max_element(bins.begin(), bins.end());
    if (max > 0) for (auto& b : bins) b /= max;
    return bins;
}

// ===================== widgets =====================
AmountSlider::AmountSlider(double min, double max, double step) : min_(min), max_(max), step_(step), value_(min) {
    setWantsKeyboardFocus(true);
    setTitle("Amount");
}
void AmountSlider::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    auto text = r.removeFromRight(34);
    const float y = r.getCentreY(), t = (float)((value_ - min_) / (max_ - min_));
    g.setColour(col::line.withAlpha(.5f));
    g.fillRoundedRectangle(r.getX(), y - 1.5f, r.getWidth(), 3, 1.5f);
    g.setColour(col::acN.withAlpha(.85f));
    g.fillRoundedRectangle(r.getX(), y - 1.5f, r.getWidth() * t, 3, 1.5f);
    g.setColour(col::text);
    g.fillEllipse(r.getX() + r.getWidth() * t - 4.5f, y - 4.5f, 9, 9);
    g.setFont(monoFont(8));
    g.drawText(juce::String((int)am::jsRound(value_ * 100)) + "%", text, juce::Justification::centredRight);
}
void AmountSlider::setFromX(float x) {
    const float w = juce::jmax(1.0f, (float)getWidth() - 34);
    double v = min_ + juce::jlimit(0.0f, 1.0f, x / w) * (max_ - min_);
    v = min_ + std::round((v - min_) / step_) * step_;
    v = juce::jlimit(min_, max_, v);
    if (std::abs(v - value_) > 1e-9) {
        value_ = v;
        repaint();
        if (onChange) onChange(value_);
    }
}
void AmountSlider::mouseDown(const juce::MouseEvent& e) { setFromX(e.position.x); }
void AmountSlider::mouseDrag(const juce::MouseEvent& e) { setFromX(e.position.x); }
bool AmountSlider::keyPressed(const juce::KeyPress& k) {
    const int code = k.getKeyCode();
    if (code != juce::KeyPress::leftKey && code != juce::KeyPress::rightKey) return false;
    const double v = juce::jlimit(min_, max_, value_ + (code == juce::KeyPress::rightKey ? step_ : -step_));
    value_ = v;
    repaint();
    if (onChange) onChange(value_);
    return true;
}

void ChipButton::paintButton(juce::Graphics& g, bool over, bool down) {
    auto r = getLocalBounds().toFloat().reduced(.5f);
    const bool on = getToggleState();
    g.setColour(on ? col::acN.withAlpha(.22f) : juce::Colour(0xff0a0d14));
    g.fillRoundedRectangle(r, 5);
    if (progress_ >= 0) {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(r.withWidth(r.getWidth() * (float)progress_).toNearestInt());
        g.setColour(col::acN.withAlpha(.3f));
        g.fillRoundedRectangle(r, 5);
    }
    g.setColour(progress_ >= 0 || on ? col::acN : isEnabled() && (over || down) ? col::acN.withAlpha(.6f) : col::line);
    g.drawRoundedRectangle(r, 5, 1);
    g.setColour(isEnabled() ? (on ? col::text : col::text.withAlpha(.9f)) : col::text.withAlpha(.35f));
    g.setFont(monoFont(8));
    g.drawFittedText(getButtonText(), getLocalBounds().reduced(3, 0), juce::Justification::centred, 1, 0.8f);
}

namespace {
void layoutActions(juce::Rectangle<int> row, juce::Button& learn, juce::Button& apply, juce::Button& undo) {
    const int w = row.getWidth() - 8;
    learn.setBounds(row.removeFromLeft(w * 13 / 33));
    row.removeFromLeft(4);
    apply.setBounds(row.removeFromLeft(w * 10 / 33));
    row.removeFromLeft(4);
    undo.setBounds(row);
}
} // namespace

// ===================== AutoEqStrip =====================
AutoEqStrip::AutoEqStrip(AutoEqSource source, ParameterSource params, juce::String prefix, double sampleRate)
    : learner_(std::move(source), std::move(params), std::move(prefix), sampleRate) {
    setName("Auto-EQ");
    for (size_t i = 0; i < am::EQ_REFERENCES.size(); ++i)
        reference_.addItem(juce::String::fromUTF8(am::EQ_REFERENCES[i].label), (int)i + 1);
    styleBox(reference_);
    reference_.setTitle("Auto-EQ reference");
    reference_.onChange = [this] {
        const int i = reference_.getSelectedId() - 1;
        if (i >= 0 && i < (int)am::EQ_REFERENCES.size()) learner_.setReference(am::EQ_REFERENCES[(size_t)i].id);
        refresh();
    };
    amount_.setValue(learner_.amount());
    amount_.setTitle("Auto-EQ amount");
    amount_.onChange = [this](double v) { learner_.setAmount(v); refresh(); };
    learn_.onClick = [this] { learner_.learn(); if (learner_.learning()) startTimerHz(30); refresh(); };
    apply_.onClick = [this] { learner_.apply(); refresh(); };
    undo_.onClick = [this] { learner_.undo(); refresh(); };
    for (auto* c : std::initializer_list<juce::Component*>{&reference_, &amount_, &learn_, &apply_, &undo_})
        addAndMakeVisible(c);
    refresh();
}
AutoEqStrip::~AutoEqStrip() { stopTimer(); }

void AutoEqStrip::refresh() {
    const auto& ref = am::referenceById(learner_.reference());
    for (size_t i = 0; i < am::EQ_REFERENCES.size(); ++i)
        if (&am::EQ_REFERENCES[i] == &ref) reference_.setSelectedId((int)i + 1, juce::dontSendNotification);
    reference_.setTooltip(juce::String::fromUTF8(ref.hint));
    learn_.setButtonText(learner_.learnLabel());
    learn_.setEnabled(learner_.available());
    learn_.setProgress(learner_.learning() ? learner_.progress() : -1);
    apply_.setEnabled(learner_.canApply());
    undo_.setEnabled(learner_.canUndo());
    if (!learner_.learning()) stopTimer();
    if (onFitChanged) onFitChanged();
    repaint();
}
void AutoEqStrip::timerCallback() {
    learner_.poll();
    refresh();
}
void AutoEqStrip::resized() {
    auto r = getLocalBounds();
    auto row = r.removeFromTop(22);
    reference_.setBounds(row.removeFromLeft(row.getWidth() * 55 / 100));
    row.removeFromLeft(8);
    amount_.setBounds(row);
    r.removeFromTop(6);
    layoutActions(r.removeFromTop(24), learn_, apply_, undo_);
    status_ = r;
}
void AutoEqStrip::paint(juce::Graphics& g) {
    g.setColour(col::acN);
    g.setFont(monoFont(8));
    g.drawFittedText(learner_.status(), status_, juce::Justification::centred, 2);
}

// ===================== AutoCompStrip =====================
AutoCompStrip::AutoCompStrip(AutoCompSource source, ParameterSource params, juce::String prefix, double sampleRate,
                             std::function<std::optional<double>()> tempo)
    : learner_(std::move(source), std::move(params), std::move(prefix), sampleRate, std::move(tempo)) {
    setName("Auto-comp");
    for (size_t i = 0; i < am::COMP_STYLES.size(); ++i) style_.addItem(juce::String::fromUTF8(am::COMP_STYLES[i].label), (int)i + 1);
    styleBox(style_);
    style_.setTitle("Auto-comp style");
    style_.onChange = [this] {
        const int i = style_.getSelectedId() - 1;
        if (i >= 0 && i < (int)am::COMP_STYLES.size()) learner_.setStyle(am::COMP_STYLES[(size_t)i].id);
        refresh();
    };
    depth_.setValue(learner_.depth());
    depth_.setTitle("Auto-comp depth");
    depth_.onChange = [this](double v) { learner_.setDepth(v); refresh(); };
    learn_.onClick = [this] { learner_.learn(); if (learner_.learning()) startTimerHz(30); refresh(); };
    apply_.onClick = [this] { learner_.apply(); refresh(); };
    undo_.onClick = [this] { learner_.undo(); refresh(); };
    for (auto* c : std::initializer_list<juce::Component*>{&style_, &depth_, &learn_, &apply_, &undo_})
        addAndMakeVisible(c);
    refresh();
}
AutoCompStrip::~AutoCompStrip() { stopTimer(); }

void AutoCompStrip::refresh() {
    const auto& style = am::compStyleById(learner_.style());
    for (size_t i = 0; i < am::COMP_STYLES.size(); ++i)
        if (&am::COMP_STYLES[i] == &style) style_.setSelectedId((int)i + 1, juce::dontSendNotification);
    style_.setTooltip(juce::String::fromUTF8(style.hint));
    learn_.setButtonText(learner_.learnLabel());
    learn_.setEnabled(learner_.available());
    learn_.setProgress(learner_.learning() ? learner_.progress() : -1);
    apply_.setEnabled(learner_.canApply());
    undo_.setEnabled(learner_.canUndo());
    if (!learner_.learning()) stopTimer();
    repaint();
}
void AutoCompStrip::timerCallback() {
    learner_.poll();
    refresh();
}
void AutoCompStrip::resized() {
    auto r = getLocalBounds();
    auto row = r.removeFromTop(22);
    style_.setBounds(row.removeFromLeft(row.getWidth() * 55 / 100));
    row.removeFromLeft(8);
    depth_.setBounds(row);
    r.removeFromTop(5);
    hist_ = r.removeFromTop(juce::jmin(34, juce::jmax(0, r.getHeight() - 54)));
    r.removeFromTop(5);
    layoutActions(r.removeFromTop(24), learn_, apply_, undo_);
    status_ = r;
}
void AutoCompStrip::paint(juce::Graphics& g) {
    // Histogram of the blocks' mean detector level with the fitted threshold.
    if (const auto* fit = learner_.fit(); fit && !hist_.isEmpty()) {
        const auto bins = learner_.histogram();
        const auto h = hist_.toFloat();
        const float bw = h.getWidth() / 30;
        for (size_t i = 0; i < bins.size(); ++i) {
            const bool over = (double)i * 2 - 60 >= fit->threshold;
            g.setColour(over ? col::acN.withAlpha(.85f) : col::acN.withAlpha(.3f));
            const float bh = bins[i] * (h.getHeight() - 4);
            g.fillRect(h.getX() + (float)i * bw + bw * .1f, h.getBottom() - 2 - bh, bw * .8f, bh);
        }
        const float x = h.getX() + (float)((fit->threshold + 60) / 60) * h.getWidth();
        g.setColour(col::text);
        g.drawLine(x, h.getY(), x, h.getBottom(), 1);
    }
    g.setColour(col::acN);
    g.setFont(monoFont(8));
    g.drawFittedText(learner_.status(), status_, juce::Justification::centred, 2);
}

// ===================== SideChainRow =====================
SideChainRow::SideChainRow(ParameterSource params, juce::String prefix, SideChainInfo info)
    : params_(std::move(params)), prefix_(std::move(prefix)), info_(std::move(info)) {
    setName("Side-chain");
    styleBox(menu_);
    menu_.setTitle("Compressor side-chain source");
    menu_.onChange = [this] {
        writeParam(params_, prefix_ + "fx.comp.sc", (float)(menu_.getSelectedId() - 1));
        refresh();
    };
    addAndMakeVisible(menu_);
    refresh();
}
SideChainRow::~SideChainRow() = default;

int SideChainRow::value() const { return (int)std::lround(readParam(params_, prefix_ + "fx.comp.sc")); }

void SideChainRow::refresh() {
    const int v = value();
    menu_.clear(juce::dontSendNotification);
    for (int i = 0; i < (int)info_.sources.size(); ++i) {
        menu_.addItem(info_.label(i), i + 1);
        // Track keys need the SQ-4 key bus.
        menu_.setItemEnabled(i + 1, i == v || info_.hosted || !info_.external(i));
    }
    menu_.setSelectedId(v + 1, juce::dontSendNotification);
    const bool changed = v != shown_;
    shown_ = v;
    repaint();
    if (changed && onValueChanged) onValueChanged(v);
}
void SideChainRow::resized() {
    auto r = getLocalBounds();
    label_ = r.removeFromLeft(62);
    auto menu = r.removeFromLeft(juce::jmin(130, r.getWidth() / 2));
    menu_.setBounds(menu.withSizeKeepingCentre(menu.getWidth(), juce::jmin(22, menu.getHeight())));
    r.removeFromLeft(8);
    note_ = r;
}
void SideChainRow::paint(juce::Graphics& g) {
    const int v = value();
    g.setFont(monoFont(8));
    g.setColour(v > 0 ? col::text : col::acN);
    g.drawText("SIDECHAIN", label_, juce::Justification::centredLeft);
    if (v > 0 && !info_.hosted && info_.external(v)) {
        g.setColour(col::acB);
        g.drawFittedText(juce::String::fromUTF8("SQ-4 ONLY — NO KEY HERE"), note_, juce::Justification::centredLeft, 2);
    }
}

} // namespace fui
