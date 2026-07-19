#include "Engine.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace fable {

static constexpr double PI = 3.14159265358979323846;
static constexpr double LN2 = 0.6931471805599453;
static const double DC_R = 0.9998; // DC-blocker pole at the 48 kHz reference (Finding 9: prepare() maps it to sr)

static constexpr size_t slot(int index) { return (size_t)index; }
template <typename T>
static bool exactlyEqual(T a, T b) { return std::equal_to<T>{}(a, b); }
template <typename T>
static bool exactlyZero(T value) { return std::fpclassify(value) == FP_ZERO; }

// Finding 6: chunk-invariant smoothers. Legacy applied a fixed coef per render
// chunk (0.35 wavetable pos, 0.5 cutoff, per 128 samples at 48 kHz), so the
// smoothing time varied with sample rate and transport-split chunk sizes.
// tau = T/(48000*ln(1/(1-c))) reproduces the legacy response exactly at the
// reference cadence; coef = 1-exp(-n/(tau*sr)) makes it rate/split-invariant.
static const double POS_TAU = 128.0 / (48000.0 * 0.4307829160924542); // 0.35 per 128
static const double CUT_TAU = 128.0 / (48000.0 * LN2);                // 0.5 per 128
static inline double smoothCoef(int n, double tauSr) { return 1 - std::exp(-(double)n / tauSr); }

// Oscillator phase normally advances by less than one table length per sample,
// but defensive wrapping keeps a malformed/modulated parameter snapshot from
// turning the table index into an out-of-bounds read on the audio thread.
static inline double wrapOscPhase(double phase, int size) {
    if (!std::isfinite(phase)) return 0.0;
    if (phase < 0.0 || phase >= size) {
        phase = std::fmod(phase, (double)size);
        if (phase < 0.0) phase += size;
    }
    return phase;
}

// Finding 10: 4-point cubic Hermite (Catmull-Rom) table read. Indices are
// pre-wrapped by the caller (branchless & mask), off selects the frame/mip.
static inline double rdH(const float* d, int off, int im1, int i0, int i1, int i2, double f) {
    const double ym1 = d[off + im1], y0 = d[off + i0], y1 = d[off + i1], y2 = d[off + i2];
    const double c1 = 0.5 * (y1 - ym1);
    const double c2 = ym1 - 2.5 * y0 + 2.0 * y1 - 0.5 * y2;
    const double c3 = 0.5 * (y2 - ym1) + 1.5 * (y0 - y1);
    return ((c3 * f + c2) * f + c1) * f + y0;
}

// Vowel formants (A-E-I-O-U) for the VOWEL filter morph.
static const double VOWELS[5][3] = {
    {730, 1090, 2440}, {530, 1840, 2480}, {390, 1990, 2550}, {570, 840, 2410}, {440, 1020, 2240}};
static const double F_AMPS[3] = {1, 0.55, 0.32};

// Numerically stable ln(cosh(z)) — antiderivative of tanh, used by the ADAA saturator.
static inline double lcosh(double z) {
    double a = std::abs(z);
    return a + std::log1p(std::exp(-2 * a)) - LN2;
}

// ---------------- Env ----------------
void Env::set(double a, double d, double sus, double r, double sr) {
    s = sus;
    // sr is part of the key: hosts can re-prepare at a new sample rate, and the
    // coefficients below bake it in (the web worklet's sr is immutable).
    if (!exactlyEqual(a, a_) || !exactlyEqual(d, d_)
        || !exactlyEqual(r, r_) || !exactlyEqual(sr, sr_)) {
        a_ = a; d_ = d; r_ = r; sr_ = sr;
        ca = 1 - std::exp(-1 / (std::max(0.0008, a) * sr));
        cd = 1 - std::exp(-1 / (std::max(0.002, d / 4.5) * sr));
        cr = 1 - std::exp(-1 / (std::max(0.002, r / 4.5) * sr));
    }
}
double Env::process() {
    switch (state) {
        case 1:
            level += (1.45 - level) * ca;
            if (level >= 1) { level = 1; state = 2; }
            break;
        case 2:
            level += (s - level) * cd;
            if (level - s < 0.0005) state = 3;
            break;
        case 3: level = s; break;
        case 4:
            level -= level * cr;
            if (level < 1e-4) { level = 0; state = 0; }
            break;
        case 5:
            // steal fade: ~2 ms to silence, then the voice is free for its pending note
            level -= level * 0.12;
            if (level < 1e-4) { level = 0; state = 0; }
            break;
    }
    return level;
}

// ---------------- Lfo ----------------
double Lfo::valueOff(int shape, double off) const {
    double p = phase + off;
    p -= std::floor(p);
    switch (shape) {
        case 0: return std::sin(2 * PI * p);
        case 1: return 1 - 4 * std::abs(p - 0.5);
        case 2: return 1 - 2 * p;
        case 3: return p < 0.5 ? 1 : -1;
        default: return hold;
    }
}
void Lfo::advance(double rate, int n, double sr) {
    elapsed += n;
    // NaN guard (mirrors the web LFO): a non-finite rate must not latch a sticky
    // NaN phase that would never recover.
    double d = (rate * n) / sr;
    if (std::isfinite(d)) phase += d;
    if (phase >= 1) { phase -= std::floor(phase); hold = rng->next() * 2 - 1; }
}

// ---------------- FilterState ----------------
void FilterState::reset() {
    for (auto& x : svf) x = 0;
    for (auto& x : fmt) x = 0;
    combL.fill(0); combR.fill(0); combW = 0;
    cutSm = 0; satXL = 0; satXR = 0;
    cutPrev = -1; combLenPrev = -1;
}

// ---------------- Voice ----------------
void Voice::noteOn(int n, double v, double startPitch, long a, Rng& rng) {
    note = n; vel = v; gate = true; age = a; pitch = startPitch;
    velGain = 0.25 + 0.75 * v * v;
    ampEnv.trigger(); modEnv.trigger();
    lfo1.rng = &rng; lfo2.rng = &rng;
    lfo1.reset(); lfo2.reset();
    // Start phase is in SAMPLES (0..SIZE): scale the random draw to a full
    // cycle so unison voices (and osc A vs B) decorrelate at note start.
    for (int i = 0; i < MAXUNI; i++) { oA.phases[i] = rng.next() * SIZE; oB.phases[i] = rng.next() * SIZE; }
    oA.posSm = -1; oB.posSm = -1;
    oA.havePrev = false; oB.havePrev = false;
    subPhase = 0; subIncPrev = -1; ampFacPrev = -1;
    f1.reset(); f2.reset();
    dcxL = dcxR = dcyL = dcyR = 0;
}

