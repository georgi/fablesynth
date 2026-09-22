// Line-faithful port of src/drum/engine/worklet-drum.js (the lockstep
// reference). Section comments cite the source lines. Two deliberate
// deviations from the JS: Math.random() becomes the engine's seeded
// xorshift Rng (deterministic tests), and output goes to 5 stereo buses
// selected by each pad's OUT param instead of one worklet output.
#include "DrumEngine.h"
#include "OneShotSamples.gen.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fable {

// Keep the DSP portable across compilers: MSVC does not provide the
// non-standard M_PI/M_LN2 macros unless an opt-in compatibility define is set.
// (Re-applying 703ac99, which a later rewrite of this file dropped — it broke
// the v0.6 Windows release build.)
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kLn2 = 0.693147180559945309417232121458176568;

// js:23-26 — log-cosh for the ADAA drive antiderivative
static inline double lcosh(double z) {
    double a = std::fabs(z);
    return a + std::log1p(std::exp(-2.0 * a)) - kLn2;
}

static constexpr double DR_ADAA_FADE_WIDTH = 0.1;
static constexpr double DR_ADAA_INPUT_LIMIT = 16.0;

static inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static bool sameRhythm(const DrumRhythm& a, const DrumRhythm& b) {
    if (a.v != b.v) return false;
    for (int i = 0; i < DR_RHYTHM_LANES; ++i) {
        const auto& x = a.lanes[(size_t)i];
        const auto& y = b.lanes[(size_t)i];
        if (x.enabled != y.enabled || x.sourceBar != y.sourceBar || x.steps != y.steps
            || x.rotation != y.rotation || x.mode != y.mode || x.cycleBeats != y.cycleBeats)
            return false;
    }
    return true;
}

static inline double adaaInput(double x) {
    return std::isfinite(x) ? clampd(x, -DR_ADAA_INPUT_LIMIT, DR_ADAA_INPUT_LIMIT) : 0.0;
}

// Finding 6: chunk-invariant smoothers (see Engine.cpp for the derivation).
// DR-1's pos smoothing legacy cadence was 0.35 per 16-sample sub-block; cutoff
// was 0.5 per 128-sample chunk, both at 48 kHz.
static const double DR_POS_TAU = 16.0 / (48000.0 * 0.4307829160924542);
static const double DR_CUT_TAU = 128.0 / (48000.0 * kLn2);
static inline double smoothCoef(int n, double tauSr) { return 1 - std::exp(-(double)n / tauSr); }

// Finding D3: decay-curve normalisation constants (see ampEnv).
static const double kExpEnd  = std::exp(-4.5);
static const double kExpNorm = 1.0 / (1.0 - kExpEnd);

// Finding D4: edge fade for the sample layer, in output seconds. Long enough
// to remove the click of a mid-sample START/END or the reverse stop, short
// enough to leave a transient attack intact.
static const double DR_SAMPLE_FADE = 0.0015;

// Crossfade time for a discrete switch — the filter type (FilterState::svfOld),
// the oscillator table/unison and the sample slot (Finding J2) all use it.
static const double DR_SWITCH_FADE = 0.003;

// Equal-power weight for a switch crossfade `i` samples into a run with `left`
// samples of fade still to go out of `len`. Returns cos for the outgoing side
// and sin for the incoming one, so the two always sum to unit power.
static inline void switchWeights(int left, int i, int len, double& wOut, double& wIn) {
    const double prog = clampd(1.0 - (double)(left - i) / (double)len, 0.0, 1.0);
    const double a = prog * kPi * 0.5;
    wOut = std::cos(a);
    wIn = std::sin(a);
}

// ---------------- Finding J1: host-automation smoothing ----------------
// The plan: which per-pad fields get a smoother, and which of those live in a
// genuinely logarithmic unit. TUNE/FINE/PENV AMT are semitones/cents and the
// compressor's THR/GAIN are dB, so a linear ramp there IS a log-domain ramp;
// only CUTOFF (Hz), RING FREQ (Hz) and DELAY TIME (s) need the geometric path.
// Everything absent from this list is a selector, a routing field or a
// sequencer field and passes straight through.
struct DrumSmoothPlan {
    int  n = 0;
    int  id[DR_NSMOOTH]{};
    bool logDomain[DR_NSMOOTH]{};
    int  slotOfField[DPAD_NFIELDS];   // -1 when the field is not smoothed
};

static const DrumSmoothPlan& drumSmoothPlan() {
    static const DrumSmoothPlan plan = [] {
        struct Entry { int field; bool log; };
        static const Entry fields[DR_NSMOOTH_FIELDS] = {
            {DP_OSCA_POS, false}, {DP_OSCA_TUNE, false}, {DP_OSCA_FINE, false},
            {DP_OSCA_DETUNE, false}, {DP_OSCA_LEVEL, false},
            {DP_OSCB_POS, false}, {DP_OSCB_TUNE, false}, {DP_OSCB_FINE, false},
            {DP_OSCB_DETUNE, false}, {DP_OSCB_LEVEL, false},
            {DP_NOISE_COLOR, false}, {DP_NOISE_LEVEL, false},
            {DP_RING_FREQ, true}, {DP_RING_MIX, false},
            {DP_PENV_AMT, false}, {DP_PENV_DEC, false},
            {DP_AENV_ATT, false}, {DP_AENV_HOLD, false}, {DP_AENV_DEC, false},
            {DP_AENV_CURVE, false},
            {DP_FLT_CUT, true}, {DP_FLT_RES, false}, {DP_FLT_DRIVE, false},
            {DP_MOD1_AMT, false}, {DP_MOD2_AMT, false},
            {DP_MOD3_AMT, false}, {DP_MOD4_AMT, false},
            {DP_MODENV_DEC, false},
            {DP_LVL, false}, {DP_PAN, false}, {DP_V2L, false}, {DP_V2M, false},
            {DP_FXDRIVE_AMT, false}, {DP_FXDRIVE_MIX, false},
            {DP_FXCOMP_THR, false}, {DP_FXCOMP_GAIN, false},
            {DP_FXCHORUS_RATE, false}, {DP_FXCHORUS_DEPTH, false}, {DP_FXCHORUS_MIX, false},
            {DP_FXDELAY_TIME, true}, {DP_FXDELAY_FB, false}, {DP_FXDELAY_MIX, false},
            {DP_FXREVERB_SIZE, false}, {DP_FXREVERB_MIX, false},
        };
        DrumSmoothPlan pl;
        for (int f = 0; f < DPAD_NFIELDS; f++) pl.slotOfField[f] = -1;
        for (int k = 0; k < DR_NSMOOTH_FIELDS; k++) pl.slotOfField[fields[k].field] = k;
        // Pad-major order keeps a pad's smoothers adjacent in cache.
        for (int pad = 0; pad < DR_NPADS; pad++)
            for (int k = 0; k < DR_NSMOOTH_FIELDS; k++) {
                pl.id[pl.n] = dpid(pad, fields[k].field);
                pl.logDomain[pl.n] = fields[k].log;
                pl.n++;
            }
        pl.id[pl.n] = DG_MASTER_VOLUME;                 // the one smoothed global
        pl.logDomain[pl.n] = false;
        pl.n++;
        return pl;
    }();
    return plan;
}

void DrumEngine::snapSmoothers() {
    const auto& pl = drumSmoothPlan();
    ps_ = p_;
    for (int k = 0; k < pl.n; k++) smCur_[k] = p_[(size_t)pl.id[k]];
}

void DrumEngine::snapSmoother(int id) {
    if (id < 0 || id >= DR_NUM_PARAMS) return;
    const auto& pl = drumSmoothPlan();
    ps_[(size_t)id] = p_[(size_t)id];
    int k = -1;
    if (id == DG_MASTER_VOLUME) {
        k = pl.n - 1;
    } else if (id < DR_NPADS * DPAD_NFIELDS) {
        const int slot = pl.slotOfField[id % DPAD_NFIELDS];
        if (slot >= 0) k = (id / DPAD_NFIELDS) * DR_NSMOOTH_FIELDS + slot;
    }
    if (k >= 0) smCur_[k] = p_[(size_t)id];
}

// Once per render() call: everything unsmoothed passes through as-is, the
// smoothed ids resume where their ramps left off, and a new ramp of n samples
// toward the host's latest targets is armed.
void DrumEngine::beginBlockParams(int n) {
    const auto& pl = drumSmoothPlan();
    ps_ = p_;
    for (int k = 0; k < pl.n; k++) {
        smFrom_[k] = smCur_[k];
        ps_[(size_t)pl.id[k]] = smCur_[k];
    }
    rampLen_ = std::max(1, n);
    rampPos_ = 0;
}

