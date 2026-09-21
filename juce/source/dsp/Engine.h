// FableSynth DSP core — C++ port of src/engine/worklet.js.
// 8-voice polyphonic wavetable engine: 2 morphing oscillators (unison), sub +
// noise, dual per-voice filter (SVF / comb / vowel) with ADAA drive, 2 ADSR
// envelopes, 2 LFOs, a 16-slot mod matrix, glide and voice stealing.
//
// JUCE-independent on purpose: the engine is a plain C++ object so it can be
// driven by the plugin AND exercised by a headless test harness.
#pragma once

#include "ClipHost.h"
#include "NoteSeq.h"
#include "Params.h"
#include "Wavetables.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace fable {

constexpr int NVOICES  = 8;
constexpr int MAXUNI   = 16;
// The comb delay is sized in prepare() from the active sample rate.  This is
// merely the small safe minimum for an unprepared engine; at 48 kHz the
// prepared capacity remains 2402 samples (20 Hz + fractional-read headroom).
constexpr int COMB_MIN_CAPACITY = 3;

// Fast deterministic RNG (xorshift32) — replaces Math.random() for noise,
// start-phase randomisation and S&H. Deterministic => reproducible tests.
struct Rng {
    uint32_t s = 0x9e3779b9u;
    inline float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s >> 8) * (1.0f / 16777216.0f); }
};

class Env {
public:
    int    state = 0;
    double level = 0, s = 0.8;
    double ca = 0.01, cd = 0.001, cr = 0.001;
    // Finding J6: the voice-steal fade was a fixed 0.12 per sample, i.e. a
    // 1.5 ms fade at 48 kHz that halved at 96 kHz. Derived from the time
    // constant below it is sample-rate invariant (and 0.12007 at 48 kHz, so
    // the 48 kHz behaviour is unchanged).
    double cs = 0.12;
    void   set(double a, double d, double sus, double r, double sr);
    void   trigger() { state = 1; }
    void   release() { if (state != 0) state = 4; }
    void   kill()    { state = 0; level = 0; }
    double process();
    // Block advance for block-rate consumers. Idle (0) and sustain (3) are
    // fixed points of process(), so they skip the loop — exact, not approximate.
    double processBlock(int n) {
        if (state == 0) return level;
        if (state == 3) { level = s; return level; }
        for (int i = 0; i < n; i++) process();
        return level;
    }
private:
    double a_ = -1, d_ = -1, r_ = -1, sr_ = -1; // last-set values (coef cache key)
};

class Lfo {
public:
    double phase = 0, hold = 0;
    long   elapsed = 0;                 // samples since reset (for rise/fade-in)
    Rng*   rng = nullptr;
    void   reset() { phase = 0; hold = rng->next() * 2 - 1; elapsed = 0; }
    double valueOff(int shape, double off) const;       // reads frac(phase + off)
    double riseGain(double riseSec, double sr) const {
        return riseSec <= 0 ? 1.0 : std::min(1.0, (double)elapsed / (riseSec * sr));
    }
    void   advance(double rate, int n, double sr);
};

// Everything one filter run needs EXCEPT the comb delay lines. Split out for
// finding J2: a discrete switch (filter type, route, on/off) freezes a copy of
// this and renders the old configuration alongside the new one for one short
// crossfade, and the delay lines are far too large to copy on the audio thread.
struct FilterCore {
    int    ftype = 0; bool twoPole = false;
    double svf[8]  = {0};   // 2 stages x 2 ch x (ic1, ic2)
    double fmt[12] = {0};   // formant: 2 ch x 3 bands x (s1, s2)
    double satXL = 0, satXR = 0;       // ADAA drive: previous input per channel
    double drive = 0;                  // ADAA drive amount for this run
    double driveMix = 0, driveMixTarget = 0; // dry <-> ADAA transition state
    double cutSm = 0;
    // k1 damps SVF stage 1, k2 stage 2. They differ only for LP24, where the
    // resonance now lives in ONE stage (finding B3); every other type sets
    // k2 == k1 and uses stage 1 alone.
    double k1 = 0, k2 = 0;
    double cutTarget = 0, cutPrev = -1;    // chunk cutoff ramp (Finding 7)
    double combLen = 1, combFb = 0, combLenPrev = -1;
    int    combW = 0;                  // write index into FilterState's lines
    double fc[9]  = {0};               // formant biquad coefs: 3 bands x (b0,a1,a2)
    double famp[3] = {0};
    void   reset();
};

