#include "KitMixPanel.h"
#include "../../ui/Theme.h"
#include "../dsp/DrumParams.h"

#include <cmath>
#include <numeric>

namespace fui {
namespace am = fable::automix;

SideChainInfo drumSideChain(const DrumUiModel& model, bool hosted, const std::vector<std::string>& trackNames) {
    SideChainInfo info { fable::DRUM_SC_SOURCES, hosted, {} };
    for (int i = 0; i < 16; ++i) {
        const auto n = juce::String(i + 1).paddedLeft('0', 2);
        info.labels[("PAD " + n).toStdString()] = n + " " + model.padName(i);
    }
    if (hosted)
        for (const auto& [key, label] : trackKeyLabels(trackNames)) info.labels[key] = label;
    return info;
}

// ===================== KitMixLearner =====================
KitMixLearner::KitMixLearner(DrumUiModel& model) : model_(model), params_(model.parameters()) {}
KitMixLearner::~KitMixLearner() {
    if (learning_) model_.setPadCapture(false);
}

void KitMixLearner::learn() {
    if (learning_) { stop(); return; }
    if (!model_.padCapture(0)) return;
    capture_ = std::make_unique<am::KitCapture>(model_.analysisSampleRate(), kPads);
    lvl_.clear();
    for (int i = 0; i < kPads; ++i) lvl_.push_back(readParam(params_, lvl(i)));
    model_.setPadCapture(true);
    for (int i = 0; i < kPads; ++i) {
        cursors_[(size_t)i] = model_.padCapture(i)->frames();
        pending_[(size_t)i].clear();
        blocks_[(size_t)i] = 0;
    }
    started_ = juce::Time::getMillisecondCounterHiRes();
    progress_ = 0;
    learning_ = true;
}

void KitMixLearner::poll() {
    if (!learning_) return;
    std::vector<float> chunk(kBlock);
    for (int i = 0; i < kPads; ++i) {
        const auto* tap = model_.padCapture(i);
        auto& pending = pending_[(size_t)i];
        for (;;) {
            const int got = tap->pull(cursors_[(size_t)i], chunk.data(), nullptr, kBlock);
            if (got <= 0) break;
            pending.insert(pending.end(), chunk.begin(), chunk.begin() + got);
        }
        // Each pad posts kBlock-frame blocks with the frame count since capture began.
        size_t at = 0;
        while (pending.size() - at >= (size_t)kBlock) {
            const int frames = ++blocks_[(size_t)i] * kBlock - 1;
            capture_->add(i, pending.data() + at, kBlock, frames);
            at += (size_t)kBlock;
        }
        pending.erase(pending.begin(), pending.begin() + (long)at);
    }
    progress_ = std::min(1.0, capture_->seconds() / kLearnSeconds);
    if (capture_->seconds() >= kLearnSeconds
        || juce::Time::getMillisecondCounterHiRes() - started_ > kTimeoutSeconds * 1000)
        stop();
}

void KitMixLearner::stop() {
    if (!learning_) return;
    learning_ = false;
    progress_ = 0;
    model_.setPadCapture(false);
    if (capture_ && capture_->seconds() >= 2) {
        bands_ = capture_->meanBands();
        learnedLvl_ = lvl_;
        learned_ = true;
        refit();
    }
    capture_.reset();
}

void KitMixLearner::setReference(const std::string& r) { reference_ = r; refit(); }
void KitMixLearner::setAmount(double a) { amount_ = a; refit(); }
void KitMixLearner::refit() {
    fit_.reset();
    if (!learned_) return;
    if (auto fit = am::fitKitMix(bands_, learnedLvl_, am::referenceById(reference_), amount_)) fit_ = *fit;
}

void KitMixLearner::apply() {
    if (!canApply()) return;
    if (undo_.empty())
        for (int i = 0; i < kPads; ++i) undo_.push_back(readParam(params_, lvl(i)));
    // The fit is relative to the levels heard while learning.
    for (int i = 0; i < kPads; ++i) {
        const auto& level = fit_->levels[(size_t)i];
        writeParam(params_, lvl(i), (float)(level ? *level : learnedLvl_[(size_t)i]));
    }
}
void KitMixLearner::undo() {
    for (size_t i = 0; i < undo_.size(); ++i) writeParam(params_, lvl((int)i), undo_[i]);
    undo_.clear();
}

juce::String KitMixLearner::status() const {
    if (learning_) return "LISTENING " + juce::String((int)am::jsRound(progress_ * 100)) + "%";
    if (fit_) return "ERROR " + toFixed(fit_->errorBefore, 1) + juce::String::fromUTF8(" → ")
                     + toFixed(fit_->errorAfter, 1) + " dB";
    return learned_ ? "NEEDS TWO SOUNDING PADS" : "PLAY THE KIT, THEN LEARN";
}
juce::String KitMixLearner::learnLabel() const { return learning_ ? "STOP" : learned_ ? "RELEARN" : "LEARN"; }

// ===================== KitMixPanel =====================
KitMixPanel::KitMixPanel(DrumUiModel& model)
    : model_(model), learner_(model),
      liveTick_(*this, LiveSpectrum::kHz, [this] { updateLiveSpectrum(juce::Time::getMillisecondCounterHiRes()); },
                [this] { live_.clear(); }) {
    setName("KIT MIX");
    for (size_t i = 0; i < am::EQ_REFERENCES.size(); ++i) reference_.addItem(juce::String::fromUTF8(am::EQ_REFERENCES[i].label), (int)i + 1);
    reference_.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0a0d14));
    reference_.setTitle("Kit mix reference");
    reference_.onChange = [this] {
        const int i = reference_.getSelectedId() - 1;
        if (i >= 0 && i < (int)am::EQ_REFERENCES.size()) learner_.setReference(am::EQ_REFERENCES[(size_t)i].id);
        refresh();
    };
    amount_.setValue(learner_.amount());
    amount_.setTitle("Kit mix amount");
    amount_.onChange = [this](double v) { learner_.setAmount(v); refresh(); };
    learn_.onClick = [this] { learner_.learn(); if (learner_.learning()) startTimerHz(30); refresh(); };
    apply_.onClick = [this] { learner_.apply(); refresh(); };
    undo_.onClick = [this] { learner_.undo(); refresh(); };
    for (auto* c : std::initializer_list<juce::Component*>{&reference_, &amount_, &learn_, &apply_, &undo_})
        addAndMakeVisible(c);
    refresh();
}
KitMixPanel::~KitMixPanel() { stopTimer(); }

