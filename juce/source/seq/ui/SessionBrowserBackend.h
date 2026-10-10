#pragma once
#include "../../ui/BrowserBackend.h"
#include "../SeqProcessor.h"

#include <functional>

// Preset browser backend for one SQ-4 track — port of src/seq/sessionPatchOps.ts.
// In SQ-4 a track's sound is its session patch: a factory index, or inline
// params over a factory base. Load = factory load on the track. Capture = a copy
// of the track's patch (factory flag, base index, params) and restore writes that
// exact patch back, so REVERT never turns a factory patch into an inline one.
// VARY = inline patch with the varied values, keeping the current base.
// Audition plays through the processor command FIFO and only while the SQ-4
// transport is stopped.
namespace fui {

class SessionBrowserBackend final : public BrowserBackend {
public:
    struct Hooks {
        std::function<void()> flush;      // push pending device edits into the session patch first
        std::function<void()> reload;     // pull the session patch into the hosted device model
        std::function<int()> selectedPad; // DR-1: pad the pad patch drawer works on
        std::function<void(int)> loadPadPatch;
        std::function<void(const std::vector<std::pair<int, float>>&)> writeFields;   // DR-1: absolute param ids
    };
    SessionBrowserBackend(SeqAudioProcessor&, int track, Hooks);

    float value(int id) const override;
    int loadedIndex() const override { return proc_.trackPatch(track_).index; }
    bool dirty() const override { return !proc_.trackPatch(track_).factory; }
    void load(int item) override;
    std::unique_ptr<BrowserSnapshot> capture() const override;
    void restore(const BrowserSnapshot&) override;
    void applyVariant(int item, const std::vector<float>& values) override;
    const fable::GeneratedTable* table(int i) const override { return proc_.deviceTableAt(track_, i); }
    float bpm() const override { return (float)proc_.conductor().session().bpm; }
    void noteOn(int id, float velocity) override;
    void noteOff(int id) override;
    bool auditionBlocked() const override { return proc_.conductor().playing(); }
    juce::String blockedHint(const juce::String& noun) const override;
    std::uint64_t transportRevision() const override { return proc_.conductor().playing() ? 1u : 0u; }
    int selectedPad() const override { return hooks_.selectedPad ? hooks_.selectedPad() : 0; }
    void loadPadPatch(int item) override { if (hooks_.loadPadPatch) hooks_.loadPadPatch(item); }
    void applyPadFields(const std::vector<std::pair<int, float>>& fields, bool) override {
        if (hooks_.writeFields) hooks_.writeFields(fields);
    }

    struct Snap : BrowserSnapshot { fable::PatchRef patch; };

private:
    SeqAudioProcessor& proc_;
    const int track_;
    Hooks hooks_;
    mutable std::uint64_t cachedRevision_ = ~0ull;
    mutable std::vector<float> cached_;
};

} // namespace fui