// ---------------- Engine ----------------
// prepare() contract (Finding 11): establishes a clean state for the new
// sample rate — every active voice is killed and all recursive per-voice state
// (filters, DC blockers, pink noise, smoothers, ramps) is cleared, and the
// fixed 48 kHz-reference per-sample coefficients are remapped to sr (Finding 9).
void Engine::prepare(double sampleRate) {
    sr_ = sampleRate;
    // Same analog pole at any rate: p' = p^(48k/sr); one-pole input gains are
    // rescaled to keep the low-frequency gain, so the pink spectrum matches
    // the 48 kHz reference (b[5]'s pole is negative — its DC gain is g/(1+p)).
    const double r = 48000.0 / sr_;
    dcR_ = std::pow(DC_R, r);
    static const double PP[6] = {0.99886, 0.99332, 0.969, 0.8665, 0.55, 0.7616};
    static const double PG[6] = {0.0555179, 0.0750759, 0.153852, 0.3104856, 0.5329522, 0.016898};
    for (int i = 0; i < 6; i++) {
        pinkP_[i] = std::pow(PP[i], r);
        pinkG_[i] = i == 5 ? PG[5] * (1 + pinkP_[5]) / (1 + PP[5])
                           : PG[i] * (1 - pinkP_[i]) / (1 - PP[i]);
    }
    for (auto& v : voices_) {
        v.kill();
        v.f1.reset(); v.f2.reset();
        v.dcxL = v.dcxR = v.dcyL = v.dcyR = 0;
        for (auto& x : v.pb) x = 0;
        v.subPhase = 0; v.subIncPrev = -1; v.ampFacPrev = -1;
        v.oA.posSm = -1; v.oB.posSm = -1;
        v.oA.havePrev = false; v.oB.havePrev = false;
        v.lfo1.rng = &rng_; v.lfo2.rng = &rng_;
    }
    gLfo1_.rng = &rng_; gLfo2_.rng = &rng_;
    gLfo1_.reset(); gLfo2_.reset();
}

double Engine::lfoHz(int base) const {
    if (!exactlyZero(p_[slot(base + LFO_SYNC)]))
        return (bpm_ / 60.0) * lfoDivFactor((int)p_[slot(base + LFO_SYNCRATE)]);
    return p_[slot(base + LFO_RATE)];
}

// Update a free-running global LFO once per block. When synced AND the host is
// playing, the phase is derived from the transport position so a synced cycle
// starts on the downbeat (ppq = quarter notes, factor = cycles per beat). When
// unsynced — or synced but stopped — it free-runs at its Hz so movement
// continues. (Retrig LFOs are per-voice / note-aligned and skip this.)
void Engine::updateGlobalLfo(Lfo& g, int base, double ppqChunk, int n) {
    if (!exactlyZero(p_[slot(base + LFO_SYNC)]) && playing_) {
        double ph = ppqChunk * lfoDivFactor((int)p_[slot(base + LFO_SYNCRATE)]);
        ph -= std::floor(ph);
        if (ph < g.phase) g.hold = rng_.next() * 2 - 1; // grid wrap -> new S&H value
        g.phase = ph;
    } else {
        g.advance(lfoHz(base), n, sr_);
    }
}

void Engine::setTables(std::vector<TablePtr> tables) {
    // Finding 2: build a complete immutable set, then publish it with an atomic
    // shared_ptr swap — the audio thread never waits and never goes silent.
    // The previously published set is parked in retired_ so it is normally
    // freed here (message thread) on the NEXT publish; if the audio thread
    // happens to drop the last reference at a block end instead, the freed
    // payload is only a small vector of views (sample data is shared TablePtrs
    // typically also owned by the processor's pool).
    auto next = std::make_shared<TableSet>();
    next->reserve(tables.size());
    for (auto& t : tables) {
        EngineTable e;
        if (t) {
            e.frames = t->frames; e.mips = t->mips; e.size = t->size; e.mask = t->size - 1;
            e.data = t->data.data();
            e.src = std::move(t);
        }
        next->push_back(std::move(e));
    }
    retired_ = std::atomic_exchange(&tables_, std::shared_ptr<const TableSet>(std::move(next)));
}

void Engine::noteOn(int n, double vel) {
    Voice* voice = nullptr;
    for (auto& v : voices_) if (v.gate && v.note == n) { voice = &v; break; }
    if (!voice) for (auto& v : voices_) if (!v.active()) { voice = &v; break; }
    if (!voice) {
        Voice* best = nullptr;
        for (auto& v : voices_) {
            if (!best) { best = &v; continue; }
            int vRel = v.gate ? 1 : 0, bRel = best->gate ? 1 : 0;
            if (vRel < bRel || (vRel == bRel && v.age < best->age)) best = &v;
        }
        voice = best;
    }
    double glide = p_[MASTER_GLIDE];
    double start = glide > 0.001 ? lastPitch_ : n;
    lastPitch_ = n;
    if (voice->ampEnv.state != 0 && voice->ampEnv.level > 1e-3) {
        // Steal / same-note retrigger of an audible voice: hard-resetting phases
        // and filter state under a hot envelope clicks. Fade out instead (Env
        // state 5) and start the note once the voice reaches silence.
        voice->pendNote = n; voice->pendVel = vel; voice->pendStart = start;
        voice->hasPending = true;
        voice->gate = false;
        voice->ampEnv.state = 5;
    } else {
        voice->noteOn(n, vel, start, clock_++, rng_);
    }
}

void Engine::noteOff(int n) {
    for (auto& v : voices_) {
        // A note released before its steal fade finished must not start at all.
        if (v.hasPending && v.pendNote == n) v.hasPending = false;
        if (v.gate && v.note == n) v.noteOff();
    }
}

// ---------------- note sequencer (worklet.js seqRead/seqGateOff/seqFire) ----------------

void Engine::seqPlay() {
    if (hostClipMode_) return;     // hosted clip owns the transport
    if (seqHostPlaying_) return;   // host owns the transport while rolling
    seqGateOff();                  // restarting must not orphan an old gate
    seqPlaying_ = true;
    seqStep_ = -1;
    seqChainPos_ = 0;
    seqToNext_ = 0;
    seqSongPos_ = 0;
}

void Engine::seqStop() {
    seqPlaying_ = false;
    seqStep_ = -1;
    seqGateOff();                    // worklet 'stop' gates off every sounding note
}

void Engine::setSeqPatterns(const uint8_t* data, int n) {
    if (data && n == (int)seqPats_.size())
        std::copy(data, data + n, seqPats_.begin());
}

void Engine::setSeqChain(const int* list, int n) {
    if (!list || n <= 0) return;
    seqChain_.assign(list, list + n);
    for (int& c : seqChain_)
        c = std::max(0, std::min(SEQ_NPATTERNS - 1, c));
    seqChainPos_ = std::min(seqChainPos_, (int)seqChain_.size() - 1);
}

void Engine::setBpmOverride(double bpm) {
    bpmOverride_ = bpm > 0 ? bpm : 0;
}

double Engine::seqEffectiveBpm() const {
    if (bpmOverride_ > 0) return bpmOverride_;
    double pbpm = !exactlyZero(p_[SEQ_BPM]) ? (double)p_[SEQ_BPM] : 120.0;
    return std::min(200.0, std::max(60.0, pbpm));
}

// worklet seqRead (noteseq.ts getStep folded to a semitone offset + duration)
Engine::SeqReadStep Engine::readSeqStep(const uint8_t* pats, int pat, int s) {
    const int o = (pat * SEQ_STEPS + s) * SEQ_STEP_STRIDE;
    const uint8_t flags = pats[o];
    SeqReadStep st;
    st.on   = (flags & 1) != 0;
    st.acc  = (flags & 2) != 0;
    st.duration = std::max(1, std::min(SEQ_MAX_NOTE_STEPS, (int)((flags >> 2) & 0x3f)));
    st.semi = std::min(11, (int)pats[o + 1]) + 12 * (std::min(2, (int)pats[o + 2]) - 1);
    return st;
}