// One <=128-sample chunk of the ramp. The value depends only on how far into
// the call the chunk ends, so it does not matter how the host splits the block.
void DrumEngine::advanceSmoothers(int n) {
    const auto& pl = drumSmoothPlan();
    rampPos_ = std::min(rampPos_ + n, rampLen_);
    const bool atEnd = rampPos_ >= rampLen_;
    const double u = (double)rampPos_ / (double)rampLen_;
    for (int k = 0; k < pl.n; k++) {
        const int id = pl.id[k];
        const float t = p_[(size_t)id];
        const float f = smFrom_[k];
        if (t == f) continue;                       // idle: one compare, no work
        float c;
        if (atEnd) {
            c = t;
        } else if (pl.logDomain[k]) {
            const double lf = std::log((double)std::max(1.0e-6f, f));
            const double lt = std::log((double)std::max(1.0e-6f, t));
            c = (float)std::exp(lf + (lt - lf) * u);
        } else {
            c = (float)((double)f + ((double)t - (double)f) * u);
        }
        smCur_[k] = c;
        ps_[(size_t)id] = c;
    }
}

// Finding 10: cubic Hermite (Catmull-Rom) table read, indices pre-wrapped.
static inline double rdH(const float* d, int off, int im1, int i0, int i1, int i2, double f) {
    const double ym1 = d[off + im1], y0 = d[off + i0], y1 = d[off + i1], y2 = d[off + i2];
    const double c1 = 0.5 * (y1 - ym1);
    const double c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2;
    const double c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
    return ((c3 * f + c2) * f + c1) * f + y0;
}

// ---- PadVoice::trigger (js:61-69) — resets exactly the same state ----
void DrumEngine::PadVoice::trigger(double v, double rnd) {
    active = true; choking = false;
    vel = v; rand = rnd;
    t = 0; ampLevel = 0;
    oA.posSm = -1; oA.havePrev = false; oA.pData = nullptr;
    oAxLeft = 0;                                   // Finding J2
    oA.tableOwner.reset(); oAx.tableOwner.reset();
    oA.data = oAx.data = nullptr;
    sample.pos = -1; sample.index = -1; sample.done = false;
    sample.pStep = 0; sample.havePrev = false;
    sample.xfIndex = -1; sample.xfLeft = 0; sample.xfDone = true;
    std::fill(std::begin(f.svf), std::end(f.svf), 0.0);
    f.cutSm = 0; f.cutPrev = -1; f.satXL = 0; f.satXR = 0; f.adaaMix = 0;
    std::fill(std::begin(f.svfOld), std::end(f.svfOld), 0.0);
    f.pFtype = -1; f.xfLeft = 0;
    noiseY = 0; ringPhase = 0.25;
    dcxL = dcxR = dcyL = dcyR = 0;
    lgPrev = -1;
}

// prepare() contract (Finding 11): kill every pad voice so nothing computed at
// the old rate survives (a trigger resets all recursive pad state anyway), and
// remap the 48 kHz-reference DC pole to the new rate (Finding 9).
void DrumEngine::prepare(double sampleRate) {
    sr_ = sampleRate;
    // The native processor publishes at most DR_NPATTERNS chain entries.
    // Reserve here so its audio-side setChain never grows storage; standalone
    // engine callers retain the existing arbitrary-length chain API.
    chain_.reserve(DR_NPATTERNS);
    dcR_ = std::pow(DR_DC_R, 48000.0 / sr_);
    chokeCoef_ = 1.0 - std::exp(-1.0 / (DR_CHOKE_TAU * sr_));   // Finding D9
    snapSmoothers();                 // Finding J1: no stale ramp survives a re-prepare
    for (auto& row : fxSeen_) std::fill(std::begin(row), std::end(row), -1.0e30f);
    std::fill(groupFxSeen_.begin(), groupFxSeen_.end(), -1.0e30f);
    for (auto& fx : padFx_) fx.prepare(sampleRate);
    for (auto& fx : groupFx_) fx.prepare(sampleRate);
    for (auto& rv : reverbs_) rv.prepare(sampleRate);
    for (auto& b : busOut_) b.prepare(sampleRate);
    // Finding D9: the one-shot bank is a function-local static that render()
    // used to touch first — its first-use construction would allocate on the
    // audio thread. Build it here instead.
    (void)drumOneShots();
    // Finding D9: sequencer/clip timing state is computed in samples, so it
    // must not survive a rate change. samplesToNext_ = 0 refires the next step
    // immediately at the new rate; the host lock and the clip host resync.
    samplesToNext_ = 0;
    hostSynced_ = false;
    hostEndPpq_ = 0;
    hostFrame_ = 0;
    clipHost_.setTempo(effectiveBpm(), hostSwing_, sr_, anchorFrame_);
    panic();
}

void DrumEngine::panic() {
    for (auto& v : voices_) v.kill();
    for (auto& v : tails_) v.kill();
}

void DrumEngine::selectPad(int i) {
    sel_ = std::max(0, std::min(DR_NPADS - 1, i));
}

// Findings 2 + J3: lock-free publication with no audio-thread free. Build the
// complete immutable set, publish its raw address with one release store, park
// the outgoing set with the epoch at retire time, then reclaim whatever is old
// enough. The audio thread never blocks and never substitutes silence.
void DrumEngine::setTables(std::vector<TablePtr> tables) {
    auto next = std::make_shared<TableSet>();
    next->reserve(tables.size());
    for (auto& t : tables) {
        DrumTable e;
        if (t) {
            e.frames = t->frames; e.mips = t->mips; e.size = t->size; e.mask = t->size - 1;
            e.data = t->data.data();
            e.src = std::move(t);
        }
        next->push_back(std::make_shared<const DrumTable>(std::move(e)));
    }
    std::shared_ptr<const TableSet> pub = std::move(next);
    const TableSet* raw = pub.get();
    auto prev = std::move(live_);
    live_ = std::move(pub);
    tablesPtr_.store(raw, std::memory_order_seq_cst);
    // Two setTables calls inside one render used to overwrite each other's
    // retired_ slot and drop the older set on the audio thread. The list holds
    // every one of them until the epoch says nobody can be reading it.
    if (prev)
        retired_.push_back({std::move(prev),
                            renderEpoch_.load(std::memory_order_seq_cst)});
    drainRetiredTables();
}

// Message thread only. Quiescence ends snapshot access, but cached oscillator
// states can outlive any number of render calls. Both conditions must hold.
void DrumEngine::drainRetiredTables() {
    const uint32_t now = renderEpoch_.load(std::memory_order_seq_cst);
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
        [now](const RetiredTables& r) {
            if ((now & 1u) != 0u && (uint32_t)(now - r.epoch) < kRetireEpochs)
                return false;
            return std::all_of(r.set->begin(), r.set->end(),
                [](const auto& t) { return t.use_count() == 1; });
        }), retired_.end());
}

// ---- trigger (js:126-143): choke group scan, velocity clamp, phase preset ----
void DrumEngine::trigger(int padI, float vel) {
    if (padI < 0 || padI >= DR_NPADS) return;
    int g = (int)param(dpid(padI, DP_CHOKE));
    if (g > 0) {
        for (int j = 0; j < DR_NPADS; j++)
            if (j != padI && (int)param(dpid(j, DP_CHOKE)) == g) {
                voices_[(size_t)j].choke();
                tails_[(size_t)j].choke();
            }
    }
    PadVoice& v = voices_[(size_t)padI];
    // Finding D2: a retrigger used to zero the sounding voice in one sample,
    // so any decay longer than the step spacing clicked on every hit. Move the
    // old voice to the pad's tail slot and let it run the same choke fade while
    // the new hit starts clean on the main slot. Two hits inside one fade
    // (~1.5 ms) would collide; keep whichever tail is louder.
    if (v.active && (!tails_[(size_t)padI].active
                     || tails_[(size_t)padI].ampLevel <= v.ampLevel)) {
        tails_[(size_t)padI] = v;
        tails_[(size_t)padI].choking = true;
    }
    double vv = std::isfinite(vel) ? (double)vel : 1.0;
    v.trigger(clampd(vv, 0.0, 1.0), rng_.next() * 2.0 - 1.0);
    double phaseA = std::fmod(clampd(param(dpid(padI, DP_OSCA_PHASE)), 0.0, 1.0) * 2048.0, 2048.0);
    for (int i = 0; i < DR_MAXUNI; i++) {
        v.oA.phases[i] = phaseA;
    }
    hits_ |= 1u << padI;
}

// ---- sequencer control (worklet onMsg 'pats'/'chain'/'play'/'stop', js:110-120) ----
void DrumEngine::play() {
    if (hostPlaying_) return;          // host owns the transport while rolling
    playing_ = true; step_ = -1; chainPos_ = 0; samplesToNext_ = 0;
    rhythmFrame_ = 0;
    rhythmMapFrame_ = 0;
    rhythmMapBeat_ = 0.0;
    rhythmHasNext_ = false;
    if (hasPolyRhythm()) {
        rhythmScheduler_.setTempo(sr_, effectiveBpm(),
                                  clampd((double)param(DG_MASTER_SWING), 0.0, 1.0));
        rhythmScheduler_.reset(0.0);
    }
}

void DrumEngine::stop() {
    playing_ = false; step_ = -1;
    rhythmHasNext_ = false;
}

void DrumEngine::setPatterns(const uint8_t* data, int n) {
    if (data && n == (int)pats_.size())
        std::copy(data, data + n, pats_.begin());
}

