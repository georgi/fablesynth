#include "MixAssistPanel.h"
#include "../SeqProcessor.h"
#include "../../ui/Theme.h"

#include <cmath>

namespace fui {
namespace am = fable::automix;

namespace {
constexpr int kTracks = 4;
double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
juce::String fmtLufs(double v) { return std::isfinite(v) ? toFixed(v, 1) : juce::String::fromUTF8("—"); }
am::Machine machineOf(fable::Machine m) {
    return m == fable::Machine::DR1 ? am::Machine::DR1 : m == fable::Machine::BL1 ? am::Machine::BL1 : am::Machine::WT1;
}
const struct { int value; const char* label; } kTargets[] = {{-14, "STREAM −14"}, {-11, "−11"}, {-9, "CLUB −9"}};
} // namespace

// ===================== MixAssistSession =====================
MixAssistSession::MixAssistSession(SeqAudioProcessor& proc) : proc_(proc), roles_((size_t)kTracks) {}

bool MixAssistSession::playing() const { return proc_.preparedSampleRate() > 0 && proc_.conductor().playing(); }

MixAssistSession::Snapshot MixAssistSession::current() const {
    Snapshot s;
    for (const auto& info : fable::masterFxParamInfo()) {
        const auto id = "master." + juce::String(info.pid);
        if (auto* p = proc_.apvts.getParameter(id)) s.masterFx[id.toStdString()] = p->convertFrom0to1(p->getValue());
    }
    for (int t = 0; t < kTracks; ++t)
        if (auto* p = proc_.apvts.getParameter("vol" + juce::String(t))) s.trackVol.push_back(p->convertFrom0to1(p->getValue()));
    if (auto* p = proc_.apvts.getParameter("master")) s.masterVol = p->convertFrom0to1(p->getValue());
    return s;
}

void MixAssistSession::write(const juce::String& id, float value) {
    if (auto* p = proc_.apvts.getParameter(id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, p->convertTo0to1(value)));
        p->endChangeGesture();
    }
}

void MixAssistSession::listen() {
    if (listening()) { stop(true); return; }
    const double sr = proc_.preparedSampleRate() > 0 ? proc_.preparedSampleRate() : 48000;
    listener_ = std::make_unique<am::MixListener>(sr, kFftSize, kTracks);
    // Taps: pre-FX sum, post-FX output, then each track.
    const fable::ProgramTap* taps[] = {&proc_.masterInputTap(), &proc_.masterOutputTap(), &proc_.trackTap(0),
                                       &proc_.trackTap(1), &proc_.trackTap(2), &proc_.trackTap(3)};
    cursors_.clear();
    pending_.assign(6, {});
    // All taps advance in the same render call; start them on one frame.
    const uint64_t start = taps[0]->frames();
    for (auto* tap : taps) cursors_.push_back(std::min(start, tap->frames()));
    base_ = current();
    started_ = nowMs();
    progress_ = 0;
}

void MixAssistSession::poll() {
    if (!listening()) return;
    const fable::ProgramTap* taps[] = {&proc_.masterInputTap(), &proc_.masterOutputTap(), &proc_.trackTap(0),
                                       &proc_.trackTap(1), &proc_.trackTap(2), &proc_.trackTap(3)};
    std::vector<float> chunk(4096);
    for (size_t i = 0; i < 6; ++i) {
        uint64_t dropped = 0;
        for (;;) {
            const int got = taps[i]->pull(cursors_[i], chunk.data(), nullptr, (int)chunk.size(), &dropped);
            if (got <= 0) break;
            pending_[i].insert(pending_[i].end(), chunk.begin(), chunk.begin() + got);
        }
    }
    auto& l = *listener_;
    std::vector<float> spec(kFftSize / 2), trackSpec((size_t)kTracks * kFftSize / 2);
    for (;;) {
        bool ready = true;
        for (const auto& p : pending_) ready &= p.size() >= (size_t)kFftSize;
        if (!ready) break;
        am::analyserSpectrumDb(pending_[0].data(), kFftSize, spec.data());
        std::vector<am::TrackFrame> frames;
        for (int t = 0; t < kTracks; ++t) {
            float* s = trackSpec.data() + (size_t)t * kFftSize / 2;
            am::analyserSpectrumDb(pending_[(size_t)t + 2].data(), kFftSize, s);
            frames.push_back({{pending_[(size_t)t + 2].data(), kFftSize}, {s, kFftSize / 2}});
        }
        l.add({spec.data(), kFftSize / 2}, {pending_[0].data(), kFftSize}, {pending_[1].data(), kFftSize}, &frames);
        for (auto& p : pending_) p.erase(p.begin(), p.begin() + kFftSize);
    }
    const double heard = l.frames * (double)kFftSize / l.sampleRate;
    progress_ = std::min(1.0, heard / kListenSeconds);
    if (heard >= kListenSeconds || nowMs() - started_ > kTimeoutSeconds * 1000) stop(true);
}