// Gate off every sounding sequencer note (worklet seqGateOff): used on
// stop, clip swap and clip stop. Drains the per-note off queue.
void Engine::seqGateOff() {
    for (int i = 0; i < seqOffCount_; ++i) noteOff(seqOff_[i].note);
    seqOffCount_ = 0;
    seqLastNote_ = -1;
}

// Per-note off queue (worklet seqScheduleOff). A retriggered pitch supersedes
// any pending off for it (one voice per note), so overlapping DIFFERENT notes
// ring concurrently while a repeated pitch takes the new duration.
void Engine::seqScheduleOff(int note, double remaining) {
    int w = 0;                       // de-dup by note value (swap-remove)
    for (int i = 0; i < seqOffCount_; ++i)
        if (seqOff_[i].note != note) seqOff_[w++] = seqOff_[i];
    seqOffCount_ = w;
    if (seqOffCount_ < kSeqOffCap) {
        seqOff_[seqOffCount_].note = note;
        seqOff_[seqOffCount_].remaining = remaining;
        ++seqOffCount_;
    } else {
        // Cap exceeded (>> realistic overlap): drop the oldest to make room.
        for (int i = 1; i < kSeqOffCap; ++i) seqOff_[i - 1] = seqOff_[i];
        seqOff_[kSeqOffCap - 1].note = note;
        seqOff_[kSeqOffCap - 1].remaining = remaining;
    }
    seqLastNote_ = note;
}

// Smallest pending-off remaining (-1 when nothing is scheduled). The render
// loop splits a block at this sample so each off lands on its exact frame.
double Engine::seqEarliestOff() const {
    double m = -1.0;
    for (int i = 0; i < seqOffCount_; ++i)
        if (m < 0.0 || seqOff_[i].remaining < m) m = seqOff_[i].remaining;
    return m;
}

// Shared step-fire body (worklet seqFire). noteOn then schedule this note's
// own gate-off at st.duration 16th-steps — no seqGateOff first, so a note can
// overlap the next step's note when its duration extends past it.
void Engine::seqFireAt(int s, int pat, int /*patNext*/, double dur) {
    const SeqReadStep st = readSeqStep(seqPats_.data(), pat, s);
    if (st.on) {
        int root = (int)p_[SEQ_ROOT];
        if (root == 0) root = 48;
        const int n = root + st.semi;
        const double vel = st.acc ? SEQ_ACCENT_VEL : SEQ_PLAIN_VEL;
        noteOn(n, vel);
        seqScheduleOff(n, st.duration * dur);
    }
    seqStep_ = s;
}

// Hosted twin of seqFireAt (docs/sq4-clips.md §6): each on-lane gates for its
// OWN duration (worklet clipFire), so a chord's voices release independently.
// Only the byte source differs — the clip layout (flags, note, oct+1 per
// lane) is byte-identical to a seq pattern, so readSeqStep() reads it with
// the clip's bar standing in for `pat`.
void Engine::clipFireAt(int abs) {
    const uint8_t* clip = clipHost_.clipData();
    const int bar = abs / SEQ_STEPS;
    const int s   = abs % SEQ_STEPS;
    std::array<SeqReadStep, SQ_WT_POLY_LANES> chord {};
    bool any = false;
    for (int lane = 0; lane < SQ_WT_POLY_LANES; ++lane) {
        const int o = sqWtNoteIdx(bar, s, lane);
        chord[(size_t)lane] = readSeqStep(clip + o, 0, 0);
        any = any || chord[(size_t)lane].on;
    }
    if (any) {
        int root = (int)p_[SEQ_ROOT];
        if (root == 0) root = 48;
        const double bpm = std::max(60.0, std::min(200.0, bpm_));
        const double dur = sqSamplesPerStep(bpm, sr_);
        for (const auto& st : chord) if (st.on) {
            const int n = root + st.semi;
            noteOn(n, st.acc ? SEQ_ACCENT_VEL : SEQ_PLAIN_VEL);
            seqScheduleOff(n, st.duration * dur);
        }
    }
}

// worklet seqFire — internal clock: real-sample step durations, swing delays
// odd 16ths by swing * SEQ_SWING_MAX of a step.
void Engine::seqFire() {
    const double bpm = seqEffectiveBpm();
    const double dur = (60.0 / bpm / 4.0) * sr_;
    const double swing = std::min(1.0, std::max(0.0, (double)p_[SEQ_SWING]));
    if (seqStep_ + 1 >= SEQ_STEPS) {               // bar wrap advances the chain
        seqStep_ = -1;
        seqChainPos_ = (seqChainPos_ + 1) % (int)seqChain_.size();
    }
    const int s = seqStep_ + 1;
    const int pat = seqChain_[(size_t)seqChainPos_];
    const int patNext = seqChain_[(size_t)((seqChainPos_ + 1) % (int)seqChain_.size())];
    seqFireAt(s, pat, patNext, dur);
    const double offNow = (s % 2 == 1) ? swing * SEQ_SWING_MAX * dur : 0.0;
    const int sNext = (s + 1) % SEQ_STEPS;
    const double offNext = (sNext % 2 == 1) ? swing * SEQ_SWING_MAX * dur : 0.0;
    seqToNext_ = dur - offNow + offNext;
}

// ---------------- host transport lock (BassEngine scheme) ----------------

void Engine::setSeqHostTransport(double ppq, double bpm, bool playing) {
    if (!std::isfinite(ppq)) ppq = 0;
    if (!(std::isfinite(bpm) && bpm > 1.0)) bpm = 120;
    if (playing && !seqHostPlaying_) {
        seqPlaying_ = false;           // host takes over; internal transport yields
        seqStep_ = -1;
        seqHostSynced_ = false;
        seqGateOff();
    }
    if (playing && seqHostSynced_ && std::fabs(ppq - seqHostEndPpq_) > 1e-4)
        seqHostSynced_ = false;        // loop / relocate -> resync from ppq
    if (!playing && seqHostPlaying_) { // host stopped
        seqStep_ = -1;
        seqGateOff();
    }
    seqHostPlaying_ = playing;
    seqHostPpq_ = ppq;
    seqHostBpm_ = bpm;
}

double Engine::seqHostStepPpq(long k) const {
    const double swing = std::min(1.0, std::max(0.0, (double)p_[SEQ_SWING]));
    return (double)k * 0.25 + ((k & 1) ? swing * SEQ_SWING_MAX * 0.25 : 0.0);
}

void Engine::seqHostResync() {
    long k = (long)std::floor(seqHostPpq_ / 0.25) - 1;
    if (k < 0) k = 0;
    while (seqHostStepPpq(k) < seqHostPpq_ - 1e-9) k++;
    seqHostNextK_ = k;
    seqHostSynced_ = true;
}

void Engine::seqFireHostStep(long k) {
    const int  s   = (int)(k % SEQ_STEPS);
    const long bar = k / SEQ_STEPS;
    seqChainPos_ = (int)(bar % (long)seqChain_.size());
    const int pat = seqChain_[(size_t)seqChainPos_];
    const int patNext = seqChain_[(size_t)((bar + 1) % (long)seqChain_.size())];
    const double dur = (60.0 / seqHostBpm_ / 4.0) * sr_;
    seqFireAt(s, pat, patNext, dur);
}

