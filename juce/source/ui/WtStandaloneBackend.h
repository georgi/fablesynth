#pragma once
#include "BrowserBackend.h"
#include "../PluginProcessor.h"

namespace fui {

// Standalone WT-1: the processor's APVTS, programs and audition command FIFO.
class WtStandaloneBackend final : public BrowserBackend {
public:
    explicit WtStandaloneBackend(FableAudioProcessor& p) : proc_(p) {
        const auto& info = fable::paramInfo();
        for (std::size_t i = 0; i < raw_.size(); ++i) raw_[i] = p.apvts.getRawParameterValue(info[i].pid);
    }
    float value(int id) const override { return raw_[(std::size_t)id] ? raw_[(std::size_t)id]->load() : 0.0f; }
    int loadedIndex() const override { return proc_.getCurrentProgram(); }
    bool dirty() const override { return proc_.isProgramDirty(); }
    void load(int item) override { proc_.setCurrentProgram(item); }
    std::unique_ptr<BrowserSnapshot> capture() const override {
        auto s = std::make_unique<Snap>();
        s->index = proc_.getCurrentProgram();
        s->dirty = proc_.isProgramDirty();
        s->values = proc_.captureParamValues();
        return s;
    }
    void restore(const BrowserSnapshot& snap) override {
        const auto& s = static_cast<const Snap&>(snap);
        proc_.restoreSound(s.index, s.values, s.dirty);
    }
    void applyVariant(int item, const std::vector<float>& values) override { proc_.restoreSound(item, values, true); }
    const fable::GeneratedTable* table(int i) const override { return proc_.tableAt(i); }
    int tableRevision() const override { return proc_.getTablesGeneration(); }
    float bpm() const override { return value(fable::SEQ_BPM); }
    void noteOn(int id, float) override { proc_.auditionNote(id, true); }
    void noteOff(int id) override { proc_.auditionNote(id, false); }
    bool auditionBlocked() const override { return proc_.isSeqPlaying() || proc_.isHostPlaying(); }
    juce::String blockedHint(const juce::String& noun) const override {
        return proc_.isSeqPlaying() ? "The sequencer plays the " + noun + "." : "The host transport plays the " + noun + ".";
    }
    std::uint64_t transportRevision() const override { return (proc_.isSeqPlaying() ? 1u : 0u) | (proc_.isHostPlaying() ? 2u : 0u); }

    bool supportsImported() const override { return true; }
    juce::String loadedImportedKey() const override { return proc_.loadedImportedKey(); }
    bool loadImported(const serumimport::ImportedEntry& entry, juce::String& error) override {
        serumimport::ImportedPreset preset;
        if (!serumimport::loadPreset(entry, preset)) { error = "Cannot read " + entry.file.getFileName(); return false; }
        std::vector<fable::UserTable> tables;
        for (const auto& t : preset.tables)
            tables.push_back(fable::userTableFromWave(juce::String(t.name).toUpperCase().substring(0, 14).toStdString(), t.frames, t.wave));
        return proc_.loadImportedPreset(entry.file.getFullPathName(), preset.params, tables, error);
    }

private:
    struct Snap : BrowserSnapshot { std::vector<float> values; };
    FableAudioProcessor& proc_;
    std::array<std::atomic<float>*, fable::NUM_PARAMS> raw_{};
};

} // namespace fui
