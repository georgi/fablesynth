#include "SeqProcessor.h"
#include "SeqEditor.h"
#include "SessionCodec.h"
#include "dsp/SeqFactory.h"

#include "../dsp/Params.h"
#include "../dsp/Presets.h"
#include "../dsp/Wavetables.h"
#include "../bass/dsp/BassParams.h"
#include "../bass/dsp/BassPatches.h"
#include "../drum/dsp/DrumParams.h"
#include "../drum/dsp/DrumKits.h"
#include "../drum/dsp/DrumTables.h"
#include "../drum/dsp/SampledTables.gen.h"

#include <cmath>

using namespace fable;

// ---- parameter table ------------------------------------------------------
// SQ-4's tiny host surface: a master fader, the global transport (swing / bpm /
// quant), and four track faders. Every musical decision still routes through
// the Conductor; these exist so a DAW can automate the mix + transport.
namespace {
enum { SQ_MASTER = 0, SQ_SWING, SQ_BPM, SQ_QUANT, SQ_VOL0, SQ_VOL1, SQ_VOL2, SQ_VOL3, SQ_NPARAMS };

const std::vector<std::string> SQ_QUANT_LABELS = { "1 BAR", "1/4", "OFF" };

// Overlay a pid->value patch map onto a flat engine param array, resolving pids
// through the engine's ParamInfo table (Enum/Bool carried verbatim, matching
// each standalone processor's inline-load path).
template <typename Arr, typename Table>
void overlayInline(Arr& arr, const std::map<std::string, float>& params, const Table& info) {
    for (const auto& kv : params)
        for (const auto& d : info)
            if (d.pid == kv.first) { arr[(size_t)d.id] = kv.second; break; }
}

// This processor is a fixed 4-track rig — every index-based engine routing
// below (prepareToPlay/applyTrackPatch/computeTrackParams/loadTrackParams/the
// render paths) hardcodes track 0 = DR1, track 1 = BL1, tracks 2-3 = WT1.
// fable::validateSession (SeqModel.h) only checks the doc is internally
// consistent (clip byte counts vs. each track's own machine) — it says
// nothing about track count or which machine sits at which index, because
// the web schema and codec are layout-agnostic by design (other layouts are
// valid SessionDoc v:1 documents). So the guard belongs here, at THIS
// processor's apply boundary, not in the shared model/codec: a schema-valid
// doc with a different track count or a swapped machine would otherwise
// reach applySessionJson (LOAD button, and setStateInformation's SESSION
// child) and drive an out-of-bounds/mismatched-size read in loadTrackParams
// (e.g. a 48-byte BL1 clip routed to DrumEngine, which reads ~256 bytes).
bool sqLayoutMatches(const SessionData& doc) {
    static const Machine kLayout[4] = { Machine::DR1, Machine::BL1, Machine::WT1, Machine::WT1 };
    if (doc.tracks.size() != 4) return false;
    for (int i = 0; i < 4; ++i)
        if (doc.tracks[(size_t)i].machine != kLayout[i]) return false;
    return true;
}
} // namespace

namespace fable {
const std::vector<ParamInfo>& seqParamInfo() {
    static const std::vector<ParamInfo> v = [] {
        std::vector<ParamInfo> t;
        t.push_back({ SQ_MASTER, "master", "MASTER", 0, 1, 0.75f, Curve::Lin, Kind::Float, nullptr });
        t.push_back({ SQ_SWING,  "swing",  "SWING",  0, 1, 0.0f,  Curve::Lin, Kind::Float, nullptr });
        t.push_back({ SQ_BPM,    "bpm",    "BPM",   60, 200, 122.0f, Curve::Lin, Kind::Float, nullptr });
        t.push_back({ SQ_QUANT,  "quant",  "QUANT",  0, (float)SQ_QUANT_LABELS.size() - 1, 0,
                      Curve::Int, Kind::Enum, &SQ_QUANT_LABELS });
        t.push_back({ SQ_VOL0, "vol0", "VOL 0", 0, 1, 0.75f, Curve::Lin, Kind::Float, nullptr });
        t.push_back({ SQ_VOL1, "vol1", "VOL 1", 0, 1, 0.75f, Curve::Lin, Kind::Float, nullptr });
        t.push_back({ SQ_VOL2, "vol2", "VOL 2", 0, 1, 0.75f, Curve::Lin, Kind::Float, nullptr });
        t.push_back({ SQ_VOL3, "vol3", "VOL 3", 0, 1, 0.75f, Curve::Lin, Kind::Float, nullptr });
        return t;
    }();
    return v;
}
} // namespace fable