// Configure one oscillator's per-block render state. Returns true if audible.
// Reads the modulated snapshot pm for the per-param dests (pos/level/pan/detune/
// spread); pitch and the pan global offset stay as direct additive terms.
bool Engine::setupOsc(OscState& o, int base, Voice& v, const double* pm, double mPitch, double mPan, int n) {
    if (p_[slot(base + OSC_ON)] < 0.5) return false;
    int ti = (int)p_[slot(base + OSC_TABLE)];
    if (ti < 0 || ti >= (int)curTables_->size()) return false;
    const EngineTable& table = (*curTables_)[(size_t)ti];
    if (!table.data) return false; // empty slot

    double basePitch = v.pitch + bend_ + p_[slot(base + OSC_OCT)] * 12
                     + p_[slot(base + OSC_SEMI)] + p_[slot(base + OSC_FINE)] / 100.0
                     + mPitch * 12;
    double freq = 440 * std::pow(2.0, (basePitch - 69) / 12);
    if (!(freq > 0 && freq <= sr_ * 0.45)) return false; // inverted: NaN-safe

    double level = std::min(1.2, std::max(0.0, pm[base + OSC_LEVEL]));
    level *= level;
    if (level < 1e-5) return false;

    int uni = std::max(1, std::min(MAXUNI, (int)p_[slot(base + OSC_UNISON)]));
    // DETUNE is a linear 0..1 parameter. Unlike the UI-controlled base
    // value, its modulation snapshot can exceed that range when routes stack.
    // Keeping it bounded also keeps oscillator increments within the table
    // reader's expected range.
    double det = pm[base + OSC_DETUNE];
    if (!std::isfinite(det)) det = 0.0;
    else det = std::max(0.0, std::min(1.0, det));
    double spr = pm[base + OSC_SPREAD];
    double blend = std::min(1.0, std::max(0.0, pm[base + OSC_BLEND]));
    double basePan = std::max(-1.0, std::min(1.0, pm[base + OSC_PAN] + mPan));

    double pos = std::min(1.0, std::max(0.0, pm[base + OSC_POS]));
    if (o.posSm < 0) o.posSm = pos;
    o.posSm += (pos - o.posSm) * smoothCoef(n, POS_TAU * sr_);
    double posF = o.posSm * (table.frames - 1);
    int f0 = (int)posF;
    int f1 = std::min(table.frames - 1, f0 + 1);
    o.ft = posF - f0;

    double cps = freq / sr_;
    // |det|: the widest unison ratio is 2^(|det|*50/1200) regardless of sign
    // (mirrors the web engine's mip-headroom fix).
    double maxRatio = std::pow(2.0, (std::abs(det) * 50) / 1200);

    const double W = 0.07;
    double mipF = std::log2((cps * maxRatio * 1024) / 0.475);
    int mip = 0; double mipBlend = 0;
    if (mipF > 0) {
        mip = std::min(table.mips - 1, (int)std::ceil(mipF));
        double over = mipF - (mip - 1);
        if (over < W) mipBlend = 1 - over / W;
    }
    int fineMip = mip > 0 ? mip - 1 : 0;

    o.off0  = (f0 * table.mips + mip) * table.size;
    o.off1  = (f1 * table.mips + mip) * table.size;
    o.off0b = (f0 * table.mips + fineMip) * table.size;
    o.off1b = (f1 * table.mips + fineMip) * table.size;
    o.mipBlend = mipBlend;
    o.data = table.data;
    o.mask = table.mask;
    o.size = table.size;
    o.uni = uni;
    if (o.cacheUni != uni || !exactlyEqual(o.cacheDet, det) || !exactlyEqual(o.cacheSpr, spr)
        || !exactlyEqual(o.cacheBlend, blend) || !exactlyEqual(o.cachePan, basePan)) {
        double sumW2 = 0;
        for (int u = 0; u < uni; u++) {
            double sprd = uni > 1 ? (double)u / (uni - 1) * 2 - 1 : 0;
            double cents = sprd * det * 50;
            double ratio = std::pow(2.0, cents / 1200);
            o.ratios[u] = ratio;
            // BLEND: outer (most-detuned) voices fade relative to the center.
            // blend==1 -> weight==1 for all voices -> sumW2==uni -> gain identical
            // to the legacy /sqrt(uni) path (preset back-compat).
            double weight = 1 - (1 - blend) * std::fabs(sprd);
            sumW2 += weight * weight;
            double pan = std::max(-1.0, std::min(1.0, sprd * spr + basePan));
            double a = ((pan + 1) * PI) / 4;
            o.gl[u] = (float)(std::cos(a) * weight);
            o.gr[u] = (float)(std::sin(a) * weight);
        }
        o.sumW2 = sumW2;
        o.cacheUni = uni;
        o.cacheDet = det;
        o.cacheSpr = spr;
        o.cacheBlend = blend;
        o.cachePan = basePan;
    }
    for (int u = 0; u < uni; u++) o.incs[u] = cps * o.ratios[u] * table.size;
    // Guard sumW2==0 (uni=2, blend=0: both voices at |sprd|=1 -> weight 0).
    // Predicate matches the web's `sumW2 || 1` exactly (swaps to 1 only at 0),
    // so the two engines stay in lockstep at low blend.
    o.gain = (level * 0.32) / std::sqrt(o.sumW2 > 0.0 ? o.sumW2 : 1.0);
    return true;
}

// Finding 7: phase increments, morph fraction and pan/level gain products ramp
// from the previous chunk's targets to this chunk's across n samples, so
// block-rate modulation (LFO->pitch, glide, POS, pan, level) has no staircase.
// The ramp is suppressed on the first chunk after note-on, on a unison-count
// change, and (for the morph fraction) when the frame pair switched — ft is a
// fraction WITHIN a pair, so ramping it across a pair change would sweep the
// wrong way. Table reads are cubic Hermite (Finding 10).
void Engine::renderOsc(OscState& o, float* tmpL, float* tmpR, int n) {
    const float* data = o.data; int mask = o.mask, size = o.size;
    const double invN = 1.0 / n;
    const bool rp = o.havePrev && o.pUni == o.uni;
    const double ft1 = o.ft;
    const double ft0 = (rp && o.pOff0 == o.off0) ? o.pFt : ft1;
    const double dFt = (ft1 - ft0) * invN;
    const double g = o.gain;
    int off0 = o.off0, off1 = o.off1;
    double blend = o.mipBlend;
    for (int u = 0; u < o.uni; u++) {
        double ph = wrapOscPhase(o.phases[u], size);
        const double inc1 = o.incs[u];
        const double inc0 = rp ? o.pIncs[u] : inc1;
        const double dInc = (inc1 - inc0) * invN;
        const double gl1 = o.gl[u] * g, gr1 = o.gr[u] * g;
        const double gl0 = rp ? (double)o.pGl[u] : gl1, gr0 = rp ? (double)o.pGr[u] : gr1;
        const double dGl = (gl1 - gl0) * invN, dGr = (gr1 - gr0) * invN;
        if (blend < 0.001) {
            for (int i = 0; i < n; i++) {
                int idx = (int)ph & mask;
                double frac = ph - idx;
                int im1 = (idx - 1) & mask, i2 = (idx + 1) & mask, i3 = (idx + 2) & mask;
                double s0 = rdH(data, off0, im1, idx, i2, i3, frac);
                double s1 = rdH(data, off1, im1, idx, i2, i3, frac);
                double s = s0 + (ft0 + dFt * i) * (s1 - s0);
                tmpL[i] += (float)(s * (gl0 + dGl * i));
                tmpR[i] += (float)(s * (gr0 + dGr * i));
                ph = wrapOscPhase(ph + inc0 + dInc * i, size);
            }
        } else {
            const int off0b = o.off0b, off1b = o.off1b;
            for (int i = 0; i < n; i++) {
                int idx = (int)ph & mask;
                double frac = ph - idx;
                int im1 = (idx - 1) & mask, i2 = (idx + 1) & mask, i3 = (idx + 2) & mask;
                double ftN = ft0 + dFt * i;
                double sc0 = rdH(data, off0, im1, idx, i2, i3, frac);
                double sc1 = rdH(data, off1, im1, idx, i2, i3, frac);
                double sc = sc0 + ftN * (sc1 - sc0);
                double sf0 = rdH(data, off0b, im1, idx, i2, i3, frac);
                double sf1 = rdH(data, off1b, im1, idx, i2, i3, frac);
                double sf = sf0 + ftN * (sf1 - sf0);
                double s = sc + blend * (sf - sc);
                tmpL[i] += (float)(s * (gl0 + dGl * i));
                tmpR[i] += (float)(s * (gr0 + dGr * i));
                ph = wrapOscPhase(ph + inc0 + dInc * i, size);
            }
        }
        o.phases[u] = ph;
        o.pIncs[u] = inc1;
        o.pGl[u] = (float)gl1; o.pGr[u] = (float)gr1;
    }
    o.pFt = ft1; o.pOff0 = o.off0; o.pUni = o.uni;
    o.havePrev = true;
}