void DrumEngine::setChain(const int* list, int n) {
    if (!list || n <= 0) return;                    // ignore empty (js:112)
    chain_.assign(list, list + n);
    for (int& c : chain_)                           // js does `x|0`; C++ additionally
        c = std::max(0, std::min(DR_NPATTERNS - 1, c));   // clamps for memory safety
    chainPos_ = std::min(chainPos_, (int)chain_.size() - 1);
}

void DrumEngine::setDrumRhythm(const DrumRhythm* rhythm) {
    if (rhythm == nullptr) {
        if (!hasRhythm_) return;
        hasRhythm_ = false;
        rhythmHasNext_ = false;
        return;
    }
    if (!validateDrumRhythm(*rhythm)) {
        hasRhythm_ = false;
        rhythmHasNext_ = false;
        return;
    }
    if (hasRhythm_ && sameRhythm(rhythm_, *rhythm)) return;
    const double currentBeat = hostPlaying_ ? std::max(0.0, hostPpq_) : currentRhythmBeat();
    rhythm_ = *rhythm;
    hasRhythm_ = true;
    rhythmHasNext_ = false;
    rhythmScheduler_.setRhythm(rhythm_);
    rhythmScheduler_.setTempo(sr_, effectiveBpm(),
                              clampd((double)param(DG_MASTER_SWING), 0.0, 1.0));
    rhythmScheduler_.reset(currentBeat);
    rhythmScheduler_.retime(currentBeat, (std::int64_t)rhythmFrame_);
    rhythmMapBeat_ = currentBeat;
    rhythmMapFrame_ = rhythmFrame_;
}

void DrumEngine::setBpmOverride(double bpm) {
    bpmOverride_ = bpm > 0 ? bpm : 0;
}

double DrumEngine::effectiveBpm() const {
    double pbpm = std::fpclassify(p_[DG_SEQ_BPM]) != FP_ZERO ? (double)p_[DG_SEQ_BPM] : 126.0;
    return bpmOverride_ > 0 ? bpmOverride_ : clampd(pbpm, 60.0, 200.0);
}

double DrumEngine::currentRhythmBeat() const {
    if (!playing_) return rhythmMapBeat_;
    const double bpm = std::max(1.0, rhythmScheduler_.bpm());
    return rhythmMapBeat_ + (double)(rhythmFrame_ - rhythmMapFrame_)
        * bpm / (60.0 * sr_);
}

void DrumEngine::syncRhythmTempo() {
    if (!hasPolyRhythm()) return;
    const double bpm = hostPlaying_ ? hostBpm_ : effectiveBpm();
    const double swing = clampd((double)param(DG_MASTER_SWING), 0.0, 1.0);
    if (std::fabs(rhythmScheduler_.bpm() - bpm) < 1.0e-12
        && std::fabs(rhythmScheduler_.swing() - swing) < 1.0e-12) return;
    const double beat = hostPlaying_ ? std::max(0.0, hostPpq_) : currentRhythmBeat();
    rhythmScheduler_.retime(beat, (std::int64_t)rhythmFrame_);
    rhythmScheduler_.setTempo(sr_, bpm, swing);
    rhythmMapBeat_ = beat;
    rhythmMapFrame_ = rhythmFrame_;
    rhythmHasNext_ = false;
}

void DrumEngine::emitSequencerHit(int pad, float velocity) {
    if (!queueHits_) {
        trigger(pad, velocity);
        return;
    }
    if (pad < 0 || pad >= DR_NPADS) return;
    auto& hit = queuedHits_[(size_t)pad];
    hit = std::max(hit, (std::uint8_t)(velocity >= DR_ACCENT_VEL ? 2 : 1));
}

void DrumEngine::flushSequencerHits() {
    for (int pad = 0; pad < DR_NPADS; ++pad) {
        const auto hit = queuedHits_[(size_t)pad];
        if (hit) trigger(pad, hit == 2 ? DR_ACCENT_VEL : DR_PLAIN_VEL);
    }
    queuedHits_.fill(0);
}

// ---- host transport lock ----
// The DAW owns the clock: steps derive from song position (ppq), so loops,
// jumps and mid-bar starts all land sample-accurately without local state.
void DrumEngine::setHostTransport(double ppq, double bpm, bool playing) {
    if (!std::isfinite(ppq)) ppq = 0;
    if (!(std::isfinite(bpm) && bpm > 1.0)) bpm = 120;
    if (playing && !hostPlaying_) {
        playing_ = false;              // host takes over; internal transport yields
        step_ = -1;
        hostSynced_ = false;
    }
    if (playing && hostSynced_ && std::fabs(ppq - hostEndPpq_) > 1e-4)
        hostSynced_ = false;           // loop / relocate -> resync from ppq
    if (!playing && hostPlaying_)
        step_ = -1;                    // host stopped
    hostPlaying_ = playing;
    hostPpq_ = ppq;
    hostBpm_ = bpm;
}

// Nominal song position of absolute 16th k, with the current swing. Swing
// shifts odd 16ths late by up to DR_SWING_MAX of a step (0.25 ppq), matching
// fireStep()'s `dur - offNow + offNext` timing in the ppq domain.
double DrumEngine::hostStepPpq(long k) const {
    double swing = clampd((double)p_[DG_MASTER_SWING], 0.0, 1.0);
    return (double)k * 0.25 + ((k & 1) ? swing * DR_SWING_MAX * 0.25 : 0.0);
}

// Smallest k >= 0 whose p(k) has not passed yet. k < 0 (pre-roll) never fires.
void DrumEngine::hostResync() {
    long k = (long)std::floor(hostPpq_ / 0.25) - 1;
    if (k < 0) k = 0;
    while (hostStepPpq(k) < hostPpq_ - 1e-9) k++;
    hostNextK_ = k;
    hostSynced_ = true;
    rhythmHasNext_ = false;
    if (hasPolyRhythm()) {
        rhythmScheduler_.setTempo(sr_, hostBpm_,
                                  clampd((double)param(DG_MASTER_SWING), 0.0, 1.0));
        rhythmScheduler_.reset(std::max(0.0, hostPpq_));
        rhythmScheduler_.retime(std::max(0.0, hostPpq_), (std::int64_t)rhythmFrame_);
        rhythmMapBeat_ = std::max(0.0, hostPpq_);
        rhythmMapFrame_ = rhythmFrame_;
    }
}

void DrumEngine::fireHostStep(long k) {
    int  s   = (int)(k % DR_STEPS);
    long bar = k / DR_STEPS;
    chainPos_ = (int)(bar % (long)chain_.size());
    int pat = chain_[(size_t)chainPos_];
    for (int i = 0; i < DR_NPADS; i++) {
        if (polyLaneEnabled(i)) continue;
        uint8_t val = pats_[(size_t)(pat * DR_NPADS * DR_STEPS + i * DR_STEPS + s)];
        if (val) emitSequencerHit(i, val == 2 ? DR_ACCENT_VEL : DR_PLAIN_VEL);
    }
    step_ = s;
}

bool DrumEngine::polyLaneEnabled(int pad) const {
    return pad >= 0 && pad < DR_NPADS && hasRhythm_
        && rhythm_.lanes[(size_t)pad].enabled;
}

bool DrumEngine::hasPolyRhythm() const {
    if (!hasRhythm_) return false;
    for (const auto& lane : rhythm_.lanes)
        if (lane.enabled) return true;
    return false;
}

void DrumEngine::fireRhythmEvent(const DrumRhythmEvent& event) {
    if (event.lane < 0 || event.lane >= DR_NPADS || !polyLaneEnabled(event.lane)) return;
    const int pat = std::max(0, std::min(DR_NPATTERNS - 1, event.sourceBar));
    const int step = std::max(0, std::min(DR_STEPS - 1, event.sourceStep));
    const uint8_t val = pats_[(size_t)(pat * DR_NPADS * DR_STEPS
                                      + event.lane * DR_STEPS + step)];
    if (val) emitSequencerHit(event.lane, val == 2 ? DR_ACCENT_VEL : DR_PLAIN_VEL);
}

// Hosted twin of fireHostStep (docs/sq4-clips.md §6): byte source is the
// ClipHost's live clip instead of pats_/chain_. Each pad's val is
// independent (no tie/slide lookahead), so this is a straight per-pad scan.
void DrumEngine::clipFireAt(int abs) {
    const uint8_t* clip = clipHost_.clipData();
    const int bar = abs / DR_STEPS, s = abs % DR_STEPS;
    for (int i = 0; i < DR_NPADS; i++) {
        uint8_t val = clip[(size_t)(bar * DR_NPADS * DR_STEPS + i * DR_STEPS + s)];
        if (val) emitSequencerHit(i, val == 2 ? DR_ACCENT_VEL : DR_PLAIN_VEL);
    }
}

