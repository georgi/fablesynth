// Line-faithful port of src/bass/engine/worklet-bass.js (the lockstep
// reference). Section comments cite the source lines. Deliberate deviations:
// Math.random() in the LFO S&H becomes the engine's seeded xorshift Rng
// (deterministic tests), and the DAW transport can slave the sequencer via
// setHostTransport (same scheme as DrumEngine).
#include "BassEngine.h"

#include <algorithm>
#include <cmath>

namespace fable {

namespace {
bool exactlyDifferent(double a, double b) {
    return std::isless(a, b) || std::isgreater(a, b) || std::isunordered(a, b);
}
}

// Keep the DSP portable across compilers: MSVC does not provide the
// non-standard M_PI/M_LN2 macros unless an opt-in compatibility define is set
// (same reason as DrumEngine.cpp).
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kLn2 = 0.693147180559945309417232121458176568;

// js:33-36 — log-cosh for the ADAA drive antiderivative
static inline double lcosh(double z) {
    double a = std::fabs(z);
    return a + std::log1p(std::exp(-2.0 * a)) - kLn2;
}

constexpr double kAdaaInputLimit = 16.0;
constexpr double kAdaaFadeWidth = 0.1;
static inline double adaaInput(double x) {
    return std::isfinite(x) ? std::clamp(x, -kAdaaInputLimit, kAdaaInputLimit) : 0.0;
}
static inline double adaaTanh(double x, double xp, double dg, double dcomp) {
    const double dx = x - xp;
    if (std::abs(dx) <= 1.0e-5 / dg)
        return dcomp * std::tanh(dg * 0.5 * (x + xp));
    const double kF = dcomp / dg;
    return kF * (lcosh(dg * x) - lcosh(dg * xp)) / dx;
}

static inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Finding 6: chunk-invariant smoothers (see Engine.cpp for the derivation).
// BL-1's pos smoothing legacy cadence was 0.35 per 16-sample sub-block; cutoff
// was 0.5 per 128-sample chunk, both at 48 kHz.
static const double BL_POS_TAU = 16.0 / (48000.0 * 0.4307829160924542);
static const double BL_CUT_TAU = 128.0 / (48000.0 * kLn2);
static inline double smoothCoef(int n, double tauSr) { return 1 - std::exp(-(double)n / tauSr); }

// Finding J1: how each parameter crosses the APVTS -> engine boundary.
//  Snap — discrete or structural (enums, unison count, sequencer timing, and
//         every FX id, which BassFx smooths itself); a switch may arm a J2
//         crossfade instead.
//  Lin  — gains, amounts, times, and pitch offsets (semitones are already the
//         log of frequency, so linear here IS log-domain pitch).
//  Log  — the filter cutoff, in Hz.
enum class BassSmooth : unsigned char { Snap, Lin, Log };
static BassSmooth bassSmoothKind(int id) {
    switch (id) {
        case BL_OSC_POS: case BL_OSC_TUNE: case BL_OSC_FINE:
        case BL_OSC_DETUNE: case BL_OSC_SPREAD: case BL_OSC_LEVEL:
        case BL_SUB_LEVEL:
        case BL_FLT_RES: case BL_FLT_DRIVE: case BL_FLT_ENV: case BL_FLT_TRACK:
        case BL_FENV_ATT: case BL_FENV_DEC:
        case BL_AENV_ATT: case BL_AENV_DEC: case BL_AENV_SUS: case BL_AENV_REL:
        case BL_ACC_AMT: case BL_SLIDE_TIME: case BL_LFO_DEPTH:
            return BassSmooth::Lin;
        case BL_FLT_CUT:
            return BassSmooth::Log;
        default:
            return BassSmooth::Snap;
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

// prepare() contract (Finding 11): clean state for the new sample rate — the
// voice is killed and every recursive state (SVF, DC blocker, smoothers, sub
// phase, LFO S&H) cleared; the DC pole is remapped from its 48 kHz reference.
void BassEngine::prepare(double sampleRate) {
    sr_ = sampleRate;
    dcR_ = std::pow(BL_DC_R, 48000.0 / sr_);   // Finding 9
    xfLen_ = std::max(8, (int)std::lround(BL_SWITCH_XFADE * sr_));  // Finding J2
    snapParams();                              // Finding J1: no ramp across a re-prepare
    panic();
    held_.clear();
    held_.reserve(128);            // Finding B6: keyOn never allocates after this
    // Finding B6: the transport state is in samples at the OLD rate (and the
    // host lock caches an old ppq), so a re-prepare must clear it too.
    playing_ = false;
    step_ = -1;
    chainPos_ = 0;
    samplesToNext_ = 0;
    samplesToGateOff_ = -1;
    songPos_ = 0;
    hostPlaying_ = false;
    hostSynced_ = false;
    hostPpq_ = 0;
    hostEndPpq_ = 0;
    hostNextK_ = 0;
    hostFrame_ = 0;
    std::fill(std::begin(phases_), std::end(phases_), 0.0);
    subPhase_ = 0; subIncPrev_ = -1;
    dcxL_ = dcxR_ = dcyL_ = dcyR_ = 0;
    shVal_ = 0; shPhase_ = -1;
    oscXfPos_ = fltXfPos_ = 1 << 30;
    std::fill(std::begin(svfOld_), std::end(svfOld_), 0.0);
    collectRetiredTables();        // message thread: reclaim anything pending
}

// Finding J3: the previous scheme published the set through the free-function
// std::atomic_load/atomic_exchange on a shared_ptr. Both libstdc++ and libc++
// implement those with a hashed spinlock pool, so the audio thread could spin
// behind the message thread, and the render's local snapshot could be the last
// reference — running ~TableSet (and every TablePtr release inside it) on the
// audio thread. Now a raw std::atomic<const TableSet*> is published, the
// message thread keeps the only owning references, and retired sets are freed
// by collectRetiredTables() once no render can still hold the pointer.
void BassEngine::setTables(std::vector<TablePtr> tables) {
    auto next = std::make_unique<TableSet>();
    next->reserve(tables.size());
    for (auto& t : tables) {
        BassTable e;
        if (t) {
            e.frames = t->frames; e.mips = t->mips; e.size = t->size; e.mask = t->size - 1;
            e.data = t->data.data();
            e.src = std::move(t);
        }
        next->push_back(std::move(e));
    }
    auto prev = std::move(live_);
    live_ = std::unique_ptr<const TableSet>(next.release());
    // Sequentially consistent: the epoch must be sampled AFTER the publish is
    // globally visible, or a render that starts in between could load the old
    // pointer and still be tagged with an already-passed epoch.
    tablesPub_.store(live_.get(), std::memory_order_seq_cst);
    if (prev)
        retired_.push_back({ std::move(prev), renderEpoch_.load(std::memory_order_seq_cst) });
    collectRetiredTables();
}

// Message thread. A retired set is safe to free once the render epoch has moved
// PAST the value sampled at retirement: renders are strictly sequential on the
// audio thread, so a higher epoch proves the render that could still hold the
// pointer has returned. Nothing here ever runs on the audio thread.
void BassEngine::collectRetiredTables() {
    const uint64_t e = renderEpoch_.load(std::memory_order_acquire);
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(),
                                  [e](const RetiredSet& r) { return e > r.epoch; }),
                   retired_.end());
}

// Finding B3 — see BassEngine.h for the derivation of the taper.
void BassEngine::resToK(double res, bool twoPole, double& k1, double& k2) {
    res = clampd(res, 0.0, 0.999);   // 0.999, not 1.0 — see BassEngine.h
    if (twoPole) {
        const double r2 = res * res;
        const double resT = res + 0.0035 * r2 * r2;
        const double kk = 2 - 1.93 * resT;
        k1 = std::max(BL_LP24_KMIN, 0.5 * kk * kk);
        k2 = BL_LP24_K2;
    } else {
        k1 = k2 = 2 - 1.93 * res;
    }
}

// A target change owns one fixed-duration, sample-clocked ramp. Re-arming it
// for every render call made its duration depend on host/MIDI fragmentation.
void BassEngine::beginParamRamp(int) {
    if (target_ == rampTarget_) return;
    ps_ = p_;
    rampTarget_ = target_;
    rampPos_ = 0;
    rampLen_ = std::max(1, (int)(BL_PARAM_RAMP_MAX_SEC * sr_));
}

// Finding J1: evaluate the ramp at the end of one <=128-sample chunk. Gains,
// amounts and times move linearly; the cutoff moves geometrically, so a decade
// sweep is perceptually even. Discrete parameters were already taken by
// applyDiscreteParams before the first chunk.
void BassEngine::advanceParams(int n) {
    if (!smoothParams_) { p_ = target_; return; }
    rampPos_ += n;
    const double f = rampLen_ <= 0 ? 1.0 : std::min(1.0, (double)rampPos_ / rampLen_);
    for (int i = 0; i < BL_NUM_PARAMS; ++i) {
        const size_t k = (size_t)i;
        const BassSmooth kind = bassSmoothKind(i);
        if (kind == BassSmooth::Snap) continue;
        const double t = rampTarget_[k], s0 = ps_[k];
        if (!(t != s0) || f >= 1.0 || !std::isfinite(t) || !std::isfinite(s0)) {
            p_[k] = rampTarget_[k];
            continue;
        }
        p_[k] = (kind == BassSmooth::Log && s0 > 0.0 && t > 0.0)
                  ? (float)(s0 * std::pow(t / s0, f))
                  : (float)(s0 + (t - s0) * f);
    }
}

// Discrete parameters take effect once per render call — the sequencer reads
// tempo/swing before the first chunk is even sized, and a host cannot move an
// enum inside a block anyway. A switch arms its J2 crossfade here.
void BassEngine::applyDiscreteParams() {
    const float oTbl = p_[BL_OSC_TABLE], oSh = p_[BL_SUB_SHAPE], oOct = p_[BL_SUB_OCT];
    const float oFt = p_[BL_FLT_TYPE];
    for (int i = 0; i < BL_NUM_PARAMS; ++i)
        if (bassSmoothKind(i) == BassSmooth::Snap) p_[(size_t)i] = target_[(size_t)i];

    // Finding J2: arm the fades. An idle voice needs none — nothing is
    // sounding to click — and a fade already in flight simply restarts from
    // the configuration that was live a moment ago.
    if (!switchXfade_ || ampStage_ == 0) return;
    if (p_[BL_OSC_TABLE] != oTbl || p_[BL_SUB_SHAPE] != oSh || p_[BL_SUB_OCT] != oOct) {
        saveOsc(oscOld_);
        oldTbl_ = oTbl; oldSubShape_ = oSh; oldSubOct_ = oOct;
        oscXfPos_ = 0;
    }
    if (p_[BL_FLT_TYPE] != oFt) {
        std::copy(std::begin(svf_), std::end(svf_), std::begin(svfOld_));
        ftypeOld_ = (int)oFt;
        twoPoleOld_ = ftypeOld_ == 1;
        fltXfPos_ = 0;
    }
}

// ---------- voice control (js:126-173) ----------

void BassEngine::noteOn(int semi, bool acc, float vel) {
    gate_ = true;
    acc_ = acc;
    vel_ = std::isfinite(vel) ? clampd(vel, 0.0, 1.0)
                              : (acc ? BL_ACCENT_VEL : BL_PLAIN_VEL);
    semi_ = semi;
    semiTarget_ = semi;
    fenvT_ = 0;
    ampStage_ = 1;
    gainPrev_ = -1;                // Finding B2: a fresh note starts at its own gain
}

void BassEngine::glideTo(int semi, bool acc) {
    semiTarget_ = semi;
    if (acc) acc_ = true;          // an accented slide target keeps the bite
    gate_ = true;
}

void BassEngine::release() {
    gate_ = false;
    if (ampStage_ != 0) ampStage_ = 3;
}

void BassEngine::kill() {
    gate_ = false; ampStage_ = 0; ampLevel_ = 0;
    fenvT_ = 1e9;
    std::fill(std::begin(svf_), std::end(svf_), 0.0);
    satXL_ = 0; satXR_ = 0;
    driveMix_ = 0;
    posSm_ = -1; cutSm_ = 0; cutPrev_ = -1;
    havePrev_ = false; subIncPrev_ = -1;
    monoPrev_ = false; gainPrev_ = -1;
    // Finding J2: a fade in flight must not survive into the next note — its
    // old-configuration oscillator state belongs to a voice that is gone.
    oscXfPos_ = fltXfPos_ = 1 << 30;
}

void BassEngine::panic() {
    kill();
    held_.clear();
}

void BassEngine::keyOn(int semi, float vel, bool acc) {
    if (isPlaying()) return;       // audition when stopped · sequencer owns the voice
    held_.erase(std::remove(held_.begin(), held_.end(), semi), held_.end());
    const bool legato = !held_.empty() && gate_;
    held_.push_back(semi);
    if (legato) glideTo(semi, acc);
    else        noteOn(semi, acc, vel);
}

void BassEngine::keyOff(int semi) {
    held_.erase(std::remove(held_.begin(), held_.end(), semi), held_.end());
    if (isPlaying()) return;
    if (held_.empty()) {
        release();
    } else if (exactlyDifferent(semiTarget_, (double)held_.back())) {
        glideTo(held_.back(), false);
    }
}

// ---------- sequencer (js:109-121, 176-218) ----------

void BassEngine::play() {
    if (hostPlaying_) return;      // host owns the transport while rolling
    playing_ = true; step_ = -1; chainPos_ = 0;
    samplesToNext_ = 0; samplesToGateOff_ = -1; songPos_ = 0;
    held_.clear();
}

void BassEngine::stop() {
    playing_ = false; step_ = -1;
    samplesToGateOff_ = -1;
    release();
}

void BassEngine::setPatterns(const uint8_t* data, int n) {
    if (data && n == (int)pats_.size())
        std::copy(data, data + n, pats_.begin());
}

// Finding B6: audio-thread safe — a fixed array and a count, no allocation.
// Entries past BL_NPATTERNS are dropped (a chain can never be longer than the
// pattern bank; BassProcessor already clamps the bar count the same way).
void BassEngine::setChain(const int* list, int n) {
    if (!list || n <= 0) return;
    chainLen_ = std::min(n, BL_NPATTERNS);
    for (int i = 0; i < chainLen_; ++i)
        chain_[(size_t)i] = std::max(0, std::min(BL_NPATTERNS - 1, list[i]));
    chainPos_ = std::min(chainPos_, chainLen_ - 1);
}

void BassEngine::setBpmOverride(double bpm) {
    bpmOverride_ = bpm > 0 ? bpm : 0;
}

double BassEngine::effectiveBpm() const {
    if (bpmOverride_ > 0) return bpmOverride_;
    double pbpm = std::fpclassify(p_[BL_SEQ_BPM]) != FP_ZERO ? (double)p_[BL_SEQ_BPM] : 138.0;
    return clampd(pbpm, 60.0, 200.0);
}

// js:176-184 (seq.ts getStep)
BassStep BassEngine::readStep(const uint8_t* pats, int pat, int s) {
    const int o = (pat * BL_STEPS + s) * BL_STEP_STRIDE;
    const uint8_t flags = pats[o];
    BassStep st;
    st.on    = (flags & 1) != 0;
    st.acc   = (flags & 2) != 0;
    st.slide = (pats[o + 1] & 0x80) != 0;
    st.semi  = std::min(11, (int)(pats[o + 1] & 0x7f)) + 12 * (std::min(2, (int)pats[o + 2]) - 1);
    st.duration = std::max(1, std::min(63, (int)(flags >> 2)));
    return st;
}

// Shared step-fire body (js:200-210): trigger the step and schedule the gate,
// holding through when the NEXT step ties in with a slide.
void BassEngine::setArp(const ArpPattern& a) {
    if (hostClipMode_) return;
    const bool changed = a.enabled != arp_.enabled;
    if (changed || (a.enabled && !arpHasNotes(a))) { release(); samplesToGateOff_ = -1; }
    if (changed) { step_ = -1; chainPos_ = 0; samplesToNext_ = 0; }
    if (changed || a.rate != arp_.rate) hostSynced_ = false;
    arp_ = a;
}
void BassEngine::arpFire(const ArpPattern& a, int s, double interval) {
    if (a.hits[s] && a.notes[s] >= 0) {
        const int semi = a.notes[s] - BL_ROOT_MIDI;
        if (a.slides[s] && gate_) glideTo(semi, a.accents[s]);
        else noteOn(semi, a.accents[s], a.accents[s] ? BL_ACCENT_VEL : BL_PLAIN_VEL);
        const int next = (s + 1) % 16;
        samplesToGateOff_ = a.hits[next] && a.notes[next] >= 0 && a.slides[next] ? -1 : interval * a.gate;
    } else { release(); samplesToGateOff_ = -1; }
}
void BassEngine::fireStepAt(int s, int pat, int patNext, double dur) {
    const BassStep st = readStep(pats_.data(), pat, s);
    if (st.on) {
        if (st.slide && gate_) glideTo(st.semi, st.acc);
        else                   noteOn(st.semi, st.acc, st.acc ? BL_ACCENT_VEL : BL_PLAIN_VEL);
        const int sN = (s + 1) % BL_STEPS;
        const int patN = sN == 0 ? patNext : pat;
        const BassStep stN = readStep(pats_.data(), patN, sN);
        samplesToGateOff_ = (stN.on && stN.slide) ? -1 : st.duration * dur;
    }
    step_ = s;
}

// Hosted twin of fireStepAt (docs/sq4-clips.md §6, js: BassProcessor.clipFire).
// Identical trigger/glide/gate-off logic to fireStepAt — only the byte source
// changes from the pattern bank (pats_/chain_) to the ClipHost's live clip,
// and the tie lookahead wraps within the clip's own bar count rather than a
// separate chain. Does not touch step_ (that's the internal/host-transport
// sequencer's own position; clipHost_.clipStep() is the hosted position).
void BassEngine::clipFireAt(int abs) {
    if (clipHost_.arp().enabled) { arpFire(clipHost_.arp(), abs, clipHost_.stepInterval(abs)); return; }
    const uint8_t* clip = clipHost_.clipData();
    const int total = std::max(1, clipHost_.clipBars() * BL_STEPS);
    const int s = abs % BL_STEPS;
    const BassStep st = readStep(clip, abs / BL_STEPS, s);
    if (st.on) {
        if (st.slide && gate_) glideTo(st.semi, st.acc);
        else                   noteOn(st.semi, st.acc, st.acc ? BL_ACCENT_VEL : BL_PLAIN_VEL);
        const int absN = (abs + 1) % total;
        const BassStep stN = readStep(clip, absN / BL_STEPS, absN % BL_STEPS);
        const double dur = sqSamplesPerStep(effectiveBpm(), sr_);
        samplesToGateOff_ = (stN.on && stN.slide) ? -1 : st.duration * dur;
    }
}

// js:187-218
void BassEngine::fireStep() {
    const double bpm = effectiveBpm();
    if (arp_.enabled) {
        const int s = (step_ + 1) % 16;
        const double interval = 60.0 / bpm * arp_.rate * sr_ * (1 + (s % 2 ? -1 : 1) * std::clamp((double)p_[BL_MASTER_SWING], 0.0, 1.0) * BL_SWING_MAX);
        arpFire(arp_, s, interval); step_ = s; chainPos_ = 0; samplesToNext_ += interval; return;
    }
    const double dur = (60.0 / bpm / 4.0) * sr_;
    const double swing = p_[BL_MASTER_SWING];
    if (step_ + 1 >= BL_STEPS) {                   // bar wrap advances the chain
        step_ = -1;
        chainPos_ = (chainPos_ + 1) % chainLen_;
    }
    const int s = (step_ + 1) % BL_STEPS;
    const int pat = chain_[(size_t)chainPos_];
    const int patNext = chain_[(size_t)((chainPos_ + 1) % chainLen_)];
    fireStepAt(s, pat, patNext, dur);
    const double offNow = (s % 2 == 1) ? swing * BL_SWING_MAX * dur : 0.0;
    const int sNext = (s + 1) % BL_STEPS;
    const double offNext = (sNext % 2 == 1) ? swing * BL_SWING_MAX * dur : 0.0;
    // Finding B7: accumulate, never reassign. render() splits the run at
    // ceil(samplesToNext_), so what is left here is the negative fractional
    // residue of the step just played; dropping it made every step
    // ceil(dur) samples (~0.01 % slow, ~35 ms over five minutes).
    samplesToNext_ += dur - offNow + offNext;
}

// ---------- host transport lock (DrumEngine scheme) ----------

void BassEngine::setHostTransport(double ppq, double bpm, bool playing) {
    if (!std::isfinite(ppq)) ppq = 0;
    if (!(std::isfinite(bpm) && bpm > 1.0)) bpm = 120;
    if (playing && !hostPlaying_) {
        playing_ = false;              // host takes over; internal transport yields
        step_ = -1;
        samplesToGateOff_ = -1;
        hostSynced_ = false;
        held_.clear();
    }
    if (playing && hostSynced_ && std::fabs(ppq - hostEndPpq_) > 1e-4)
        hostSynced_ = false;           // loop / relocate -> resync from ppq
    if (!playing && hostPlaying_) {    // host stopped
        step_ = -1;
        samplesToGateOff_ = -1;
        release();
    }
    hostPlaying_ = playing;
    hostPpq_ = ppq;
    hostBpm_ = bpm;
}

double BassEngine::hostStepPpq(long k) const {
    double swing = clampd((double)p_[BL_MASTER_SWING], 0.0, 1.0);
    const double rate = arp_.enabled ? arp_.rate : .25;
    return (double)k * rate + ((k & 1) ? swing * BL_SWING_MAX * rate : 0.0);
}

void BassEngine::hostResync() {
    long k = (long)std::floor(hostPpq_ / (arp_.enabled ? arp_.rate : .25)) - 1;
    if (k < 0) k = 0;
    while (hostStepPpq(k) < hostPpq_ - 1e-9) k++;
    hostNextK_ = k;
    hostSynced_ = true;
}

void BassEngine::fireHostStep(long k) {
    const int  s   = (int)(k % BL_STEPS);
    if (arp_.enabled) {
        arpFire(arp_, s, (hostStepPpq(k + 1) - hostStepPpq(k)) * 60.0 / hostBpm_ * sr_);
        step_ = s; chainPos_ = 0; return;
    }
    const long bar = k / BL_STEPS;
    chainPos_ = (int)(bar % (long)chainLen_);
    const int pat = chain_[(size_t)chainPos_];
    const int patNext = chain_[(size_t)((bar + 1) % (long)chainLen_)];
    const double dur = (60.0 / hostBpm_ / 4.0) * sr_;
    fireStepAt(s, pat, patNext, dur);
}

// ---------- osc setup / render (js:221-322, per 16-sample sub-block) ----------

bool BassEngine::setupOsc(double noteAbs, int n) {
    const int ti = (int)p_[BL_OSC_TABLE];
    const BassTable* table =
        (ti >= 0 && ti < (int)curTables_->size() && (*curTables_)[(size_t)ti].data)
            ? &(*curTables_)[(size_t)ti] : nullptr;
    if (!table) return false;
    const double freq = 440.0 * std::pow(2.0, (noteAbs - 69.0) / 12.0);
    if (!(freq > 0 && freq <= sr_ * 0.45)) return false;

    double level = clampd(p_[BL_OSC_LEVEL], 0.0, 1.2);
    level *= level;
    if (!(level >= 1e-5)) return false;

    const int uni = std::max(1, std::min(BL_MAXUNI, (int)p_[BL_OSC_UNISON]));
    const double det = p_[BL_OSC_DETUNE];
    const double spr = clampd(p_[BL_OSC_SPREAD], 0.0, 1.0);

    const double pos = clampd(p_[BL_OSC_POS], 0.0, 1.0);
    if (posSm_ < 0) posSm_ = pos;
    posSm_ += (pos - posSm_) * smoothCoef(n, BL_POS_TAU * sr_);
    const double posF = posSm_ * (table->frames - 1);
    const int f0 = (int)posF;
    const int f1 = std::min(table->frames - 1, f0 + 1);
    ft_ = posF - f0;

    const double cps = freq / sr_;
    const double maxRatio = std::pow(2.0, (std::fabs(det) * 50.0) / 1200.0);
    const double W = 0.07;
    const double mipF = std::log2((cps * maxRatio * 1024.0) / 0.475);
    int mip = 0; double mipBlend = 0;
    if (mipF > 0) {
        mip = std::min(table->mips - 1, (int)std::ceil(mipF));
        const double over = mipF - (mip - 1);
        if (over < W) mipBlend = 1 - over / W;
    }
    const int fineMip = mip > 0 ? mip - 1 : 0;

    off0_  = (f0 * table->mips + mip) * table->size;
    off1_  = (f1 * table->mips + mip) * table->size;
    off0b_ = (f0 * table->mips + fineMip) * table->size;
    off1b_ = (f1 * table->mips + fineMip) * table->size;
    mipBlend_ = mipBlend;
    data_ = table->data;
    mask_ = table->mask;
    size_ = table->size;
    uni_ = uni;

    for (int u = 0; u < uni; u++) {
        const double sprd = uni > 1 ? ((double)u / (uni - 1)) * 2 - 1 : 0;
        const double cents = sprd * det * 50;
        const double ratio = std::pow(2.0, cents / 1200.0);
        incs_[u] = cps * ratio * table->size;
        const double pan = clampd(sprd * spr, -1.0, 1.0);
        const double a = ((pan + 1) * kPi) / 4;
        gl_[u] = (float)std::cos(a);
        gr_[u] = (float)std::sin(a);
    }
    oscGain_ = (level * 0.32) / std::sqrt((double)uni);
    return true;
}

// Finding 7 + 10: increments / morph fraction / pan gains ramp from the
// previous sub-block's targets across each sub-block (staircase-free slides
// and pos sweeps), and table reads are cubic Hermite. Same scheme as
// Engine::renderOsc — see there for the ramp-validity rules.
void BassEngine::renderOsc(float* tmpL, float* tmpR, int off, int n) {
    const float* data = data_;
    const int mask = mask_, size = size_;
    const double invN = 1.0 / n;
    const bool rp = havePrev_ && pUni_ == uni_;
    const double ft1 = ft_;
    const double ft0 = (rp && pOff0_ == off0_) ? pFt_ : ft1;
    const double dFt = (ft1 - ft0) * invN;
    const double g = oscGain_;
    const int off0 = off0_, off1 = off1_;
    const double blend = mipBlend_;
    for (int u = 0; u < uni_; u++) {
        double ph = phases_[u];
        const double inc1 = incs_[u];
        const double inc0 = rp ? pIncs_[u] : inc1;
        const double dInc = (inc1 - inc0) * invN;
        const double gl1 = gl_[u] * g, gr1 = gr_[u] * g;
        const double gl0 = rp ? (double)pGl_[u] : gl1, gr0 = rp ? (double)pGr_[u] : gr1;
        const double dGl = (gl1 - gl0) * invN, dGr = (gr1 - gr0) * invN;
        if (blend < 0.001) {
            for (int i = 0; i < n; i++) {
                const int idx = (int)ph;
                const double frac = ph - idx;
                const int im1 = (idx - 1) & mask, i2 = (idx + 1) & mask, i3 = (idx + 2) & mask;
                const double s0 = rdH(data, off0, im1, idx, i2, i3, frac);
                const double s1 = rdH(data, off1, im1, idx, i2, i3, frac);
                const double s = s0 + (ft0 + dFt * i) * (s1 - s0);
                tmpL[off + i] += (float)(s * (gl0 + dGl * i));
                tmpR[off + i] += (float)(s * (gr0 + dGr * i));
                ph += inc0 + dInc * i;
                if (ph >= size) ph -= size;
            }
        } else {
            const int off0b = off0b_, off1b = off1b_;
            for (int i = 0; i < n; i++) {
                const int idx = (int)ph;
                const double frac = ph - idx;
                const int im1 = (idx - 1) & mask, i2 = (idx + 1) & mask, i3 = (idx + 2) & mask;
                const double ftN = ft0 + dFt * i;
                const double sc0 = rdH(data, off0, im1, idx, i2, i3, frac);
                const double sc1 = rdH(data, off1, im1, idx, i2, i3, frac);
                const double sc = sc0 + ftN * (sc1 - sc0);
                const double sf0 = rdH(data, off0b, im1, idx, i2, i3, frac);
                const double sf1 = rdH(data, off1b, im1, idx, i2, i3, frac);
                const double sf = sf0 + ftN * (sf1 - sf0);
                const double s = sc + blend * (sf - sc);
                tmpL[off + i] += (float)(s * (gl0 + dGl * i));
                tmpR[off + i] += (float)(s * (gr0 + dGr * i));
                ph += inc0 + dInc * i;
                if (ph >= size) ph -= size;
            }
        }
        phases_[u] = ph;
        pIncs_[u] = inc1;
        pGl_[u] = (float)gl1; pGr_[u] = (float)gr1;
    }
    pFt_ = ft1; pOff0_ = off0_; pUni_ = uni_;
    havePrev_ = true;
}

// js:324-354 — sine / polyblep-square sub, -1/-2 oct below the (un-tuned) note
void BassEngine::renderSub(float* tmpL, float* tmpR, int off, int n, double noteRootAbs) {
    double level = clampd(p_[BL_SUB_LEVEL], 0.0, 1.0);
    level *= level;
    const double gain = level * 0.35;
    if (gain < 1e-6) return;
    int octI = (int)p_[BL_SUB_OCT];
    if (octI == 0) octI = -1;                      // js: p['sub.oct'] | 0 || -1
    const int oct = std::max(-2, std::min(-1, octI));
    const double freq = 440.0 * std::pow(2.0, (noteRootAbs + 12 * oct - 69.0) / 12.0);
    if (!(freq > 4 && freq <= sr_ * 0.45)) { subIncPrev_ = -1; return; }
    // Finding 7: ramp the sub increment across the sub-block (slide smoothing).
    const double inc1 = freq / sr_;
    const double inc0 = subIncPrev_ > 0 ? subIncPrev_ : inc1;
    const double dInc = (inc1 - inc0) / n;
    const bool square = (int)p_[BL_SUB_SHAPE] == 1;
    double ph = subPhase_;
    if (square) {
        for (int i = 0; i < n; i++) {
            const double inc = inc0 + dInc * i;
            double s = ph < 0.5 ? 1.0 : -1.0;
            // Finding B4: polyBLEP on BOTH sides of each edge — the pre-edge
            // (t+1)^2 term was missing, so only half the correction was
            // applied. Identical form to WT-1's sub (Engine.cpp).
            if (ph < inc) { const double t = ph / inc; s += -(t * t) + 2 * t - 1; }
            else if (ph > 1 - inc) { const double t = (ph - 1) / inc; s += t * t + 2 * t + 1; }
            const double h = ph - 0.5;
            if (h >= 0 && h < inc) { const double t = h / inc; s -= -(t * t) + 2 * t - 1; }
            else if (h < 0 && h > -inc) { const double t = h / inc; s -= t * t + 2 * t + 1; }
            const float v = (float)(s * gain * 0.8);
            tmpL[off + i] += v; tmpR[off + i] += v;
            ph += inc; if (ph >= 1) ph -= 1;
        }
    } else {
        for (int i = 0; i < n; i++) {
            const float v = (float)(std::sin(ph * 2 * kPi) * gain * 1.2);
            tmpL[off + i] += v; tmpR[off + i] += v;
            ph += inc0 + dInc * i; if (ph >= 1) ph -= 1;
        }
    }
    subPhase_ = ph;
    subIncPrev_ = inc1;
}

// ---------- LFO (js:357-374, bar-locked while playing) ----------
// beats = quarter notes since play (internal) or the host song position.
double BassEngine::lfoValue(double beats) {
    const double cpb = lfoDivFactor((int)p_[BL_LFO_RATE]);
    const double cycles = beats * cpb;
    const double phase = cycles - std::floor(cycles);
    switch ((int)p_[BL_LFO_SHAPE]) {
        case 1: return 1 - 4 * std::fabs(phase - 0.5);   // tri
        case 2: return 1 - 2 * phase;                    // saw (falling)
        case 3: return phase < 0.5 ? 1.0 : -1.0;         // sqr
        case 4: {                                        // s&h
            const long step = (long)std::floor(cycles);
            if (step != shPhase_) { shPhase_ = step; shVal_ = rng_.next() * 2.0 - 1.0; }
            return shVal_;
        }
        default: return std::sin(phase * 2 * kPi);
    }
}

// ---------- filter (js:377-413) ----------
void BassEngine::setupFilter(double noteAbs, double beats, int n) {
    const double accAmt = clampd(p_[BL_ACC_AMT], 0.0, 1.0);
    const double accBoost = acc_ ? accAmt : 0.0;

    // filter AD env — accent raises the peak and shortens the decay
    const double att = std::max(1.0, p_[BL_FENV_ATT] * sr_);
    const double dec = std::max(1.0, p_[BL_FENV_DEC] * sr_ * (1 - BL_ACC_DEC_SHORTEN * accBoost));
    double env;
    if (fenvT_ < att) env = fenvT_ / att;
    else              env = std::exp(-4.5 * (fenvT_ - att) / dec);
    env *= 1 + accBoost;
    fenvVal_ = env;

    const double lfo = isPlaying() ? lfoValue(beats) * clampd(p_[BL_LFO_DEPTH], 0.0, 1.0) : 0.0;
    const double track = clampd(p_[BL_FLT_TRACK], 0.0, 1.0);
    const double key = ((noteAbs - BL_KEYTRACK_REF) / 12.0) * track;
    const double oct = p_[BL_FLT_ENV] * env * BL_FENV_OCT + lfo * BL_LFO_OCT + key;

    double fc = p_[BL_FLT_CUT] * std::pow(2.0, oct);
    if (!std::isfinite(fc)) fc = 20;
    fc = clampd(fc, 20.0, sr_ * 0.45);
    if (cutSm_ <= 0) cutSm_ = fc;
    cutSm_ += (fc - cutSm_) * smoothCoef(n, BL_CUT_TAU * sr_);
    curCut_ = cutSm_;
    cutTarget_ = cutSm_;              // runFilter ramps cutPrev_ -> cutTarget_
    const double res = clampd(p_[BL_FLT_RES], 0.0, 0.999);

    const int ftype = (int)p_[BL_FLT_TYPE];
    const bool twoPole = ftype == 1;
    // Finding B8: the second LP24 stage is frozen while a one-pole type is
    // selected, so it still holds whatever it last integrated. Clear it as it
    // re-engages, otherwise the switch injects stale (possibly full-scale)
    // energy. The FIRST stage keeps its state deliberately: it runs for every
    // type, so clearing it would ADD a discontinuity rather than remove one.
    if (twoPole && !twoPole_) { svf_[4] = svf_[5] = svf_[6] = svf_[7] = 0; }
    ftype_ = ftype;
    twoPole_ = twoPole;
    // Finding B3: for LP24 the resonance now lives in stage 1 alone and stage
    // 2 stays critically damped, instead of two coincident resonant stages.
    // SVF a1..a3 are recomputed per sub-block in runFilter.
    resToK(res, twoPole, k1_, k2_);

    // Finding B8: the mono fast path is valid only when every unison voice
    // pans dead centre AND it was already valid last chunk — renderOsc ramps
    // the pan gains from the previous chunk's targets, so the transition
    // chunk still has L != R and must run both channels.
    const int uniP = std::max(1, std::min(BL_MAXUNI, (int)p_[BL_OSC_UNISON]));
    const double sprP = clampd(p_[BL_OSC_SPREAD], 0.0, 1.0);
    const bool monoNow = uniP <= 1 || !(sprP > 0.0);
    mono_ = monoNow && monoPrev_;
    monoPrev_ = monoNow;
}

// Finding B8: the filter-type switch used to sit in the innermost sample loop.
// The core is now templated on the type and selected once per <=32-sample
// sub-block. FT: 0 LP (both LP12 and the LP24 stages), 2 BP, 3 notch, 4 HP.
template <int FT>
static inline void svfRun(float* buf, int from, int to,
                          double a1, double a2, double a3, double k1,
                          double& ic1r, double& ic2r) {
    double ic1 = ic1r, ic2 = ic2r;
    for (int i = from; i < to; i++) {
        const double x = buf[i];
        const double v3 = x - ic2;
        const double v1 = a1 * ic1 + a2 * v3;
        const double v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        if constexpr (FT == 0)      buf[i] = (float)v2;
        else if constexpr (FT == 2) buf[i] = (float)(k1 * v1);
        else if constexpr (FT == 3) buf[i] = (float)(x - k1 * v1 - v2);
        else                        buf[i] = (float)(x - k1 * v1);
    }
    ic1r = ic1; ic2r = ic2;
}

static inline void svfRunType(int ftype, float* buf, int from, int to,
                              double a1, double a2, double a3, double k1,
                              double& ic1, double& ic2) {
    switch (ftype) {
        case 0: case 1: svfRun<0>(buf, from, to, a1, a2, a3, k1, ic1, ic2); break;
        case 2:         svfRun<2>(buf, from, to, a1, a2, a3, k1, ic1, ic2); break;
        case 3:         svfRun<3>(buf, from, to, a1, a2, a3, k1, ic1, ic2); break;
        default:        svfRun<4>(buf, from, to, a1, a2, a3, k1, ic1, ic2); break;
    }
}

// js:415-479 — ADAA lcosh drive, Cytomic SVF, LP24 second pass
void BassEngine::runFilter(const float* inL, const float* inR,
                           float* outL, float* outR, double drive, int n) {
    // Finding B8: with uni = 1 or spread = 0 the two channels are sample-for-
    // sample identical, so run one and mirror it — that halves the ADAA
    // exp/log1p and SVF cost, which dominate this engine.
    const bool mono = mono_;
    drive = std::isfinite(drive) ? clampd(drive, 0.0, 1.0) : 0.0;
    const double mix0 = driveMix_;
    // Fade the ADAA path in from silence over a small drive interval instead
    // of making the old enable threshold a binary transfer-function switch.
    const double mix1 = clampd(drive / kAdaaFadeWidth, 0.0, 1.0);
    if (mix0 > 0.0 || mix1 > 0.0) {
        const double dg = 1 + drive * 7;
        const double dcomp = 1 / std::pow(dg, 0.55);
        double xpL = satXL_, xpR = satXR_;
        if (mono) {
            for (int i = 0; i < n; i++) {
                const double aL = adaaInput(inL[i]);
                const double m = mix0 + (mix1 - mix0) * ((double)(i + 1) / n);
                outL[i] = (float)(aL + m * (adaaTanh(aL, xpL, dg, dcomp) - aL));
                xpL = aL;
            }
            satXL_ = satXR_ = xpL;
        } else {
            for (int i = 0; i < n; i++) {
                const double aL = adaaInput(inL[i]), aR = adaaInput(inR[i]);
                const double m = mix0 + (mix1 - mix0) * ((double)(i + 1) / n);
                outL[i] = (float)(aL + m * (adaaTanh(aL, xpL, dg, dcomp) - aL));
                outR[i] = (float)(aR + m * (adaaTanh(aR, xpR, dg, dcomp) - aR));
                xpL = aL; xpR = aR;
            }
            satXL_ = xpL; satXR_ = xpR;
        }
    } else {
        for (int i = 0; i < n; i++) outL[i] = inL[i];
        if (!mono) for (int i = 0; i < n; i++) outR[i] = inR[i];
        if (n > 0) { satXL_ = adaaInput(inL[n - 1]); satXR_ = mono ? satXL_ : adaaInput(inR[n - 1]); }
    }
    driveMix_ = mix1;


    // Finding 7: cutoff ramps from the previous chunk's value; coefficients
    // recomputed per <=32-sample sub-block.
    const double c1c = cutTarget_;
    const double c0c = cutPrev_ > 0 ? cutPrev_ : c1c;

    // Finding J2: while a filter-type switch is fading, the OLD type keeps
    // running on the copy of the SVF state taken at the switch, over the same
    // post-drive signal, and the two are equal-power mixed. Both see the same
    // cutoff ramp, so only the type (and its k) differ.
    const bool fade = fltXfPos_ < xfLen_;
    if (fade) {
        std::copy(outL, outL + n, fxL_);
        if (!mono) std::copy(outR, outR + n, fxR_);
    }

    svfChain(outL, outR, n, c0c, c1c, ftype_, twoPole_, k1_, k2_, svf_, mono);

    if (fade) {
        double ok1 = 0, ok2 = 0;
        resToK(clampd(p_[BL_FLT_RES], 0.0, 0.999), twoPoleOld_, ok1, ok2);
        svfChain(fxL_, fxR_, n, c0c, c1c, ftypeOld_, twoPoleOld_, ok1, ok2, svfOld_, mono);
        for (int i = 0; i < n; i++) {
            const double w = std::min(1.0, (double)(fltXfPos_ + i) / xfLen_);
            const double gOld = std::cos(w * kPi * 0.5), gNew = std::sin(w * kPi * 0.5);
            outL[i] = (float)(fxL_[i] * gOld + outL[i] * gNew);
            outR[i] = (float)(fxR_[i] * gOld + outR[i] * gNew);
        }
        fltXfPos_ += n;
    }
    cutPrev_ = c1c;
}

// One filter pass over `n` samples with its own state block F[8].
void BassEngine::svfChain(float* bufL, float* bufR, int n, double c0c, double c1c,
                          int ftype, bool twoPole, double k1, double k2,
                          double* F, bool mono) const {
    for (int at = 0; at < n; at += 32) {
        const int m = std::min(32, n - at);
        const double cut = c0c + (c1c - c0c) * ((double)(at + m) / n);
        const double gC = std::tan((kPi * cut) / sr_);
        const double a1 = 1 / (1 + gC * (gC + k1));
        const double a2 = gC * a1, a3 = gC * a2;
        const int chans = mono ? 1 : 2;
        for (int ch = 0; ch < chans; ch++) {
            float* buf = ch == 0 ? bufL : bufR;
            const int o1 = ch * 2;
            svfRunType(ftype, buf, at, at + m, a1, a2, a3, k1, F[o1], F[o1 + 1]);
        }
        if (twoPole) {
            // Finding B3: the LP24 second stage is critically damped, not a
            // second copy of the resonant pair — it needs its own coefficients.
            const double b1 = 1 / (1 + gC * (gC + k2));
            const double b2 = gC * b1, b3 = gC * b2;
            for (int ch = 0; ch < chans; ch++) {
                float* buf = ch == 0 ? bufL : bufR;
                const int o1 = 4 + ch * 2;
                svfRun<0>(buf, at, at + m, b1, b2, b3, k2, F[o1], F[o1 + 1]);
            }
        }
    }
    if (mono) {
        // Keep the right channel's state in lockstep so a later spread > 0
        // resumes without a discontinuity, and mirror the samples out.
        F[2] = F[0]; F[3] = F[1]; F[6] = F[4]; F[7] = F[5];
        for (int i = 0; i < n; i++) bufR[i] = bufL[i];
    }
}

// Finding J2: the oscillator + sub pass over one chunk, factored out so the
// old table / sub shape can be rendered a second time from its own state
// while a switch crossfades.
void BassEngine::oscPass(float* dstL, float* dstR, int n) {
    std::fill(dstL, dstL + n, 0.0f);
    std::fill(dstR, dstR + n, 0.0f);

    // glide: one-pole approach of semiTarget with time-constant slide.time
    const double tau = std::max(0.005, (double)p_[BL_SLIDE_TIME]) * sr_;
    const double gk16 = 1 - std::exp(-16 / tau);

    for (int at = 0; at < n; at += 16) {
        const int count = std::min(16, n - at);
        if (exactlyDifferent(semi_, semiTarget_)) {
            semi_ += (semiTarget_ - semi_) * gk16;
            if (std::fabs(semiTarget_ - semi_) < 0.001) semi_ = semiTarget_;
        }
        const double noteRootAbs = BL_ROOT_MIDI + semi_;
        const double noteAbs = noteRootAbs + p_[BL_OSC_TUNE] + p_[BL_OSC_FINE] / 100.0;
        if (setupOsc(noteAbs, count)) renderOsc(dstL, dstR, at, count);
        else havePrev_ = false;
        renderSub(dstL, dstR, at, count, noteRootAbs);
    }
}

void BassEngine::saveOsc(OscSnap& s) const {
    std::copy(std::begin(phases_), std::end(phases_), std::begin(s.phases));
    std::copy(std::begin(pIncs_), std::end(pIncs_), std::begin(s.pIncs));
    std::copy(std::begin(pGl_), std::end(pGl_), std::begin(s.pGl));
    std::copy(std::begin(pGr_), std::end(pGr_), std::begin(s.pGr));
    s.pFt = pFt_; s.posSm = posSm_; s.subPhase = subPhase_;
    s.subIncPrev = subIncPrev_; s.semi = semi_;
    s.pOff0 = pOff0_; s.pUni = pUni_; s.havePrev = havePrev_;
}

void BassEngine::restoreOsc(const OscSnap& s) {
    std::copy(std::begin(s.phases), std::end(s.phases), std::begin(phases_));
    std::copy(std::begin(s.pIncs), std::end(s.pIncs), std::begin(pIncs_));
    std::copy(std::begin(s.pGl), std::end(s.pGl), std::begin(pGl_));
    std::copy(std::begin(s.pGr), std::end(s.pGr), std::begin(pGr_));
    pFt_ = s.pFt; posSm_ = s.posSm; subPhase_ = s.subPhase;
    subIncPrev_ = s.subIncPrev; semi_ = s.semi;
    pOff0_ = s.pOff0; pUni_ = s.pUni; havePrev_ = s.havePrev;
}

// ---------- render voice (js:482-543) ----------
void BassEngine::renderVoice(float* L, float* R, int off, int n, double beats) {
    if (ampStage_ == 0 && !gate_) return;          // LFO clock advances outside
    float* tmpL = tmpL_;
    float* tmpR = tmpR_;

    oscPass(tmpL, tmpR, n);

    // Finding J2: a table / sub-shape / sub-octave switch renders the chunk a
    // second time from the pre-switch configuration and its own oscillator
    // state, then equal-power mixes the two. The published table set is
    // immutable and snapshotted for the whole render call, so the old table
    // index stays valid for as long as the fade runs.
    if (oscXfPos_ < xfLen_) {
        OscSnap post; saveOsc(post);
        restoreOsc(oscOld_);
        const float sT = p_[BL_OSC_TABLE], sS = p_[BL_SUB_SHAPE], sO = p_[BL_SUB_OCT];
        p_[BL_OSC_TABLE] = oldTbl_; p_[BL_SUB_SHAPE] = oldSubShape_; p_[BL_SUB_OCT] = oldSubOct_;
        oscPass(xL_, xR_, n);
        p_[BL_OSC_TABLE] = sT; p_[BL_SUB_SHAPE] = sS; p_[BL_SUB_OCT] = sO;
        saveOsc(oscOld_);
        restoreOsc(post);
        for (int i = 0; i < n; i++) {
            const double w = std::min(1.0, (double)(oscXfPos_ + i) / xfLen_);
            const double gOld = std::cos(w * kPi * 0.5), gNew = std::sin(w * kPi * 0.5);
            tmpL[i] = (float)(xL_[i] * gOld + tmpL[i] * gNew);
            tmpR[i] = (float)(xR_[i] * gOld + tmpR[i] * gNew);
        }
        oscXfPos_ += n;
    }

    setupFilter(BL_ROOT_MIDI + semi_ + p_[BL_OSC_TUNE], beats, n);
    runFilter(tmpL, tmpR, fL_, fR_, p_[BL_FLT_DRIVE], n);
    fenvT_ += n;

    // amp ADSR + accent gain
    const double attK = 1.0 / std::max(1.0, p_[BL_AENV_ATT] * sr_);
    const double sus = clampd(p_[BL_AENV_SUS], 0.0, 1.0);
    const double decK = 1 - std::exp(-4.5 / std::max(1.0, p_[BL_AENV_DEC] * sr_));
    const double relK = 1 - std::exp(-4.5 / std::max(1.0, p_[BL_AENV_REL] * sr_));
    const double accAmt = clampd(p_[BL_ACC_AMT], 0.0, 1.0);
    // Finding B2: an accented slide target flips acc_ on the RUNNING voice, so
    // this gain jumped by up to +3.5 dB at a chunk boundary. Ramp it across the
    // chunk instead. gainPrev_ < 0 means "new note": start at the target, so a
    // note-on is not softened by the previous note's level.
    const double gain1 = vel_ * (1 + (acc_ ? accAmt * BL_ACC_GAIN : 0)) * 0.9;
    const double gain0 = gainPrev_ >= 0 ? gainPrev_ : gain1;
    const double dGain = (gain1 - gain0) / n;
    gainPrev_ = gain1;

    for (int i = 0; i < n; i++) {
        switch (ampStage_) {
            case 1:
                ampLevel_ += attK;
                if (ampLevel_ >= 1) { ampLevel_ = 1; ampStage_ = 2; }
                break;
            case 2:
                ampLevel_ += (sus - ampLevel_) * decK;
                break;
            case 3:
                ampLevel_ += (0 - ampLevel_) * relK;
                if (ampLevel_ < 1e-4) { ampLevel_ = 0; ampStage_ = 0; }
                break;
            default: ampLevel_ = 0;
        }
        const double amp = ampLevel_ * (gain0 + dGain * (i + 1));
        const double sl = fL_[i] * amp, sr = fR_[i] * amp;
        const double yL = sl - dcxL_ + dcR_ * dcyL_;
        const double yR = sr - dcxR_ + dcR_ * dcyR_;
        dcxL_ = sl; dcyL_ = yL;
        dcxR_ = sr; dcyR_ = yR;
        L[off + i] += (float)yL;
        R[off + i] += (float)yR;
    }
}

// ---------- process (js:545-590). Chunks to <=128 samples so the filter/env
// update cadence matches the worklet's 128-sample process quantum, and splits
// at step + gate-off boundaries so both land sample-accurately regardless of
// the host buffer size. ----------
void BassEngine::render(float* L, float* R, int n) {
    std::fill(L, L + n, 0.0f);
    std::fill(R, R + n, 0.0f);
    applyDiscreteParams();       // Finding J1/J2: block-rate discrete update
    beginParamRamp(n);           // Finding J1: automation ramp for this call

    // Finding J3: read the published table set once for the whole call. The
    // pointer stays valid because setTables retires the outgoing set to a
    // message-thread list instead of dropping the audio thread's last
    // reference. Never blocks, never allocates, never frees, never silent.
    // Finding J3: bump the epoch BEFORE loading the published pointer so a
    // concurrent setTables either sees this render in flight (and defers the
    // free) or publishes before this load (and we take the new set).
    renderEpoch_.fetch_add(1, std::memory_order_seq_cst);
    curTables_ = tablesPub_.load(std::memory_order_seq_cst);

    // Hosted clip mode owns the transport exclusively: it suppresses both the
    // host-transport-locked and internal-clock firing below so the
    // standalone sequencer and the hosted clip can never double-fire.
    const bool hostRun = hostPlaying_ && !hostClipMode_;
    double ppqPerSample = 0, samplesPerPpq = 0;
    if (hostRun) {
        ppqPerSample = hostBpm_ / 60.0 / sr_;
        samplesPerPpq = 1.0 / ppqPerSample;
        if (!hostSynced_) hostResync();
    }
    const double beatsPerSample = effectiveBpm() / 60.0 / sr_;
    const bool internalRun = playing_ && !hostClipMode_;

    int pos = 0;
    while (pos < n) {
        int run = std::min(128, n - pos);
        if (hostRun) {
            // Fire every step due at/before pos; split the run at the next one.
            for (;;) {
                const long fireAt = (long)std::ceil(
                    (hostStepPpq(hostNextK_) - hostPpq_) * samplesPerPpq - 1e-9);
                if (fireAt <= pos) { fireHostStep(hostNextK_++); continue; }
                if (fireAt - pos < run) run = (int)(fireAt - pos);
                break;
            }
        } else if (internalRun) {
            if (samplesToNext_ <= 0) fireStep();
            run = std::min(run, std::max(1, (int)std::ceil(samplesToNext_)));
        } else if (hostClipMode_) {
            // At most one fire per quantum (ClipHost contract). onSwap ends
            // the OUTGOING clip's sounding note before the new clip's entry
            // fire (docs §6 rule 4). A Stop event means the clip transport
            // just gated the voice off (docs §6 rule 3 — release, not panic:
            // Stop semantics release the mono voice, never a hard kill).
            const size_t evBefore = clipHost_.events.size();
            clipHost_.tick(
                hostFrame_, run,
                [&](int abs) { clipFireAt(abs); },
                [&](bool wasPlaying) { if (wasPlaying) release(); });
            for (size_t i = evBefore; i < clipHost_.events.size(); i++)
                if (clipHost_.events[i].t == HostEvent::T::Stop) release();
        }
        if (samplesToGateOff_ >= 0)
            run = std::min(run, std::max(1, (int)std::ceil(samplesToGateOff_)));

        const double beats = hostRun ? hostPpq_ + pos * ppqPerSample
                            : hostClipMode_ ? std::max(0.0, hostFrame_ - anchorFrame_) * beatsPerSample
                                            : songPos_ * beatsPerSample;
        advanceParams(run);          // Finding J1/J2: chunk-rate parameter update
        renderVoice(L, R, pos, run, beats);

        if (internalRun) {
            samplesToNext_ -= run;
            songPos_ += run;
        }
        if (hostClipMode_) hostFrame_ += run;
        if (samplesToGateOff_ >= 0) {
            samplesToGateOff_ -= run;
            if (samplesToGateOff_ <= 0) {
                release();
                samplesToGateOff_ = -1;
            }
        }
        pos += run;
    }
    if (hostRun) {
        hostEndPpq_ = hostPpq_ + n * ppqPerSample;
        // Consecutive renders may be MIDI-delimited segments of one host
        // block. Continue from this segment until setHostTransport replaces it.
        hostPpq_ = hostEndPpq_;
    }

    vizPos  = ampStage_ != 0 ? (float)posSm_ : -1.0f;
    vizEnv  = (float)ampLevel_;
    vizFenv = ampStage_ != 0 ? (float)fenvVal_ : 0.0f;
    vizCut  = ampStage_ != 0 ? (float)curCut_ : -1.0f;
    vizGate = gate_;
    vizSemi = ampStage_ != 0 ? (int)std::lround(semiTarget_) : -100;
}

} // namespace fable