void Engine::setupFilter(FilterState& fs, int base, Voice& v, double e2, double mCut, const double* pm, int n) {
    int ftype = (int)p_[slot(base + FLT_TYPE)];
    fs.ftype = ftype;

    // The cutoff Log route is kept OUT of pm and passed as mCut here so the whole
    // exponent stays in a single std::pow — bit-identical to the legacy
    // p[CUTOFF] * 2^(env*4*e2 + key*(note-60)/12 + x*5). env/key are still read from
    // pm so THEY remain modulatable; the base cutoff is read straight from p_.
    double fc = p_[slot(base + FLT_CUTOFF)] *
        std::pow(2.0, pm[base + FLT_ENV] * 4 * e2 + (pm[base + FLT_KEY] * (v.note - 60)) / 12.0 + mCut * 5.0);
    fc = std::min(sr_ * 0.45, std::max(20.0, fc));
    if (fs.cutSm <= 0) fs.cutSm = fc;
    fs.cutSm += (fc - fs.cutSm) * smoothCoef(n, CUT_TAU * sr_);
    double cut = fs.cutSm;
    fs.cutTarget = cut;                // runFilter ramps cutPrev -> cutTarget
    double res = std::min(0.999, std::max(0.0, pm[base + FLT_RES]));

    if (ftype <= 4) {
        // SVF coefficients are recomputed per <=32-sample sub-block in
        // runFilter from the ramped cutoff (Finding 7); only k is fixed here.
        fs.twoPole = ftype == 1;
        fs.k1 = 2 - 1.93 * res;
    } else if (ftype == 5) {
        double len = sr_ / cut;
        len = std::min((double)COMB_MAX - 2, std::max(1.0, len));
        fs.combLen = len;
        fs.combFb = res * 0.97;
    } else {
        double norm = std::min(0.999, std::max(0.0, std::log(cut / 20) / std::log(1000.0)));
        double pos = norm * 4;
        int vi = std::min(3, (int)pos);
        double fr = pos - vi;
        double q = 2 + res * 22;
        for (int j = 0; j < 3; j++) {
            double f0 = std::min(sr_ * 0.45, VOWELS[vi][j] + (VOWELS[vi + 1][j] - VOWELS[vi][j]) * fr);
            double w0 = (2 * PI * f0) / sr_;
            double alpha = std::sin(w0) / (2 * q);
            double a0 = 1 + alpha;
            fs.fc[j * 3]     = alpha / a0;
            fs.fc[j * 3 + 1] = (-2 * std::cos(w0)) / a0;
            fs.fc[j * 3 + 2] = (1 - alpha) / a0;
            fs.famp[j] = F_AMPS[j];
        }
    }
}

void Engine::runFilter(FilterState& fs, const float* inL, const float* inR,
                       float* outL, float* outR, double drive, int n) {
    if (drive > 0.005) {
        double dg = 1 + drive * 7;
        double dcomp = 1 / std::pow(dg, 0.55);
        double kF = dcomp / dg;
        double xpL = fs.satXL, xpR = fs.satXR;
        double FpL = kF * lcosh(dg * xpL), FpR = kF * lcosh(dg * xpR);
        for (int i = 0; i < n; i++) {
            double aL = inL[i], aR = inR[i];
            double dxL = aL - xpL;
            double FL = kF * lcosh(dg * aL);
            outL[i] = (float)((dxL > 1e-5 || dxL < -1e-5) ? (FL - FpL) / dxL : dcomp * std::tanh(dg * 0.5 * (aL + xpL)));
            xpL = aL; FpL = FL;
            double dxR = aR - xpR;
            double FR = kF * lcosh(dg * aR);
            outR[i] = (float)((dxR > 1e-5 || dxR < -1e-5) ? (FR - FpR) / dxR : dcomp * std::tanh(dg * 0.5 * (aR + xpR)));
            xpR = aR; FpR = FR;
        }
        fs.satXL = xpL; fs.satXR = xpR;
    } else {
        for (int i = 0; i < n; i++) { outL[i] = inL[i]; outR[i] = inR[i]; }
        if (n > 0) { fs.satXL = inL[n - 1]; fs.satXR = inR[n - 1]; }
    }

    int ftype = fs.ftype;
    if (ftype <= 4) {
        // Finding 7: cutoff ramps from the previous chunk's value across the
        // chunk; SVF coefficients are recomputed per <=32-sample sub-block.
        const double k1 = fs.k1;
        const double c1c = fs.cutTarget;
        const double c0c = fs.cutPrev > 0 ? fs.cutPrev : c1c;
        double* F = fs.svf;
        for (int at = 0; at < n; at += 32) {
            const int m = std::min(32, n - at);
            const double cut = c0c + (c1c - c0c) * ((double)(at + m) / n);
            const double gC = std::tan((PI * cut) / sr_);
            const double a1 = 1 / (1 + gC * (gC + k1));
            const double a2 = gC * a1, a3 = gC * a2;
            auto runSvf = [&](float* buf, int o1, auto out) {
                double ic1 = F[o1], ic2 = F[o1 + 1];
                for (int i = at; i < at + m; i++) {
                    double x = buf[i];
                    double v3 = x - ic2;
                    double v1 = a1 * ic1 + a2 * v3;
                    double v2 = ic2 + a2 * ic1 + a3 * v3;
                    ic1 = 2 * v1 - ic1;
                    ic2 = 2 * v2 - ic2;
                    buf[i] = (float)out(x, v1, v2);
                }
                F[o1] = ic1; F[o1 + 1] = ic2;
            };
            auto outV2 = [](double, double, double v2) { return v2; };
            switch (ftype) {
                case 0:
                case 1:
                    for (int ch = 0; ch < 2; ch++) runSvf(ch == 0 ? outL : outR, ch * 2, outV2);
                    break;
                case 2:
                    for (int ch = 0; ch < 2; ch++)
                        runSvf(ch == 0 ? outL : outR, ch * 2, [&](double, double v1, double) { return k1 * v1; });
                    break;
                case 3:
                    for (int ch = 0; ch < 2; ch++)
                        runSvf(ch == 0 ? outL : outR, ch * 2, [&](double x, double v1, double v2) { return x - k1 * v1 - v2; });
                    break;
                default:
                    for (int ch = 0; ch < 2; ch++)
                        runSvf(ch == 0 ? outL : outR, ch * 2, [&](double x, double v1, double) { return x - k1 * v1; });
                    break;
            }
            if (fs.twoPole) {
                for (int ch = 0; ch < 2; ch++) runSvf(ch == 0 ? outL : outR, 4 + ch * 2, outV2);
            }
        }
        fs.cutPrev = c1c;
    } else if (ftype == 5) {
        // Comb delay length ramps across the chunk (Finding 7); the fractional
        // read below already supports a per-sample length.
        const double len1 = fs.combLen;
        const double len0 = fs.combLenPrev > 0 ? fs.combLenPrev : len1;
        const double dLen = (len1 - len0) / n;
        double fb = fs.combFb, g0 = 1 - fb;
        float* cl = fs.combL.data(); float* cr = fs.combR.data();
        int w = fs.combW;
        for (int i = 0; i < n; i++) {
            double len = len0 + dLen * (i + 1);
            double rd = w - len;
            // w in [0, COMB_MAX), len in [1, COMB_MAX-2] => rd in (-COMB_MAX, COMB_MAX).
            if (rd < 0) rd += COMB_MAX;
            int i0 = (int)rd;
            double frac = rd - i0;
            int i1 = i0 + 1 < COMB_MAX ? i0 + 1 : 0;
            double yL = g0 * outL[i] + fb * (cl[i0] + frac * (cl[i1] - cl[i0]));
            double yR = g0 * outR[i] + fb * (cr[i0] + frac * (cr[i1] - cr[i0]));
            cl[w] = (float)yL; cr[w] = (float)yR;
            outL[i] = (float)yL; outR[i] = (float)yR;
            w = w + 1 < COMB_MAX ? w + 1 : 0;
        }
        fs.combW = w;
        fs.combLenPrev = len1;
    } else {
        const double* fc = fs.fc; const double* fa = fs.famp; double* z = fs.fmt;
        for (int ch = 0; ch < 2; ch++) {
            float* buf = ch == 0 ? outL : outR;
            int zb = ch * 6;
            for (int i = 0; i < n; i++) {
                double x = buf[i];
                double acc = 0.04 * x;
                for (int j = 0; j < 3; j++) {
                    double b0 = fc[j * 3], ca1 = fc[j * 3 + 1], ca2 = fc[j * 3 + 2];
                    int zi = zb + j * 2;
                    double y = b0 * x + z[zi];
                    z[zi] = z[zi + 1] - ca1 * y;
                    z[zi + 1] = -b0 * x - ca2 * y;
                    acc += fa[j] * y;
                }
                buf[i] = (float)(acc * 0.8);
            }
        }
    }
}

