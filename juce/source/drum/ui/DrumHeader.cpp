#include "DrumHeader.h"

namespace fui {

// The shared fui controls look their metadata up in the WT-1 fable::paramInfo()
// table; install the DR-1 table as the primary resolver so ids like
// "master.swing" / "pad3.lvl" resolve (and overlapping ids like
// "master.volume" get the DR-1 labels). Runs at binary load, before any
// control can be constructed; never linked into WT-1 binaries.
static const fable::ParamInfo* drumInfoLookup(const std::string& pid) {
    const int i = fable::drumIdFromString(pid);
    return i >= 0 ? &fable::drumParamInfo()[(size_t)i] : nullptr;
}
static const bool g_drumResolverInstalled = [] {
    setParamInfoResolver(&drumInfoLookup);
    return true;
}();

// ===================== DrumScopeView =====================
DrumScopeView::DrumScopeView(DrumAudioProcessor& p) : proc(p) { startTimerHz(30); }
void DrumScopeView::timerCallback() {
    // Silent output draws the same flat line every frame: skip the repaint until
    // audio returns. One extra paint after the transition draws the final flat line.
    std::array<float, 2048> buf;
    proc.readScope(buf.data(), (int)buf.size());
    float peak = 0;
    for (float v : buf) peak = std::max(peak, std::abs(v));
    bool active = peak > 1.0e-5f;
    if (active || wasActive_) repaint();
    wasActive_ = active;
}
void DrumScopeView::paint(juce::Graphics& g) {
    const int N = 2048;
    std::array<float, 2048> buf;
    proc.readScope(buf.data(), N);
    const float w = (float)getWidth(), h = (float)getHeight();
    int start = 0;
    for (int i = 1; i < N / 2; i++) if (buf[i - 1] <= 0 && buf[i] > 0) { start = i; break; }
    int M = std::min(900, N - start);
    juce::Path path;
    for (int i = 0; i < M; i++) {
        float x = (i / (float)(M - 1)) * w, y = h / 2 - buf[start + i] * h * 0.46f;
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.setColour(col::acA.withAlpha(0.95f));
    g.strokePath(path, juce::PathStrokeType(1.2f));
}

// ===================== BpmReadout =====================
BpmReadout::BpmReadout(DrumAudioProcessor& p) : proc(p) {
    param = dynamic_cast<juce::RangedAudioParameter*>(p.apvts.getParameter("seq.bpm"));
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    startTimerHz(10);
}
int BpmReadout::shown() const {
    if (proc.isHostSynced()) return (int)std::lround(proc.getHostBpm());
    return param ? (int)std::lround(param->convertFrom0to1(param->getValue())) : 0;
}
void BpmReadout::timerCallback() {
    const bool sync = proc.isHostSynced();
    const int v = shown();
    if (v != lastShown || sync != lastSync) {
        lastShown = v; lastSync = sync;
        setMouseCursor(sync ? juce::MouseCursor::NormalCursor
                            : juce::MouseCursor::UpDownResizeCursor);
        repaint();
    }
}
void BpmReadout::nudge(float d) {
    if (proc.isHostSynced() || !param) return; // host owns the tempo
    param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, param->getValue() + d));
    repaint();
}
void BpmReadout::mouseDown(const juce::MouseEvent& e) {
    if (proc.isHostSynced() || !param) return;
    param->beginChangeGesture();
    lastY = e.position.y;
}
void BpmReadout::mouseDrag(const juce::MouseEvent& e) {
    float dy = lastY - e.position.y;
    lastY = e.position.y;
    nudge(dy * (e.mods.isShiftDown() ? 0.0008f : 0.005f));
}
void BpmReadout::mouseUp(const juce::MouseEvent&) {
    if (proc.isHostSynced() || !param) return;
    param->endChangeGesture();
}
void BpmReadout::mouseDoubleClick(const juce::MouseEvent&) {
    if (proc.isHostSynced() || !param) return;
    param->setValueNotifyingHost(param->getDefaultValue());
}
void BpmReadout::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    nudge((w.deltaY > 0 ? 1.0f : -1.0f) / 140.0f); // one BPM per notch (range 60-200)
}
void BpmReadout::paint(juce::Graphics& g) {
    // .st-value well, cyan mono digits like the web tempo stepper.
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff0c0f16));
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
    g.setColour(proc.isHostSynced() ? col::acA : col::text);
    g.setFont(monoFont(11.0f));
    g.drawText(juce::String(shown()), getLocalBounds(), juce::Justification::centred);
}

// ===================== DrumHeader =====================
DrumHeader::DrumHeader(DrumAudioProcessor& p)
    : proc(p), scope(p), bpm(p),
      swing(p.apvts, "master.swing", Knob::Md, Accent::N),
      vol(p.apvts, "master.volume", Knob::Md, Accent::N) {
    auto styleBtn = [this](juce::TextButton& b, int dir) {
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
        b.setColour(juce::TextButton::textColourOffId, col::text);
        b.onClick = [this, dir] {
            const int n = proc.getNumPrograms();
            proc.setCurrentProgram(((proc.getCurrentProgram() + dir) % n + n) % n);
            repaint(kitNameArea);
        };
        addAndMakeVisible(b);
    };
    styleBtn(prevBtn, -1);
    styleBtn(nextBtn, +1);
    addAndMakeVisible(scope);
    addAndMakeVisible(bpm);
    addAndMakeVisible(swing);
    addAndMakeVisible(vol);
    startTimerHz(10);
}