struct FilterState {
    FilterCore c;                      // live configuration
    FilterCore old;                    // frozen pre-switch copy (J2 crossfade)
    // Allocated only by Engine::prepare(), never from render(). A fixed 4096
    // sample line silently shortened a 20 Hz comb at high sample rates.
    std::vector<float> combL, combR;
    void   prepare(int combCapacity);
    void   reset();
};

struct EngineTable;

struct OscState {
    double phases[MAXUNI] = {0};
    double incs[MAXUNI]   = {0};
    double ratios[MAXUNI] = {0};
    float  gl[MAXUNI]     = {0};
    float  gr[MAXUNI]     = {0};
    int    uni = 1;
    int    off0 = 0, off1 = 0, off0b = 0, off1b = 0;
    double mipBlend = 0, ft = 0, gain = 0;
    double sumW2 = 0;
    int    cacheUni = -1;
    double cacheDet = 0, cacheSpr = 0, cacheBlend = 0, cachePan = 0;
    int    mask = 0, size = 0;
    const float* data = nullptr;
    // Pins the table between render calls, including the frozen fade side.
    // Retired sets retain the final owner so destruction stays off audio.
    std::shared_ptr<const EngineTable> tableOwner;
    double posSm = -1;
    // Which table slot / on-state this state was last configured for. A change
    // in either freezes a copy of the whole OscState and crossfades it out
    // against the new one (finding J2), so table switches do not click.
    int    tableIdx = -1;
    bool   wasOn = false;
    // Previous chunk's targets for the intra-chunk ramps (Finding 7): phase
    // increments, morph fraction and pan/level gain products are interpolated
    // from these to the current values across each render chunk.
    double pIncs[MAXUNI] = {0};
    float  pGl[MAXUNI] = {0};
    float  pGr[MAXUNI] = {0};
    double pFt = 0;
    int    pOff0 = -1, pUni = -1;
    bool   havePrev = false;
};

class Voice {
public:
    int    note = 60; double vel = 1; bool gate = false; long age = 0;
    double pitch = 60, velGain = 0;
    // Note queued behind a steal fade (Env state 5); fired by renderBlock once
    // the fading voice reaches silence. Mirrors the worklet's voice.pending.
    bool   hasPending = false;
    int    pendNote = 60; double pendVel = 1, pendStart = 60;
    Env    ampEnv, modEnv;
    Lfo    lfo1, lfo2;
    OscState oA, oB;
    double subPhase = 0, subIncPrev = -1;
    double pb[7] = {0};                // pink noise filter state
    FilterState f1, f2;
    double dcxL = 0, dcxR = 0, dcyL = 0, dcyR = 0;
    double ampFacPrev = -1;            // AMP-mod factor ramp start (Finding 7)

    // ---- discrete-switch crossfades (finding J2) ----
    // Filter section: the topology actually rendered last chunk, the frozen
    // pre-switch topology, and the samples left in the fade. While fading, the
    // whole section runs twice (old cores + old topology, new cores + new
    // topology) and the two are equal-power mixed.
    bool   fHave = false;
    int    fRoute = -1;  bool fOn1 = false, fOn2 = false;   // last rendered
    int    oRoute = -1;  bool oOn1 = false, oOn2 = false;   // frozen
    int    fXfRemain = 0, fXfLen = 0;
    // Oscillators: a frozen copy keeps playing the old table for one fade.
    OscState oldA, oldB;
    int    aXfRemain = 0, aXfLen = 0, bXfRemain = 0, bXfLen = 0;
    // Sub oscillator shape (sine <-> square).
    int    subShapePrev = -1, subShapeOld = -1, subXfRemain = 0, subXfLen = 0;

