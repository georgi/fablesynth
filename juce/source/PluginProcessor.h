#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/Engine.h"
#include "dsp/Fx.h"
#include "dsp/Params.h"
#include "dsp/Presets.h"
#include "dsp/UserTables.h"

// Host transport snapshot for the LFO displays: tempo, song position, and
// whether the transport is running. The synced-LFO dot tracks ppq when playing.
struct HostTransport {
    double bpm = 120.0;
    double ppq = 0.0;
    bool   playing = false;
};

// FableSynth VST/AU processor. Owns the JUCE-independent DSP core (Engine + Fx)
// and bridges the APVTS parameter tree + MIDI to it.
class FableAudioProcessor : public juce::AudioProcessor {
public:
    FableAudioProcessor();
    ~FableAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FableSynth WT-1"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return (int)fable::factoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- wavetable visualization feed (read on the message thread) ----
    // The live modulated frame position per oscillator is published from the
    // audio thread via atomics (-1 = idle).
    float getVizPos(int osc) const { return (osc == 0 ? vizPosA : vizPosB).load(); }
    int getTablesGeneration() const { return tablesGen.load(); }

    // Full transport snapshot for the LFO displays (tempo + position + running).
    // The synced-LFO dot tracks ppq while playing so it sits on the beat grid.
    HostTransport getTransport() const {
        return { hostBpm.load(), hostPpq.load(), hostPlaying.load() };
    }

    // ---- table addressing (procedural slots 0..5, then user tables) ----
    // The oscillator TABLE param is an index into this combined space. Returns
    // nullptr for an empty user slot so callers can fall back / draw nothing.
    int  numTables() const { return (int)tables.size() + (int)userTables.size(); }
    const fable::GeneratedTable* tableAt(int idx) const;
    juce::String tableName(int idx) const;

    // ---- user wavetables (import / draw), all on the message thread ----
    // Source-of-truth pool; persisted in plugin state and pushed to the engine.
    const std::vector<fable::UserTable>& getUserTables() const { return userTables; }
    int  maxUserTables() const { return fable::MAX_USER_TABLES; }
    // Add a freshly built table; returns its combined table index (or -1 if full).
    int  addUserTable(fable::UserTable table);
    void deleteUserTable(int poolIndex);
    void renameUserTable(int poolIndex, std::string name);
    void updateUserTable(int poolIndex, fable::UserTable u); // in-place replace, keeps index
    int  duplicateUserTable(int poolIndex);   // returns new combined index, or -1
    int  duplicateFactoryTable(int factoryIndex); // returns new combined index, or -1
    const std::vector<fable::TablePtr>& factoryTables() const { return tables; }

    // ---- HUD feeds (scope / spectrum / voices / MIDI led) ----
    int    getVoiceCount() const { return voiceCount.load(); }
    bool   getMidiActive() const { return midiGlow.load() > 0; }
    double getCurrentSr()  const { return currentSr.load(); }
    // Copy the most recent n post-FX mono samples (oldest -> newest) into dst.
    void   readScope(float* dst, int n) const;

    // ---- note sequencer (BL-1 conventions) --------------------------------
    // Transport: message thread -> engine via a lock-free command FIFO.
    void setSeqPlaying(bool on);
    bool isSeqPlaying() const { return seqPlaying_.load(); }
    int  getCurrentStep() const { return curStep_.load(); }      // -1 stopped
    int  getCurrentPattern() const { return curPattern_.load(); }
    bool isHostSynced() const { return hostSynced_.load(); }     // host reported a tempo
    double getHostBpm() const { return hostSeqBpm_.load(); }
    // Patterns / chain are owned by the message thread; every edit copies the
    // whole array into a shared mirror the audio thread applies on its next
    // block (try-lock there — never blocks the audio thread).
    fable::NoteSeqStep getSeqStep(int pattern, int step) const;
    void setSeqStep(int pattern, int step, const fable::NoteSeqStep& s);
    const std::vector<int>& getChain() const { return chain_; }
    void setChain(std::vector<int> c);
    int  getEditPattern() const { return editPattern_; }
    void setEditPattern(int p);

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pushCmd(int type);
    void shareSeqState(bool patterns, bool chain);

    // Rebuild the engine's table set from procedural + user tables (message thread).
    void rebuildEngineTables();

    fable::Engine engine;
    fable::Fx fx;
    std::vector<fable::TablePtr> tables;         // procedural tables (+ viz frames)
    std::vector<fable::UserTable> userTables;    // imported / drawn tables
    std::atomic<int> tablesGen{0};
    std::atomic<float> vizPosA{-1.0f}, vizPosB{-1.0f};
    std::atomic<float> hostBpm{120.0f};
    std::atomic<double> hostPpq{0.0};
    std::atomic<bool> hostPlaying{false};
    juce::AudioBuffer<float> scratchR; // mono-output downmix scratch

    // HUD feeds
    static constexpr int kScopeSize = 4096; // power of two
    std::array<float, kScopeSize> scopeBuf{};
    std::atomic<int> scopeW{0};
    std::atomic<int> voiceCount{0};
    std::atomic<int> midiGlow{0};
    std::atomic<double> currentSr{48000.0};
    std::array<std::atomic<float>*, fable::NUM_PARAMS> rawParams{};
    int currentProgram = 0;
    bool prepared = false;

    // ---- note sequencer bridge (BassAudioProcessor scheme) ----
    enum CmdType { CmdPlay = 0, CmdStop, CmdPanic };
    juce::AbstractFifo cmdFifo_{64};
    std::array<int, 64> cmds_{};

    // sequencer content — message-thread source of truth + audio-side mirror
    std::vector<uint8_t> patterns_ = fable::makeEmptySeqPatterns();
    std::vector<int> chain_{0};
    mutable std::mutex shareMutex_;
    std::vector<uint8_t> patternsShared_ = fable::makeEmptySeqPatterns();
    std::vector<int> chainShared_{0};
    bool patternsDirty_ = false, chainDirty_ = false;
    int editPattern_ = 0;

    // atomics published from the audio thread
    std::atomic<bool> seqPlaying_{false};
    std::atomic<int> curStep_{-1}, curPattern_{0};
    std::atomic<bool> hostSynced_{false};
    std::atomic<double> hostSeqBpm_{0.0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FableAudioProcessor)
};