// ---- fireStep (js:493-518). The host-tempo override bypasses the 60..200 ----
// ---- param clamp — the sequencer follows whatever the DAW runs at.       ----
void DrumEngine::fireStep() {
    double bpm = effectiveBpm();
    double dur = (60.0 / bpm / 4.0) * sr_;
    double swing = p_[DG_MASTER_SWING];
    if (step_ + 1 >= DR_STEPS) {                    // bar wrap advances the chain
        step_ = -1;
        chainPos_ = (chainPos_ + 1) % (int)chain_.size();
    }
    int s = (step_ + 1) % DR_STEPS;
    int pat = chain_[(size_t)chainPos_];
    for (int i = 0; i < DR_NPADS; i++) {
        if (polyLaneEnabled(i)) continue;
        uint8_t val = pats_[(size_t)(pat * DR_NPADS * DR_STEPS + i * DR_STEPS + s)];
        if (val) trigger(i, val == 2 ? DR_ACCENT_VEL : DR_PLAIN_VEL);
    }
    step_ = s;
    double offNow = (s % 2 == 1) ? swing * DR_SWING_MAX * dur : 0.0;
    int sNext = (s + 1) % DR_STEPS;
    double offNext = (sNext % 2 == 1) ? swing * DR_SWING_MAX * dur : 0.0;
    samplesToNext_ = dur - offNow + offNext;
}

// ---- padMod (js:145-169) ----
DrumEngine::Mod DrumEngine::padMod(int padI, const PadVoice& v) const {
    Mod m;
    double dec = std::max(0.002, (double)param(dpid(padI, DP_MODENV_DEC)) / 4.5);
    double env = std::exp(-(double)v.t / (dec * sr_));
    double srcs[4] = { 0.0, env, v.vel * param(dpid(padI, DP_V2M)), v.rand };
    for (int n = 0; n < 4; n++) {
        int src = (int)param(dpid(padI, DP_MOD1_SRC + n * 3));
        int dst = (int)param(dpid(padI, DP_MOD1_DST + n * 3));
        if (src < 1 || src > 3 || dst < 1 || dst > 9) continue;
        double x = srcs[src] * param(dpid(padI, DP_MOD1_AMT + n * 3));
        switch (dst) {
            case 1: m.posA  += x; break;
            case 2: m.posB  += x; break;
            case 3: m.level += x; break;
            case 4: m.cut   += x; break;
            case 5: m.pitch += x * 24; break;
            case 6: m.fineA += x * 200; break;
            case 7: m.fineB += x * 200; break;
            case 8: m.noise += x; break;
            case 9: m.res   += x; break;
        }
    }
    return m;
}

// ---- setupOsc (js:171-230). base = dpid(pad, DP_OSCA_TABLE or DP_OSCB_TABLE);
// the 8 osc fields are contiguous: table,pos,tune,fine,phase,unison,detune,level.
bool DrumEngine::setupOsc(OscState& o, int base, double pitchEnv,
                          double mPos, double mFine, double mPitch, int n) {
    int ti = (int)param(base);
    const DrumTable* table =
        (ti >= 0 && ti < (int)curTables_->size() && (*curTables_)[(size_t)ti]->data)
            ? (*curTables_)[(size_t)ti].get() : nullptr;
    if (!table) return false;

    double basePitch = DR_BASE_NOTE + param(base + 2)
                     + (param(base + 3) + mFine) / 100.0 + pitchEnv + mPitch;
    double freq = 440.0 * std::pow(2.0, (basePitch - 69.0) / 12.0);
    if (!(freq > 0 && freq <= sr_ * 0.45)) return false;

    double level = clampd(param(base + 7), 0.0, 1.2);
    level *= level;
    if (!(level >= 1e-5)) return false;

    int uni = std::max(1, std::min(DR_MAXUNI, (int)param(base + 5)));
    double det = param(base + 6);
    const double spr = 0.6;

    double pos = clampd(param(base + 1) + mPos, 0.0, 1.0);
    if (o.posSm < 0) o.posSm = pos;
    o.posSm += (pos - o.posSm) * smoothCoef(n, DR_POS_TAU * sr_);
    double posF = o.posSm * (table->frames - 1);
    int f0 = (int)posF;
    o.posF = posF;
    o.f0 = f0;

    double cps = freq / sr_;
    o.data = table->data;
    o.tableOwner = (*curTables_)[(size_t)ti];
    o.frames = table->frames;
    o.mips = table->mips;
    o.mask = table->mask;
    o.size = table->size;
    o.uni = uni;

    for (int u = 0; u < uni; u++) {
        double sprd = uni > 1 ? ((double)u / (uni - 1)) * 2 - 1 : 0;
        double cents = sprd * det * 50;
        double ratio = std::pow(2.0, cents / 1200.0);
        o.incs[u] = cps * ratio * table->size;
        double pan = clampd(sprd * spr, -1.0, 1.0);
        double a = ((pan + 1) * kPi) / 4;
        o.gl[u] = (float)std::cos(a);
        o.gr[u] = (float)std::sin(a);
    }
    o.gain = (level * 0.32) / std::sqrt((double)uni);
    return true;
}

// ---- renderOsc (js:232-280) — Finding 7 + 10: increments / morph fraction /
// pan gains ramp from the previous sub-block's targets (staircase-free pitch
// env / mod sweeps), and table reads are cubic Hermite. Same scheme as
// Engine::renderOsc — see there for the ramp-validity rules. ----
void DrumEngine::renderOsc(OscState& o, float* tmpL, float* tmpR, int off, int n) {
    const float* data = o.data;
    const int mask = o.mask, size = o.size;
    const double invN = 1.0 / n;
    const bool rp = o.havePrev && o.pUni == o.uni && o.pData == data;
    const double ft1 = o.posF - o.f0;
    const double ft0 = rp ? clampd(o.pPosF - o.f0, -1.0, 1.0) : ft1;
    const double dFt = (ft1 - ft0) * invN;
    // Derive the mip from the actual linearly ramped increment, not a
    // logarithmic pitch target. Both tables remain below Nyquist throughout
    // this 0.07-octave fade (0.475 * 2^0.07 < 0.5), including rapid envelopes.
    // Precompute once for all unison voices; no allocations in this path.
    int coarse[128], fine[128];
    double blend[128];
    double maxInc0 = 0, maxInc1 = 0;
    for (int u = 0; u < o.uni; ++u) {
        maxInc0 = std::max(maxInc0, rp ? o.pIncs[u] : o.incs[u]);
        maxInc1 = std::max(maxInc1, o.incs[u]);
    }
    const int mipSamples = maxInc0 == maxInc1 ? 1 : n;
    for (int i = 0; i < mipSamples; ++i) {
        const double inc = maxInc0 + (maxInc1 - maxInc0) * (i * invN);
        const double mipF = std::log2(inc * 0.5 / 0.475);
        const int mip = mipF > 0 ? std::min(o.mips - 1, (int)std::ceil(mipF)) : 0;
        blend[i] = mip > 0 ? clampd(1 - (mipF - (mip - 1)) / 0.07, 0.0, 1.0) : 0;
        coarse[i] = mip * size;
        fine[i] = std::max(0, mip - 1) * size;
    }
    if (mipSamples == 1) {
        std::fill_n(coarse + 1, n - 1, coarse[0]);
        std::fill_n(fine + 1, n - 1, fine[0]);
        std::fill_n(blend + 1, n - 1, blend[0]);
    }
    const double g = o.gain;
    const int frame0 = o.f0 * o.mips * size;
    const int frame1 = std::min(o.frames - 1, o.f0 + 1) * o.mips * size;
    for (int u = 0; u < o.uni; u++) {
        double ph = o.phases[u];
        const double inc1 = o.incs[u];
        const double inc0 = rp ? o.pIncs[u] : inc1;
        const double dInc = (inc1 - inc0) * invN;
        const double gl1 = o.gl[u] * g, gr1 = o.gr[u] * g;
        const double gl0 = rp ? (double)o.pGl[u] : gl1, gr0 = rp ? (double)o.pGr[u] : gr1;
        const double dGl = (gl1 - gl0) * invN, dGr = (gr1 - gr0) * invN;
        for (int i = 0; i < n; i++) {
            int idx = (int)ph;
            double frac = ph - idx;
            int im1 = (idx - 1) & mask, i2 = (idx + 1) & mask, i3 = (idx + 2) & mask;
            const double ft = ft0 + dFt * i;
            double sc0 = rdH(data, frame0 + coarse[i], im1, idx, i2, i3, frac);
            double sc1 = rdH(data, frame1 + coarse[i], im1, idx, i2, i3, frac);
            double s = sc0 + ft * (sc1 - sc0);
            if (blend[i] > 0) {
                double sf0 = rdH(data, frame0 + fine[i], im1, idx, i2, i3, frac);
                double sf1 = rdH(data, frame1 + fine[i], im1, idx, i2, i3, frac);
                s += blend[i] * (sf0 + ft * (sf1 - sf0) - s);
            }
            tmpL[off + i] += (float)(s * (gl0 + dGl * i));
            tmpR[off + i] += (float)(s * (gr0 + dGr * i));
            ph += inc0 + dInc * i;
            if (ph >= size) ph -= size;
        }
        o.phases[u] = ph;
        o.pIncs[u] = inc1;
        o.pGl[u] = (float)gl1; o.pGr[u] = (float)gr1;
    }
    o.pPosF = o.posF; o.pUni = o.uni; o.pData = data;
    o.havePrev = true;
}

