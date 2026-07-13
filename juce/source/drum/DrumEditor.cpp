#include "DrumEditor.h"
#include "dsp/DrumParams.h"

namespace fui {
class StandaloneDrumUiModel final : public DrumUiModel {
public:
    explicit StandaloneDrumUiModel(DrumAudioProcessor& p) : proc(p) {}
    ParameterSource parameters() override { const auto& i = fable::drumParamInfo(); return ParameterSource::fromApvts(proc.apvts, i.data(), i.size()); }
    DeviceUiCapabilities capabilities() const override { return {}; }
    int selectedPad() const override { return proc.getSelectedPad(); }
    void selectPad(int i) override { proc.setSelectedPad(i); }
    juce::ChangeBroadcaster& selectionChanges() override { return proc.selectionBroadcaster; }
    juce::String padName(int i) const override { return proc.getPadName(i); }
    uint32_t patchContextRevision() const override { return proc.getPatchContextRevision(); }
    void applyFactoryPadPatch(int i) override { proc.applyFactoryPatch(i); }
    int currentProgram() const override { return proc.getCurrentProgram(); }
    int numPrograms() const override { return proc.getNumPrograms(); }
    juce::String programName(int i) const override { return proc.getProgramName(i); }
    void selectProgram(int i) override { proc.setCurrentProgram(i); }
    int numTables() const override { return proc.numTables(); }
    const fable::GeneratedTable* tableAt(int i) const override { return proc.tableAt(i); }
    juce::String tableName(int i) const override { return proc.tableName(i); }
    int tablesGeneration() const override { return proc.getTablesGeneration(); }
    int addUserTableForPad(int i, fable::UserTable t) override { return proc.addUserTableForPad(i, std::move(t)); }
    void triggerPad(int i, float v) override { proc.triggerPad(i, v); }
    uint32_t consumeHitFlags() override { return proc.consumeHitFlags(); }
    float vizPosition(int i) const override { return proc.getVizPos(i); }
    float vizEnvelope() const override { return proc.getVizEnv(); }
    void readScope(float* d, int n) const override { proc.readScope(d, n); }
    bool midiActive() const override { return proc.getMidiActive(); }
    bool hostSynced() const override { return proc.isHostSynced(); }
    double hostBpm() const override { return proc.getHostBpm(); }
    bool sequencerPlaying() const override { return proc.isSeqPlaying(); }
    void setSequencerPlaying(bool b) override { proc.setSeqPlaying(b); }
    int currentStep() const override { return proc.getCurrentStep(); }
    int currentPattern() const override { return proc.getCurrentPattern(); }
    int editPattern() const override { return proc.getEditPattern(); }
    void setEditPattern(int i) override { proc.setEditPattern(i); }
    uint8_t step(int a, int b, int c) const override { return proc.getStep(a, b, c); }
    void setStep(int a, int b, int c, uint8_t v) override { proc.setStep(a, b, c, v); }
    const std::vector<int>& chain() const override { return proc.getChain(); }
    void setChain(std::vector<int> c) override { proc.setChain(std::move(c)); }
private:
    DrumAudioProcessor& proc;
};
std::unique_ptr<DrumUiModel> makeStandaloneDrumUiModel(DrumAudioProcessor& p) {
    return std::make_unique<StandaloneDrumUiModel>(p);
}
}

// Web layout, measured from the running DR-1 app (src/drum/drum.css,
// #drum-rack at its 1460px max-width; rack-relative px):
//   rack              1460 x 880   (padding 14px 18px 22px)
//   header            (18,  14) 1424 x 80
//   dr-main           (18, 103) 1424 x 501 = left 352px column + right, 9px gap
//     #dr-pads        (18, 103)  352 x 369
//     #dr-padstrip    (18, 481)  352 x 119
//     #dr-selbar      (379,103) 1063 x 31
//     #dr-oscrow      (379,143) 1063 x 243  cols 1fr 1fr 196px, 9px gaps
//       OSC A (379,143,424) SAMPLE (812,143,425) NOISE (1246,143,196)
//     #dr-editrow     (379,395) 1063 x 209  cols 1fr 1.15fr 1.15fr 1.3fr, 9px gaps
//       PITCH (379,395,225) AMP (613,395,259) FLT (881,395,259) MOD (1149,395,293)
//   #dr-stepseq       (18, 613) 1424 x 105
//   #dr-fxrack        (18, 727) 1424 x 131

// ---- DrumRack ----
DrumRack::DrumRack(fui::DrumUiModel& p) : header(p), body(p) {
    addAndMakeVisible(body);
    // The body spans the full logical rack, including the otherwise-empty
    // header strip. Keep the header above it so its program and master
    // controls receive mouse events.
    addAndMakeVisible(header);
}

void DrumRack::resized() {
    header.setBounds(18, 14, 1424, 80);
    body.setBounds(getLocalBounds());
}

// ---- DrumEditor ----
DrumEditor::DrumEditor(DrumAudioProcessor& p)
    : juce::AudioProcessorEditor(p), model(fui::makeStandaloneDrumUiModel(p)), rack(*model) {
    setLookAndFeel(&lnf);
    setWantsKeyboardFocus(true); // QWERTY pad map (PadGrid key-listens on us)
    addAndMakeVisible(rack);
    rack.setBounds(0, 0, DrumRack::LW, DrumRack::LH);

    setResizable(true, true);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio((double)DrumRack::LW / DrumRack::LH);
    setResizeLimits(840, (int)(840 * (double)DrumRack::LH / DrumRack::LW),
                    2100, (int)(2100 * (double)DrumRack::LH / DrumRack::LW));
    setSize(1200, (int)(1200 * (double)DrumRack::LH / DrumRack::LW));
}

DrumEditor::~DrumEditor() { setLookAndFeel(nullptr); }

void DrumEditor::paint(juce::Graphics& g) {
    g.fillAll(fui::col::bg);
    // subtle top radial glow, like the web background
    const float width = static_cast<float>(getWidth());
    const float height = static_cast<float>(getHeight());
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff11141d), width * 0.5f, -120.0f,
                                           fui::col::bg, width * 0.5f, height * 0.6f, true));
    g.fillRect(getLocalBounds());
}

void DrumEditor::resized() {
    const float width = static_cast<float>(getWidth());
    const float height = static_cast<float>(getHeight());
    const float rackWidth = static_cast<float>(DrumRack::LW);
    const float rackHeight = static_cast<float>(DrumRack::LH);
    const float sc = juce::jmin(width / rackWidth, height / rackHeight);
    const float dx = (width - rackWidth * sc) * 0.5f;
    const float dy = (height - rackHeight * sc) * 0.5f;
    rack.setTransform(juce::AffineTransform::scale(sc).translated(dx, dy));
}