void MixAssistSession::stop(bool finish) {
    if (!listening()) return;
    const auto& l = *listener_;
    if (finish && l.frames * (double)l.fftSize / l.sampleRate >= kMinSeconds) analysis_ = am::analyse(l);
    listener_.reset();
    pending_.clear();
    progress_ = 0;
    rebuildPlan();
}

void MixAssistSession::setReference(const std::string& r) { reference_ = r; rebuildPlan(); }
void MixAssistSession::setLoudness(int v) { loudness_ = v; rebuildPlan(); }
void MixAssistSession::setAmount(double a) { amount_ = a; rebuildPlan(); }
void MixAssistSession::setRole(int track, std::optional<am::MixRole> r) {
    if (track >= 0 && track < kTracks) roles_[(size_t)track] = r;
    rebuildPlan();
}
std::optional<am::MixRole> MixAssistSession::role(int track) const {
    return track >= 0 && track < kTracks ? roles_[(size_t)track] : std::nullopt;
}

// Plan against the state the analysis heard, so the preview does not chase
// its own changes after they land.
void MixAssistSession::rebuildPlan() {
    plan_.reset();
    if (!analysis_ || !base_) return;
    am::PlanInput in;
    in.analysis = *analysis_;
    in.reference = reference_;
    in.loudness = loudness_;
    in.amount = amount_;
    in.bpm = proc_.conductor().session().bpm;
    in.sampleRate = proc_.preparedSampleRate() > 0 ? proc_.preparedSampleRate() : 48000;
    in.masterFx = base_->masterFx;
    in.masterVol = base_->masterVol;
    in.trackVol = base_->trackVol;
    for (const auto& t : proc_.conductor().session().tracks) in.tracks.push_back({machineOf(t.machine), t.name});
    in.roles = roles_;
    in.roles.resize(in.tracks.size());
    plan_ = am::buildPlan(in);
}

void MixAssistSession::apply() {
    if (!canApply() || !base_) return;
    if (!snapshot_) snapshot_ = current();
    // Re-applying after a target change starts from the same baseline.
    for (const auto& [id, v] : base_->masterFx) {
        auto it = plan_->masterFx.find(id);
        write(id, it != plan_->masterFx.end() ? it->second : v);
    }
    for (size_t t = 0; t < base_->trackVol.size(); ++t) {
        const auto planned = t < plan_->trackVol.size() ? plan_->trackVol[t] : std::nullopt;
        write("vol" + juce::String((int)t), planned ? *planned : (float)base_->trackVol[t]);
    }
    write("master", plan_->masterVol ? *plan_->masterVol : (float)base_->masterVol);
}

void MixAssistSession::revert() {
    if (!snapshot_) return;
    for (const auto& [id, v] : snapshot_->masterFx) write(id, v);
    for (size_t t = 0; t < snapshot_->trackVol.size(); ++t) write("vol" + juce::String((int)t), (float)snapshot_->trackVol[t]);
    write("master", (float)snapshot_->masterVol);
    snapshot_.reset();
}

juce::String MixAssistSession::caption() const {
    if (listening()) return "LISTENING " + juce::String((int)am::jsRound(progress_ * 100)) + "%";
    if (snapshot_) return "APPLIED";
    if (analysis_) return toFixed(analysis_->seconds, 0) + " s ANALYSED";
    return playing() ? "READY" : "START PLAYBACK";
}