    bool   active() const { return ampEnv.state != 0; }
    void   noteOn(int n, double v, double startPitch, long a, Rng& rng);
    void   noteOff() { gate = false; ampEnv.release(); modEnv.release(); }
    void   kill()    { gate = false; hasPending = false; ampEnv.kill(); modEnv.kill(); }
};

// Engine table view — mirrors the worklet's {frames,mips,size,mask,data}.
// Shares the source table's sample data (src keeps it alive): swapping the
// pool moves pointers, never the multi-MB float pyramids.
struct EngineTable {
    int frames = 0, mips = 0, size = 0, mask = 0;
    const float* data = nullptr; // = src->data.data(); nullptr marks an empty slot
    TablePtr src;
};

class Engine {
public:
    void prepare(double sampleRate);
    void setTables(std::vector<TablePtr> tables);
    // Free every retired table set the audio thread has provably finished with.
    // Message thread only; setTables() calls it, and a host may call it from a
    // timer so retired sets do not linger while the UI is idle (finding J3).
    void collectRetiredTables();
    size_t retiredTableSetCount() const { return retired_.size(); }
    // Direct (snapped) parameter access — preset loads, state restore and the
    // offline harness. Both arrays move together so the smoothers below have
    // nothing to chase.
    void setParam(int id, float v) { p_[(size_t)id] = ps_[(size_t)id] = pt_[(size_t)id] = rampTarget_[(size_t)id] = v; }
    void setParams(const ParamArray& p) { p_ = ps_ = pt_ = rampTarget_ = p; }
    // NOTE: an engine driven by paramTargets() must not also be written through
    // params() — the ramp would pull the direct write back to the last target.
    // Nothing does today: the plugin uses paramTargets() exclusively and SQ-4
    // (which loads whole patches) uses params() exclusively.
    ParamArray& params() { return p_; }

    // ---- host-automation smoothing (finding J1) ----
    // The processor writes the APVTS snapshot HERE once per host block; the
    // engine then pulls every continuous parameter toward its target once per
    // <=128-sample render chunk, so an automated cutoff no longer steps once
    // per host block. Touching this accessor switches the engine into the
    // smoothed path (params()/setParams() stay snapped, so no test or preset
    // load ramps). Discrete parameters (Int/Enum/Bool and the sequencer clock)
    // always snap; every continuous one ramps to its target across the render
    // call — linearly for gains/pan/pos, geometrically (constant octaves per
    // second) for cutoff/rate/time — capped at PARAM_RAMP_MAX_SEC so an
    // oversized offline block does not stretch the move across the whole
    // buffer. Interpolating across the block IS what a block-rate value means,
    // so at any normal block size the parameter is continuous per sample and
    // leaves no line at the block rate.
    ParamArray& paramTargets() { smoothParams_ = true; return pt_; }
    static constexpr double PARAM_RAMP_MAX_SEC = 0.050;
    // Narrow native-test hooks for the prepare-time comb allocation and the
    // target-ramp clock. Neither exposes mutable DSP state.
    int combCapacity() const { return combCapacity_; }
    float smoothedParam(int id) const { return p_[(size_t)id]; }

    void noteOn(int note, double vel);
    void noteOff(int note);
    void pitchBend(double semis) { bend_ = semis; }
    // Clamp to a sane musical range; non-finite or <=1 falls back to 120. The
    // upper bound keeps a degenerate host tempo from making the per-block LFO
    // phase step exceed a cycle.
    void setBpm(double b) { bpm_ = (std::isfinite(b) && b > 1.0) ? std::min(b, 1000.0) : 120.0; }
    // Host transport for synced-LFO phase locking: ppq = quarter notes since the
    // song origin, playing = host transport running. When playing, a synced
    // free-run LFO derives its phase from ppq so it lines up with the downbeat.
    // ppq is sanitised so a non-finite host position can't latch a NaN phase.
    void setTransport(double ppq, bool playing) { ppq_ = std::isfinite(ppq) ? ppq : 0.0; playing_ = playing; }
    void panic() { for (auto& v : voices_) v.kill(); seqOffCount_ = 0; seqLastNote_ = -1; }

