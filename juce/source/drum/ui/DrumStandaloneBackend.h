#pragma once
#include "../../ui/BrowserBackend.h"
#include "../DrumProcessor.h"

namespace fui {

// Standalone DR-1: kits as host programs (parameters + pad names), pad patches on the selected pad.
class DrumStandaloneBackend final : public BrowserBackend {
public:
    explicit DrumStandaloneBackend(DrumAudioProcessor& p) : proc_(p) {
        const auto& info = fable::drumParamInfo();
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
        for (int i = 0; i < fable::DR_NPADS; ++i) s->names[(std::size_t)i] = proc_.getPadName(i);
        return s;
    }
    void restore(const BrowserSnapshot& snap) override {
        const auto& s = static_cast<const Snap&>(snap);
        proc_.restoreKit(s.index, s.values, s.names, s.dirty);
    }
    void applyVariant(int, const std::vector<float>&) override {}   // kits have no VARY
    float bpm() const override { return value(fable::DG_SEQ_BPM); }
    void noteOn(int id, float v) override { proc_.triggerPad(id, v); }
    void noteOff(int) override {}
    bool auditionBlocked() const override { return proc_.isSeqPlaying() || proc_.isHostPlaying(); }
    juce::String blockedHint(const juce::String& noun) const override {
        return proc_.isSeqPlaying() ? "The sequencer plays the " + noun + "." : "The host transport plays the " + noun + ".";
    }
    std::uint64_t transportRevision() const override { return (proc_.isSeqPlaying() ? 1u : 0u) | (proc_.isHostPlaying() ? 2u : 0u); }
    int selectedPad() const override { return proc_.getSelectedPad(); }
    void loadPadPatch(int item) override { proc_.applyFactoryPatch(item); }
    void applyPadFields(const std::vector<std::pair<int, float>>& fields, bool dirty) override { proc_.restorePadFields(fields, dirty); }

private:
    struct Snap : BrowserSnapshot {
        std::vector<float> values;
        std::array<juce::String, fable::DR_NPADS> names;
    };
    DrumAudioProcessor& proc_;
    std::array<std::atomic<float>*, fable::DR_NUM_PARAMS> raw_{};
};

} // namespace fui
