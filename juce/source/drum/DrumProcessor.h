#pragma once

#include "../agent/FableAgent.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/DrumEngine.h"
#include "dsp/DrumKits.h"
#include "dsp/DrumParams.h"
#include "dsp/DrumRhythm.h"
#include "../dsp/UserTables.h"
#include "../ui/ProgramDirty.h"

#include <array>
#include <atomic>
#include <vector>

// FableSynth DR-1 VST/AU processor. Owns the JUCE-independent DSP core
// (DrumEngine, including one FX chain per pad) and bridges the APVTS parameter tree, MIDI pads
// (notes 36-51), host tempo, and the 5-bus multi-out (MAIN + AUX 1-4) to it.
//
// Threading model (mirrors FableAudioProcessor):
//  - Parameters flow through APVTS raw-value atomics, copied into the engine's
//    flat array at the top of every processBlock.
//  - UI actions (pad audition, transport, pad selection) go through a
//    lock-free command FIFO drained in processBlock.
//  - Patterns/chain are owned by the message thread; edits copy the whole
//    state into a fixed triple buffer. The audio thread acquires the latest
//    complete snapshot with one exchange, without locking or allocating.
//  - Step/pattern/hit/viz/tempo feedback is published as atomics after render.
class DrumAudioProcessor : public juce::AudioProcessor {
public:
    DrumAudioProcessor();
    ~DrumAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override { agentOutputMeter_.reset(); }
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FableSynth DR-1"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return (int)fable::factoryKits().size(); }
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram(int index) override;   // applyKit onto APVTS + patterns/chain/names
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    fable::FableAgent& getAgent();
    juce::AudioProcessorValueTreeState apvts;

    // Edited-since-kit-load flag for the header's dirty dot.
    bool isProgramDirty() const { return programDirty_.isDirty(); }

    // ---- pads / transport (message thread -> engine via the command FIFO) ----
    void triggerPad(int pad, float vel);              // UI audition
    void setSeqPlaying(bool on);
    bool isSeqPlaying() const { return seqPlaying_.load(); }
    int  getCurrentStep() const { return curStep_.load(); }      // -1 stopped
    int  getCurrentPattern() const { return curPattern_.load(); }
    uint32_t consumeHitFlags() { return hitFlags_.exchange(0); } // bit i = pad i
    bool isHostSynced() const { return hostSynced_.load(); }     // host reported a tempo
    double getHostBpm() const { return hostBpm_.load(); }

    // ---- patterns / chain / names / selection (message thread) ----
    uint8_t getStep(int pattern, int pad, int step) const;
    void    setStep(int pattern, int pad, int step, uint8_t v);  // 0/1/2
    const std::vector<int>& getChain() const { return chain_; }
    void setChain(std::vector<int> c);
    void setDrumRhythm(const fable::DrumRhythm& rhythm);
    void clearDrumRhythm();
    int  getEditPattern() const { return editPattern_; }
    void setEditPattern(int p);
    juce::String getPadName(int i) const;
    void setPadName(int i, juce::String n);
    int  getSelectedPad() const { return selectedPad_; }
    void setSelectedPad(int i);                       // notifies listeners
    juce::ChangeBroadcaster selectionBroadcaster;

    // Changes whenever an operation makes the currently displayed patch name
    // unknowable (pad selection, kit load, or host-state restore).
    uint32_t getPatchContextRevision() const { return patchContextRevision_.load(); }

    // Apply factory pad patch `index` to the selected pad via the APVTS
    // (same setValueNotifyingHost path as setCurrentProgram). out/choke and
    // other pads are untouched.
    void applyFactoryPatch(int index);

    // ---- tables (4 drum + 6 WT-1 procedural, then user slots) ----
    int numTables() const { return (int)tables_.size() + (int)userTables_.size(); }
    const fable::GeneratedTable* tableAt(int idx) const;
    juce::String tableName(int idx) const;
    // Import a user table and point the pad's OSC A at it; returns the
    // combined table index, or -1 when the pool is full.
    int addUserTableForPad(int pad, fable::UserTable t);
    const std::vector<fable::UserTable>& getUserTables() const { return userTables_; }
    int getTablesGeneration() const { return tablesGen_.load(); }

