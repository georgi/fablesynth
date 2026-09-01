// Line-faithful port of src/drum/engine/worklet-drum.js (the lockstep
// reference). Section comments cite the source lines. Two deliberate
// deviations from the JS: Math.random() becomes the engine's seeded
// xorshift Rng (deterministic tests), and output goes to 5 stereo buses
// selected by each pad's OUT param instead of one worklet output.
#include "DrumEngine.h"
#include "OneShotSamples.gen.h"

#include <algorithm>
#include <cmath>

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

static inline double clampd(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
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

// Crossfade time for a filter-type change (see FilterState::svfOld).
static const double DR_SWITCH_FADE = 0.003;

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
    sample.pos = -1; sample.index = -1; sample.done = false;
    sample.pStep = 0; sample.havePrev = false;
    std::fill(std::begin(f.svf), std::end(f.svf), 0.0);
    f.cutSm = 0; f.cutPrev = -1; f.satXL = 0; f.satXR = 0;
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
    dcR_ = std::pow(DR_DC_R, 48000.0 / sr_);
    chokeCoef_ = 1.0 - std::exp(-1.0 / (DR_CHOKE_TAU * sr_));   // Finding D9
    for (auto& fx : padFx_) fx.prepare(sampleRate);
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

// Finding 2: lock-free publication — same scheme as Engine::setTables. The
// audio thread never blocks on a table swap and never substitutes silence;
// retired_ keeps the previous set so its free lands on the message thread.
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
        next->push_back(std::move(e));
    }
    retired_ = std::atomic_exchange(&tables_, std::shared_ptr<const TableSet>(std::move(next)));
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
}

