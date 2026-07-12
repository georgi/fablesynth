#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Conductor.h"
#include "dsp/SeqModel.h"
#include "../dsp/Engine.h"
#include "../dsp/Fx.h"
#include "../dsp/ClipHost.h"   // fable::HostEvent
#include "../bass/dsp/BassEngine.h"
#include "../bass/dsp/BassFx.h"
#include "../drum/dsp/DrumEngine.h"
#include "../drum/dsp/DrumFx.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace fable {
// SQ-4's flat host-parameter table (master/swing/bpm/quant/vol0..3), pid
// strings "master" … "vol3" — the fui control resolver installed by
// SeqHeader.cpp (same pattern as bassParamInfo/drumParamInfo).
const std::vector<ParamInfo>& seqParamInfo();
} // namespace fable
// FableSynth SQ-4 (session launcher) processor. Hosts all four FableSynth
// engines — DR-1 (DrumEngine), BL-1 (BassEngine), and two WT-1 (Engine) — in a
// single plugin and renders them under one shared frame timebase, driven by the
// message-thread Conductor (owner/queue launcher, quantize scheduling, mute/
// solo gain math). v1 assumes the factory 4-track layout {DR1, BL1, WT1, WT1}.
//
// Threading (mirrors the standalone processors' lock-free scheme):
//  - The Conductor lives on the message thread. Its ConductorIO turns each
//    musical decision into a Cmd pushed through cmdFifo_ (juce::AbstractFifo,
//    256 slots), drained at the top of processBlock and applied to the engines.
//  - Clip bytes travel as std::shared_ptr<std::vector<uint8_t>> built on the
//    message thread — no audio-thread allocation *in Task 8's command scheme*
//    (the Cmd/Ack FIFOs and their payload handoff). ClipHost::scheduleClip/
//    updateClip and the engines' internal events.push_back() may still
//    allocate on first growth, inside the engines (pre-existing Tasks 2-5
//    behavior) — this claim doesn't cover that. The audio thread reads the
//    slot's shared_ptr in place (hostClip/hostClipUpdate *copy* the bytes into
//    the engine's ClipHost) and never resets it. The shared_ptr is recycled
//    only when the message thread overwrites that slot on a later push, so the
//    only free() ever happens on the message thread (see cmdSlots_ note below).
//  - Acks (clipstart/clipstop) flow back through ackFifo_ (512); drainAcks()
//    pops them into Conductor::onClipStart/Stop and polls the musical APVTS
//    params (swing/bpm/vol0..3) into the conductor. The processor itself owns
//    this pump via a private juce::Timer (30 Hz, started in prepareToPlay,
//    stopped in releaseResources/destructor) so acks and param changes are
//    delivered even headless (offline bounce, no editor open); the editor and
//    tests may still call drainAcks() directly — it's idempotent, and every
//    caller runs on the message thread, so concurrent/double drains are safe.
//    Step/bar positions are written straight to atomics on the audio thread.
class SeqAudioProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    SeqAudioProcessor();
    ~SeqAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { stopTimer(); }
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FableSynth SQ-4"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "NEON TALE"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---- SQ-4 surface --------------------------------------------------------
    // The conductor half (message thread). Valid after prepareToPlay().
    fable::Conductor& conductor() { return *conductor_; }

    // Editor timer (~30 Hz) / tests call this to deliver the audio thread's
    // clipstart/clipstop acks to the conductor and reset stopped step readouts.
    void drainAcks();

    // Re-apply a track's PatchRef (message thread) via the command FIFO — the
    // patch stepper (Task 11) uses this to hot-swap a track's sound.
    void applyTrackPatch(int t);

    // Test hook: snapshot of a track's live audio-thread param array. Used to
    // verify a patch swap (applyTrackPatch) actually reached the engine and
    // not just the session doc — call after draining the Cmd FIFO (any
    // processBlock).
    std::vector<float> debugTrackParams(int t);

    // Published by the audio thread. currentFrame is the shared timebase read by
    // ConductorIO::now(): frames elapsed = the frame index the *next* block will
    // render from, so a launch issued between blocks and the block that consumes
    // it agree on the frame (see processBlock).
    std::atomic<double> currentFrame { 0 };
    std::atomic<int>    trackStep[4];   // -1 = no position (stopped)
    std::atomic<int>    trackBar[4];
    std::atomic<float>  trackRms[4];    // post-gain running RMS, for the VU

    // Copy the most recent n post-limiter mono samples (oldest -> newest).
    float readScope(float* dest, int n);

    void setPaused(bool p) { paused_.store(p); }
    bool paused() const { return paused_.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static constexpr int kTracks = 4;

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- command FIFO (message thread -> audio thread) ----------------------
    struct Cmd {
        enum class K { Clip, Stop, Update, Gain, Tempo, Patch } k = K::Gain;
        int t = 0, bars = 0;
        double at = 0, bpm = 0, swing = 0, anchor = 0;
        float gain = 0;
        std::shared_ptr<std::vector<uint8_t>> bytes; // Clip / Update
        std::shared_ptr<std::vector<float>>  params; // Patch (flat engine params)
    };
    void pushCmd(Cmd&& c);
    void drainCmds();

    // ConductorIO: every method builds a Cmd on the message thread and pushes it.
    struct IO : fable::ConductorIO {
        explicit IO(SeqAudioProcessor& o) : p(o) {}
        double now() override { return p.currentFrame.load(); }
        void ioScheduleClip(int t, const std::vector<uint8_t>& bytes, int bars, double at) override;
        void ioScheduleStop(int t, double at) override;
        void ioUpdateClip(int t, const std::vector<uint8_t>& bytes, int bars) override;
        void ioSetTrackGain(int t, float gain) override;
        void ioSendTempo(double bpm, double swing, double anchor) override;
        SeqAudioProcessor& p;
    };
    IO io_ { *this };

    // ---- ack FIFO (audio thread -> message thread) --------------------------
    struct Ack { int t = 0; fable::HostEvent ev {}; };
    void pushAck(int t, const fable::HostEvent& ev);

    // ---- per-track render helpers (audio thread) ----------------------------
    void renderDrum(float* L, float* R, int n);
    void renderBass(float* L, float* R, int n);
    void renderWt(int i, float* L, float* R, int n);
    int  takeEvents(int t, fable::HostEvent* out, int max);

    // ---- patch application --------------------------------------------------
    std::vector<float> computeTrackParams(int t, const fable::PatchRef& patch) const;
    void loadTrackParams(int t, const std::vector<float>& v); // audio thread

    // ---- session param polling (message thread) -----------------------------
    void pollSessionParams();

    // juce::Timer: the processor's own 30 Hz pump for drainAcks() (see the
    // threading note above) — private since only the processor drives it.
    void timerCallback() override { drainAcks(); }

    // The four engines + their standalone FX chains (identical topology/order).
    fable::DrumEngine drum_;  fable::DrumFx drumFx_;
    fable::BassEngine bass_;  fable::BassFx bassFx_;
    fable::Engine     wt_[2]; fable::Fx     wtFx_[2];

    std::vector<fable::TablePtr> wtTables_, drumTables_, bassTables_;

    std::unique_ptr<fable::Conductor> conductor_;
    // Seed only — the constructor (factory session) and setStateInformation
    // (restored doc) write it, then immediately hand it to a freshly built
    // Conductor. Once conductor_ exists, IT is the runtime truth (patch
    // swaps, mute/solo, vol, ...); anything on a live/runtime path must read
    // conductor_->session(), never this member, or it'll act on a stale copy
    // (see applyTrackPatch's history — it did exactly that).
    fable::SessionData initialSession_;

    // NB: cmdSlots_ holds the shared_ptr for a consumed command until the
    // message thread overwrites that slot on its next wrap of the FIFO — that
    // overwrite is where the byte vector is freed, always off the audio thread.
    juce::AbstractFifo cmdFifo_ { 256 };
    std::array<Cmd, 256> cmdSlots_;
    juce::AbstractFifo ackFifo_ { 512 };
    std::array<Ack, 512> ackSlots_;

    double frame_ = 0;
    std::atomic<bool> paused_ { false };

    juce::SmoothedValue<float> trackGain_[4], masterGain_; // ~15 ms ramps
    std::atomic<float>* rawMaster_ = nullptr;

    // Master safety limiter — the WT/DR/BL web DynamicsCompressorNode port,
    // configured threshold -6 dB, knee 4 dB, ratio 12, attack 2 ms, release
    // 250 ms (declared/defined in SeqProcessor.cpp).
    struct Limiter {
        double sr = 48000, env = 0, atk = 0, rel = 0, makeup = 1;
        void prepare(double sampleRate);
        void reset() { env = 0; }
        void process(float& l, float& r);
    } limiter_;

    // per-track render scratch + DR-1 AUX backing (sized in prepareToPlay).
    juce::AudioBuffer<float> trackBuf_; // 2 ch
    juce::AudioBuffer<float> drumAux_;  // 8 ch (AUX 1..4)

    static constexpr int kScopeSize = 2048;
    std::array<float, kScopeSize> scopeRing_ {};
    std::atomic<int> scopeWrite_ { 0 };

    // cached raw params for the message-thread poll (swing / bpm / vol0..3).
    std::atomic<float>* rawSwing_ = nullptr;
    std::atomic<float>* rawBpm_ = nullptr;
    std::atomic<float>* rawVol_[4] {};
    float lastSwing_ = 0, lastBpm_ = 0, lastVol_[4] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SeqAudioProcessor)
};