    // ---- note sequencer (port of worklet.js seqRead/seqGateOff/seqFire).
    // 16 steps x 4 chained patterns firing noteOn/noteOff into the polyphonic
    // voice allocator. Each on-step gates for its own `duration` 16th-steps via
    // a per-note off queue (seqScheduleOff), so overlapping notes ring
    // concurrently — matching the web worklet's poly seqOffQueue; accents fire
    // velocity SEQ_ACCENT_VEL vs SEQ_PLAIN_VEL so VELO mod routes respond.
    void seqPlay();                                 // worklet 'play' (yields to a rolling host)
    void setArp(const ArpPattern&);
    void seqStop();                                 // worklet 'stop'
    bool seqIsPlaying() const { return seqPlaying_ || seqHostPlaying_; }
    void setSeqPatterns(const uint8_t* data, int n); // n must be SEQ_PATTERN_BYTES; copies
    void setSeqChain(const int* list, int n);        // ignores empty; clamps entries + chainPos
    void setBpmOverride(double bpm);                 // host tempo wins over SEQ_BPM; <= 0 clears
    int  seqCurrentStep() const { return seqStep_; } // -1 when stopped
    int  seqCurrentPattern() const { return seqChain_[(size_t)seqChainPos_]; }
    // The most recent MIDI note the sequencer (standalone or hosted-clip) fired
    // while any seq note is still sounding, -1 when none — backed by the
    // per-note off queue, so this observes hosted-clip mode too (test/UI).
    int  seqCurrentNote() const { return seqOffCount_ > 0 ? seqLastNote_ : -1; }
    // Number of seq-fired notes still sounding (pending their per-note off).
    int  seqPendingOffCount() const { return seqOffCount_; }

    // Host transport lock (same contract as BassEngine::setHostTransport):
    // while the host is rolling with a song position, absolute 16th k fires at
    //   p(k) = k*0.25 + (k odd ? swing*SEQ_SWING_MAX*0.25 : 0) ppq,
    // the pattern is chain[(k/16) % chain.size()], and k < 0 never fires.
    void setSeqHostTransport(double ppq, double bpm, bool playing);

    // ---- SQ-4 hosted-clip mode (docs/sq4-clips.md §6) ----
    // While on, seqPlay()/seqStop() and host-transport seq firing are
    // suppressed (guarded by hostClipMode_) so the standalone sequencer and
    // the hosted clip can never double-fire the same voice slot; the
    // standalone render path is otherwise byte-identical when this is off.
    void setHostClipMode(bool on, int maxBlock = 0) {
        hostClipMode_ = on;
        // Reserve the clip host's buffers so no launch/update/tick allocates on
        // the audio thread (8192 covers SQ_MAX_BARS of the 8-note WT-1 clip
        // format; the event headroom is sized to maxBlock — see hostMaxEvents).
        if (on) clipHost_.prepare(SQ_MAX_BARS * 512, hostMaxEvents(maxBlock));
        else clipHost_.clear();
    }
    void hostTempo(double bpm, double swing, double anchorFrame) {
        setBpm(bpm);
        hostAnchor_ = anchorFrame; // beat zero of the shared timebase (synced-LFO phase)
        clipHost_.setTempo(bpm_, swing, sr_, anchorFrame);
    }
    void hostClip(const uint8_t* data, int bytes, int bars, double atFrame, int tag = 0, ArpPattern arp = {}) {
        clipHost_.scheduleClip(data, (size_t)bytes, bars, atFrame, tag, arp);
    }
    void hostClipStop(double atFrame) { clipHost_.scheduleStop(atFrame); }
    void hostClipUpdate(const uint8_t* data, int bytes, int bars, ArpPattern arp = {}) {
        if (!clipHost_.hasPending() && (clipHost_.arp().enabled != arp.enabled || (arp.enabled && !arpHasNotes(arp)))) seqGateOff();
        clipHost_.updateClip(data, (size_t)bytes, bars, arp);
    }
    void hostSetFrame(double blockStartFrame) { hostFrame_ = blockStartFrame; } // SQ-4 processor calls before render() each block
    // Lossless drain: copy up to `max` events, then erase ONLY the copied
    // prefix, keeping the remainder for the next call (Finding 3). The old
    // clear()-everything dropped Pos events past `max` when one prepared block
    // spanned more grid steps than the caller's buffer; the SeqProcessor now
    // loops takeHostEvents until it returns 0 so a burst can't leak. erase()
    // shrinks in place (no realloc), so this stays audio-thread-safe.
    int  takeHostEvents(HostEvent* out, int max) {
        if (max <= 0 || out == nullptr) return 0;
        int n = std::min((int)clipHost_.events.size(), max);
        std::copy(clipHost_.events.begin(), clipHost_.events.begin() + n, out);
        clipHost_.events.erase(clipHost_.events.begin(), clipHost_.events.begin() + n);
        return n;
    }
    // Test hook: the clip host's reserved event capacity, to assert the
    // worst-case sizing holds with no audio-thread realloc (Finding 3).
    size_t hostEventsCapacity() const { return clipHost_.eventsCapacity(); }