void DrumEngine::stop() {
    playing_ = false; step_ = -1;
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

void DrumEngine::setBpmOverride(double bpm) {
    bpmOverride_ = bpm > 0 ? bpm : 0;
}

double DrumEngine::effectiveBpm() const {
    double pbpm = std::fpclassify(p_[DG_SEQ_BPM]) != FP_ZERO ? (double)p_[DG_SEQ_BPM] : 126.0;
    return bpmOverride_ > 0 ? bpmOverride_ : clampd(pbpm, 60.0, 200.0);
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
}

void DrumEngine::fireHostStep(long k) {
    int  s   = (int)(k % DR_STEPS);
    long bar = k / DR_STEPS;
    chainPos_ = (int)(bar % (long)chain_.size());
    int pat = chain_[(size_t)chainPos_];
    for (int i = 0; i < DR_NPADS; i++) {
        uint8_t val = pats_[(size_t)(pat * DR_NPADS * DR_STEPS + i * DR_STEPS + s)];
        if (val) trigger(i, val == 2 ? DR_ACCENT_VEL : DR_PLAIN_VEL);
    }
    step_ = s;
}

// Hosted twin of fireHostStep (docs/sq4-clips.md §6): byte source is the
// ClipHost's live clip instead of pats_/chain_. Each pad's val is
// independent (no tie/slide lookahead), so this is a straight per-pad scan.
void DrumEngine::clipFireAt(int abs) {
    const uint8_t* clip = clipHost_.clipData();
    const int bar = abs / DR_STEPS, s = abs % DR_STEPS;
    for (int i = 0; i < DR_NPADS; i++) {
        uint8_t val = clip[(size_t)(bar * DR_NPADS * DR_STEPS + i * DR_STEPS + s)];
        if (val) trigger(i, val == 2 ? DR_ACCENT_VEL : DR_PLAIN_VEL);
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
        (ti >= 0 && ti < (int)curTables_->size() && (*curTables_)[(size_t)ti].data)
            ? &(*curTables_)[(size_t)ti] : nullptr;
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
    int f1 = std::min(table->frames - 1, f0 + 1);
    o.posF = posF;
    o.f0 = f0;

    double cps = freq / sr_;
    double maxRatio = std::pow(2.0, (std::fabs(det) * 50.0) / 1200.0);
    // Finding D5: full trilinear mip blending. The old code only crossfaded
    // inside a 0.07-octave band and hard-switched everywhere else, which a drum
    // pitch envelope crosses in milliseconds. blend = mip - mipF is continuous
    // across a mip boundary: just below integer M the coarse read is mip M with
    // blend ~0, just above it is mip M+1 with blend ~1 onto fineMip = M.
    double mipF = std::log2((cps * maxRatio * 1024.0) / 0.475);
    int mip = 0;
    if (mipF > 0) mip = std::min(table->mips - 1, (int)std::ceil(mipF));
    else mipF = 0;                       // mip 0: off0b == off0, blend is a no-op
    int fineMip = mip > 0 ? mip - 1 : 0;

    o.off0  = (f0 * table->mips + mip) * table->size;
    o.off1  = (f1 * table->mips + mip) * table->size;
    o.off0b = (f0 * table->mips + fineMip) * table->size;
    o.off1b = (f1 * table->mips + fineMip) * table->size;
    o.mipF = mipF;
    o.mip = mip;
    o.mipBlend = clampd(mip - mipF, 0.0, 1.0);
    o.data = table->data;
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
    // Finding D5: the morph fraction used to stop ramping whenever the frame
    // or mip offset changed. Ramp the ABSOLUTE frame position instead and
    // express it in the current sub-block's f0, so a frame or mip step is a
    // continuous sweep rather than a jump. The same trick ramps mipF, which
    // makes the trilinear blend continuous too.
    const double ft1 = o.posF - o.f0;
    const double ft0 = rp ? clampd(o.pPosF - o.f0, -1.0, 1.0) : ft1;
    const double dFt = (ft1 - ft0) * invN;
    const double mf1 = o.mipF;
    const double mf0 = rp ? o.pMipF : mf1;
    const double dMf = (mf1 - mf0) * invN;
    const double blend0 = clampd(o.mip - mf0, 0.0, 1.0);
    const double blend1 = o.mipBlend;
    const bool   useBlend = blend0 > 0.001 || blend1 > 0.001;
    const int    mipI = o.mip;
    const double g = o.gain;
    const int off0 = o.off0, off1 = o.off1;
    for (int u = 0; u < o.uni; u++) {
        double ph = o.phases[u];
        const double inc1 = o.incs[u];
        const double inc0 = rp ? o.pIncs[u] : inc1;
        const double dInc = (inc1 - inc0) * invN;
        const double gl1 = o.gl[u] * g, gr1 = o.gr[u] * g;
        const double gl0 = rp ? (double)o.pGl[u] : gl1, gr0 = rp ? (double)o.pGr[u] : gr1;
        const double dGl = (gl1 - gl0) * invN, dGr = (gr1 - gr0) * invN;
        if (!useBlend) {
            for (int i = 0; i < n; i++) {
                int idx = (int)ph;
                double frac = ph - idx;
                int im1 = (idx - 1) & mask, i2 = (idx + 1) & mask, i3 = (idx + 2) & mask;
                double s0 = rdH(data, off0, im1, idx, i2, i3, frac);
                double s1 = rdH(data, off1, im1, idx, i2, i3, frac);
                double s = s0 + (ft0 + dFt * i) * (s1 - s0);
                tmpL[off + i] += (float)(s * (gl0 + dGl * i));
                tmpR[off + i] += (float)(s * (gr0 + dGr * i));
                ph += inc0 + dInc * i;
                if (ph >= size) ph -= size;
            }
        } else {
            const int off0b = o.off0b, off1b = o.off1b;
            for (int i = 0; i < n; i++) {
                int idx = (int)ph;
                double frac = ph - idx;
                int im1 = (idx - 1) & mask, i2 = (idx + 1) & mask, i3 = (idx + 2) & mask;
                double ftN = ft0 + dFt * i;
                double blendN = clampd(mipI - (mf0 + dMf * i), 0.0, 1.0);
                double sc0 = rdH(data, off0, im1, idx, i2, i3, frac);
                double sc1 = rdH(data, off1, im1, idx, i2, i3, frac);
                double sc = sc0 + ftN * (sc1 - sc0);
                double sf0 = rdH(data, off0b, im1, idx, i2, i3, frac);
                double sf1 = rdH(data, off1b, im1, idx, i2, i3, frac);
                double sf = sf0 + ftN * (sf1 - sf0);
                double s = sc + blendN * (sf - sc);
                tmpL[off + i] += (float)(s * (gl0 + dGl * i));
                tmpR[off + i] += (float)(s * (gr0 + dGr * i));
                ph += inc0 + dInc * i;
                if (ph >= size) ph -= size;
            }
        }
        o.phases[u] = ph;
        o.pIncs[u] = inc1;
        o.pGl[u] = (float)gl1; o.pGr[u] = (float)gr1;
    }
    o.pPosF = o.posF; o.pMipF = mf1; o.pUni = o.uni; o.pData = data;
    o.havePrev = true;
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
void DrumEngine::renderSample(SampleState& state, int padI, double pitchEnv,
                              double modStart, double modFine, double modPitch,
                              float* tmpL, float* tmpR, int off, int n) const {
    if (state.done) return;
    const auto& bank = drumOneShots();
    if (bank.empty()) return;
    const int index = std::max(0, std::min((int)bank.size() - 1,
        (int)param(dpid(padI, DP_OSCB_TABLE))));
    const auto& sample = bank[(size_t)index];
    if (sample.data == nullptr || sample.length < 2) return;

    const double start = clampd(param(dpid(padI, DP_OSCB_POS)) + modStart, 0.0, 0.999);
    const double end = std::max(start + 1.0 / sample.length,
        clampd(param(dpid(padI, DP_OSCB_DETUNE)), 0.0, 1.0));
    const bool reverse = param(dpid(padI, DP_OSCB_PHASE)) >= 0.5f;
    if (state.pos < 0 || state.index != index) {
        state.index = index;
        state.pos = (reverse ? end : start) * (sample.length - 1);
        state.havePrev = false;
    }
    const double semis = param(dpid(padI, DP_OSCB_TUNE))
        + (param(dpid(padI, DP_OSCB_FINE)) + modFine) / 100.0
        + pitchEnv + modPitch;
    const double step1 = ((double)sample.sampleRate / sr_) * std::pow(2.0, semis / 12.0)
        * (reverse ? -1.0 : 1.0);
    const double step0 = state.havePrev ? state.pStep : step1;
    const double dStep = n > 0 ? (step1 - step0) / n : 0.0;
    const double level = clampd(param(dpid(padI, DP_OSCB_LEVEL)), 0.0, 1.2);
    const double gain = level * level * 0.75 / 32768.0;
    const int last = sample.length - 1;
    const double lo = start * last;
    const double hi = end * last;

    // Hermite read with clamped edges (the source is a one-shot, not a loop).
    const int16_t* d = sample.data;
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

    // Edge fades, expressed in source samples so the fade lasts the same
    // output time at any transposition. A region that starts at the sample's
    // own beginning keeps its transient: only an interior edge is faded in.
    const double half = 0.5 * (hi - lo);
    const bool fadeInEdge = reverse ? (hi < last - 1e-9) : (lo > 1e-9);
    double pos = state.pos;
    for (int i = 0; i < n; ++i) {
        if ((!reverse && pos >= hi) || (reverse && pos <= lo)) {
            state.done = true;
            break;
        }
        const double st = step0 + dStep * i;
        const double ast = std::fabs(st);
        const double fadeSrc = std::min(std::max(ast, 1.0) * DR_SAMPLE_FADE * sr_,
                                        std::max(half, 1.0));
        const double dIn  = reverse ? hi - pos : pos - lo;
        const double dOut = reverse ? pos - lo : hi - pos;
        double fade = clampd(dOut / fadeSrc, 0.0, 1.0);
        if (fadeInEdge) fade = std::min(fade, clampd(dIn / fadeSrc, 0.0, 1.0));

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
        const float out = (float)(value * gain * fade);
        tmpL[off + i] += out;
        tmpR[off + i] += out;
        pos += st;
    }
    state.pos = pos;
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
    if (drive > 0.005) {
        const double dg = 1 + drive * 7;
        const double dcomp = 1 / std::pow(dg, 0.55);
        const double kF = dcomp / dg;
        double xpL = fs.satXL, xpR = fs.satXR;
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
        fs.satXL = xpL; fs.satXR = xpR;
    } else {
        for (int i = 0; i < n; i++) { outL[i] = inL[i]; outR[i] = inR[i]; }
        if (n > 0) { fs.satXL = inL[n - 1]; fs.satXR = inR[n - 1]; }
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
        bool aOn = setupOsc(v.oA, dpid(padI, DP_OSCA_TABLE), pe, m.posA, m.fineA, m.pitch, count);
        if (aOn) renderOsc(v.oA, tmpL, tmpR, at, count); else v.oA.havePrev = false;
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

    // Finding 2: snapshot the published table set once for the whole call —
    // the shared_ptr keeps setupOsc's cached raw pointers valid even if the
    // message thread publishes a new set mid-block. Never blocks, never silent.
    const std::shared_ptr<const TableSet> snap = std::atomic_load(&tables_);
    curTables_ = snap.get();
    if (padFxEnabled_) {
        for (int i = 0; i < DR_NPADS; ++i) padFx_[(size_t)i].setParams(p_, i);
        for (auto& b : busOut_) b.setParams(p_);   // Finding D1
    }

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

    int pos = 0;
    while (pos < n) {
        int run = std::min(128, n - pos);
        if (hostRun) {
            // Fire every step due at/before pos; split the run at the next one.
            for (;;) {
                long fireAt = (long)std::ceil(
                    (hostStepPpq(hostNextK_) - hostPpq_) * samplesPerPpq - 1e-9);
                if (fireAt <= pos) { fireHostStep(hostNextK_++); continue; }
                if (fireAt - pos < run) run = (int)(fireAt - pos);
                break;
            }
        } else if (internalRun) {                    // js:465-469
            if (samplesToNext_ <= 0) fireStep();
            run = std::min(run, (int)std::ceil(samplesToNext_));
        } else if (hostClipMode_) {
            // At most one fire per quantum (ClipHost contract). DR-1 pads
            // are one-shot voices with no gate to release on Stop/swap
            // (worklet-drum.js hostTick/clipFire never touch a sounding
            // pad) — 2-arg tick, no onSwap hook needed.
            clipHost_.tick(hostFrame_, run, [&](int abs) { clipFireAt(abs); });
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
            if (tl.active) renderPad(tl, i, padL_, padR_, 0, run);
            if (v.active) renderPad(v, i, padL_, padR_, 0, run);
            // Each pad owns a continuous chain, including its delay/reverb
            // tail. Processing silence after the one-shot voice ends is what
            // lets that tail ring naturally and keeps all pad paths aligned.
            padFx_[(size_t)i].process(padL_, padR_, run);
            int out = std::max(0, std::min(DR_NBUSES - 1, (int)param(dpid(i, DP_OUT))));
            for (int s = 0; s < run; ++s) {
                outs[out][0][pos + s] += padL_[s];
                outs[out][1][pos + s] += padR_[s];
            }
        }
        // Finding D1: master gain, DC block and the safety limiter run here,
        // once per bus after the sum, not sixteen times inside the pad chains.
        // MAIN now actually has a ceiling.
        if (padFxEnabled_)
            for (int b = 0; b < DR_NBUSES; ++b)
                busOut_[(size_t)b].process(outs[b][0] + pos, outs[b][1] + pos, run);

        if (internalRun) samplesToNext_ -= run;      // js:475
        if (hostClipMode_) hostFrame_ += run;
        pos += run;
    }
    if (hostRun) hostEndPpq_ = hostPpq_ + n * ppqPerSample;

    const PadVoice& v = voices_[(size_t)sel_];
    vizA = v.active ? (float)v.oA.posSm : -1.0f;
    const auto& samples = drumOneShots();
    vizB = v.active && v.sample.index >= 0 && v.sample.index < (int)samples.size()
        ? (float)clampd(v.sample.pos / std::max(1, samples[(size_t)v.sample.index].length - 1), 0.0, 1.0)
        : -1.0f;
    vizEnv = v.active ? (float)v.ampLevel : 0.0f;
}

} // namespace fable