void KitMixPanel::refresh() {
    const auto& ref = am::referenceById(learner_.reference());
    for (size_t i = 0; i < am::EQ_REFERENCES.size(); ++i)
        if (&am::EQ_REFERENCES[i] == &ref) reference_.setSelectedId((int)i + 1, juce::dontSendNotification);
    reference_.setTooltip(juce::String::fromUTF8(ref.hint));
    learn_.setButtonText(learner_.learnLabel());
    learn_.setEnabled(model_.padCapture(0) != nullptr);
    learn_.setProgress(learner_.learning() ? learner_.progress() : -1);
    apply_.setEnabled(learner_.canApply());
    undo_.setEnabled(learner_.canUndo());
    if (!learner_.learning()) stopTimer();
    repaint();
}
void KitMixPanel::timerCallback() {
    learner_.poll();
    refresh();
}

// The kit as it plays now (the kit output).
void KitMixPanel::updateLiveSpectrum(double nowMs) {
    if (live_.update(model_.liveSpectrumTap(-1), model_.analysisSampleRate(), nowMs)) repaint(plot_);
}

void KitMixPanel::resized() {
    auto r = getLocalBounds().reduced(12, 10);
    head_ = r.removeFromTop(24);
    r.removeFromTop(6);
    auto side = r.removeFromLeft(170);
    reference_.setBounds(side.removeFromTop(24));
    side.removeFromTop(8);
    amount_.setBounds(side.removeFromTop(20));
    side.removeFromTop(8);
    auto actions = side.removeFromTop(26);
    const int w = actions.getWidth() - 8;
    learn_.setBounds(actions.removeFromLeft(w * 13 / 33));
    actions.removeFromLeft(4);
    apply_.setBounds(actions.removeFromLeft(w * 10 / 33));
    actions.removeFromLeft(4);
    undo_.setBounds(actions);
    r.removeFromLeft(10);
    const int plotW = (r.getWidth() - 10) * 14 / 24;
    plot_ = r.removeFromLeft(plotW);
    r.removeFromLeft(10);
    pads_ = r;
}