juce::AudioProcessorValueTreeState::ParameterLayout SeqAudioProcessor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    const auto& info = seqParamInfo();

    auto make = [](const ParamInfo& d) -> std::unique_ptr<juce::RangedAudioParameter> {
        juce::ParameterID pid(d.pid, 1);
        juce::String name = juce::String(d.pid).toUpperCase();
        if (d.kind == Kind::Bool)
            return std::make_unique<juce::AudioParameterBool>(pid, name, d.def != 0.0f);
        if (d.kind == Kind::Enum) {
            juce::StringArray choices;
            for (const auto& s : *d.options) choices.add(s);
            return std::make_unique<juce::AudioParameterChoice>(pid, name, choices, (int)d.def);
        }
        ParamInfo di = d;
        juce::NormalisableRange<float> range(
            d.min, d.max,
            [di](float, float, float n) { return normToValue(di, n); },
            [di](float, float, float v) { return valueToNorm(di, v); });
        if (d.curve == Curve::Int) range.interval = 1.0f;
        return std::make_unique<juce::AudioParameterFloat>(pid, name, range, d.def);
    };

    auto mix = std::make_unique<juce::AudioProcessorParameterGroup>("mix", "MIX", " | ");
    for (int i : { (int)SQ_MASTER, (int)SQ_VOL0, (int)SQ_VOL1, (int)SQ_VOL2, (int)SQ_VOL3 })
        mix->addChild(make(info[(size_t)i]));
    layout.add(std::move(mix));
    auto tr = std::make_unique<juce::AudioProcessorParameterGroup>("transport", "TRANSPORT", " | ");
    for (int i : { (int)SQ_SWING, (int)SQ_BPM, (int)SQ_QUANT })
        tr->addChild(make(info[(size_t)i]));
    layout.add(std::move(tr));
    return layout;
}

// ---- master safety limiter -------------------------------------------------
// The WT/DR/BL web DynamicsCompressorNode port: threshold -6 dB, knee 4 dB,
// ratio 12, attack 2 ms, release 250 ms, spec-defined implicit makeup gain.
namespace {
constexpr double kSqThrDb = -6.0, kSqKnee = 4.0, kSqRatio = 12.0;

// Static curve, in dB of gain reduction (<= 0). Below threshold: 0; within the
// soft knee: quadratic; above: slope 1/ratio - 1 (WebAudio DynamicsCompressor).
double sqCompDb(double xDb) {
    const double over = xDb - kSqThrDb;
    if (over <= 0) return 0.0;
    if (over < kSqKnee) return (1.0 / kSqRatio - 1.0) * over * over / (2.0 * kSqKnee);
    return (1.0 / kSqRatio - 1.0) * (over - kSqKnee * 0.5);
}
} // namespace

void SeqAudioProcessor::Limiter::prepare(double sampleRate) {
    sr = sampleRate;
    atk = 1.0 - std::exp(-1.0 / (0.002 * sr));
    rel = 1.0 - std::exp(-1.0 / (0.25 * sr));
    env = 0;
    // WebAudio spec makeup: (1 / c(0 dBFS))^0.6.
    const double g0 = std::pow(10.0, sqCompDb(0.0) / 20.0);
    makeup = std::pow(1.0 / g0, 0.6);
}

void SeqAudioProcessor::Limiter::process(float& l, float& r) {
    const double peak = std::max(std::abs((double)l), std::abs((double)r));
    const double coef = peak > env ? atk : rel;
    env += (peak - env) * coef;
    const double xDb = 20.0 * std::log10(std::max(env, 1.0e-9));
    const double g = std::pow(10.0, sqCompDb(xDb) / 20.0) * makeup;
    l = (float)(l * g);
    r = (float)(r * g);
}

// ---- construction ----------------------------------------------------------

