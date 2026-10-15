#pragma once
#include <cstdio>
#include "../source/bass/BassProcessor.h"
#include "../source/bass/dsp/BassParams.h"
#include "../source/bass/ui/PitchSeqView.h"

namespace bassSeqCacheChecks {
class BassSeqCacheTestModel final : public fui::BassUiModel {
public:
    explicit BassSeqCacheTestModel(BassAudioProcessor& p) : proc(p) {}
    fable::FxTelemetry fxTelemetry(int, int) const override { return proc.fxTelemetry(); }
    fui::ParameterSource parameters() override { const auto& i = fable::bassParamInfo(); return fui::ParameterSource::fromApvts(proc.apvts, i.data(), i.size()); }
    fui::DeviceUiCapabilities capabilities() const override { return {}; }
    fable::ArpSettings arpSettings() const override { return proc.getArpSettings(); }
    void setArpSettings(const fable::ArpSettings& a) override { proc.setArpSettings(a); }
    fable::ArpSettings arpLiveSettings() const override { return proc.getLiveArp(); }
    bool seqEnabled() const override { return proc.isSeqEnabled(); }
    void setSeqEnabled(bool on) override { proc.setSeqEnabled(on); }
    void clearArpKeys() override { proc.clearArpKeys(); }
    void arpKeyInput(int n, bool on) override { if (on) proc.noteOn(n - fable::BL_ROOT_MIDI, .8f); else proc.noteOff(n - fable::BL_ROOT_MIDI); }
    bool programDirty() const override { return proc.isProgramDirty(); }
    int currentProgram() const override { return proc.getCurrentProgram(); }
    int numPrograms() const override { return proc.getNumPrograms(); }
    juce::String programName(int i) const override { return proc.getProgramName(i); }
    void selectProgram(int i) override { proc.setCurrentProgram(i); }
    const fable::GeneratedTable* tableAt(int i) const override { return proc.tableAt(i); }
    float vizPosition() const override { return proc.getVizPos(); }
    float vizCutoff() const override { return proc.getVizCut(); }
    void readScope(float* d, int n) const override { proc.readScope(d, n); }
    bool midiActive() const override { return proc.getMidiActive(); }
    bool hostSynced() const override { return proc.isHostSynced(); }
    double hostBpm() const override { return proc.getHostBpm(); }
    void noteOn(int s, float v) override { proc.noteOn(s, v); }
    void noteOff(int s) override { proc.noteOff(s); }
    int currentSemitone() const override { return proc.getCurrentSemi(); }
    bool sequencerPlaying() const override { return proc.isSeqPlaying(); }
    void setSequencerPlaying(bool b) override { proc.setSeqPlaying(b); }
    int currentStep() const override { return proc.getCurrentStep(); }
    int currentPattern() const override { return proc.getCurrentPattern(); }
    int editPattern() const override { return proc.getEditPattern(); }
    void setEditPattern(int i) override { proc.setEditPattern(i); }
    fable::BassSeqStep sequenceStep(int p, int s) const override { return proc.getSeqStep(p, s); }
    void setSequenceStep(int p, int s, const fable::BassSeqStep& v) override { proc.setSeqStep(p, s, v); }
    const std::vector<int>& chain() const override { return proc.getChain(); }
    void setChain(std::vector<int> c) override { proc.setChain(std::move(c)); }
    std::vector<uint8_t> patternBytes() const override { return proc.getPatternBytes(); }
    void setPatternBytes(std::vector<uint8_t> b) override { proc.setPatternBytes(b); }
    // patternSourceId() defaults to 0 — the standalone plugin never swaps
    // its pattern source, so undo history never needs clearing underneath it.
private:
    BassAudioProcessor& proc;
};
}