// ===================== MixAssistPanel =====================
MixAssistPanel::MixAssistPanel(SeqAudioProcessor& proc) : proc_(proc), session_(proc) {
    setName("ASSIST");
    for (size_t i = 0; i < am::EQ_REFERENCES.size(); ++i) reference_.addItem(juce::String::fromUTF8(am::EQ_REFERENCES[i].label), (int)i + 1);
    reference_.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0a0d14));
    reference_.setTitle("Tonal reference");
    reference_.onChange = [this] {
        const int i = reference_.getSelectedId() - 1;
        if (i >= 0 && i < (int)am::EQ_REFERENCES.size()) session_.setReference(am::EQ_REFERENCES[(size_t)i].id);
        live_.clear();
        refresh();
    };
    addAndMakeVisible(reference_);
    for (size_t i = 0; i < targets_.size(); ++i) {
        auto& b = targets_[i];
        b.setButtonText(juce::String::fromUTF8(kTargets[i].label));
        b.setTitle("Loudness target " + juce::String(kTargets[i].value) + " LUFS");
        b.setColour(juce::TextButton::buttonOnColourId, col::acN.withAlpha(.22f));
        b.setColour(juce::TextButton::textColourOnId, col::text);
        b.setColour(juce::TextButton::textColourOffId, col::textDim);
        const int value = kTargets[i].value;
        b.onClick = [this, value] { session_.setLoudness(value); refresh(); };
        addAndMakeVisible(b);
    }
    amount_.setValue(session_.amount());
    amount_.setTitle("Assistant amount");
    amount_.onChange = [this](double v) { session_.setAmount(v); refresh(); };
    listen_.onClick = [this] { session_.listen(); refresh(); };
    apply_.onClick = [this] { session_.apply(); refresh(); };
    revert_.onClick = [this] { session_.revert(); refresh(); };
    for (auto* c : std::initializer_list<juce::Component*>{&amount_, &listen_, &apply_, &revert_}) addAndMakeVisible(c);
    refresh();
    startVisualTimer(*this, 30);
}
MixAssistPanel::~MixAssistPanel() { stopTimer(); }

void MixAssistPanel::rebuildRoleMenus() {
    const auto* plan = session_.plan();
    std::vector<int> tracks;
    if (plan)
        for (const auto& l : plan->levels) tracks.push_back(l.index);
    if (tracks != roleTracks_) {
        roles_.clear();
        roleTracks_ = tracks;
        for (size_t row = 0; row < tracks.size(); ++row) {
            auto* box = roles_.add(new juce::ComboBox());
            box->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0a0d14));
            box->setTitle(juce::String(plan->levels[row].name) + " role");
            const int track = tracks[row];
            box->onChange = [this, box, track] {
                const int id = box->getSelectedId();
                session_.setRole(track, id <= 1 ? std::nullopt : std::optional<am::MixRole>(am::MIX_ROLES[(size_t)id - 2].id));
                refresh();
            };
            addAndMakeVisible(box);
        }
        resized();
    }
    if (!plan) return;
    for (size_t row = 0; row < plan->levels.size() && row < (size_t)roles_.size(); ++row) {
        const auto& l = plan->levels[row];
        auto* box = roles_[(int)row];
        box->clear(juce::dontSendNotification);
        box->addItem(juce::String::fromUTF8("AUTO · ") + juce::String(am::roleInfo(l.detected).key).toUpperCase(), 1);
        for (size_t r = 0; r < am::MIX_ROLES.size(); ++r) box->addItem(juce::String::fromUTF8(am::MIX_ROLES[r].label), (int)r + 2);
        int selected = 1;
        if (auto role = session_.role(l.index))
            for (size_t r = 0; r < am::MIX_ROLES.size(); ++r)
                if (am::MIX_ROLES[r].id == *role) selected = (int)r + 2;
        box->setSelectedId(selected, juce::dontSendNotification);
    }
}

void MixAssistPanel::refresh() {
    const auto& ref = am::referenceById(session_.reference());
    for (size_t i = 0; i < am::EQ_REFERENCES.size(); ++i)
        if (&am::EQ_REFERENCES[i] == &ref) reference_.setSelectedId((int)i + 1, juce::dontSendNotification);
    reference_.setTooltip(juce::String::fromUTF8(ref.hint));
    for (size_t i = 0; i < targets_.size(); ++i)
        targets_[i].setToggleState(session_.loudness() == kTargets[i].value, juce::dontSendNotification);
    const bool listening = session_.listening();
    listen_.setButtonText(listening ? "STOP" : session_.analysis() ? "RE-LISTEN" : "LISTEN");
    listen_.setEnabled(listening || session_.playing());
    listen_.setProgress(listening ? session_.progress() : -1);
    apply_.setButtonText(session_.applied() ? "RE-APPLY" : "APPLY");
    apply_.setEnabled(session_.canApply());
    revert_.setEnabled(session_.applied());
    rebuildRoleMenus();
    repaint();
}