SeqAudioProcessor::SeqAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createLayout()) {
    rawMaster_ = apvts.getRawParameterValue("master");
    rawSwing_  = apvts.getRawParameterValue("swing");
    rawBpm_    = apvts.getRawParameterValue("bpm");
    for (int t = 0; t < kTracks; ++t) {
        rawVol_[t] = apvts.getRawParameterValue("vol" + juce::String(t));
        trackStep[t].store(-1);
        trackBar[t].store(0);
        trackRms[t].store(0.0f);
    }

    // Build each engine's table set (same order the standalone processors use).
    for (auto& g : generateTables())
        wtTables_.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    bassTables_ = wtTables_; // BL-1 hosts the same 6 WT-1 procedurals
    for (auto& g : generateDrumTables())
        drumTables_.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    for (auto& g : generateTables())
        drumTables_.push_back(std::make_shared<const GeneratedTable>(std::move(g)));
    for (auto& g : generateSampledDrumTables())
        drumTables_.push_back(std::make_shared<const GeneratedTable>(std::move(g)));

    initialSession_ = factorySession();
    jassert(initialSession_.tracks.size() == (size_t)kTracks
            && initialSession_.tracks[0].machine == Machine::DR1
            && initialSession_.tracks[1].machine == Machine::BL1
            && initialSession_.tracks[2].machine == Machine::WT1
            && initialSession_.tracks[3].machine == Machine::WT1);
}

SeqAudioProcessor::~SeqAudioProcessor() { stopTimer(); }

bool SeqAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    auto s = layouts.getMainOutputChannelSet();
    return s == juce::AudioChannelSet::stereo() || s == juce::AudioChannelSet::mono();
}

// ---- patch application -----------------------------------------------------

std::vector<float> SeqAudioProcessor::computeTrackParams(int t, const PatchRef& patch) const {
    if (t == 0) {
        const auto& kits = factoryKits();
        int idx = patch.factory ? patch.index : 0;
        if (idx < 0 || idx >= (int)kits.size()) idx = 0;
        DrumParamArray a = applyKit(kits[(size_t)idx]);
        if (!patch.factory) overlayInline(a, patch.params, drumParamInfo());
        return std::vector<float>(a.begin(), a.end());
    }
    if (t == 1) {
        const auto& bank = bassFactoryPatches();
        int idx = patch.factory ? patch.index : 0;
        if (idx < 0 || idx >= (int)bank.size()) idx = 0;
        BassParamArray a = applyBassPatch(bank[(size_t)idx]);
        if (!patch.factory) overlayInline(a, patch.params, bassParamInfo());
        return std::vector<float>(a.begin(), a.end());
    }
    const auto& bank = factoryPresets();
    int idx = patch.factory ? patch.index : 0;
    if (idx < 0 || idx >= (int)bank.size()) idx = 0;
    ParamArray a = applyPreset(bank[(size_t)idx]);
    if (!patch.factory) overlayInline(a, patch.params, paramInfo());
    return std::vector<float>(a.begin(), a.end());
}

void SeqAudioProcessor::loadTrackParams(int t, const std::vector<float>& v) {
    if (t == 0) {
        auto& p = drum_.params();
        for (int i = 0; i < DR_NUM_PARAMS && i < (int)v.size(); ++i) p[(size_t)i] = v[(size_t)i];
        drumFx_.setParams(p);
    } else if (t == 1) {
        auto& p = bass_.params();
        for (int i = 0; i < BL_NUM_PARAMS && i < (int)v.size(); ++i) p[(size_t)i] = v[(size_t)i];
        bassFx_.setParams(p);
    } else {
        auto& p = wt_[t - 2].params();
        for (int i = 0; i < NUM_PARAMS && i < (int)v.size(); ++i) p[(size_t)i] = v[(size_t)i];
        wtFx_[t - 2].setParams(p);
    }
}

void SeqAudioProcessor::applyTrackPatch(int t) {
    if (t < 0 || t >= kTracks || !conductor_) return;
    // conductor_->session() is the runtime truth for patch swaps (setTrackPatch
    // writes there, not to initialSession_) — reading initialSession_ here
    // would re-apply whatever patch the track shipped with, not the one just
    // selected.
    Cmd c;
    c.k = Cmd::K::Patch;
    c.t = t;
    c.params = std::make_shared<std::vector<float>>(
        computeTrackParams(t, conductor_->session().tracks[(size_t)t].patch));
    pushCmd(std::move(c));
}