// Finding J2: one sub-block of the OSC A layer. Selecting another table (or
// another unison count) swapped the read out from under the oscillator in a
// single sample — a step that can be as large as the waveform's own peak, the
// same class of click the filter-type crossfade already removed. On a switch
// the voice forks: `oAx` keeps running the OUTGOING configuration, including
// its table pointer and phases, and the two renders are mixed equal-power over
// DR_SWITCH_FADE. A second switch inside the fade simply restarts it from the
// current mix, exactly like the filter-type case.
void DrumEngine::renderOscLayer(PadVoice& v, int padI, double pitchEnv, const Mod& m,
                                float* tmpL, float* tmpR, int at, int count) {
    const OscState before = v.oA;                 // pre-setup: the outgoing config
    const bool aOn = setupOsc(v.oA, dpid(padI, DP_OSCA_TABLE), pitchEnv,
                              m.posA, m.fineA, m.pitch, count);
    if (aOn && before.havePrev && before.data != nullptr
        && (v.oA.data != before.data || v.oA.uni != before.uni)) {
        v.oAx = before;
        v.oAx.havePrev = false;      // frozen configuration: nothing to ramp from
        v.oAxLen = std::max(1, (int)(DR_SWITCH_FADE * sr_));
        v.oAxLeft = v.oAxLen;
        v.oA.havePrev = false;       // and the incoming one has no valid previous target
    }

    if (v.oAxLeft <= 0) {
        if (aOn) renderOsc(v.oA, tmpL, tmpR, at, count);
        else {
            v.oA.havePrev = false;
            v.oA.data = nullptr; v.oA.tableOwner.reset();
        }
        return;
    }

    std::fill(xL_, xL_ + count, 0.0f); std::fill(xR_, xR_ + count, 0.0f);
    std::fill(yL_, yL_ + count, 0.0f); std::fill(yR_, yR_ + count, 0.0f);
    renderOsc(v.oAx, xL_, xR_, 0, count);
    if (aOn) renderOsc(v.oA, yL_, yR_, 0, count);
    else {
        v.oA.havePrev = false;
        v.oA.data = nullptr; v.oA.tableOwner.reset();
    }
    for (int i = 0; i < count; i++) {
        double wOut, wIn;
        switchWeights(v.oAxLeft, i, v.oAxLen, wOut, wIn);
        tmpL[at + i] += (float)(wOut * xL_[i] + wIn * yL_[i]);
        tmpR[at + i] += (float)(wOut * xR_[i] + wIn * yR_[i]);
    }
    v.oAxLeft = std::max(0, v.oAxLeft - count);
    if (v.oAxLeft == 0) {
        v.oAx.data = nullptr; v.oAx.tableOwner.reset();
    }
}

// One instance of the sample layer: a slot and region frozen at call time, so
// the same read path serves both sides of the Finding J2 crossfade.
struct DrumSampleRun {
    const int16_t* d = nullptr;
    int    last = 0;
    double lo = 0, hi = 0;
    bool   reverse = false;
    double gain = 0;
    double step0 = 0, dStep = 0;
    bool   fadeInEdge = false;
    double half = 0;
    double sr = 48000;
};

// Render `n` samples of one instance. `xfLen > 0` applies the equal-power
// switch weight: `fadeOut` for the outgoing slot, its complement for the
// incoming one. Advances `pos` and sets `done` when the region runs out.
static void runSampleLayer(const DrumSampleRun& c, double& pos, bool& done,
                           float* tmpL, float* tmpR, int off, int n,
                           int xfLeft, int xfLen, bool fadeOut) {
    const int16_t* d = c.d;
    const int last = c.last;
    // Hermite read with clamped edges (the source is a one-shot, not a loop).
    auto rd = [d, last](double p) -> double {
        const int i1c = (int)std::floor(p);
        const double f = p - i1c;
        const int i1 = std::max(0, std::min(last, i1c));
        const int i0 = std::max(0, std::min(last, i1c - 1));
        const int i2 = std::max(0, std::min(last, i1c + 1));
        const int i3 = std::max(0, std::min(last, i1c + 2));
        const double ym1 = d[i0], y0 = d[i1], y1 = d[i2], y2 = d[i3];
        const double c1 = 0.5 * (y1 - ym1);
        const double c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2;
        const double c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
        return ((c3 * f + c2) * f + c1) * f + y0;
    };

    for (int i = 0; i < n; ++i) {
        if ((!c.reverse && pos >= c.hi) || (c.reverse && pos <= c.lo)) {
            done = true;
            break;
        }
        const double st = c.step0 + c.dStep * i;
        const double ast = std::fabs(st);
        const double fadeSrc = std::min(std::max(ast, 1.0) * DR_SAMPLE_FADE * c.sr,
                                        std::max(c.half, 1.0));
        const double dIn  = c.reverse ? c.hi - pos : pos - c.lo;
        const double dOut = c.reverse ? pos - c.lo : c.hi - pos;
        double fade = clampd(dOut / fadeSrc, 0.0, 1.0);
        if (c.fadeInEdge) fade = std::min(fade, clampd(dIn / fadeSrc, 0.0, 1.0));
        if (xfLen > 0) {
            double wOut, wIn;
            switchWeights(xfLeft, i, xfLen, wOut, wIn);
            fade *= fadeOut ? wOut : wIn;
        }

        double value = rd(pos);
        if (ast > 1.0) {
            // Decimation pre-filter: a short Hann-weighted average across the
            // source samples this output sample spans, so pitching up folds far
            // less energy back into the band. Mixed in over the first octave so
            // a pitch envelope crossing 1 source sample per output sample does
            // not switch filters. Capped at 8 taps: beyond +3 octaves the window
            // is undersampled and some fold-back remains.
            const int taps = std::min(8, (int)std::ceil(ast) + 1);
            double acc = 0, wsum = 0;
            for (int k = 0; k < taps; ++k) {
                const double u = (double)k / (taps - 1) - 0.5;
                const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * (k + 1) / (taps + 1));
                acc += w * rd(pos + u * ast);
                wsum += w;
            }
            value += (acc / wsum - value) * clampd(ast - 1.0, 0.0, 1.0);
        }
        const float out = (float)(value * c.gain * fade);
        tmpL[off + i] += out;
        tmpR[off + i] += out;
        pos += st;
    }
}

// Raw PCM16 one-shot player. The legacy oscB parameter slots now select and
// shape this layer, preserving saved automation and all later flat ids.
//
// Finding D4: cubic Hermite reads instead of linear, a Hann-weighted decimation
// average as the anti-alias pre-filter whenever the read rate exceeds 1
// source sample per output sample, DR_SAMPLE_FADE edge fades on an interior
// START/END and on the reverse stop, and the read rate ramped per sample the
// way renderOsc ramps its increments (it used to be held for 16 samples, so the
// pitch envelope staircased on this layer only).
//
// Finding J2 splits the read loop into runSampleLayer above so the same path
// can serve both sides of a sample-slot crossfade.
void DrumEngine::renderSample(SampleState& state, int padI, double pitchEnv,
                              double modStart, double modFine, double modPitch,
                              float* tmpL, float* tmpR, int off, int n) const {
    const auto& bank = drumOneShots();
    if (bank.empty()) return;
    if (state.done && state.xfLeft <= 0) return;
    const int index = std::max(0, std::min((int)bank.size() - 1,
        (int)param(dpid(padI, DP_OSCB_TABLE))));

    const double start = clampd(param(dpid(padI, DP_OSCB_POS)) + modStart, 0.0, 0.999);
    const double end = std::max(start + 1.0 / bank[(size_t)index].length,
        clampd(param(dpid(padI, DP_OSCB_DETUNE)), 0.0, 1.0));
    const bool reverse = param(dpid(padI, DP_OSCB_PHASE)) >= 0.5f;

    // Finding J2: the slot changed under a playing one-shot. Fork before the
    // reset below moves `pos` back to START: the old slot keeps reading from
    // where it is and fades out while the new one fades in.
    if (state.index >= 0 && state.index != index && state.pos >= 0 && !state.done) {
        state.xfIndex = state.index;
        state.xfPos = state.pos;
        state.xfDone = false;
        state.xfLen = std::max(1, (int)(DR_SWITCH_FADE * sr_));
        state.xfLeft = state.xfLen;
    }
    if (state.pos < 0 || state.index != index) {
        state.index = index;
        state.pos = (reverse ? end : start) * (bank[(size_t)index].length - 1);
        state.havePrev = false;
    }

    const double semis = param(dpid(padI, DP_OSCB_TUNE))
        + (param(dpid(padI, DP_OSCB_FINE)) + modFine) / 100.0
        + pitchEnv + modPitch;
    const double level = clampd(param(dpid(padI, DP_OSCB_LEVEL)), 0.0, 1.2);
    const double pitchRatio = std::pow(2.0, semis / 12.0) * (reverse ? -1.0 : 1.0);

    // Build one frozen instance for a slot at the current region/tune settings.
    auto makeRun = [&](int slot, double step0, double dStep) {
        const auto& sample = bank[(size_t)slot];
        DrumSampleRun c;
        c.d = sample.data;
        c.last = sample.length - 1;
        c.lo = start * c.last;
        c.hi = end * c.last;
        c.reverse = reverse;
        c.gain = level * level * 0.75 / 32768.0;
        c.step0 = step0;
        c.dStep = dStep;
        // Edge fades, expressed in source samples so the fade lasts the same
        // output time at any transposition. A region that starts at the
        // sample's own beginning keeps its transient: only an interior edge is
        // faded in.
        c.half = 0.5 * (c.hi - c.lo);
        c.fadeInEdge = reverse ? (c.hi < c.last - 1e-9) : (c.lo > 1e-9);
        c.sr = sr_;
        return c;
    };

    // The outgoing slot first: it must run even after the incoming one is done.
    // xfLen == 0 means "no weighting": the common path skips the sin/cos.
    const int xfLeft = state.xfLeft, xfLen = state.xfLeft > 0 ? state.xfLen : 0;
    if (xfLeft > 0 && state.xfIndex >= 0 && !state.xfDone) {
        const auto& old = bank[(size_t)state.xfIndex];
        if (old.data != nullptr && old.length >= 2) {
            const double st = ((double)old.sampleRate / sr_) * pitchRatio;
            runSampleLayer(makeRun(state.xfIndex, st, 0.0), state.xfPos, state.xfDone,
                           tmpL, tmpR, off, n, xfLeft, xfLen, true);
        } else {
            state.xfDone = true;
        }
    }
    if (xfLeft > 0) state.xfLeft = std::max(0, xfLeft - n);

    if (state.done) return;
    const auto& sample = bank[(size_t)index];
    if (sample.data == nullptr || sample.length < 2) return;
    const double step1 = ((double)sample.sampleRate / sr_) * pitchRatio;
    const double step0 = state.havePrev ? state.pStep : step1;
    const double dStep = n > 0 ? (step1 - step0) / n : 0.0;
    runSampleLayer(makeRun(index, step0, dStep), state.pos, state.done,
                   tmpL, tmpR, off, n, xfLeft, xfLen, false);
    state.pStep = step1;
    state.havePrev = true;
}

