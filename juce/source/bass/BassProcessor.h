#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/BassEngine.h"
#include "dsp/BassFx.h"
#include "dsp/BassParams.h"
#include "dsp/BassPatches.h"
#include "../ui/ProgramDirty.h"

#include <array>
#include <atomic>
#include <mutex>
#include <vector>

// FableSynth BL-1 VST/AU processor. Owns the JUCE-independent DSP core
// (BassEngine + BassFx) and bridges the APVTS parameter tree, MIDI notes
// (mono last-note audition around root C2 = 36), host tempo/transport, and
// the stereo output to it.
//
// Threading model (mirrors DrumAudioProcessor):
//  - Parameters flow through APVTS raw-value atomics, copied into the engine's
//    flat array at the top of every processBlock.
//  - UI actions (keyboard audition, transport, panic) go through a lock-free
//    command FIFO drained in processBlock.
//  - Patterns/chain are owned by the message thread; edits copy the whole
//    array into a shared buffer under a mutex which processBlock try-locks
//    (skip on contention, retry next block — never blocks the audio thread).
//  - Step/note/viz/tempo feedback is published as atomics after render.
class BassAudioProcessor : public juce::AudioProcessor {
public:
    BassAudioProcessor();
    ~BassAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FableSynth BL-1"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return (int)fable::bassFactoryPatches().size(); }
    int getCurrentProgram() override { return currentProgram_; }
    void setCurrentProgram(int index) override;   // patch onto APVTS + patterns/chain
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Edited-since-patch-load flag for the header's dirty dot.
    bool isProgramDirty() const { return programDirty_.isDirty(); }

    // ---- audition / transport (message thread -> engine via the command FIFO) ----
    void noteOn(int semi, float vel);             // UI keyboard audition
    void noteOff(int semi);
    void setSeqPlaying(bool on);
    bool isSeqPlaying() const { return seqPlaying_.load(); }
    int  getCurrentStep() const { return curStep_.load(); }      // -1 stopped
    int  getCurrentPattern() const { return curPattern_.load(); }
    int  getCurrentSemi() const { return curSemi_.load(); }      // -100 = idle
    bool isHostSynced() const { return hostSynced_.load(); }     // host reported a tempo
    double getHostBpm() const { return hostBpm_.load(); }

    // ---- patterns / chain (message thread) ----
    fable::BassSeqStep getSeqStep(int pattern, int step) const;
    void setSeqStep(int pattern, int step, const fable::BassSeqStep& s);
    const std::vector<int>& getChain() const { return chain_; }
    void setChain(std::vector<int> c);
    int  getEditPattern() const { return editPattern_; }
    void setEditPattern(int p);

    // Decision-6: bulk pattern-buffer access for StepEditOps.h range/pattern
    // ops and the view's undo history (mirrors the per-step accessors above).
    std::vector<uint8_t> getPatternBytes() const;
    void setPatternBytes(const std::vector<uint8_t>& bytes);

    // ---- tables (the 6 WT-1 procedurals) ----
    int numTables() const { return (int)tables_.size(); }
    const fable::GeneratedTable* tableAt(int idx) const;

    // ---- HUD feeds ----
    void  readScope(float* dst, int n) const;         // post-FX ring buffer
    float getVizPos() const { return vizPos_.load(); }
    float getVizEnv() const { return vizEnv_.load(); }
    float getVizFenv() const { return vizFenv_.load(); }
    float getVizCut() const { return vizCut_.load(); }
    bool  getVizGate() const { return vizGate_.load(); }
    bool  getMidiActive() const { return midiGlow_.load() > 0; }
    double getCurrentSr() const { return currentSr_.load(); }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pushCmd(int type, int a, float v);
    void shareSeqState(bool patterns, bool chain);

    fable::BassEngine engine;
    fable::BassFx fx;
    std::vector<fable::TablePtr> tables_;

    std::array<std::atomic<float>*, fable::BL_NUM_PARAMS> rawParams_{};

    // command FIFO (message thread -> audio thread)
    enum CmdType { CmdNoteOn = 0, CmdNoteOff, CmdPlay, CmdStop, CmdPanic };
    struct Cmd { int type = 0; int a = 0; float v = 0; };
    juce::AbstractFifo cmdFifo_{64};
    std::array<Cmd, 64> cmds_{};

    // sequencer content — message-thread source of truth + audio-side mirror
    std::array<uint8_t, fable::BL_PATTERN_BYTES> patterns_{};
    std::vector<int> chain_{0};
    mutable std::mutex shareMutex_;
    std::array<uint8_t, fable::BL_PATTERN_BYTES> patternsShared_{};
    std::vector<int> chainShared_{0};
    bool patternsDirty_ = false, chainDirty_ = false;

    int editPattern_ = 0;
    int currentProgram_ = 0;
    fui::ProgramDirtyTracker programDirty_{*this};

    // atomics published from the audio thread
    std::atomic<bool> seqPlaying_{false};
    std::atomic<int> curStep_{-1}, curPattern_{0}, curSemi_{-100};
    std::atomic<bool> hostSynced_{false};
    std::atomic<double> hostBpm_{0.0};
    std::atomic<float> vizPos_{-1.0f}, vizEnv_{0.0f}, vizFenv_{0.0f}, vizCut_{-1.0f};
    std::atomic<bool> vizGate_{false};
    std::atomic<int> midiGlow_{0};
    std::atomic<double> currentSr_{48000.0};

    static constexpr int kScopeSize = 4096; // power of two
    std::array<float, kScopeSize> scopeBuf_{};
    std::atomic<int> scopeW_{0};

    juce::AudioBuffer<float> scratch_; // R backing when the host gives us mono

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassAudioProcessor)
};