void MixAssistPanel::timerCallback() {
    const bool wasListening = session_.listening();
    session_.poll();
    updateLive(nowMs());
    if (wasListening || listen_.isEnabled() != session_.playing()) refresh();
    else repaint(plot_);
}

// Live post-FX spectrum: third-octave bands with ~250 ms smoothing, aligned
// to the reference like the measured curve, cleared on silence.
void MixAssistPanel::updateLive(double now) {
    if (!session_.playing()) { live_.clear(); return; }
    live_.update(&proc_.masterOutputTap(), proc_.preparedSampleRate(), now);
}

// Left: plot and controls over the LEVELS table. Right: the moves list.
void MixAssistPanel::resized() {
    auto r = getLocalBounds().reduced(12, 8);
    head_ = r.removeFromTop(22);
    r.removeFromTop(4);
    auto left = r.removeFromLeft(juce::jmin(520, r.getWidth() * 55 / 100));
    r.removeFromLeft(12);
    moves_ = r;
    levels_ = left.removeFromBottom(4 * kLevelRow);
    left.removeFromBottom(6);
    auto side = left.removeFromRight(170);
    left.removeFromRight(12);
    legend_ = left.removeFromBottom(14);
    plot_ = left;
    meters_ = side.removeFromTop(38);
    side.removeFromTop(4);
    reference_.setBounds(side.removeFromTop(20));
    side.removeFromTop(4);
    auto seg = side.removeFromTop(20);
    const int segW = (seg.getWidth() - 6) / 3;
    for (size_t i = 0; i < targets_.size(); ++i) {
        targets_[i].setBounds(i < 2 ? seg.removeFromLeft(segW) : seg);
        if (i < 2) seg.removeFromLeft(3);
    }
    side.removeFromTop(4);
    amount_.setBounds(side.removeFromTop(16).withTrimmedLeft(50));
    side.removeFromTop(4);
    auto actions = side.removeFromTop(22);
    const int w = actions.getWidth() - 8;
    listen_.setBounds(actions.removeFromLeft(w * 13 / 33));
    actions.removeFromLeft(4);
    apply_.setBounds(actions.removeFromLeft(w * 10 / 33));
    actions.removeFromLeft(4);
    revert_.setBounds(actions);
    auto rows = levels_;
    for (auto* box : roles_)
        box->setBounds(rows.removeFromTop(kLevelRow).withTrimmedLeft(kLevelName).withWidth(kLevelRole).reduced(0, 1));
}