std::vector<float> SeqAudioProcessor::debugTrackParams(int t) {
    if (t == 0) { auto& p = drum_.params(); return std::vector<float>(p.begin(), p.end()); }
    if (t == 1) { auto& p = bass_.params(); return std::vector<float>(p.begin(), p.end()); }
    auto& p = wt_[t - 2].params(); return std::vector<float>(p.begin(), p.end());
}

// ---- prepare ---------------------------------------------------------------

void SeqAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    preparedSampleRate_ = sampleRate;
    drum_.prepare(sampleRate); drumFx_.prepare(sampleRate);
    bass_.prepare(sampleRate); bassFx_.prepare(sampleRate);
    for (int i = 0; i < 2; ++i) { wt_[i].prepare(sampleRate); wtFx_[i].prepare(sampleRate); }

    drum_.setTables(drumTables_);
    bass_.setTables(bassTables_);
    for (int i = 0; i < 2; ++i) wt_[i].setTables(wtTables_);

    // prepareToPlay can run again later (e.g. a host sample-rate change)
    // after the conductor has already diverged from initialSession_ (patch
    // swaps land in SessionData.tracks[t].patch, so they live in the
    // conductor's session, not initialSession_) — seed this pass from the
    // conductor's live session when one already exists, so a re-prepare
    // can't quietly revert a patch swap. Mute/solo/vol do NOT survive a
    // re-prepare either way: they're Conductor-owned vectors outside
    // SessionData, reset when the fresh Conductor below is constructed.
    // Copied by value:
    // the old conductor_ (and the session it owns) is destroyed below when
    // conductor_ is reassigned, so a reference here would dangle.
    const SessionData liveSession = conductor_ ? conductor_->session() : initialSession_;

    // Apply each track's patch directly (audio is not running yet).
    for (int t = 0; t < kTracks; ++t)
        loadTrackParams(t, computeTrackParams(t, liveSession.tracks[(size_t)t].patch));

    trackBuf_.setSize(2, samplesPerBlock);
    drumAux_.setSize(2 * (DR_NBUSES - 1), samplesPerBlock);
    limiter_.prepare(sampleRate);

    for (int t = 0; t < kTracks; ++t) {
        trackGain_[t].reset(sampleRate, 0.015);
        trackGain_[t].setCurrentAndTargetValue(0.0f);
    }
    masterGain_.reset(sampleRate, 0.015);
    masterGain_.setCurrentAndTargetValue(rawMaster_ ? rawMaster_->load() : 0.75f);

    frame_ = 0;
    currentFrame.store(0.0);
    for (int t = 0; t < kTracks; ++t) { trackStep[t].store(-1); trackBar[t].store(0); }

    // Hosted mode on before any tempo/clip reaches the engines. Pass the
    // prepared block size so each ClipHost reserves event headroom for the
    // worst-case number of grid steps one chunk can span (Finding 3): the
    // processor renders each engine in chunks of at most samplesPerBlock, so
    // that is the largest quantum a ClipHost ever sees between drains.
    drum_.setHostClipMode(true, samplesPerBlock);
    bass_.setHostClipMode(true, samplesPerBlock);
    for (int i = 0; i < 2; ++i) wt_[i].setHostClipMode(true, samplesPerBlock);

    conductor_ = std::make_unique<Conductor>(liveSession, io_, sampleRate);
    conductor_->powerOn(); // anchor = 256; enqueues tempo + gain commands

    // Also stamp tempo directly (safe here — audio not running): the shared
    // anchor is fixed once, never re-anchored mid-flight (a BL-1 LFO term
    // depends on this).
    const double a = conductor_->anchor(), sw = conductor_->swing();
    drum_.hostTempo(liveSession.bpm, sw, a);
    bass_.hostTempo(liveSession.bpm, sw, a);
    for (int i = 0; i < 2; ++i) wt_[i].hostTempo(liveSession.bpm, sw, a);

    lastSwing_ = rawSwing_->load();
    lastBpm_   = rawBpm_->load();
    for (int t = 0; t < kTracks; ++t) lastVol_[t] = rawVol_[t]->load();

    // Own the ack-drain/param-poll pump so it runs headless too (offline
    // bounce, no editor open) — see the class-doc threading note. Restarting
    // an already-running juce::Timer just reschedules it.
    startTimerHz(30);
}