// ---- setupFilter (js:282-301): Cytomic SVF; smoothing is chunk-invariant
// (Finding 6) and runFilter ramps cutPrev -> cutTarget (Finding 7). ----
void DrumEngine::setupFilter(FilterState& fs, int padI, double mCut, double mRes, int n) {
    int ftype = (int)param(dpid(padI, DP_FLT_TYPE));
    fs.ftype = ftype;
    double fc = param(dpid(padI, DP_FLT_CUT)) * std::pow(2.0, mCut * DR_MOD_LOG_D);
    if (!std::isfinite(fc)) fc = 20;
    fc = clampd(fc, 20.0, sr_ * 0.45);
    if (fs.cutSm <= 0) fs.cutSm = fc;
    fs.cutSm += (fc - fs.cutSm) * smoothCoef(n, DR_CUT_TAU * sr_);
    fs.cutTarget = fs.cutSm;
    double res = clampd(param(dpid(padI, DP_FLT_RES)) + mRes, 0.0, 0.999);

    fs.twoPole = ftype == 1;
    fs.k1 = 2 - 1.93 * res;           // a1..a3 recomputed per sub-block in runFilter
}

// ---- runFilter (js:303-367): ADAA lcosh drive, SVF, LP24 second pass ----
void DrumEngine::runFilter(FilterState& fs, const float* inL, const float* inR,
                           float* outL, float* outR, double drive, int n) const {
    const double adaaTarget = clampd(drive / DR_ADAA_FADE_WIDTH, 0.0, 1.0);
    const double adaaStart = fs.adaaMix;
    if (adaaTarget > 0 || adaaStart > 1e-6) {
        const double dg = 1 + drive * 7;
        const double dcomp = 1 / std::pow(dg, 0.55);
        const double kF = dcomp / dg;
        double xpL = fs.satXL, xpR = fs.satXR;
        double FpL = kF * lcosh(dg * xpL), FpR = kF * lcosh(dg * xpR);
        for (int i = 0; i < n; i++) {
            const double aL = adaaInput(inL[i]), aR = adaaInput(inR[i]);
            const double dxL = aL - xpL;
            const double FL = kF * lcosh(dg * aL);
            const double satL = dxL > 1e-5 || dxL < -1e-5 ? (FL - FpL) / dxL
                                                          : dcomp * std::tanh(dg * 0.5 * (aL + xpL));
            const double dxR = aR - xpR;
            const double FR = kF * lcosh(dg * aR);
            const double satR = dxR > 1e-5 || dxR < -1e-5 ? (FR - FpR) / dxR
                                                          : dcomp * std::tanh(dg * 0.5 * (aR + xpR));
            const double mix = adaaStart + (adaaTarget - adaaStart) * ((double)(i + 1) / std::max(1, n));
            outL[i] = (float)(aL + mix * (satL - aL));
            xpL = aL; FpL = FL;
            outR[i] = (float)(aR + mix * (satR - aR));
            xpR = aR; FpR = FR;
        }
        fs.satXL = xpL; fs.satXR = xpR;
        fs.adaaMix = adaaTarget;
    } else {
        for (int i = 0; i < n; i++) { outL[i] = inL[i]; outR[i] = inR[i]; }
        if (n > 0) {
            fs.satXL = adaaInput(inL[n - 1]); fs.satXR = adaaInput(inR[n - 1]);
        }
        fs.adaaMix = 0;
    }

    // Finding 7: cutoff ramps from the previous chunk's value; coefficients
    // recomputed per <=32-sample sub-block.
    const int ftype = fs.ftype;
    const double k1 = fs.k1;
    const double c1c = fs.cutTarget;
    const double c0c = fs.cutPrev > 0 ? fs.cutPrev : c1c;

    // Finding D9: the output selector used to be a switch inside the sample
    // loop. A generic lambda takes it per sub-block instead, so the inner loop
    // is branch-free and each output form inlines on its own.
    auto stage = [](float* buf, double* F, int o1, int at, int m,
                    double a1, double a2, double a3, auto sel) {
        double ic1 = F[o1], ic2 = F[o1 + 1];
        for (int i = at; i < at + m; i++) {
            const double x = buf[i];
            const double v3 = x - ic2;
            const double v1 = a1 * ic1 + a2 * v3;
            const double v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1;
            ic2 = 2 * v2 - ic2;
            buf[i] = (float)sel(x, v1, v2);
        }
        F[o1] = ic1; F[o1 + 1] = ic2;
    };
    const auto selLp = [](double, double, double v2) { return v2; };
    const auto selBp = [k1](double, double v1, double) { return k1 * v1; };
    const auto selNo = [k1](double x, double v1, double v2) { return x - k1 * v1 - v2; };
    const auto selHp = [k1](double x, double v1, double) { return x - k1 * v1; };

    // One full pass over the chunk for one filter type, on its own state.
    auto runSvf = [&](float* bufL, float* bufR, double* F, int type, bool two) {
        for (int at = 0; at < n; at += 32) {
            const int m = std::min(32, n - at);
            const double cut = c0c + (c1c - c0c) * ((double)(at + m) / n);
            const double gC = std::tan((kPi * cut) / sr_);
            const double a1 = 1 / (1 + gC * (gC + k1));
            const double a2 = gC * a1, a3 = gC * a2;
            for (int ch = 0; ch < 2; ch++) {
                float* buf = ch == 0 ? bufL : bufR;
                const int o1 = ch * 2;
                switch (type) {
                    case 0: case 1: stage(buf, F, o1, at, m, a1, a2, a3, selLp); break;
                    case 2:         stage(buf, F, o1, at, m, a1, a2, a3, selBp); break;
                    case 3:         stage(buf, F, o1, at, m, a1, a2, a3, selNo); break;
                    default:        stage(buf, F, o1, at, m, a1, a2, a3, selHp); break;
                }
            }
            if (two)
                for (int ch = 0; ch < 2; ch++)
                    stage(ch == 0 ? bufL : bufR, F, 4 + ch * 2, at, m, a1, a2, a3, selLp);
        }
    };

    // Discrete-switch crossfade. A type change forks the state: the outgoing
    // type keeps running on svfOld while the incoming one takes over svf, and
    // the two are mixed over DR_SWITCH_FADE. A second change inside the fade
    // simply restarts it from the current mix (2 x 3 ms apart never happens in
    // practice, and restarting is still far smaller a step than a hard switch).
    if (fs.pFtype >= 0 && (fs.pFtype != ftype || fs.pTwoPole != fs.twoPole)) {
        std::copy(std::begin(fs.svf), std::end(fs.svf), std::begin(fs.svfOld));
        fs.xfType = fs.pFtype;
        fs.xfTwoPole = fs.pTwoPole;
        fs.xfLen = std::max(1, (int)(DR_SWITCH_FADE * sr_));
        fs.xfLeft = fs.xfLen;
    }
    fs.pFtype = ftype;
    fs.pTwoPole = fs.twoPole;

    if (fs.xfLeft > 0) {
        // renderPad's scratch buffers are 128 samples, so n never exceeds that.
        float oldL[128], oldR[128];
        const int m = n;
        std::copy(outL, outL + m, oldL);
        std::copy(outR, outR + m, oldR);
        runSvf(oldL, oldR, fs.svfOld, fs.xfType, fs.xfTwoPole);
        runSvf(outL, outR, fs.svf, ftype, fs.twoPole);
        const double inv = 1.0 / fs.xfLen;
        for (int i = 0; i < m; i++) {
            const int left = fs.xfLeft - i;
            const double w = left > 0 ? 1.0 - (double)left * inv : 1.0;  // 0 -> 1
            outL[i] = (float)(oldL[i] + (outL[i] - oldL[i]) * w);
            outR[i] = (float)(oldR[i] + (outR[i] - oldR[i]) * w);
        }
        fs.xfLeft = std::max(0, fs.xfLeft - m);
    } else {
        runSvf(outL, outR, fs.svf, ftype, fs.twoPole);
    }
    fs.cutPrev = c1c;
}