void MixAssistPanel::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat(), 9);
    const auto& ref = am::referenceById(session_.reference());
    g.setColour(col::text);
    g.setFont(dispFont(12));
    g.drawText("ASSIST", head_.withWidth(80), juce::Justification::centredLeft);
    g.setFont(monoFont(8));
    g.setColour(col::acN);
    g.drawText(session_.caption(), head_.withTrimmedLeft(86).withWidth(160), juce::Justification::centredLeft);
    g.drawText(juce::String::fromUTF8("PRE-FX ANALYSIS · ") + juce::String::fromUTF8(ref.label), head_, juce::Justification::centredRight);

    // ---- plot: target ±3 dB, live output, measured, after EQ ----
    drawDisplayBox(g, plot_.toFloat());
    const auto box = plot_.toFloat();
    const float L = box.getX() + 26, R = box.getRight() - 8, T = box.getY() + 8, B = box.getBottom() - 16;
    const auto X = [&](double f) { return L + (float)(std::log(f / 25) / std::log(20000.0 / 25)) * (R - L); };
    const auto Y = [&](double d) { return T + (float)((18 - juce::jlimit(-30.0, 18.0, d)) / 48) * (B - T); };
    g.setFont(monoFont(7));
    for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0}) {
        g.setColour(col::acN.withAlpha(.08f));
        g.drawLine(X(f), T, X(f), B, .8f);
        g.setColour(col::acN);
        g.drawText(f >= 1000 ? juce::String((int)(f / 1000)) + "k" : juce::String((int)f),
                   juce::Rectangle<float>(X(f) - 18, B + 3, 36, 10), juce::Justification::centred);
    }
    for (double d : {12.0, 0.0, -12.0, -24.0}) {
        g.setColour(col::acN.withAlpha(std::abs(d) < 0.5 ? .2f : .08f));
        g.drawLine(L, Y(d), R, Y(d), .8f);
        g.setColour(col::acN);
        g.drawText(juce::String((int)d), juce::Rectangle<float>(L - 26, Y(d) - 5, 22, 10), juce::Justification::centredRight);
    }
    std::vector<double> target;
    for (double f : am::BAND_HZ) target.push_back(am::referenceDb(ref, f));
    const auto line = [&](const std::vector<double>& ys) {
        juce::Path p;
        for (size_t i = 0; i < ys.size(); ++i) {
            if (i) p.lineTo(X(am::BAND_HZ[i]), Y(ys[i])); else p.startNewSubPath(X(am::BAND_HZ[i]), Y(ys[i]));
        }
        return p;
    };
    juce::Path band;
    for (size_t i = 0; i < target.size(); ++i) {
        if (i) band.lineTo(X(am::BAND_HZ[i]), Y(target[i] + 3)); else band.startNewSubPath(X(am::BAND_HZ[i]), Y(target[i] + 3));
    }
    for (size_t i = target.size(); i-- > 0;) band.lineTo(X(am::BAND_HZ[i]), Y(target[i] - 3));
    band.closeSubPath();
    g.setColour(col::acN.withAlpha(.12f));
    g.fillPath(band);
    g.setColour(col::acN.withAlpha(.3f));
    g.strokePath(band, juce::PathStrokeType(.8f));
    if (live_.active()) {
        auto levels = live_.levels();
        const double offset = am::alignToReference(levels, ref).offset;
        for (auto& v : levels) v -= offset;
        auto live = LiveSpectrum::path(levels, X, Y, B);
        g.setColour(col::acA.withAlpha(.12f));
        g.fillPath(live);
        g.setColour(col::acA.withAlpha(.45f));
        g.strokePath(live, juce::PathStrokeType(.8f));
    }
    const auto* plan = session_.plan();
    if (plan) {
        g.setColour(col::text);
        g.strokePath(line(plan->eq.measured), juce::PathStrokeType(1.4f));
        std::vector<double> after;
        for (size_t i = 0; i < plan->eq.measured.size(); ++i)
            after.push_back(plan->eq.present[i] ? target[i] + plan->eq.residual[i] : plan->eq.measured[i]);
        juce::Path dashed;
        const float dash[] = {4, 3};
        juce::PathStrokeType(1.4f).createDashedStroke(dashed, line(after), dash, 2);
        g.setColour(col::acB);
        g.fillPath(dashed);
    }
    {
        auto lg = legend_;
        const int w = lg.getWidth() / 4;
        const std::pair<const char*, juce::Colour> keys[] = {{"TARGET \xc2\xb1" "3 dB", col::acN}, {"LIVE OUT", col::acA},
                                                            {"MEASURED", col::text}, {"AFTER EQ", col::acB}};
        g.setFont(monoFont(7));
        for (const auto& [label, colour] : keys) {
            auto cell = lg.removeFromLeft(w);
            g.setColour(colour);
            g.fillRect(cell.getX() + 2, cell.getCentreY() - 1, 10, 2);
            g.drawText(juce::String::fromUTF8(label), cell.withTrimmedLeft(16), juce::Justification::centredLeft);
        }
    }

    // ---- meters ----
    const auto* a = session_.analysis();
    const std::pair<const char*, std::pair<juce::String, const char*>> meters[] = {
        {"IN", {a ? fmtLufs(a->lufsIn) : juce::String::fromUTF8("—"), "LUFS"}},
        {"OUT", {a ? fmtLufs(a->lufsOut) : juce::String::fromUTF8("—"), "LUFS"}},
        {"PEAK", {a ? toFixed(a->peakDb, 1) : juce::String::fromUTF8("—"), "dBFS"}},
        {"CREST", {a ? toFixed(a->crestDb, 1) : juce::String::fromUTF8("—"), "dB"}}};
    for (int i = 0; i < 4; ++i) {
        const auto cell = juce::Rectangle<int>(meters_.getX() + (i % 2) * meters_.getWidth() / 2,
                                               meters_.getY() + (i / 2) * 20, meters_.getWidth() / 2 - 4, 18);
        g.setColour(juce::Colour(0xff0a0d14));
        g.fillRoundedRectangle(cell.toFloat(), 5);
        g.setFont(monoFont(7));
        g.setColour(col::textDim);
        g.drawText(meters[i].first, cell.withTrimmedLeft(6), juce::Justification::centredLeft);
        g.setFont(monoFont(9));
        g.setColour(col::text);
        g.drawText(meters[i].second.first, cell.withTrimmedRight(30), juce::Justification::centredRight);
        g.setFont(monoFont(6.5f));
        g.setColour(col::textDim);
        g.drawText(meters[i].second.second, cell.withTrimmedRight(4), juce::Justification::centredRight);
    }
    g.setFont(monoFont(8));
    g.setColour(col::textDim);
    g.drawText("AMOUNT", amount_.getBounds().withX(meters_.getX()).withWidth(50), juce::Justification::centredLeft);

    // ---- moves and notes ----
    g.setColour(juce::Colour(0xff0a0d14));
    g.fillRoundedRectangle(moves_.toFloat(), 7);
    auto list = moves_.reduced(8, 6);
    g.setFont(monoFont(8));
    if (!plan) {
        g.setColour(col::textDim);
        g.drawFittedText("Play a full scene, then press LISTEN. The assistant hears the pre-FX mix for 12 s and "
                         "proposes master EQ, dynamics, loudness and fader moves.",
                         list, juce::Justification::topLeft, 6);
    } else {
        for (const auto& m : plan->moves) {
            if (list.getHeight() < 13) break;
            auto row = list.removeFromTop(13);
            auto to = row.removeFromRight(100), from = row.removeFromRight(92);
            g.setColour(col::textDim);
            g.drawText(juce::String::fromUTF8(m.label.c_str()), row, juce::Justification::centredLeft, true);
            g.setColour(col::textDim.withAlpha(.6f));
            g.drawText(juce::String::fromUTF8(m.from.c_str()), from, juce::Justification::centredRight, true);
            g.setColour(col::acB);
            g.drawText(juce::String::fromUTF8(m.to.c_str()), to, juce::Justification::centredRight, true);
        }
        list.removeFromTop(4);
        g.setColour(col::acN);
        for (const auto& note : plan->notes) {
            if (list.getHeight() < 13) break;
            g.drawFittedText(juce::String::fromUTF8(note.c_str()), list.removeFromTop(26), juce::Justification::topLeft, 2);
        }
    }

    // ---- track levels: now, target and move on a -16..+4 LU scale ----
    if (plan) {
        constexpr double luMin = -16, luMax = 4;
        auto rows = levels_;
        for (const auto& l : plan->levels) {
            if (rows.getHeight() < kLevelRow) break;
            auto row = rows.removeFromTop(kLevelRow);
            g.setFont(monoFont(8));
            g.setColour(col::text);
            g.drawText(juce::String(l.name), row.withWidth(kLevelName - 4), juce::Justification::centredLeft, true);
            auto bar = row.withTrimmedLeft(kLevelName + kLevelRole + 8).withTrimmedRight(44).reduced(0, 6).toFloat();
            const auto pos = [&](double v) {
                return bar.getX() + (float)((juce::jlimit(luMin, luMax, v) - luMin) / (luMax - luMin)) * bar.getWidth();
            };
            g.setColour(juce::Colour(0xff0a0d14));
            g.fillRoundedRectangle(bar, 3);
            g.setColour(col::acN.withAlpha(.45f));
            g.fillRoundedRectangle(bar.withRight(pos(l.current)), 3);
            if (am::truthy(l.move)) {
                const float x0 = pos(std::min(l.current, l.current + l.move));
                g.setColour(col::acB.withAlpha(.7f));
                g.fillRect(juce::Rectangle<float>(x0, bar.getY(), (float)(std::abs(l.move) / (luMax - luMin)) * bar.getWidth(), bar.getHeight()));
            }
            g.setColour(col::text);
            g.fillRect(pos(l.target) - 1, bar.getY() - 2, 2.0f, bar.getHeight() + 4);
            g.setColour(am::truthy(l.move) ? col::acB : col::textDim);
            g.drawText(am::truthy(l.move) ? (l.move > 0 ? "+" : "") + toFixed(l.move, 1) : juce::String("HOLD"),
                       row.withTrimmedLeft(row.getWidth() - 40), juce::Justification::centredRight);
        }
    }
}

} // namespace fui