inline bool runBassSeqCacheChecks() {
    BassAudioProcessor proc;
    proc.prepareToPlay(48000, 4096);
    bassSeqCacheChecks::BassSeqCacheTestModel model(proc);
    fui::PitchSeqView view(model);
    view.setSize(1424, 276);
    auto fail = [](int line) { std::fprintf(stderr, "BassSeqCacheChecks failed at line %d\n", line); return false; };
    auto same = [](const juce::Image& a, const juce::Image& b) {
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return false;
        return true;
    };
    auto paint = [&](bool direct = false, float scale = 1, float offset = 0, juce::Colour tint = juce::Colours::white) {
        juce::Image image(juce::Image::ARGB, (int)std::ceil(view.getWidth() * scale + offset),
                          (int)std::ceil(view.getHeight() * scale + offset), true);
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(scale).translated(offset, offset));
        g.reduceClipRegion(view.getLocalBounds());
        g.setColour(tint);
        if (direct) view.paintUncachedForTesting(g); else view.paint(g);
        return image;
    };
    auto parity = [&] {
        for (float scale : {1.0f, 2.0f, .65f, .8f}) {
            const auto cached = paint(false, scale), direct = paint(true, scale);
            int maxDelta = 0;
            for (int y = 0; y < cached.getHeight(); ++y)
                for (int x = 0; x < cached.getWidth(); ++x) {
                    const auto a = cached.getPixelAt(x, y).getARGB();
                    const auto b = direct.getPixelAt(x, y).getARGB();
                    for (int shift : {0, 8, 16, 24})
                        maxDelta = std::max(maxDelta, std::abs(int((a >> shift) & 255) - int((b >> shift) & 255)));
                }
            // JUCE's clipped gradient replay rounds at most one channel unit
            // differently: this playback fixture differs at 20 pixels at 1x
            // and 84 pixels at 2x, maximum delta 1. Fractional transforms
            // use the original painter and must match exactly.
            if (maxDelta > (scale == std::floor(scale) ? 1 : 0)) {
                std::fprintf(stderr, "Bass seq parity scale %.2f max channel difference %d\n", scale, maxDelta);
                return false;
            }
        }
        return same(paint(false, .65f, 37.2f), paint(true, .65f, 37.2f));
    };
    const auto initial = paint();
    if (!same(initial, paint(true)) || view.cacheBuildCountForTesting() != 1) return fail(__LINE__);
    if (!same(initial, paint(false, 1, 0, juce::Colours::red.withAlpha(.2f)))) return fail(__LINE__);
    if (view.cacheBuildCountForTesting() != 1 || view.overlayBuildCountForTesting() != 1) return fail(__LINE__);
    view.toggleCell(0, 3);
    const auto edited = paint();
    if (same(initial, edited) || view.cacheBuildCountForTesting() != 2) return fail(__LINE__);
    view.resizeStep(0, 4);
    const auto longer = paint();
    if (same(edited, longer) || view.cacheBuildCountForTesting() != 3) return fail(__LINE__);
    view.toggleStepSlide(0); view.toggleStepAcc(0); view.cycleStepOct(0);
    if (!parity()) return fail(__LINE__);
    view.setSelection({0, 3, 0, 5});
    if (!parity()) return fail(__LINE__);
    view.duplicateSel();
    if (!parity()) return fail(__LINE__);
    view.undoEdit();
    if (!parity()) return fail(__LINE__);
    view.beginGhostPaste(true);
    if (!parity()) return fail(__LINE__);
    view.clearSelection();
    model.setSequencerPlaying(true);
    paint();
    const int artwork = view.cacheBuildCountForTesting(), overlays = view.overlayBuildCountForTesting();
    juce::AudioBuffer<float> buffer(2, 4096);
    juce::MidiBuffer midi;
    for (int i = 0; i < 8; ++i) { buffer.clear(); proc.processBlock(buffer, midi); }
    paint();
    if (view.cacheBuildCountForTesting() != artwork || view.overlayBuildCountForTesting() <= overlays) return fail(__LINE__);
    if (!parity()) return fail(__LINE__);
    view.patternClick(1); view.setSequenceLength(3);
    if (!parity()) return fail(__LINE__);
    view.setSize(1000, 276);
    if (!parity()) return fail(__LINE__);
    juce::Component parent;
    parent.addAndMakeVisible(view);
    parent.setTransform(juce::AffineTransform::translation(.3f, .7f));
    const int beforeParent = view.cacheBuildCountForTesting();
    if (!same(paint(), paint(true)) || view.cacheBuildCountForTesting() != beforeParent) return fail(__LINE__);
    parent.removeChildComponent(&view);
    proc.releaseResources();
    return true;
}