// ---- ampEnv (js:369-383): one-shot AHD, DECAY morphs linear->exp by CURVE ----
double DrumEngine::ampEnv(const PadVoice& v, int padI, int i) const {
    double att = std::max(1.0, param(dpid(padI, DP_AENV_ATT)) * sr_);
    double hold = param(dpid(padI, DP_AENV_HOLD)) * sr_;
    double dec = std::max(1.0, param(dpid(padI, DP_AENV_DEC)) * sr_);
    double t = (double)v.t + i;
    if (t < att) return t / att;
    double td = t - att - hold;
    if (td < 0) return 1;
    if (td >= dec) return 0;
    double lin = 1 - td / dec;
    // Finding D3: the raw exponential still sits at e^-4.5 (0.011) when td
    // reaches dec, so the envelope used to end with a -40 dB step. Normalise it
    // to reach exactly 0 at the boundary; the shape is otherwise unchanged.
    double ex = (std::exp(-4.5 * td / dec) - kExpEnd) * kExpNorm;
    double c = param(dpid(padI, DP_AENV_CURVE));
    return lin + (ex - lin) * c;
}

// ---- renderPad (js:385-454) ----
void DrumEngine::renderPad(PadVoice& v, int padI, float* L, float* R, int off, int n) {
    Mod m = padMod(padI, v);
    float* tmpL = tmpL_;
    float* tmpR = tmpR_;
    std::fill(tmpL, tmpL + n, 0.0f);
    std::fill(tmpR, tmpR + n, 0.0f);

    // oscillators, re-evaluated every 16-sample subblock with the pitch env
    double pDec = std::max(0.002, (double)param(dpid(padI, DP_PENV_DEC)));
    double pAmt = param(dpid(padI, DP_PENV_AMT));
    for (int at = 0; at < n; at += 16) {
        int count = std::min(16, n - at);
        double pe = pAmt * std::exp(-4.5 * (double)(v.t + at) / (pDec * sr_));
        renderOscLayer(v, padI, pe, m, tmpL, tmpR, at, count);
        renderSample(v.sample, padI, pe, m.posB, m.fineB, m.pitch,
                     tmpL, tmpR, at, count);
    }

    // noise: white -> one-pole tilt, level squared x 0.35
    double noiseLevel = clampd(param(dpid(padI, DP_NOISE_LEVEL)) + m.noise, 0.0, 1.0);
    double noiseGain = noiseLevel * noiseLevel * 0.35;
    if (noiseGain > 1e-6) {
        double color = clampd(param(dpid(padI, DP_NOISE_COLOR)), -1.0, 1.0);
        // Finding 9: the tilt coefficient is specified at 48 kHz; map the pole
        // so the noise color is identical at any rate (exact at 48 kHz).
        double a48 = 0.02 + (color + 1) * 0.49;
        double a = 1 - std::pow(1 - a48, 48000.0 / sr_);
        double y = v.noiseY;
        for (int i = 0; i < n; i++) {
            double w = rng_.next() * 2.0 - 1.0;
            y += (w - y) * a;
            float s = (float)(y * noiseGain);
            tmpL[i] += s; tmpR[i] += s;
        }
        v.noiseY = y;
    }

    // Fixed-Hz sine ring modulation creates inharmonic sidebands for bells,
    // struck metal and cymbals. MIX=0 is an exact bypass; sqrt(2) compensates
    // the sine carrier's RMS loss at full wet.
    const double ringMix = clampd(param(dpid(padI, DP_RING_MIX)), 0.0, 1.0);
    if (ringMix > 1e-6) {
        const double ringFreq = clampd(param(dpid(padI, DP_RING_FREQ)), 20.0, sr_ * 0.45);
        const double ringInc = ringFreq / sr_;
        double phase = v.ringPhase;
        for (int i = 0; i < n; ++i) {
            const double carrier = std::sin(phase * 2.0 * kPi) * std::sqrt(2.0);
            const float gain = (float)(1.0 + ringMix * (carrier - 1.0));
            tmpL[i] *= gain;
            tmpR[i] *= gain;
            phase += ringInc;
            if (phase >= 1.0) phase -= 1.0;
        }
        v.ringPhase = phase;
    }

    const float* srcL = tmpL;
    const float* srcR = tmpR;
    if (std::fpclassify(param(dpid(padI, DP_FLT_ON))) != FP_ZERO) {
        setupFilter(v.f, padI, m.cut, m.res, n);
        runFilter(v.f, tmpL, tmpR, fL_, fR_, param(dpid(padI, DP_FLT_DRIVE)), n);
        srcL = fL_; srcR = fR_;
    }

    double velGain = 1 - param(dpid(padI, DP_V2L)) * (1 - v.vel);
    double level = clampd(param(dpid(padI, DP_LVL)) + m.level, 0.0, 1.0);
    // m.level is block-rate (mod env) — ramp the gain across the chunk
    // (Finding 7); the DC pole is sr-derived (Finding 9).
    double lg1 = velGain * level * level;
    double lg0 = v.lgPrev >= 0 ? v.lgPrev : lg1;
    double dLg = (lg1 - lg0) / n;
    double pan = clampd(param(dpid(padI, DP_PAN)), -1.0, 1.0);
    double panA = ((pan + 1) * kPi) / 4;
    double panL = std::cos(panA), panR = std::sin(panA);
    for (int i = 0; i < n; i++) {
        const double xl = srcL[i], xr = srcR[i];
        const double yL = xl - v.dcxL + dcR_ * v.dcyL;   // per-voice DC block
        const double yR = xr - v.dcxR + dcR_ * v.dcyR;
        v.dcxL = xl; v.dcyL = yL;
        v.dcxR = xr; v.dcyR = yR;

        if (v.choking) {
            v.ampLevel *= 1 - chokeCoef_;
            if (v.ampLevel < 1e-4) {
                v.kill();
                break;
            }
        } else {
            v.ampLevel = ampEnv(v, padI, i);
        }
        const double amp = v.ampLevel * (lg0 + dLg * i);
        L[off + i] += (float)(yL * amp * panL);
        R[off + i] += (float)(yR * amp * panR);
    }
    v.lgPrev = lg1;

    v.t += n;
    double end = (param(dpid(padI, DP_AENV_ATT)) + param(dpid(padI, DP_AENV_HOLD))
                + param(dpid(padI, DP_AENV_DEC))) * sr_;
    if (v.active && !v.choking && (double)v.t >= end && v.ampLevel < 1e-4) v.kill();
}