    // Worst-case host-event count for one prepared render block (Finding 3).
    // A single prepared chunk can span many grid steps in an offline render;
    // each step emits one Pos event, plus Start/Stop/entry headroom. The
    // shortest possible step is at the max bpm (200), so
    // ceil(maxBlock / minStepDur) + 8 bounds it. Diverges from the web's fixed
    // 64 deliberately: JUCE hands offline renders arbitrarily large blocks, and
    // a reserve() overflow here would realloc on the audio thread. maxBlock<=0
    // (the standalone/unit callers) keeps the old fixed 64.
    int hostMaxEvents(int maxBlock) const {
        if (maxBlock <= 0) return 64;
        const double minStepDur = sr_ * 60.0 / 200.0 / 8.0; // fastest arp: 1/32
        const int n = (int)std::ceil((double)maxBlock / minStepDur) + 8;
        return std::max(64, n);
    }

    // One unpacked engine step: on/acc/tie + semitone offset from SEQ_ROOT
    // (worklet seqRead). Static so the harness asserts the unpack directly.
    struct SeqReadStep { bool on = false, acc = false; int semi = 0, duration = 1; };
    static SeqReadStep readSeqStep(const uint8_t* pats, int pat, int s);

    // Render the summed (pre-FX) voice mix into L/R. Chunks internally to the
    // 128-sample block cadence so block-rate modulation matches the web engine
    // regardless of the host buffer size.
    void render(float* L, float* R, int n);

    // Live visualization feedback (modulated wt positions + active voice count).
    double vizA = -1, vizB = -1; int vizActive = 0;

