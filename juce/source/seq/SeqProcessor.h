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
#include <map>
#include <unordered_map>
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
//    (the Cmd/Ack FIFOs and their payload handoff). Nor inside the engines:
//    ClipHost::prepare() (called from setHostClipMode(true) in prepareToPlay)
//    reserves the clip byte buffers and the event buffer, and the
//    pending->playing handoff is a capacity-preserving std::swap, so
//    scheduleClip/updateClip/tick and events.push_back() are all allocation-
//    free in steady state after prepare. The audio thread reads the
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

    // Session import/export (web-compatible SessionDoc v:1 JSON — see
    // SessionCodec.h). LOAD/SAVE (SeqHeader) and setStateInformation's session
    // child both funnel through these so there is one rebuild-the-conductor
    // path. applySessionJson stops all tracks first, then rebuilds the
    // conductor from the decoded doc (same sequencing as the old
    // setStateInformation logic); returns false (session unchanged) if the
    // JSON fails validation.
    bool applySessionJson(const juce::String& json);
    juce::String currentSessionJson() const;

    // ---- SQ-4 surface --------------------------------------------------------
    // The conductor half (message thread). Valid after prepareToPlay().
    fable::Conductor& conductor() { return *conductor_; }

    // Editor timer (~30 Hz) / tests call this to deliver the audio thread's
    // clipstart/clipstop acks to the conductor and reset stopped step readouts.
    void drainAcks();

    // Re-apply a track's PatchRef (message thread) via the command FIFO — the
    // patch stepper (Task 11) uses this to hot-swap a track's sound.
    void applyTrackPatch(int t);

    // Hosted native-device bridge (message thread). Parameter edits update the
    // session PatchRef first, then cross to the audio thread through Cmd::Patch.
    std::unordered_map<std::string, float> trackParameterValues(int t) const;
    void setTrackInlineParams(int t, const std::unordered_map<std::string, float>& values);
    void setTrackFactoryPatch(int t, int program);
    int trackFactoryProgram(int t) const;

    int deviceNumTables(int t) const;
    const fable::GeneratedTable* deviceTableAt(int t, int index) const;
    juce::String deviceTableName(int t, int index) const;

    void auditionDrum(int pad, float velocity);
    void selectDrumPad(int pad);
    void auditionBassOn(int semitone, float velocity);
    void auditionBassOff(int semitone);
    void auditionWtOn(int track, int note, float velocity);
    void auditionWtOff(int track, int note);

    uint32_t consumeDrumHitFlags() { return drumHitFlags_.exchange(0); }
    float drumVizPosition(int oscillator) const;
    float drumVizEnvelope() const { return drumVizEnv_.load(); }
    float bassVizPosition() const { return bassVizPos_.load(); }
    float bassVizCutoff() const { return bassVizCut_.load(); }
    int bassCurrentSemitone() const { return bassVizSemi_.load(); }
    float wtVizPosition(int track, int oscillator) const;
    int wtVoiceCount(int track) const;
    double preparedSampleRate() const { return preparedSampleRate_; }

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
    float readScope(float* dest, int n) const;

    void setPaused(bool p) { paused_.store(p); }
    bool paused() const { return paused_.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    static constexpr int kTracks = 4;

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // ---- command FIFO (message thread -> audio thread) ----------------------
    struct Cmd {
        enum class K { Clip, Stop, Update, Gain, Tempo, Patch, Reset,
                       DrumTrigger, DrumSelect, BassOn, BassOff, WtOn, WtOff } k = K::Gain;
        int t = 0, bars = 0, tag = 0, key = 0;       // tag = launch scene (Clip)
        double at = 0, bpm = 0, swing = 0, anchor = 0;
        float gain = 0;
        uint32_t gen = 0;                            // Reset: the new generation
        std::shared_ptr<std::vector<uint8_t>> bytes; // Clip / Update
        std::shared_ptr<std::vector<float>>  params; // Patch (flat engine params)
    };
    void pushCmd(Cmd&& c);
    void drainCmds();

    // ConductorIO: every method builds a Cmd on the message thread and pushes it.
    struct IO : fable::ConductorIO {
        explicit IO(SeqAudioProcessor& o) : p(o) {}
        double now() override { return p.currentFrame.load(); }
        void ioScheduleClip(int t, const std::vector<uint8_t>& bytes, int bars, double at, int tag) override;
        void ioScheduleStop(int t, double at) override;
        void ioUpdateClip(int t, const std::vector<uint8_t>& bytes, int bars) override;
        void ioSetTrackGain(int t, float gain) override;
        void ioSendTempo(double bpm, double swing, double anchor) override;
        SeqAudioProcessor& p;
    };
    IO io_ { *this };

    // ---- ack FIFO (audio thread -> message thread) --------------------------
    // Each ack carries the command generation live when the audio thread pushed
    // it (see cmdGen_); drainAcks drops any whose generation no longer matches.
    struct Ack { int t = 0; fable::HostEvent ev {}; uint32_t gen = 0; };
    void pushAck(int t, const fable::HostEvent& ev, uint32_t gen);

    // Generation tag that invalidates acks stamped before a conductor swap.
    // Two copies deliberately, one per thread:
    //  - cmdGen_ (atomic) is the MESSAGE-thread reference. applySessionJson
    //    bumps it, then pushes a K::Reset command carrying the new value, then
    //    the four stops + rebuild. drainAcks (message thread) drops any ack
    //    whose gen != cmdGen_.
    //  - audioGen_ (plain, audio-thread-owned) is what the audio thread STAMPS
    //    on every ack. It only changes when a K::Reset command drains, i.e. in
    //    FIFO order relative to the stops that follow it.
    // Why not stamp acks directly from cmdGen_? Because reading the atomic after
    // drainCmds races the message thread: applySessionJson could bump it in the
    // gap between drainCmds and the load, so an event generated by the OLD
    // session this block would be stamped with the NEW gen and wrongly survive
    // the drainAcks filter. Advancing audioGen_ only through the ordered Reset
    // command removes the race: an event produced before the Reset drains
    // carries the old gen (dropped); after the Reset drains, the only events
    // that can be produced are Stop acks, because the Reset is FIFO-ordered
    // ahead of the four stops and those stops disarm every pending clip before
    // any Start could fire — so no false owner can ever reach the new conductor.
    std::atomic<uint32_t> cmdGen_ { 0 };
    uint32_t audioGen_ = 0; // audio-thread copy, advanced only by a K::Reset cmd

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
    // Seed only — the constructor (factory session) and applySessionJson
    // (restored doc — also setStateInformation's SESSION path, which funnels
    // through applySessionJson) write it, then immediately hand it to a
    // freshly built Conductor. Once conductor_ exists, IT is the runtime
    // truth (patch swaps, mute/solo, vol, ...); anything on a live/runtime
    // path must read
    // conductor_->session(), never this member, or it'll act on a stale copy
    // (see applyTrackPatch's history — it did exactly that).
    fable::SessionData initialSession_;

    // Sample rate from the most recent prepareToPlay() call. applySessionJson
    // uses this (not getSampleRate()) to decide whether it's safe to rebuild
    // the conductor/engines: JUCE's getSampleRate() only reflects a value a
    // host wrapper pushed via setRateAndBufferSizeDetails, which plugin-
    // boundary tests that call prepareToPlay() directly never do — this
    // member tracks the same fact from the value we actually receive.
    double preparedSampleRate_ = 0.0;

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

    // Audio-thread visual state published atomically for hosted device views.
    std::atomic<uint32_t> drumHitFlags_ { 0 };
    std::atomic<float> drumVizA_ { -1 }, drumVizB_ { -1 }, drumVizEnv_ { 0 };
    std::atomic<float> bassVizPos_ { -1 }, bassVizCut_ { -1 };
    std::atomic<int> bassVizSemi_ { -100 };
    std::atomic<float> wtVizA_[2] {{ -1 }, { -1 }}, wtVizB_[2] {{ -1 }, { -1 }};
    std::atomic<int> wtVoices_[2] {{ 0 }, { 0 }};

    // cached raw params for the message-thread poll (swing / bpm / vol0..3).
    std::atomic<float>* rawSwing_ = nullptr;
    std::atomic<float>* rawBpm_ = nullptr;
    std::atomic<float>* rawVol_[4] {};
    float lastSwing_ = 0, lastBpm_ = 0, lastVol_[4] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SeqAudioProcessor)
};
