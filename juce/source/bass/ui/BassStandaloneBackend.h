#pragma once
#include "../../ui/BrowserBackend.h"
#include "../BassProcessor.h"

namespace fui {

// Standalone BL-1: the processor's APVTS, patches (params + patterns + chain) and audition FIFO.
class BassStandaloneBackend final : public BrowserBackend {
public:
    explicit BassStandaloneBackend(BassAudioProcessor& p) : proc_(p) {
        const auto& info = fable::bassParamInfo();
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
        s->patterns = proc_.getPatternBytes();
        s->chainBars = (int)proc_.getChain().size();
        s->editPattern = proc_.getEditPattern();
        s->rhythm = proc_.getSequenceRhythm();       // web captureSound keeps sequenceRhythm
        return s;
    }
    void restore(const BrowserSnapshot& snap) override {
        const auto& s = static_cast<const Snap&>(snap);
        proc_.setSequenceRhythm(s.rhythm);           // before restoreSound, which sets the dirty flag
        proc_.restoreSound(s.index, s.values, s.patterns, s.chainBars, s.editPattern, s.dirty);
    }
    void applyVariant(int item, const std::vector<float>& values) override {
        // A variant replaces the sound only; the patterns and chain stay as they are.
        proc_.restoreSound(item, values, proc_.getPatternBytes(), (int)proc_.getChain().size(), proc_.getEditPattern(), true);
    }
    const fable::GeneratedTable* table(int i) const override { return proc_.tableAt(i); }
    float bpm() const override { return value(fable::BL_SEQ_BPM); }
    void noteOn(int id, float v) override { proc_.noteOn(id, v); }
    void noteOff(int id) override { proc_.noteOff(id); }
    bool auditionBlocked() const override { return proc_.isSeqPlaying() || proc_.isHostPlaying(); }
    juce::String blockedHint(const juce::String& noun) const override {
        return proc_.isSeqPlaying() ? "The sequencer plays the " + noun + "." : "The host transport plays the " + noun + ".";
    }
    std::uint64_t transportRevision() const override { return (proc_.isSeqPlaying() ? 1u : 0u) | (proc_.isHostPlaying() ? 2u : 0u); }

private:
    struct Snap : BrowserSnapshot {
        std::vector<float> values;
        std::vector<std::uint8_t> patterns;
        int chainBars = 1, editPattern = 0;
        fable::SequenceRhythm rhythm{};
    };
    BassAudioProcessor& proc_;
    std::array<std::atomic<float>*, fable::BL_NUM_PARAMS> raw_{};
};

} // namespace fui
