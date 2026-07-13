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

// js:33-36 — log-cosh for the ADAA drive antiderivative
static inline double lcosh(double z) {
    double a = std::fabs(z);
    return a + std::log1p(std::exp(-2.0 * a)) - M_LN2;
}

static inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Finding 6: chunk-invariant smoothers (see Engine.cpp for the derivation).
// BL-1's pos smoothing legacy cadence was 0.35 per 16-sample sub-block; cutoff
// was 0.5 per 128-sample chunk, both at 48 kHz.
static const double BL_POS_TAU = 16.0 / (48000.0 * 0.4307829160924542);
static const double BL_CUT_TAU = 128.0 / (48000.0 * M_LN2);
static inline double smoothCoef(int n, double tauSr) { return 1 - std::exp(-(double)n / tauSr); }

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
    panic();
    held_.clear();
    std::fill(std::begin(phases_), std::end(phases_), 0.0);
    subPhase_ = 0; subIncPrev_ = -1;
    dcxL_ = dcxR_ = dcyL_ = dcyR_ = 0;
    shVal_ = 0; shPhase_ = -1;
}

// Finding 2: lock-free publication — same scheme as Engine::setTables. The
// audio thread never blocks on a table swap and never substitutes silence;
// retired_ keeps the previous set so its free lands on the message thread.
void BassEngine::setTables(std::vector<TablePtr> tables) {
    auto next = std::make_shared<TableSet>();
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
    retired_ = std::atomic_exchange(&tables_, std::shared_ptr<const TableSet>(std::move(next)));
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
    posSm_ = -1; cutSm_ = 0; cutPrev_ = -1;
    havePrev_ = false; subIncPrev_ = -1;
}

void BassEngine::panic() {
    kill();
    held_.clear();
}