    // Live per-destination modulation feedback for the editor's knob dots
    // (web {t:'mod'} telemetry parity). vizMod[dst] = the SAME voice vizA/vizB
    // describe: its summed route value x = Σ src·amt for MOD_DESTS index dst —
    // the raw value the curve rules fold into that param. vizModAny is true
    // only while that voice carries at least one per-param route; false when
    // idle so the UI hides its indicators instead of freezing them.
    double vizMod[NUM_MOD_DESTS] = {0};
    bool   vizModAny = false;

private:
    void beginParamRamp();             // once per target change, sample-clocked
    void smoothParams(int n);          // finding J1, per render chunk
    // Crossfade length for a discrete switch (finding J2): 3 ms, sample-rate
    // derived, matching the DR-1 engine's filter-type fade.
    int  xfSamples() const { return std::max(8, (int)(0.003 * sr_)); }
    bool setupOsc(OscState& o, int base, Voice& v, const double* pm, double mPitch, double mPan, int n);
    void renderOsc(OscState& o, float* tmpL, float* tmpR, int n);
    void setupFilter(FilterCore& fc, int base, Voice& v, double e2, double mCut, const double* pm, int n);
    void runFilter(FilterState& fs, FilterCore& c, bool writeComb,
                   const float* inL, const float* inR, float* outL, float* outR, int n);
    // One pass of the voice's filter section (route + on/off topology) using
    // the supplied cores. Writes into the two scratch pairs and hands back the
    // pair holding the result. Never writes tmpL_/tmpR_/bL_/bR_, so the frozen
    // pre-switch pass can read the same oscillator mix.
    void runFilterSection(Voice& v, FilterCore& c1, FilterCore& c2,
                          int route, bool f1on, bool f2on, bool writeComb,
                          float* s1L, float* s1R, float* s2L, float* s2R,
                          float*& oL, float*& oR, int n);
    // LFO value with a shape-switch crossfade folded in (finding J2).
    double lfoShapeValue(int idx, const Lfo& l, int base) const;
    void renderVoice(Voice& v, float* L, float* R, int n);
    void renderBlock(float* L, float* R, int n, double ppqChunk); // n <= 128
    void snapshotVizMod();             // copy the just-rendered voice's route sums
    double lfoHz(int base) const;
    void updateGlobalLfo(Lfo& g, int base, double ppqChunk, int n);

    // ---- note sequencer internals ----
    void seqGateOff();                       // worklet seqGateOff (gate off all)
    void arpFire(const ArpPattern&, int step, double interval);
    void seqScheduleOff(int note, double remaining); // per-note off queue (worklet seqScheduleOff)
    double seqEarliestOff() const;           // smallest pending-off remaining (-1 = none)
    void seqFire();                          // worklet seqFire (internal clock)
    void seqFireAt(int s, int pat, int /*patNext*/, double dur); // shared step-fire body
    void clipFireAt(int abs); // hosted twin of seqFireAt; byte source is clipHost_'s clip
    double seqEffectiveBpm() const;
    // host-lock helpers (BassEngine scheme)
    double seqHostStepPpq(long k) const;
    void   seqHostResync();
    void   seqFireHostStep(long k);

    ParamArray p_ = defaultParams();   // smoothed values the DSP reads
    ParamArray pt_ = defaultParams();  // automation targets (finding J1)
    ParamArray ps_ = defaultParams();  // ramp start: p_ as of this render() entry
    ParamArray rampTarget_ = defaultParams(); // target that armed the current ramp
    int rampLen_ = 0, rampPos_ = 0;    // automation ramp, in samples
    bool smoothParams_ = false;        // set by paramTargets()

    // Lock-free table publication (findings 2 + J3). The message thread builds
    // a complete immutable set and publishes a RAW pointer to it; the audio
    // thread loads that pointer once per render() and uses it for the whole
    // block. The previous free-function std::atomic_load on a shared_ptr was a
    // hashed spinlock in both libstdc++ and libc++, so the audio thread could
    // block behind a UI table swap, and the last reference could be dropped on
    // audio (a free in the render callback). Now the message thread owns every
    // set: a replaced set moves to retired_ tagged with the render epoch, and
    // collectRetiredTables() first waits for a LATER render or an idle audio
    // thread, then checks that no oscillator pins any entry. Normal shared_ptr
    // copies pin entries across blocks/fades; no atomic shared_ptr load or
    // final destruction happens on audio. Renders are strictly sequential.
    using TableSet = std::vector<std::shared_ptr<const EngineTable>>;
    std::unique_ptr<const TableSet> live_ = std::make_unique<const TableSet>();
    std::atomic<const TableSet*> tablesPub_{live_.get()};
    std::atomic<uint64_t> renderEpoch_{0};
    std::atomic<bool> rendering_{false};   // a render call is in flight
    struct RetiredSet { std::unique_ptr<const TableSet> set; uint64_t epoch = 0; };
    std::vector<RetiredSet> retired_;      // message thread only
    const TableSet* curTables_ = nullptr;  // render-call snapshot (audio thread only)