// ---- command FIFO ----------------------------------------------------------

void SeqAudioProcessor::pushCmd(Cmd&& c) {
    int s1, sz1, s2, sz2;
    cmdFifo_.prepareToWrite(1, s1, sz1, s2, sz2);
    // Overwriting the slot destroys the previous command's shared_ptr HERE, on
    // the message thread — the only place a clip byte vector is ever freed.
    if (sz1 > 0)      cmdSlots_[(size_t)s1] = std::move(c);
    else if (sz2 > 0) cmdSlots_[(size_t)s2] = std::move(c);
    cmdFifo_.finishedWrite(sz1 + sz2);
}

void SeqAudioProcessor::drainCmds() {
    int s1, sz1, s2, sz2;
    cmdFifo_.prepareToRead(cmdFifo_.getNumReady(), s1, sz1, s2, sz2);
    auto run = [&](int start, int count) {
        for (int i = 0; i < count; ++i) {
            const Cmd& c = cmdSlots_[(size_t)(start + i)]; // reference: no shared_ptr copy/free
            switch (c.k) {
                case Cmd::K::Clip: {
                    const uint8_t* d = c.bytes->data(); const int nb = (int)c.bytes->size();
                    switch (c.t) {
                        case 0: drum_.hostClip(d, nb, c.bars, c.at, c.tag); break;
                        case 1: bass_.hostClip(d, nb, c.bars, c.at, c.tag); break;
                        default: wt_[c.t - 2].hostClip(d, nb, c.bars, c.at, c.tag); break;
                    }
                } break;
                case Cmd::K::Stop:
                    switch (c.t) {
                        case 0: drum_.hostClipStop(c.at); break;
                        case 1: bass_.hostClipStop(c.at); break;
                        default: wt_[c.t - 2].hostClipStop(c.at); break;
                    }
                    break;
                case Cmd::K::Update: {
                    const uint8_t* d = c.bytes->data(); const int nb = (int)c.bytes->size();
                    switch (c.t) {
                        case 0: drum_.hostClipUpdate(d, nb, c.bars); break;
                        case 1: bass_.hostClipUpdate(d, nb, c.bars); break;
                        default: wt_[c.t - 2].hostClipUpdate(d, nb, c.bars); break;
                    }
                } break;
                case Cmd::K::Gain:
                    trackGain_[c.t].setTargetValue(c.gain);
                    break;
                case Cmd::K::Tempo:
                    drum_.hostTempo(c.bpm, c.swing, c.anchor);
                    bass_.hostTempo(c.bpm, c.swing, c.anchor);
                    for (int i = 0; i < 2; ++i) wt_[i].hostTempo(c.bpm, c.swing, c.anchor);
                    break;
                case Cmd::K::Patch:
                    loadTrackParams(c.t, *c.params);
                    break;
                case Cmd::K::Reset:
                    // Ordered generation advance (see cmdGen_/audioGen_): every
                    // ack the audio thread stamps from here on carries the new
                    // generation. FIFO-ordered ahead of the swap's stops.
                    audioGen_ = c.gen;
                    break;
            }
        }
    };
    run(s1, sz1); run(s2, sz2);
    cmdFifo_.finishedRead(sz1 + sz2);
}

// ConductorIO -> command FIFO (all on the message thread).
void SeqAudioProcessor::IO::ioScheduleClip(int t, const std::vector<uint8_t>& bytes, int bars, double at, int tag) {
    Cmd c; c.k = Cmd::K::Clip; c.t = t; c.bars = bars; c.at = at; c.tag = tag;
    c.bytes = std::make_shared<std::vector<uint8_t>>(bytes);
    p.pushCmd(std::move(c));
}
void SeqAudioProcessor::IO::ioScheduleStop(int t, double at) {
    Cmd c; c.k = Cmd::K::Stop; c.t = t; c.at = at; p.pushCmd(std::move(c));
}
void SeqAudioProcessor::IO::ioUpdateClip(int t, const std::vector<uint8_t>& bytes, int bars) {
    Cmd c; c.k = Cmd::K::Update; c.t = t; c.bars = bars;
    c.bytes = std::make_shared<std::vector<uint8_t>>(bytes);
    p.pushCmd(std::move(c));
}
void SeqAudioProcessor::IO::ioSetTrackGain(int t, float gain) {
    Cmd c; c.k = Cmd::K::Gain; c.t = t; c.gain = gain; p.pushCmd(std::move(c));
}
void SeqAudioProcessor::IO::ioSendTempo(double bpm, double swing, double anchor) {
    Cmd c; c.k = Cmd::K::Tempo; c.bpm = bpm; c.swing = swing; c.anchor = anchor; p.pushCmd(std::move(c));
}