void BassEngine::keyOn(int semi, float vel) {
    if (isPlaying()) return;       // audition when stopped · sequencer owns the voice
    held_.erase(std::remove(held_.begin(), held_.end(), semi), held_.end());
    const bool legato = !held_.empty() && gate_;
    held_.push_back(semi);
    if (legato) glideTo(semi, false);
    else        noteOn(semi, false, vel);
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

void BassEngine::setChain(const int* list, int n) {
    if (!list || n <= 0) return;
    chain_.assign(list, list + n);
    for (int& c : chain_)
        c = std::max(0, std::min(BL_NPATTERNS - 1, c));
    chainPos_ = std::min(chainPos_, (int)chain_.size() - 1);
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
    st.slide = (flags & 4) != 0;
    st.semi  = std::min(11, (int)pats[o + 1]) + 12 * (std::min(2, (int)pats[o + 2]) - 1);
    return st;
}

// Shared step-fire body (js:200-210): trigger the step and schedule the gate,
// holding through when the NEXT step ties in with a slide.
void BassEngine::fireStepAt(int s, int pat, int patNext, double dur) {
    const BassStep st = readStep(pats_.data(), pat, s);
    if (st.on) {
        if (st.slide && gate_) glideTo(st.semi, st.acc);
        else                   noteOn(st.semi, st.acc, st.acc ? BL_ACCENT_VEL : BL_PLAIN_VEL);
        const int sN = (s + 1) % BL_STEPS;
        const int patN = sN == 0 ? patNext : pat;
        const BassStep stN = readStep(pats_.data(), patN, sN);
        samplesToGateOff_ = (stN.on && stN.slide) ? -1 : BL_GATE_FRAC * dur;
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
        samplesToGateOff_ = (stN.on && stN.slide) ? -1 : BL_GATE_FRAC * dur;
    }
}

// js:187-218
void BassEngine::fireStep() {
    const double bpm = effectiveBpm();
    const double dur = (60.0 / bpm / 4.0) * sr_;
    const double swing = p_[BL_MASTER_SWING];
    if (step_ + 1 >= BL_STEPS) {                   // bar wrap advances the chain
        step_ = -1;
        chainPos_ = (chainPos_ + 1) % (int)chain_.size();
    }
    const int s = (step_ + 1) % BL_STEPS;
    const int pat = chain_[(size_t)chainPos_];
    const int patNext = chain_[(size_t)((chainPos_ + 1) % (int)chain_.size())];
    fireStepAt(s, pat, patNext, dur);
    const double offNow = (s % 2 == 1) ? swing * BL_SWING_MAX * dur : 0.0;
    const int sNext = (s + 1) % BL_STEPS;
    const double offNext = (sNext % 2 == 1) ? swing * BL_SWING_MAX * dur : 0.0;
    samplesToNext_ = dur - offNow + offNext;
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
    return (double)k * 0.25 + ((k & 1) ? swing * BL_SWING_MAX * 0.25 : 0.0);
}

void BassEngine::hostResync() {
    long k = (long)std::floor(hostPpq_ / 0.25) - 1;
    if (k < 0) k = 0;
    while (hostStepPpq(k) < hostPpq_ - 1e-9) k++;
    hostNextK_ = k;
    hostSynced_ = true;
}

void BassEngine::fireHostStep(long k) {
    const int  s   = (int)(k % BL_STEPS);
    const long bar = k / BL_STEPS;
    chainPos_ = (int)(bar % (long)chain_.size());
    const int pat = chain_[(size_t)chainPos_];
    const int patNext = chain_[(size_t)((bar + 1) % (long)chain_.size())];
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
        const double a = ((pan + 1) * M_PI) / 4;
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
            // polyblep both edges
            if (ph < inc) { const double t = ph / inc; s += -(t * t) + 2 * t - 1; }
            else if (ph > 0.5 && ph < 0.5 + inc) {
                const double t = (ph - 0.5) / inc; s -= -(t * t) + 2 * t - 1;
            }
            const float v = (float)(s * gain * 0.8);
            tmpL[off + i] += v; tmpR[off + i] += v;
            ph += inc; if (ph >= 1) ph -= 1;
        }
    } else {
        for (int i = 0; i < n; i++) {
            const float v = (float)(std::sin(ph * 2 * M_PI) * gain * 1.2);
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
        default: return std::sin(phase * 2 * M_PI);
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
    ftype_ = ftype;
    twoPole_ = ftype == 1;
    k1_ = 2 - 1.93 * res;             // SVF a1..a3 recomputed per sub-block in runFilter
}

// js:415-479 — ADAA lcosh drive, Cytomic SVF, LP24 second pass
void BassEngine::runFilter(const float* inL, const float* inR,
                           float* outL, float* outR, double drive, int n) {
    if (drive > 0.005) {
        const double dg = 1 + drive * 7;
        const double dcomp = 1 / std::pow(dg, 0.55);
        const double kF = dcomp / dg;
        double xpL = satXL_, xpR = satXR_;
        double FpL = kF * lcosh(dg * xpL), FpR = kF * lcosh(dg * xpR);
        for (int i = 0; i < n; i++) {
            const double aL = inL[i], aR = inR[i];
            const double dxL = aL - xpL;
            const double FL = kF * lcosh(dg * aL);
            outL[i] = (float)(dxL > 1e-5 || dxL < -1e-5 ? (FL - FpL) / dxL
                                                        : dcomp * std::tanh(dg * 0.5 * (aL + xpL)));
            xpL = aL; FpL = FL;
            const double dxR = aR - xpR;
            const double FR = kF * lcosh(dg * aR);
            outR[i] = (float)(dxR > 1e-5 || dxR < -1e-5 ? (FR - FpR) / dxR
                                                        : dcomp * std::tanh(dg * 0.5 * (aR + xpR)));
            xpR = aR; FpR = FR;
        }
        satXL_ = xpL; satXR_ = xpR;
    } else {
        for (int i = 0; i < n; i++) { outL[i] = inL[i]; outR[i] = inR[i]; }
        if (n > 0) { satXL_ = inL[n - 1]; satXR_ = inR[n - 1]; }
    }

    // Finding 7: cutoff ramps from the previous chunk's value; coefficients
    // recomputed per <=32-sample sub-block.
    const int ftype = ftype_;
    const double k1 = k1_;
    const double c1c = cutTarget_;
    const double c0c = cutPrev_ > 0 ? cutPrev_ : c1c;
    double* F = svf_;
    for (int at = 0; at < n; at += 32) {
        const int m = std::min(32, n - at);
        const double cut = c0c + (c1c - c0c) * ((double)(at + m) / n);
        const double gC = std::tan((M_PI * cut) / sr_);
        const double a1 = 1 / (1 + gC * (gC + k1));
        const double a2 = gC * a1, a3 = gC * a2;
        for (int ch = 0; ch < 2; ch++) {
            float* buf = ch == 0 ? outL : outR;
            const int o1 = ch * 2;
            double ic1 = F[o1], ic2 = F[o1 + 1];
            for (int i = at; i < at + m; i++) {
                const double x = buf[i];
                const double v3 = x - ic2;
                const double v1 = a1 * ic1 + a2 * v3;
                const double v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2 * v1 - ic1;
                ic2 = 2 * v2 - ic2;
                switch (ftype) {
                    case 0: case 1: buf[i] = (float)v2; break;
                    case 2: buf[i] = (float)(k1 * v1); break;
                    case 3: buf[i] = (float)(x - k1 * v1 - v2); break;
                    default: buf[i] = (float)(x - k1 * v1); break;
                }
            }
            F[o1] = ic1; F[o1 + 1] = ic2;
        }
        if (twoPole_) {
            for (int ch = 0; ch < 2; ch++) {
                float* buf = ch == 0 ? outL : outR;
                const int o1 = 4 + ch * 2;
                double ic1 = F[o1], ic2 = F[o1 + 1];
                for (int i = at; i < at + m; i++) {
                    const double x = buf[i];
                    const double v3 = x - ic2;
                    const double v1 = a1 * ic1 + a2 * v3;
                    const double v2 = ic2 + a2 * ic1 + a3 * v3;
                    ic1 = 2 * v1 - ic1;
                    ic2 = 2 * v2 - ic2;
                    buf[i] = (float)v2;
                }
                F[o1] = ic1; F[o1 + 1] = ic2;
            }
        }
    }
    cutPrev_ = c1c;
}

// ---------- render voice (js:482-543) ----------
void BassEngine::renderVoice(float* L, float* R, int off, int n, double beats) {
    if (ampStage_ == 0 && !gate_) return;          // LFO clock advances outside
    float* tmpL = tmpL_;
    float* tmpR = tmpR_;
    std::fill(tmpL, tmpL + n, 0.0f);
    std::fill(tmpR, tmpR + n, 0.0f);

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
        if (setupOsc(noteAbs, count)) renderOsc(tmpL, tmpR, at, count);
        else havePrev_ = false;
        renderSub(tmpL, tmpR, at, count, noteRootAbs);
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
    const double gain = vel_ * (1 + (acc_ ? accAmt * BL_ACC_GAIN : 0)) * 0.9;

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
        const double amp = ampLevel_ * gain;
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

    // Finding 2: snapshot the published table set once for the whole call —
    // the shared_ptr keeps setupOsc's cached raw pointers valid even if the
    // message thread publishes a new set mid-block. Never blocks, never silent.
    const std::shared_ptr<const TableSet> snap = std::atomic_load(&tables_);
    curTables_ = snap.get();

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
            run = std::min(run, (int)std::ceil(samplesToNext_));
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
    if (hostRun) hostEndPpq_ = hostPpq_ + n * ppqPerSample;

    vizPos  = ampStage_ != 0 ? (float)posSm_ : -1.0f;
    vizEnv  = (float)ampLevel_;
    vizFenv = ampStage_ != 0 ? (float)fenvVal_ : 0.0f;
    vizCut  = ampStage_ != 0 ? (float)curCut_ : -1.0f;
    vizGate = gate_;
    vizSemi = ampStage_ != 0 ? (int)std::lround(semiTarget_) : -100;
}

} // namespace fable