    // LFO shape-switch crossfade state (finding J2), one entry per global LFO.
    int lfoShapePrev_[2] = {-1, -1};
    int lfoXfRemain_[2] = {0, 0}, lfoXfLen_[2] = {0, 0};
    std::array<Voice, NVOICES> voices_;
    double sr_ = 48000;
    int    combCapacity_ = COMB_MIN_CAPACITY;
    // Sample-rate-derived per-sample coefficients (Finding 9), set in prepare():
    // DC-blocker pole and Kellet pink-noise poles/gains mapped from their
    // 48 kHz reference so 44.1/48/96/192 kHz produce the same spectra.
    double dcR_ = 0.9998;
    double pinkP_[6] = {0.99886, 0.99332, 0.969, 0.8665, 0.55, 0.7616};
    double pinkG_[6] = {0.0555179, 0.0750759, 0.153852, 0.3104856, 0.5329522, 0.016898};
    double bend_ = 0;
    double lastPitch_ = 60;
    long   clock_ = 0;
    Rng    rng_;
    Lfo    gLfo1_, gLfo2_;             // free-running (retrig=0) global LFO phases
    double bpm_ = 120;
    double ppq_ = 0;                  // host transport position (quarter notes)
    bool   playing_ = false;          // host transport running

    // ---- note sequencer state (worklet fields) ----
    std::vector<uint8_t> seqPats_ = makeEmptySeqPatterns();
    ArpPattern arp_;
    std::vector<int> seqChain_ { 0 };
    int    seqChainPos_ = 0;
    bool   seqPlaying_ = false;       // internal clock running
    int    seqStep_ = -1;
    double seqToNext_ = 0;            // samples until the next step fires
    // Per-note pending off queue (worklet seqOffQueue): {note, remaining} for
    // each seq-fired note not yet gated off. Fixed-cap, no audio-thread alloc.
    static constexpr int kSeqOffCap = 32;
    struct SeqOff { int note = -1; double remaining = 0.0; };
    SeqOff seqOff_[kSeqOffCap];
    int    seqOffCount_ = 0;
    int    seqLastNote_ = -1;        // most recent fired note (for seqCurrentNote/viz)
    double seqSongPos_ = 0;           // samples since play (virtual transport for synced LFOs)
    double bpmOverride_ = 0;          // > 0: host tempo wins over SEQ_BPM

    // ---- hosted-clip mode state (SQ-4) ----
    bool     hostClipMode_ = false;
    double   hostFrame_ = 0;
    double   hostAnchor_ = 0;         // shared-timebase beat zero (hostTempo)
    ClipHost clipHost_;

    // host transport lock state (BassEngine scheme)
    bool   seqHostPlaying_ = false;
    bool   seqHostSynced_  = false;
    double seqHostPpq_ = 0;
    double seqHostBpm_ = 120;
    double seqHostEndPpq_ = 0;
    long   seqHostNextK_ = 0;

    // Per-voice modulated parameter snapshot: p_ with each route's offset folded in
    // (Lin/Log curve rules from the design contract). Reused per voice — no per-call
    // allocation. Reading pm_ for non-modulated fields is safe (pm_ == p_ there).
    double pm_[NUM_PARAMS];
    // Per-voice route-sum scratch (member, not stack, so renderBlock can snapshot
    // the viz voice's sums into vizMod right after its renderVoice — same lifetime
    // trick as pm_). modAnyRoute_ = that voice saw >= 1 active per-param route.
    double modAccum_[NUM_PARAMS] = {0};
    bool   modAnyRoute_ = false;

    // per-block scratch (128)
    float tmpL_[128], tmpR_[128];
    float bL_[128], bR_[128];
    float f1L_[128], f1R_[128], f2L_[128], f2R_[128];
    // Second scratch pair set: the frozen pre-switch filter section runs into
    // these while the live one runs into f1_/f2_ (finding J2).
    float g1L_[128], g1R_[128], g2L_[128], g2R_[128];
    // Oscillator crossfade scratch: each side renders here and is added to the
    // voice mix under its own fade ramp (finding J2).
    float xL_[128], xR_[128];
};

} // namespace fable