// ---- acks ------------------------------------------------------------------

void SeqAudioProcessor::pushAck(int t, const HostEvent& ev, uint32_t gen) {
    int s1, sz1, s2, sz2;
    ackFifo_.prepareToWrite(1, s1, sz1, s2, sz2);
    if (sz1 > 0)      ackSlots_[(size_t)s1] = { t, ev, gen };
    else if (sz2 > 0) ackSlots_[(size_t)s2] = { t, ev, gen };
    ackFifo_.finishedWrite(sz1 + sz2);
}

void SeqAudioProcessor::drainAcks() {
    int s1, sz1, s2, sz2;
    ackFifo_.prepareToRead(ackFifo_.getNumReady(), s1, sz1, s2, sz2);
    const uint32_t cur = cmdGen_.load(std::memory_order_relaxed);
    auto run = [&](int start, int count) {
        for (int i = 0; i < count; ++i) {
            const Ack& a = ackSlots_[(size_t)(start + i)];
            if (!conductor_) continue;
            // Drop acks stamped before the conductor was last replaced — they
            // target a session that no longer exists (see cmdGen_).
            if (a.gen != cur) continue;
            if (a.ev.t == HostEvent::T::Start) {
                conductor_->onClipStart(a.t, a.ev.tag);
            } else if (a.ev.t == HostEvent::T::Stop) {
                conductor_->onClipStop(a.t);
                trackStep[a.t].store(-1);
                trackBar[a.t].store(0);
            }
        }
    };
    run(s1, sz1); run(s2, sz2);
    ackFifo_.finishedRead(sz1 + sz2);

    pollSessionParams();
}

// The editor timer's musical params -> conductor (all decisions stay in one
// place). Change-detected so we never fight the conductor's own updates.
void SeqAudioProcessor::pollSessionParams() {
    if (!conductor_) return;
    float sw = rawSwing_->load();
    if (sw != lastSwing_) { lastSwing_ = sw; conductor_->setSwing(sw); }
    float bpm = rawBpm_->load();
    if (bpm != lastBpm_) { lastBpm_ = bpm; conductor_->setBpm(bpm); } // guarded in the conductor
    for (int t = 0; t < kTracks; ++t) {
        float v = rawVol_[t]->load();
        if (v != lastVol_[t]) { lastVol_[t] = v; conductor_->setTrackVol(t, v); }
    }
}

// ---- render helpers --------------------------------------------------------

void SeqAudioProcessor::renderDrum(float* L, float* R, int n) {
    drum_.hostSetFrame(frame_);
    float* outs[DR_NBUSES][2];
    outs[0][0] = L; outs[0][1] = R;
    for (int b = 1; b < DR_NBUSES; ++b) {
        outs[b][0] = drumAux_.getWritePointer(2 * (b - 1));
        outs[b][1] = drumAux_.getWritePointer(2 * (b - 1) + 1);
    }
    drum_.render(outs, n);         // zero-fills all buses, pads accumulate
    drumFx_.process(L, R, n);      // master FX on MAIN only (AUX dropped)
}

void SeqAudioProcessor::renderBass(float* L, float* R, int n) {
    bass_.hostSetFrame(frame_);
    bass_.render(L, R, n);
    bassFx_.process(L, R, n);
}

void SeqAudioProcessor::renderWt(int i, float* L, float* R, int n) {
    wt_[i].hostSetFrame(frame_);
    wt_[i].render(L, R, n);
    wtFx_[i].process(L, R, n);
}

int SeqAudioProcessor::takeEvents(int t, HostEvent* out, int max) {
    switch (t) {
        case 0: return drum_.takeHostEvents(out, max);
        case 1: return bass_.takeHostEvents(out, max);
        default: return wt_[t - 2].takeHostEvents(out, max);
    }
}

// ---- audio -----------------------------------------------------------------

void SeqAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    // Pause = web ctx.suspend(): silence, and the shared frame counter freezes.
    if (paused_.load()) { buffer.clear(); return; }

    drainCmds();
    masterGain_.setTargetValue(rawMaster_ ? rawMaster_->load() : 0.75f);

    float* outL = buffer.getWritePointer(0);
    const bool stereo = buffer.getNumChannels() > 1;
    float* outR = stereo ? buffer.getWritePointer(1) : outL;
    for (int i = 0; i < n; ++i) { outL[i] = 0.0f; if (stereo) outR[i] = 0.0f; }

    // The scratch buffers are sized to the prepared block once in
    // prepareToPlay; a host that hands us a larger buffer is processed in
    // sub-blocks of at most that size, so nothing here ever setSize()s (which
    // would allocate) on the audio thread. drainCmds runs once up top; each
    // engine renders per chunk from its own chunk-start frame; the shared
    // currentFrame is published once for the whole block, as before.
    const int cap = std::max(1, trackBuf_.getNumSamples());
    const double base = frame_;
    // Stamp acks with audioGen_, advanced only by an ordered K::Reset command in
    // drainCmds above — NOT a fresh cmdGen_.load(), which would race a
    // concurrent applySessionJson bump and mis-stamp this block's old-session
    // events with the new generation (see cmdGen_/audioGen_ in the header).
    const uint32_t gen = audioGen_;
    const int scopeW = scopeWrite_.load(std::memory_order_relaxed);

    float* tl = trackBuf_.getWritePointer(0);
    float* tr = trackBuf_.getWritePointer(1);
    double trackSumSq[kTracks] = { 0.0, 0.0, 0.0, 0.0 };

    for (int off = 0; off < n; off += cap) {
        const int c = std::min(cap, n - off);
        frame_ = base + off;                         // engines render this chunk from here
        float* oL = outL + off;
        float* oR = (stereo ? outR : outL) + off;

        for (int t = 0; t < kTracks; ++t) {
            switch (t) {
                case 0: renderDrum(tl, tr, c); break;
                case 1: renderBass(tl, tr, c); break;
                default: renderWt(t - 2, tl, tr, c); break;
            }
            double sumSq = 0.0;
            for (int i = 0; i < c; ++i) {
                const float g = trackGain_[t].getNextValue();
                const float l = tl[i] * g, r = tr[i] * g;
                if (stereo) { oL[i] += l; oR[i] += r; }
                else        { oL[i] += 0.5f * (l + r); }
                sumSq += (double)l * l + (double)r * r;
            }
            trackSumSq[t] += sumSq;
        }

        // Master gain -> limiter -> output; post-limiter mono into the scope ring.
        for (int i = 0; i < c; ++i) {
            const float g = masterGain_.getNextValue();
            float l = oL[i] * g, r = (stereo ? oR[i] : oL[i]) * g;
            limiter_.process(l, r);
            oL[i] = l; if (stereo) oR[i] = r;
            scopeRing_[(size_t)((scopeW + off + i) & (kScopeSize - 1))] = 0.5f * (l + r);
        }

        // Drain each engine's host events for this chunk: Pos updates the
        // step/bar readouts; Start/Stop become acks the conductor consumes via
        // drainAcks(), stamped with this block's command generation.
        // Drain losslessly: one prepared chunk can emit more host events than
        // the local buffer holds (offline render spanning many grid steps), and
        // takeEvents now keeps the remainder, so loop until it returns 0
        // (Finding 3). No allocation — evs is fixed and takeEvents erases in
        // place.
        HostEvent evs[64];
        for (int t = 0; t < kTracks; ++t) {
            int m;
            while ((m = takeEvents(t, evs, 64)) > 0) {
                for (int k = 0; k < m; ++k) {
                    if (evs[k].t == HostEvent::T::Pos) {
                        trackStep[t].store(evs[k].step, std::memory_order_relaxed);
                        trackBar[t].store(evs[k].bar, std::memory_order_relaxed);
                    } else {
                        pushAck(t, evs[k], gen);
                    }
                }
            }
        }
    }

    for (int t = 0; t < kTracks; ++t)
        trackRms[t].store((float)std::sqrt(trackSumSq[t] / (2.0 * (double)n)));
    scopeWrite_.store(scopeW + n, std::memory_order_relaxed);

    // Advance the shared timebase. currentFrame is published as elapsed frames
    // (= the next block's start frame) so a launch issued between blocks and the
    // block that consumes it agree on the reference frame.
    frame_ = base + n;
    currentFrame.store(frame_);
}