void KitMixPanel::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat(), 9);
    g.setColour(col::text);
    g.setFont(dispFont(12));
    g.drawText("KIT MIX", head_.withWidth(90), juce::Justification::centredLeft);
    g.setColour(col::textDim);
    g.setFont(monoFont(8));
    g.drawText(learner_.status(), head_.withTrimmedLeft(96), juce::Justification::centredLeft);

    // Plot: target band, kit before and predicted after.
    drawDisplayBox(g, plot_.toFloat(), 8);
    const auto p = plot_.toFloat().reduced(6, 4);
    const float L = p.getX() + 16, R = p.getRight() - 4, T = p.getY() + 4, B = p.getBottom() - 12;
    const auto X = [&](double f) { return L + (float)(std::log(f / 25) / std::log(20000.0 / 25)) * (R - L); };
    const auto Y = [&](double d) { return T + (float)((18 - juce::jlimit(-30.0, 18.0, d)) / 48) * (B - T); };
    g.setFont(monoFont(7));
    for (double f : {100.0, 1000.0, 10000.0}) {
        g.setColour(col::textDim.withAlpha(.15f));
        g.drawLine(X(f), T, X(f), B, .8f);
        g.setColour(col::textDim);
        g.drawText(f >= 1000 ? juce::String((int)(f / 1000)) + "k" : juce::String((int)f),
                   juce::Rectangle<float>(X(f) - 20, B + 1, 40, 10), juce::Justification::centred);
    }
    const auto* fit = learner_.fit();
    const auto& ref = am::referenceById(learner_.reference());
    std::vector<double> target;
    if (fit) target = fit->target;
    else for (double f : am::BAND_HZ) target.push_back(am::referenceDb(ref, f));
    const auto line = [&](const std::vector<double>& ys) {
        juce::Path path;
        for (size_t i = 0; i < ys.size() && i < am::NUM_BANDS; ++i) {
            const float x = X(am::BAND_HZ[i]), y = Y(ys[i]);
            if (i) path.lineTo(x, y); else path.startNewSubPath(x, y);
        }
        return path;
    };
    // The kit as it plays now, aligned to the reference like the learned curves.
    if (live_.active()) {
        auto levels = live_.levels();
        const double offset = am::alignToReference(levels, ref).offset;
        for (auto& v : levels) v -= offset;
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(p.toNearestInt());
        LiveSpectrum::draw(g, LiveSpectrum::path(levels, X, Y, B), col::text, .09f, .35f);
    }
    juce::Path band;
    for (size_t i = 0; i < am::NUM_BANDS; ++i) {
        const float x = X(am::BAND_HZ[i]), y = Y(target[i] + 3);
        if (i) band.lineTo(x, y); else band.startNewSubPath(x, y);
    }
    for (size_t i = am::NUM_BANDS; i-- > 0;) band.lineTo(X(am::BAND_HZ[i]), Y(target[i] - 3));
    band.closeSubPath();
    g.setColour(col::acN.withAlpha(.1f));
    g.fillPath(band);
    {
        juce::Path dashed;
        const float dash[] = {2, 3};
        juce::PathStrokeType(1).createDashedStroke(dashed, band, dash, 2);
        g.setColour(col::acN.withAlpha(.35f));
        g.fillPath(dashed);
    }
    if (fit) {
        g.setColour(col::text);
        g.strokePath(line(fit->before), juce::PathStrokeType(1.3f));
        juce::Path dashed;
        const float dash[] = {4, 3};
        juce::PathStrokeType(1.3f).createDashedStroke(dashed, line(fit->after), dash, 2);
        g.setColour(col::acA);
        g.fillPath(dashed);
    }

    // Pad level moves, sorted by energy share.
    g.setColour(juce::Colour(0xff0a0d14));
    g.fillRoundedRectangle(pads_.toFloat(), 8);
    g.setColour(col::line);
    g.drawRoundedRectangle(pads_.toFloat().reduced(.5f), 8, 1);
    if (!fit) return;
    std::vector<int> rows;
    for (int i = 0; i < KitMixLearner::kPads; ++i)
        if (fit->heard[(size_t)i]) rows.push_back(i);
    std::stable_sort(rows.begin(), rows.end(), [fit](int a, int b) { return fit->share[(size_t)a] > fit->share[(size_t)b]; });
    auto list = pads_.reduced(8, 5);
    g.setFont(monoFont(8));
    for (int i : rows) {
        if (list.getHeight() < 14) break;
        auto row = list.removeFromTop(16);
        const double move = fit->moves[(size_t)i];
        g.setColour(col::textDim);
        g.drawText(juce::String(i + 1).paddedLeft('0', 2) + " " + model_.padName(i), row.withTrimmedRight(50),
                   juce::Justification::centredLeft);
        g.setColour(am::truthy(move) ? col::acA : col::textDim);
        g.drawText(am::truthy(move) ? (move > 0 ? "+" : "") + toFixed(move, 1) : juce::String("HOLD"), row,
                   juce::Justification::centredRight);
        g.setColour(col::textDim.withAlpha(.5f));
        g.fillRect((float)row.getX(), (float)row.getBottom() - 1,
                   (float)row.getWidth() * (float)fit->share[(size_t)i], 1.0f);
    }
}

} // namespace fui