    // ---- HUD feeds ----
    void  readScope(float* dst, int n) const;         // MAIN post-FX ring buffer
    float getVizPos(int osc) const { return (osc == 0 ? vizA_ : vizB_).load(); }
    float getVizEnv() const { return vizEnv_.load(); }
    bool  getMidiActive() const { return midiGlow_.load() > 0; }
    double getCurrentSr() const { return currentSr_.load(); }
    fable::FxTelemetry fxTelemetry(int pad, int bus) const { return engine.fxTelemetry(pad, bus); }

private:
    fable::AudioMeter agentOutputMeter_;
    std::unique_ptr<fable::FableAgent> agent_;
    std::atomic<std::uint64_t> agentStateGeneration_ { 0 };
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void rebuildEngineTables();
    void pushCmd(int type, int a, float v);
    void shareSeqState();   // copy message-thread seq state for the audio thread

    fable::DrumEngine engine;
    std::vector<fable::TablePtr> tables_;            // procedural: 4 drum + 6 WT-1
    std::vector<fable::UserTable> userTables_;       // imported tables
    std::atomic<int> tablesGen_{0};

    std::array<std::atomic<float>*, fable::DR_NUM_PARAMS> rawParams_{};

    // command FIFO (message thread -> audio thread)
    enum CmdType { CmdTrigger = 0, CmdPlay, CmdStop, CmdSelect };
    struct Cmd { int type = 0; int a = 0; float v = 0; };
    juce::AbstractFifo cmdFifo_{64};
    std::array<Cmd, 64> cmds_{};

    // sequencer content — message-thread source of truth + audio-side mirror
    static constexpr int kPatternBytes = fable::DR_NPATTERNS * fable::DR_NPADS * fable::DR_STEPS;
    std::array<uint8_t, kPatternBytes> patterns_{};
    std::vector<int> chain_{0};
    bool hasRhythm_ = false;
    fable::DrumRhythm rhythm_{};
    struct SeqSnapshot {
        std::array<uint8_t, kPatternBytes> patterns{};
        std::array<int, fable::DR_NPATTERNS> chain{};
        int chainSize = 1;
        bool hasRhythm = false;
        fable::DrumRhythm rhythm{};
    };
    // Single producer (message thread), single consumer (audio). Each owns
    // one slot; exchange hands off the middle slot. The dirty bit coalesces
    // edits while audio is stopped without ever overwriting a reader's slot.
    std::array<SeqSnapshot, 3> seqSnapshots_{};
    static constexpr unsigned kSeqDirty = 4;
    std::atomic<unsigned> seqMiddle_{1};
    unsigned seqWrite_ = 2, seqRead_ = 0;
    static_assert(std::atomic<unsigned>::is_always_lock_free);

    std::array<juce::String, fable::DR_NPADS> padNames_;
    int selectedPad_ = 0;
    int editPattern_ = 0;
    int currentProgram_ = 0;
    std::atomic<uint32_t> patchContextRevision_{0};
    fui::ProgramDirtyTracker programDirty_{*this};

    // atomics published from the audio thread
    std::atomic<bool> seqPlaying_{false};
    std::atomic<int> curStep_{-1}, curPattern_{0};
    std::atomic<uint32_t> hitFlags_{0};
    std::atomic<bool> hostSynced_{false};
    std::atomic<double> hostBpm_{0.0};
    std::atomic<float> vizA_{-1.0f}, vizB_{-1.0f}, vizEnv_{0.0f};
    std::atomic<int> midiGlow_{0};
    std::atomic<double> currentSr_{48000.0};

    static constexpr int kScopeSize = 4096; // power of two
    std::array<float, kScopeSize> scopeBuf_{};
    std::atomic<int> scopeW_{0};

    juce::AudioBuffer<float> scratch_; // backing for disabled AUX buses

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumAudioProcessor)
};
