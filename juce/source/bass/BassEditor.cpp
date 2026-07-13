#include "BassEditor.h"
#include "dsp/BassParams.h"

namespace {
class StandaloneBassUiModel final : public fui::BassUiModel {
public:
    explicit StandaloneBassUiModel(BassAudioProcessor& p) : proc(p) {}
    fui::ParameterSource parameters() override { const auto& i = fable::bassParamInfo(); return fui::ParameterSource::fromApvts(proc.apvts, i.data(), i.size()); }
    fui::DeviceUiCapabilities capabilities() const override { return {}; }
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
private:
    BassAudioProcessor& proc;
};
}

// Web layout, measured from the running BL-1 app (src/bass/bass.css,
// #bass-rack at its 1460px max-width; rack-relative px):
//   rack              1460 x 931   (padding 14px 18px 22px)
//   header            (18,  14) 1424 x 80
//   #bl-editrow       (18, 103) 1424 x 243  cols 1.5fr 0.62fr 1.15fr 1.25fr, 9px gaps
//     OSC (18,464) SUB (491,192) FILTER (692,355) ENV (1056,386)
//   #bl-modrow        (18, 355) 1424 x 140  cols 290px 250px 1fr, 9px gaps
//     LFO (18,290) ACCENT (317,250) KEYS (576,866)
//   #bl-seq           (18, 504) 1424 x 276
//   #bl-fxrack        (18, 789) 1424 x 120

// ---- BassRack ----
BassRack::BassRack(fui::BassUiModel& p) : header(p), body(p) {
    addAndMakeVisible(header); addAndMakeVisible(body);
}

void BassRack::resized() {
    header.setBounds(18, 14, 1424, 80);
    body.setBounds(getLocalBounds());
}

// ---- BassEditor ----
BassEditor::BassEditor(BassAudioProcessor& p)
    : juce::AudioProcessorEditor(p), model(std::make_unique<StandaloneBassUiModel>(p)), rack(*model) {
    setLookAndFeel(&lnf);
    addAndMakeVisible(rack);
    rack.setBounds(0, 0, BassRack::LW, BassRack::LH);

    setResizable(true, true);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio((double)BassRack::LW / BassRack::LH);
    setResizeLimits(840, (int)(840 * (double)BassRack::LH / BassRack::LW),
                    2100, (int)(2100 * (double)BassRack::LH / BassRack::LW));
    setSize(1200, (int)(1200 * (double)BassRack::LH / BassRack::LW));
}

BassEditor::~BassEditor() { setLookAndFeel(nullptr); }

void BassEditor::paint(juce::Graphics& g) {
    g.fillAll(fui::col::bg);
    // subtle top radial glow, like the web background
    const float width = static_cast<float>(getWidth());
    const float height = static_cast<float>(getHeight());
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff11141d), width * 0.5f, -120.0f,
                                           fui::col::bg, width * 0.5f, height * 0.6f, true));
    g.fillRect(getLocalBounds());
}

void BassEditor::resized() {
    const float width = static_cast<float>(getWidth());
    const float height = static_cast<float>(getHeight());
    const float rackWidth = static_cast<float>(BassRack::LW);
    const float rackHeight = static_cast<float>(BassRack::LH);
    const float sc = juce::jmin(width / rackWidth, height / rackHeight);
    const float dx = (width - rackWidth * sc) * 0.5f;
    const float dy = (height - rackHeight * sc) * 0.5f;
    rack.setTransform(juce::AffineTransform::scale(sc).translated(dx, dy));
}