void Engine::renderVoice(Voice& v, float* L, float* R, int n) {
    v.ampEnv.set(p_[ENV1_BASE + 0], p_[ENV1_BASE + 1], p_[ENV1_BASE + 2], p_[ENV1_BASE + 3], sr_);
    v.modEnv.set(p_[ENV2_BASE + 0], p_[ENV2_BASE + 1], p_[ENV2_BASE + 2], p_[ENV2_BASE + 3], sr_);

    double gl = p_[MASTER_GLIDE];
    if (gl > 0.001) {
        double c = 1 - std::exp(-(double)n / (gl * 0.3 * sr_ + 1));
        v.pitch += (v.note - v.pitch) * c;
    } else v.pitch = v.note;

    bool   rt1 = !exactlyZero(p_[(size_t)paramIndex(LFO1_BASE, LFO_RETRIG)]),
           rt2 = !exactlyZero(p_[(size_t)paramIndex(LFO2_BASE, LFO_RETRIG)]);
    double l1 = (rt1 ? v.lfo1 : gLfo1_).valueOff(
                    (int)p_[(size_t)paramIndex(LFO1_BASE, LFO_SHAPE)],
                    p_[(size_t)paramIndex(LFO1_BASE, LFO_PHASE)])
                * v.lfo1.riseGain(p_[(size_t)paramIndex(LFO1_BASE, LFO_RISE)], sr_);
    double l2 = (rt2 ? v.lfo2 : gLfo2_).valueOff(
                    (int)p_[(size_t)paramIndex(LFO2_BASE, LFO_SHAPE)],
                    p_[(size_t)paramIndex(LFO2_BASE, LFO_PHASE)])
                * v.lfo2.riseGain(p_[(size_t)paramIndex(LFO2_BASE, LFO_RISE)], sr_);
    double e2 = v.modEnv.level;
    double srcs[6] = {0, l1, l2, e2, v.vel, (v.note - 60) / 24.0};

    // Accumulate routes: globals (pitch/amp/pan) stay as direct offsets with their
    // existing math; per-param dests sum into modAccum[targetPid] and are folded
    // into the pm_ snapshot below via the curve rules.
    double mPitch = 0, mAmp = 0, mPan = 0;
    double* modAccum = modAccum_;
    std::fill(modAccum_, modAccum_ + NUM_PARAMS, 0.0);
    modAnyRoute_ = false;
    for (int s = 1; s <= MOD_MATRIX_SIZE; s++) {
        int b = matBase(s);
        int src = (int)p_[slot(b + MAT_SRC)];
        int dst = (int)p_[slot(b + MAT_DST)];
        if (!src || !dst) continue;
        double x = srcs[src] * p_[slot(b + MAT_AMT)];
        int target = dstTarget(dst);
        switch (target) {
            case DST_NONE:  break;
            case DST_PITCH: mPitch += x; break;
            case DST_AMP:   mAmp += x; break;
            case DST_PAN:   mPan += x; break;
            default:        modAccum[target] += x; modAnyRoute_ = true; break;
        }
    }

    // Build the per-voice modulated snapshot: copy p_ then apply each targeted
    // param's curve rule. Lin Float: pm = p + x*(hi-lo) (reproduces POS/LEVEL/RES
    // width-1 exactly). Log Float: pm = p * 2^(x*D), D=5 (reproduces CUTOFF). Int/
    // Enum/Bool are never destinations, so they pass through untouched.
    std::copy(p_.begin(), p_.end(), pm_);
    const auto& info = paramInfo();
    for (int P = 0; P < NUM_PARAMS; P++) {
        double x = modAccum[P];
        if (exactlyZero(x)) continue;
        // The filter cutoff routes are NOT folded into pm_: they are applied as the
        // single-exponent mCut term inside setupFilter so the result is IEEE-bit-
        // identical to the legacy single pow. All other Log/Lin dests fold here.
        if (P == paramIndex(FILTER1_BASE, FLT_CUTOFF)
            || P == paramIndex(FILTER2_BASE, FLT_CUTOFF)) continue;
        const ParamInfo& d = info[(size_t)P];
        if (d.curve == Curve::Log)      pm_[P] = (double)p_[(size_t)P] * std::pow(2.0, x * 5.0);
        else if (d.curve == Curve::Lin) pm_[P] = (double)p_[(size_t)P] + x * ((double)d.max - (double)d.min);
    }

    int route = (int)p_[FILTER_ROUTE];
    bool split = route == 2;

    std::fill(tmpL_, tmpL_ + n, 0.0f);
    std::fill(tmpR_, tmpR_ + n, 0.0f);
    if (split) { std::fill(bL_, bL_ + n, 0.0f); std::fill(bR_, bR_ + n, 0.0f); }

    bool aOn = setupOsc(v.oA, OSCA_BASE, v, pm_, mPitch, mPan, n);
    bool bOn = setupOsc(v.oB, OSCB_BASE, v, pm_, mPitch, mPan, n);
    if (aOn) renderOsc(v.oA, tmpL_, tmpR_, n); else v.oA.havePrev = false;
    if (bOn) renderOsc(v.oB, split ? bL_ : tmpL_, split ? bR_ : tmpR_, n); else v.oB.havePrev = false;

    // sub oscillator
    if (p_[SUB_ON] > 0.5) {
        double lvl = pm_[SUB_LEVEL] * pm_[SUB_LEVEL] * 0.3;
        if (lvl > 1e-6) {
            double sf = 440 * std::pow(2.0, (v.pitch + bend_ + p_[SUB_OCT] * 12 + mPitch * 12 - 69) / 12.0);
            double inc1 = sf / sr_;
            if (inc1 > 0 && inc1 < 0.45) {
                // Finding 7: ramp the sub increment across the chunk so glide /
                // pitch modulation is staircase-free on the sub too.
                double inc0 = (v.subIncPrev > 0 && v.subIncPrev < 0.45) ? v.subIncPrev : inc1;
                double dInc = (inc1 - inc0) / n;
                double ph = v.subPhase;
                bool square = (int)p_[SUB_SHAPE] == 1;
                for (int i = 0; i < n; i++) {
                    double inc = inc0 + dInc * i;
                    double s;
                    if (square) {
                        s = ph < 0.5 ? 1 : -1;
                        if (ph < inc) { double t = ph / inc; s += -(t * t) + 2 * t - 1; }
                        else if (ph > 1 - inc) { double t = (ph - 1) / inc; s += t * t + 2 * t + 1; }
                        double h = ph - 0.5;
                        if (h >= 0 && h < inc) { double t = h / inc; s -= -(t * t) + 2 * t - 1; }
                        else if (h < 0 && h > -inc) { double t = h / inc; s -= t * t + 2 * t + 1; }
                        s *= 0.7;
                    } else {
                        s = std::sin(2 * PI * ph);
                    }
                    float o = (float)(s * lvl);
                    tmpL_[i] += o; tmpR_[i] += o;
                    ph += inc; if (ph >= 1) ph -= 1;
                }
                v.subPhase = ph;
                v.subIncPrev = inc1;
            } else v.subIncPrev = -1;
        }
    }

    // noise
    if (p_[NOISE_ON] > 0.5) {
        double lvl = pm_[NOISE_LEVEL] * pm_[NOISE_LEVEL] * 0.35;
        if (lvl > 1e-6) {
            if ((int)p_[NOISE_TYPE] == 1) {
                double* b = v.pb;
                // Kellet pink filter with sr-mapped poles/gains (Finding 9;
                // identical to the fixed literals at the 48 kHz reference).
                for (int i = 0; i < n; i++) {
                    double w = rng_.next() * 2 - 1;
                    b[0] = pinkP_[0] * b[0] + w * pinkG_[0];
                    b[1] = pinkP_[1] * b[1] + w * pinkG_[1];
                    b[2] = pinkP_[2] * b[2] + w * pinkG_[2];
                    b[3] = pinkP_[3] * b[3] + w * pinkG_[3];
                    b[4] = pinkP_[4] * b[4] + w * pinkG_[4];
                    b[5] = -pinkP_[5] * b[5] - w * pinkG_[5];
                    double pink = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362) * 0.11;
                    b[6] = w * 0.115926;
                    float o = (float)(pink * lvl);
                    tmpL_[i] += o; tmpR_[i] += o;
                }
            } else {
                for (int i = 0; i < n; i++) {
                    float o = (float)((rng_.next() * 2 - 1) * lvl);
                    tmpL_[i] += o; tmpR_[i] += o;
                }
            }
        }
    }

    // ---- per-voice filters with routing ----
    bool f1on = p_[(size_t)paramIndex(FILTER1_BASE, FLT_ON)] > 0.5;
    bool f2on = p_[(size_t)paramIndex(FILTER2_BASE, FLT_ON)] > 0.5;
    if (f1on) setupFilter(v.f1, FILTER1_BASE, v, e2,
                          modAccum[(size_t)paramIndex(FILTER1_BASE, FLT_CUTOFF)], pm_, n);
    if (f2on) setupFilter(v.f2, FILTER2_BASE, v, e2,
                          modAccum[(size_t)paramIndex(FILTER2_BASE, FLT_CUTOFF)], pm_, n);

    double dr1 = pm_[(size_t)paramIndex(FILTER1_BASE, FLT_DRIVE)];
    double dr2 = pm_[(size_t)paramIndex(FILTER2_BASE, FLT_DRIVE)];
    float* oL; float* oR;

    if (split) {
        if (f1on) runFilter(v.f1, tmpL_, tmpR_, f1L_, f1R_, dr1, n);
        if (f2on) runFilter(v.f2, bL_, bR_, f2L_, f2R_, dr2, n);
        float* aL = f1on ? f1L_ : tmpL_; float* aR = f1on ? f1R_ : tmpR_;
        float* sL = f2on ? f2L_ : bL_;   float* sR = f2on ? f2R_ : bR_;
        for (int i = 0; i < n; i++) { f1L_[i] = aL[i] + sL[i]; f1R_[i] = aR[i] + sR[i]; }
        oL = f1L_; oR = f1R_;
    } else if (route == 1) {
        if (f1on) runFilter(v.f1, tmpL_, tmpR_, f1L_, f1R_, dr1, n);
        if (f2on) runFilter(v.f2, tmpL_, tmpR_, f2L_, f2R_, dr2, n);
        if (f1on && f2on) {
            for (int i = 0; i < n; i++) { f1L_[i] += f2L_[i]; f1R_[i] += f2R_[i]; }
            oL = f1L_; oR = f1R_;
        } else if (f1on) { oL = f1L_; oR = f1R_; }
        else if (f2on) { oL = f2L_; oR = f2R_; }
        else { oL = tmpL_; oR = tmpR_; }
    } else {
        float* cL = tmpL_; float* cR = tmpR_;
        if (f1on) { runFilter(v.f1, cL, cR, f1L_, f1R_, dr1, n); cL = f1L_; cR = f1R_; }
        if (f2on) { runFilter(v.f2, cL, cR, f2L_, f2R_, dr2, n); cL = f2L_; cR = f2R_; }
        oL = cL; oR = cR;
    }

    // AMP-mod factor ramps from the previous chunk's value (Finding 7); the DC
    // blocker pole is sr-derived (Finding 9).
    double ampFactor = std::min(2.0, std::max(0.0, 1 + mAmp));
    double af0 = v.ampFacPrev >= 0 ? v.ampFacPrev : ampFactor;
    double dAf = (ampFactor - af0) / n;
    for (int i = 0; i < n; i++) {
        double sl = oL[i], sr = oR[i];
        double yL = sl - v.dcxL + dcR_ * v.dcyL;
        double yR = sr - v.dcxR + dcR_ * v.dcyR;
        v.dcxL = sl; v.dcyL = yL;
        v.dcxR = sr; v.dcyR = yR;
        double amp = v.ampEnv.process() * v.velGain * (af0 + dAf * i);
        L[i] += (float)(yL * amp);
        R[i] += (float)(yR * amp);
    }
    v.ampFacPrev = ampFactor;

    v.lfo1.advance(lfoHz(LFO1_BASE), n, sr_);
    v.lfo2.advance(lfoHz(LFO2_BASE), n, sr_);
    v.modEnv.processBlock(n);
}