// ---- process (js:456-491). Chunks to <=128 samples so padMod's block-  ----
// ---- rate env matches the worklet's 128-sample process cadence, and    ----
// ---- additionally splits at step boundaries (samplesToNext) so steps   ----
// ---- fire sample-accurately regardless of the host buffer size.        ----
void DrumEngine::render(float* outs[DR_NBUSES][2], int n) {
    for (int b = 0; b < DR_NBUSES; b++)
        for (int c = 0; c < 2; c++)
            std::fill(outs[b][c], outs[b][c] + n, 0.0f);

    // Findings 2 + J3: one acquire load of a raw pointer, held for the whole
    // call so setupOsc's cached data pointers stay valid even if the message
    // thread publishes a new set mid-block. The odd epoch published here is
    // what stops setTables from freeing the set under us. Oscillator pins keep
    // cached reads alive between calls; retired sets retain their final owner.
    renderEpoch_.fetch_add(1, std::memory_order_seq_cst);      // odd: in render
    curTables_ = tablesPtr_.load(std::memory_order_seq_cst);

    // Finding J1: start from the raw targets for everything that is not
    // smoothed, then hand the DSP the smoothed view; advanceSmoothers moves it
    // once per chunk below. The FX chains are re-parameterised per chunk too
    // (they used to see one value for the whole host block), which is what
    // makes an automated FX amount a ramp instead of a step.
    beginBlockParams(n);

    // Host-locked mode: step times come from song position, not samplesToNext_.
    // Hosted clip mode owns the transport exclusively: it suppresses both
    // the host-transport-locked and internal-clock firing below so the
    // standalone sequencer and the hosted clip can never double-fire pads.
    const bool hostRun = hostPlaying_ && !hostClipMode_;
    double ppqPerSample = 0, samplesPerPpq = 0;
    if (hostRun) {
        ppqPerSample = hostBpm_ / 60.0 / sr_;
        samplesPerPpq = 1.0 / ppqPerSample;
        if (!hostSynced_) hostResync();
    }
    const bool internalRun = playing_ && !hostClipMode_;
    const bool polyRun = hasPolyRhythm() && (hostRun || internalRun);
    syncRhythmTempo();
    queueHits_ = polyRun;

    int pos = 0;
    while (pos < n) {
        int run = std::min(128, n - pos);
        if (hostRun) {
            // Fire every step due at/before pos; split the run at the next one.
            for (;;) {
                if (polyRun) {
                    if (!rhythmHasNext_) {
                        const double endBeat = hostPpq_ + (pos + run) * ppqPerSample;
                        if (rhythmScheduler_.nextEvent(endBeat - 1.0e-12, rhythmNext_))
                            rhythmHasNext_ = true;
                    }
                    if (rhythmHasNext_) {
                        const double eventPos = (rhythmNext_.beat - hostPpq_) / ppqPerSample;
                        if (eventPos <= pos + 1.0e-9) {
                            fireRhythmEvent(rhythmNext_);
                            rhythmHasNext_ = false;
                            continue;
                        }
                        if (eventPos - pos < run)
                            run = std::max(1, (int)std::ceil(eventPos - pos));
                    }
                }
                long fireAt = (long)std::ceil(
                    (hostStepPpq(hostNextK_) - hostPpq_) * samplesPerPpq - 1e-9);
                if (fireAt <= pos) { fireHostStep(hostNextK_++); continue; }
                if (fireAt - pos < run) run = (int)(fireAt - pos);
                break;
            }
        } else if (internalRun) {                    // js:465-469
            if (polyRun) {
                if (!rhythmHasNext_) {
                    const double bpm = effectiveBpm();
                    const auto endFrame = static_cast<std::int64_t>(rhythmFrame_)
                        + pos + run;
                    const double endBeat = rhythmMapBeat_
                        + (double)(endFrame - static_cast<std::int64_t>(rhythmMapFrame_))
                            * bpm / (60.0 * sr_);
                    if (rhythmScheduler_.nextEvent(endBeat - 1.0e-12, rhythmNext_))
                        rhythmHasNext_ = true;
                }
                if (rhythmHasNext_) {
                    const auto eventFrame = rhythmNext_.sample;
                    const auto nowFrame = static_cast<std::int64_t>(rhythmFrame_) + pos;
                    if (eventFrame <= nowFrame) {
                        fireRhythmEvent(rhythmNext_);
                        rhythmHasNext_ = false;
                        continue;
                    }
                    if (eventFrame - nowFrame < run)
                        run = std::max(1, (int)std::min<std::int64_t>(
                            run, eventFrame - nowFrame));
                }
            }
            if (samplesToNext_ <= 0) fireStep();
            run = std::min(run, (int)std::ceil(samplesToNext_));
        } else if (hostClipMode_) {
            // At most one fire per quantum (ClipHost contract). DR-1 pads
            // are one-shot voices with no gate to release on Stop/swap
            // (worklet-drum.js hostTick/clipFire never touch a sounding
            // pad) — 2-arg tick, no onSwap hook needed.
            clipHost_.tick(hostFrame_, run, [&](int abs) { clipFireAt(abs); });
        }
        flushSequencerHits();
        // Finding J1: the chunk length is now known, so move every smoother by
        // exactly this many samples and re-derive the FX coefficients from the
        // result. setParams is skipped for a pad whose values did not move, so
        // a static patch costs one memcmp per pad per chunk.
        advanceSmoothers(run);
        if (padFxEnabled_) {
            for (int i = 0; i < DR_NPADS; ++i) {
                const int b = dpid(i, DP_FXDRIVE_ON);
                if (std::memcmp(&fxSeen_[(size_t)i][0], &ps_[(size_t)b],
                                kNFxFields * sizeof(float)) != 0) {
                    std::memcpy(&fxSeen_[(size_t)i][0], &ps_[(size_t)b],
                                kNFxFields * sizeof(float));
                    padFx_[(size_t)i].setParams(ps_, i);
                }
            }
            const int groupBase = dgfx(DP_FXDRIVE_ON);
            if (std::memcmp(groupFxSeen_.data(), &ps_[(size_t)groupBase],
                            kNFxFields * sizeof(float)) != 0) {
                std::memcpy(groupFxSeen_.data(), &ps_[(size_t)groupBase],
                            kNFxFields * sizeof(float));
                for (auto& fx : groupFx_) fx.setGroupParams(ps_);
            }
            for (auto& bo : busOut_) bo.setParams(ps_);   // Finding D1
        }

        for (int b = 0; b < DR_NBUSES; ++b) {
            std::fill(verbInL_[b], verbInL_[b] + run, 0.0f);
            std::fill(verbInR_[b], verbInR_[b] + run, 0.0f);
        }

        for (int i = 0; i < DR_NPADS; i++) {
            PadVoice& v = voices_[(size_t)i];
            PadVoice& tl = tails_[(size_t)i];   // Finding D2: retrigger fade-out
            if (!padFxEnabled_) {
                if (!v.active && !tl.active) continue;
                const int out = std::max(0, std::min(DR_NBUSES - 1, (int)param(dpid(i, DP_OUT))));
                if (tl.active) renderPad(tl, i, outs[out][0], outs[out][1], pos, run);
                if (v.active) renderPad(v, i, outs[out][0], outs[out][1], pos, run);
                continue;
            }
            std::fill(padL_, padL_ + run, 0.0f);
            std::fill(padR_, padR_ + run, 0.0f);
            std::fill(verbSendL_, verbSendL_ + run, 0.0f);
            std::fill(verbSendR_, verbSendR_ + run, 0.0f);
            if (tl.active) renderPad(tl, i, padL_, padR_, 0, run);
            if (v.active) renderPad(v, i, padL_, padR_, 0, run);
            // Each pad owns a continuous insert chain, including its delay
            // tail. Reverb is sent to the shared network below so pads on the
            // same output bus share one tail.
            padFx_[(size_t)i].processInsert(padL_, padR_, verbSendL_, verbSendR_, run);
            int out = std::max(0, std::min(DR_NBUSES - 1, (int)param(dpid(i, DP_OUT))));
            for (int s = 0; s < run; ++s) {
                outs[out][0][pos + s] += padL_[s];
                outs[out][1][pos + s] += padR_[s];
                verbInL_[out][s] += verbSendL_[s];
                verbInR_[out][s] += verbSendR_[s];
            }
        }
        // Shared reverb receives the summed pad sends, then the bus gain/DC/
        // limiter runs once per output, matching the web graph.
        for (int b = 0; b < DR_NBUSES; ++b) {
            double sizeAcc = 0, sizeW = 0;
            for (int i = 0; i < DR_NPADS; ++i) {
                const auto& fx = padFx_[(size_t)i];
                if (fx.isIdle()) continue; // web excludes a gated pad from the mean
                const int base = dpid(i, 0);
                const int out = std::max(0, std::min(DR_NBUSES - 1, (int)param(base + DP_OUT)));
                if (out != b) continue;
                const double w = std::max(0.0, (double)fx.reverbSendWeight());
                sizeAcc += w * fx.reverbSize(); sizeW += w;
            }
            if (sizeW > 0) reverbs_[(size_t)b].setSize((float)(sizeAcc / sizeW));
            reverbs_[(size_t)b].process(verbInL_[b], verbInR_[b], outs[b][0] + pos, outs[b][1] + pos, run);
        }
        // The group strip follows each output sum (one logical global strip;
        // five independent instances preserve routed MAIN/AUX streams). Then
        // master gain, DC block and the safety limiter run once per bus.
        if (padFxEnabled_)
            for (int b = 0; b < DR_NBUSES; ++b)
                groupFx_[(size_t)b].process(outs[b][0] + pos, outs[b][1] + pos, run);
        if (padFxEnabled_)
            for (int b = 0; b < DR_NBUSES; ++b)
                busOut_[(size_t)b].process(outs[b][0] + pos, outs[b][1] + pos, run);

        if (internalRun) samplesToNext_ -= run;      // js:475
        if (hostClipMode_) hostFrame_ += run;
        pos += run;
    }
    if (internalRun || hostRun) rhythmFrame_ += (std::uint64_t)n;
    queueHits_ = false;
    if (hostRun) hostEndPpq_ = hostPpq_ + n * ppqPerSample;

    curTables_ = nullptr;
    renderEpoch_.fetch_add(1, std::memory_order_seq_cst);      // even: out of render

    const PadVoice& v = voices_[(size_t)sel_];
    vizA = v.active ? (float)v.oA.posSm : -1.0f;
    const auto& samples = drumOneShots();
    vizB = v.active && v.sample.index >= 0 && v.sample.index < (int)samples.size()
        ? (float)clampd(v.sample.pos / std::max(1, samples[(size_t)v.sample.index].length - 1), 0.0, 1.0)
        : -1.0f;
    vizEnv = v.active ? (float)v.ampLevel : 0.0f;
}

} // namespace fable