void DrumHeader::timerCallback() {
    if (proc.getMidiActive() != lastMidi) { lastMidi = proc.getMidiActive(); repaint(midiArea); }
    if (proc.isHostSynced() != lastSync)  { lastSync = proc.isHostSynced();  repaint(syncArea); }
    if (proc.getCurrentProgram() != lastProgram) {
        lastProgram = proc.getCurrentProgram();
        repaint(kitNameArea);
    }
}

void DrumHeader::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());

    // brand: FABLE (white) SYNTH (cyan)  DR-1 (dim) — web .brand
    g.setFont(dispFont(17.0f));
    int bx = brandArea.getX(), by = brandArea.getY();
    g.setColour(col::text);
    drawSpaced(g, "FABLE", { bx, by, 70, brandArea.getHeight() }, 1.5f);
    int fableW = (int)g.getCurrentFont().getStringWidthFloat("FABLE") + 5 * 5;
    g.setColour(col::acA);
    drawSpaced(g, "SYNTH", { bx + fableW, by, 80, brandArea.getHeight() }, 1.5f);
    g.setColour(col::textDim);
    g.setFont(monoFont(9.0f));
    drawSpaced(g, "DR-1", { bx + fableW + 88, by + 4, 50, brandArea.getHeight() }, 2.0f);

    // kit name readout (.dr-kitname): dark well, cyan 10px, current program
    g.setColour(juce::Colour(0xff0c0f16));
    g.fillRoundedRectangle(kitNameArea.toFloat(), 6.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(kitNameArea.toFloat().reduced(0.5f), 6.0f, 1.0f);
    g.setColour(col::acA);
    g.setFont(monoFont(10.0f));
    drawSpaced(g, proc.getProgramName(proc.getCurrentProgram()),
               kitNameArea.reduced(10, 0), 0.8f, juce::Justification::centred);

    // scope well (.hud-cell) + caption
    drawDisplayBox(g, scopeBox.toFloat(), 8.0f);
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    drawSpaced(g, "SCOPE", scopeBox.reduced(6, 3).removeFromTop(10), 2.0f,
               juce::Justification::right);

    // MIDI activity LED (.dr-midi)
    auto midiRow = midiArea;
    auto led = midiRow.removeFromLeft(13).withSizeKeepingCentre(7, 7).toFloat();
    g.setColour(proc.getMidiActive() ? col::acA : juce::Colour(0xff232936));
    g.fillEllipse(led);
    if (proc.getMidiActive()) { g.setColour(col::acA.withAlpha(0.5f)); g.fillEllipse(led.expanded(2)); }
    g.setColour(col::textDim);
    g.setFont(monoFont(9.0f));
    g.drawText("MIDI", midiRow, juce::Justification::centredLeft);

    // SYNC tag under the BPM box — dim normally, lit while the host owns tempo
    g.setColour(proc.isHostSynced() ? col::acA : col::textDim);
    g.setFont(monoFont(7.0f));
    drawSpaced(g, "SYNC", syncArea, 1.6f, juce::Justification::horizontallyCentred);
}

void DrumHeader::resized() {
    // Measured web rects (viewport >= 1460): header content box 1424x80,
    // padding 7px 16px; items left-to-right with 12px gaps, HUD block pushed
    // right by margin-left:auto.
    auto r = getLocalBounds().reduced(16, 7);
    brandArea = r.removeFromLeft(228).withSizeKeepingCentre(228, 24);
    r.removeFromLeft(12);

    // kit stepper: prev(28) 6 name(128) 6 next(28) — SAVE dropped
    auto kit = r.removeFromLeft(196).withSizeKeepingCentre(196, 28);
    prevBtn.setBounds(kit.removeFromLeft(28));
    kit.removeFromLeft(6);
    nextBtn.setBounds(kit.removeFromRight(28));
    kit.removeFromRight(6);
    kitNameArea = kit;

    // right-aligned cluster: [scope 156x46] 12 [midi 42] 12 [tempo 86] 12 [knobs 117]
    auto master = r.removeFromRight(117).withSizeKeepingCentre(117, 64);
    swing.setBounds(master.removeFromLeft(56));
    vol.setBounds(master.removeFromRight(56));
    r.removeFromRight(12);
    auto tempo = r.removeFromRight(86).withSizeKeepingCentre(86, 40);
    bpm.setBounds(tempo.removeFromTop(26));
    tempo.removeFromTop(4);
    syncArea = tempo;
    r.removeFromRight(12);
    midiArea = r.removeFromRight(42).withSizeKeepingCentre(42, 17);
    r.removeFromRight(12);
    scopeBox = r.removeFromRight(156).withSizeKeepingCentre(156, 46);
    scope.setBounds(scopeBox.reduced(1));
}

} // namespace fui