void Engine::renderBlock(float* L, float* R, int n, double ppqChunk) {
    std::fill(L, L + n, 0.0f);
    std::fill(R, R + n, 0.0f);

    // Update the global (free-run / transport-locked) LFOs before voices read them.
    updateGlobalLfo(gLfo1_, LFO1_BASE, ppqChunk, n);
    updateGlobalLfo(gLfo2_, LFO2_BASE, ppqChunk, n);

    int act = 0;
    bool vizSet = false;
    for (auto& v : voices_) {
        if (!v.active() && v.hasPending) {
            v.hasPending = false;
            v.noteOn(v.pendNote, v.pendVel, v.pendStart, clock_++, rng_);
        }
        if (!v.active()) continue;
        renderVoice(v, L, R, n);
        // Prefer held voices for the POS viz, like the worklet's `if (v.gate ||
        // !viz)`: release tails must not yank the indicator off the held note.
        // The live-mod snapshot rides the same selection so the knob dots and
        // the wavetable highlight always describe the same voice.
        if (v.gate || !vizSet) { vizA = v.oA.posSm; vizB = v.oB.posSm; vizSet = true; snapshotVizMod(); }
        act++;
    }
    vizActive = act;
    if (act == 0) { vizA = -1; vizB = -1; vizModAny = false; } // idle -> hide indicators
}