float SeqAudioProcessor::readScope(float* dest, int n) {
    int w = scopeWrite_.load(std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
        dest[i] = scopeRing_[(size_t)((w - n + i) & (kScopeSize - 1))];
    return 0.0f;
}

// ---- state -----------------------------------------------------------------

void SeqAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    if (!state.isValid()) return;
    // SQ4STATE wraps the parameter tree + the session (web SessionDoc v:1 JSON).
    juce::ValueTree root("SQ4STATE");
    root.appendChild(state, nullptr);
    juce::ValueTree sess("SESSION");
    sess.setProperty("doc", currentSessionJson(), nullptr);
    root.appendChild(sess, nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary(*xml, destData);
}

void SeqAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (!xml) return; // garbage -> keep current session/params

    juce::ValueTree params, sess;
    if (xml->hasTagName("SQ4STATE")) {
        auto root = juce::ValueTree::fromXml(*xml);
        params = root.getChildWithName(apvts.state.getType());
        sess   = root.getChildWithName("SESSION");
    } else if (xml->hasTagName(apvts.state.getType())) {
        params = juce::ValueTree::fromXml(*xml); // legacy: bare parameter tree
    }

    // Restore the session first (tolerant: a bad doc keeps the current one).
    // applySessionJson() owns the rebuild-the-conductor sequencing (stop every
    // track before re-anchoring — see its comment for why).
    if (sess.isValid())
        applySessionJson(sess.getProperty("doc", "").toString());

    if (params.isValid())
        apvts.replaceState(params);
}

// ---- editor ----------------------------------------------------------------

juce::AudioProcessorEditor* SeqAudioProcessor::createEditor() {
    return new SeqEditor(*this);
}

// ---- session import/export (message thread) --------------------------------

bool SeqAudioProcessor::applySessionJson(const juce::String& json) {
    SessionData restored;
    if (!fable::sessionFromJson(json, restored)) return false;
    if (!sqLayoutMatches(restored)) return false; // reject: not the fixed {DR1,BL1,WT1,WT1} rig
    initialSession_ = std::move(restored);
    if (preparedSampleRate_ > 0.0) {
        // Invalidate any in-flight acks BEFORE the swap: a pre-swap Start ack
        // that lands after this point would otherwise flip an owner in the new
        // conductor (see cmdGen_/audioGen_). Bump the message-thread reference
        // first, then push a Reset carrying the new generation as the FIRST
        // command — the audio thread advances audioGen_ only when this Reset
        // drains, in FIFO order ahead of the stops below. That ordering is what
        // makes the invalidation race-free: any event the old session produces
        // before the Reset drains still carries the old generation (dropped);
        // once it drains, the stops that follow have already disarmed every
        // pending clip, so only Stop acks can be produced.
        cmdGen_.fetch_add(1, std::memory_order_relaxed);
        const uint32_t newGen = cmdGen_.load(std::memory_order_relaxed);
        { Cmd r; r.k = Cmd::K::Reset; r.gen = newGen; pushCmd(std::move(r)); }
        // Same stop-before-re-anchor sequencing as setStateInformation (see the
        // comment there): stop every track first, ordered ahead of the new
        // conductor's first tempo command, so no clip is mid-flight across the
        // swap.
        const double now = currentFrame.load();
        for (int t = 0; t < kTracks; ++t) {
            Cmd c; c.k = Cmd::K::Stop; c.t = t; c.at = now;
            pushCmd(std::move(c));
        }
        conductor_ = std::make_unique<Conductor>(initialSession_, io_, preparedSampleRate_);
        conductor_->powerOn();
        for (int t = 0; t < kTracks; ++t) applyTrackPatch(t);
        lastSwing_ = rawSwing_->load();
        lastBpm_   = rawBpm_->load();
        for (int t = 0; t < kTracks; ++t) lastVol_[t] = rawVol_[t]->load();
    }
    return true;
}

juce::String SeqAudioProcessor::currentSessionJson() const {
    return fable::sessionToJson(conductor_ ? conductor_->session() : initialSession_);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SeqAudioProcessor();
}
