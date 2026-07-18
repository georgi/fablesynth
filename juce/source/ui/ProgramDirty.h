#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace fui {

// Tracks "edited since the current program was loaded" for the dirty dot the
// headers draw next to the preset/kit/patch name (web parity: the stores'
// dirty/kitDirty flags). Listens to every parameter on the processor; program
// loads and full state restores run inside a LoadScope so the flood of
// setValueNotifyingHost calls they make doesn't count as an edit, and the
// flag clears when the scope closes. markEdited() lets non-parameter edits
// (patterns, chain, pad names) set the flag through the same object.
class ProgramDirtyTracker : private juce::AudioProcessorListener {
public:
    explicit ProgramDirtyTracker(juce::AudioProcessor& p) : proc(p) { proc.addListener(this); }
    ~ProgramDirtyTracker() override { proc.removeListener(this); }

    bool isDirty() const { return dirty_.load(); }
    void markEdited() { if (!loading_.load()) dirty_.store(true); }

    // RAII guard around applying a program / restoring state.
    class LoadScope {
    public:
        explicit LoadScope(ProgramDirtyTracker& t) : trk(t) { trk.loading_.store(true); }
        ~LoadScope() { trk.loading_.store(false); trk.dirty_.store(false); }
    private:
        ProgramDirtyTracker& trk;
        JUCE_DECLARE_NON_COPYABLE(LoadScope)
    };

private:
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override { markEdited(); }
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}

    juce::AudioProcessor& proc;
    std::atomic<bool> dirty_{false}, loading_{false};

    JUCE_DECLARE_NON_COPYABLE(ProgramDirtyTracker)
};

} // namespace fui