// Copy the just-rendered voice's per-destination route sums (modAccum_, still
// valid right after its renderVoice) into the vizMod snapshot the processor
// publishes. Globals (PITCH/AMP/PAN, negative sentinels) have no owning knob.
void Engine::snapshotVizMod() {
    for (int d = 1; d < NUM_MOD_DESTS; d++) {
        const int t = dstTarget(d);
        vizMod[d] = t >= 0 ? modAccum_[t] : 0.0;
    }
    vizModAny = modAnyRoute_;
}

void Engine::render(float* L, float* R, int n) {
    // Snapshot the published table set once for the whole call (Finding 2):
    // the shared_ptr keeps every raw pointer setupOsc caches valid even if the
    // message thread publishes a new set mid-block. Wait-free — the audio
    // thread never blocks on a UI table swap and never substitutes silence.
    const std::shared_ptr<const TableSet> snap = std::atomic_load(&tables_);
    curTables_ = snap.get();
    // Sequencer clocking. The host-locked path derives absolute 16ths from the
    // playhead ppq (sample-accurate splits at each due step); the internal path
    // counts real samples so the clock never drifts (worklet parity). Chunks
    // are additionally cut at the pending gate-off so it lands on its sample.
    // Hosted clip mode owns the transport exclusively: it suppresses both the
    // host-transport-locked and internal-clock seq firing below so the
    // standalone sequencer and the hosted clip can never double-fire.
    const bool hostRun = seqHostPlaying_ && !hostClipMode_;
    double ppqPerSample = 0, samplesPerPpq = 0;
    if (hostRun) {
        ppqPerSample = seqHostBpm_ / 60.0 / sr_;
        samplesPerPpq = 1.0 / ppqPerSample;
        if (!seqHostSynced_) seqHostResync();
    }
    const double beatsPerSample = (bpm_ / 60.0) / sr_;
    // Virtual transport: while the internal sequencer plays without a host
    // transport, synced LFOs phase-lock to the sequencer clock — the web
    // build's transportBeats follows seq.bpm the same way.
    const double seqBeatsPerSample = (seqEffectiveBpm() / 60.0) / sr_;
    const bool internalRun = seqPlaying_ && !hostRun && !hostClipMode_;
    const bool hostPlayingFlag = playing_;
    // Synced LFOs treat the seq as transport; hosted (SQ-4) the conductor's
    // shared anchor is the transport, so they phase-lock there too.
    if (internalRun || hostClipMode_) playing_ = true;

    int off = 0;
    while (off < n) {
        int run = std::min(128, n - off);
        if (hostRun) {
            // Fire every step due at/before off; split the run at the next one.
            for (;;) {
                const long fireAt = (long)std::ceil(
                    (seqHostStepPpq(seqHostNextK_) - seqHostPpq_) * samplesPerPpq - 1e-9);
                if (fireAt <= off) { seqFireHostStep(seqHostNextK_++); continue; }
                if (fireAt - off < run) run = (int)(fireAt - off);
                break;
            }
        } else if (internalRun) {
            if (seqToNext_ <= 0) seqFire();
            run = std::min(run, std::max(1, (int)std::ceil(seqToNext_)));
        } else if (hostClipMode_) {
            // At most one fire per quantum (ClipHost contract); a Stop event
            // means the clip transport just gated the seq voice off (docs
            // §6 rule 3 — sequencer notes only, never panic(): live MIDI
            // notes must survive). onSwap ends the OUTGOING clip's sounding
            // note before the new clip's entry-step fire, in the same block
            // (docs §6 rule 4: old gate-off before new trigger).
            const size_t evBefore = clipHost_.events.size();
            clipHost_.tick(
                hostFrame_, run,
                [&](int abs) { clipFireAt(abs); },
                [&](bool wasPlaying) { if (wasPlaying) seqGateOff(); });
            for (size_t i = evBefore; i < clipHost_.events.size(); i++)
                if (clipHost_.events[i].t == HostEvent::T::Stop) seqGateOff();
        }
        // Split the run at the next pending note-off so each off lands on its
        // exact sample (worklet seqOffQueue countdown; native is sample-accurate).
        const double earliestOff = seqEarliestOff();
        if (earliestOff >= 0)
            run = std::min(run, std::max(1, (int)std::ceil(earliestOff)));

        // Hosted, hostFrame_ is this chunk's absolute start frame (advanced
        // per chunk below), and the anchor is beat zero — the same timebase
        // the web worklet derives its hosted ppq from.
        const double chunkPpq = hostRun    ? seqHostPpq_ + off * ppqPerSample
                              : internalRun ? seqSongPos_ * seqBeatsPerSample
                              : hostClipMode_ ? std::max(0.0, hostFrame_ - hostAnchor_) * beatsPerSample
                                            : ppq_ + off * beatsPerSample;
        renderBlock(L + off, R + off, run, chunkPpq);

        if (internalRun) {
            seqToNext_ -= run;
            seqSongPos_ += run;
        }
        if (hostClipMode_) hostFrame_ += run;
        // Drain the per-note off queue: decrement every pending off, fire
        // noteOff for those due, compact the survivors (no alloc).
        if (seqOffCount_ > 0) {
            int w = 0;
            for (int i = 0; i < seqOffCount_; ++i) {
                seqOff_[i].remaining -= run;
                if (seqOff_[i].remaining <= 0.0) noteOff(seqOff_[i].note);
                else seqOff_[w++] = seqOff_[i];
            }
            seqOffCount_ = w;
            seqLastNote_ = w > 0 ? seqOff_[w - 1].note : -1;
        }
        off += run;
    }
    playing_ = hostPlayingFlag;
    if (hostRun) seqHostEndPpq_ = seqHostPpq_ + n * ppqPerSample;
}

} // namespace fable
