#include "BassHeader.h"
#include "../dsp/BassParams.h"

namespace fui {

// The shared fui controls look their metadata up in the WT-1 fable::paramInfo()
// table; install the BL-1 table as the primary resolver so ids like "osc.pos"
// / "flt.cut" resolve (and overlapping ids like "master.volume" get the BL-1
// labels). Runs at binary load, before any control can be constructed; never
// linked into WT-1/DR-1 binaries. The same initialiser re-themes accent A to
// the BL-1 acid green (the web's --ac-a: #4dff9e override in bass.css).
static const fable::ParamInfo* bassInfoLookup(const std::string& pid) {
    const int i = fable::bassIdFromString(pid);
    return i >= 0 ? &fable::bassParamInfo()[(size_t)i] : nullptr;
}
static const bool g_bassThemeInstalled = [] {
    setParamInfoResolver(&bassInfoLookup);
    accentA() = juce::Colour(0xff4dff9e);
    return true;
}();

// ===================== BassScopeView =====================
BassScopeView::BassScopeView(BassUiModel& p) : proc(p) { startTimerHz(30); }
void BassScopeView::timerCallback() {
    std::array<float, 2048> buf;
    proc.readScope(buf.data(), (int)buf.size());
    float peak = 0;
    for (float v : buf) peak = std::max(peak, std::abs(v));
    bool active = peak > 1.0e-5f;
    if (active || wasActive_) repaint();
    wasActive_ = active;
}
void BassScopeView::paint(juce::Graphics& g) {
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
    g.setColour(accentA().withAlpha(0.95f));
    g.strokePath(path, juce::PathStrokeType(1.2f));
}

// ===================== BassBpmReadout =====================
BassBpmReadout::BassBpmReadout(BassUiModel& p) : proc(p) {
    param = p.parameters().parameter("seq.bpm");
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    startTimerHz(10);
}
int BassBpmReadout::shown() const {
    if (proc.hostSynced()) return (int)std::lround(proc.hostBpm());
    return param ? (int)std::lround(param->convertFrom0to1(param->getValue())) : 0;
}
void BassBpmReadout::timerCallback() {
    const bool sync = proc.hostSynced();
    const int v = shown();
    if (v != lastShown || sync != lastSync) {
        lastShown = v; lastSync = sync;
        setMouseCursor(sync ? juce::MouseCursor::NormalCursor
                            : juce::MouseCursor::UpDownResizeCursor);
        repaint();
    }
}
void BassBpmReadout::nudge(float d) {
    if (proc.hostSynced() || !param) return; // host owns the tempo
    param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, param->getValue() + d));
    repaint();
}
void BassBpmReadout::mouseDown(const juce::MouseEvent& e) {
    if (proc.hostSynced() || !param) return;
    param->beginChangeGesture();
    lastY = e.position.y;
}
void BassBpmReadout::mouseDrag(const juce::MouseEvent& e) {
    float dy = lastY - e.position.y;
    lastY = e.position.y;
    nudge(dy * (e.mods.isShiftDown() ? 0.0008f : 0.005f));
}
void BassBpmReadout::mouseUp(const juce::MouseEvent&) {
    if (proc.hostSynced() || !param) return;
    param->endChangeGesture();
}
void BassBpmReadout::mouseDoubleClick(const juce::MouseEvent&) {
    if (proc.hostSynced() || !param) return;
    param->setValueNotifyingHost(param->getDefaultValue());
}
void BassBpmReadout::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    nudge((w.deltaY > 0 ? 1.0f : -1.0f) / 140.0f); // one BPM per notch (range 60-200)
}
void BassBpmReadout::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff0c0f16));
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
    g.setColour(accentA());
    g.setFont(monoFont(11.0f));
    g.drawText(juce::String(shown()), getLocalBounds(), juce::Justification::centred);
}

// ===================== BassHeader =====================
BassHeader::BassHeader(BassUiModel& p)
    : proc(p), scope(p), bpm(p),
      swing(p.parameters(), "master.swing", Knob::Md, Accent::N),
      vol(p.parameters(), "master.volume", Knob::Md, Accent::N) {
    auto styleBtn = [this](juce::TextButton& b, int dir) {
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
        b.setColour(juce::TextButton::textColourOffId, col::text);
        b.onClick = [this, dir] {
            const int n = proc.numPrograms();
            proc.selectProgram(((proc.currentProgram() + dir) % n + n) % n);
            repaint(patchNameArea);
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

void BassHeader::timerCallback() {
    if (proc.midiActive() != lastMidi) { lastMidi = proc.midiActive(); repaint(midiArea); }
    if (proc.hostSynced() != lastSync)  { lastSync = proc.hostSynced();  repaint(syncArea); }
    if (proc.currentProgram() != lastProgram) {
        lastProgram = proc.currentProgram();
        repaint(patchNameArea);
    }
}

void BassHeader::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());

    // brand: FABLE (white) SYNTH (green)  BL-1 (dim) — web .brand
    g.setFont(dispFont(17.0f));
    int bx = brandArea.getX(), by = brandArea.getY();
    g.setColour(col::text);
    drawSpaced(g, "FABLE", { bx, by, 70, brandArea.getHeight() }, 1.5f);
    int fableW = (int)g.getCurrentFont().getStringWidthFloat("FABLE") + 5 * 5;
    g.setColour(accentA());
    drawSpaced(g, "SYNTH", { bx + fableW, by, 80, brandArea.getHeight() }, 1.5f);
    g.setColour(col::textDim);
    g.setFont(monoFont(9.0f));
    drawSpaced(g, "BL-1", { bx + fableW + 88, by + 4, 50, brandArea.getHeight() }, 2.0f);

    // "PATCH" mini head left of the stepper (web .bl-patch-label)
    g.setColour(col::textDim);
    g.setFont(dispFont(8.0f));
    drawSpaced(g, "PATCH",
               { prevBtn.getX() - 46, patchNameArea.getY(), 42, patchNameArea.getHeight() },
               1.3f, juce::Justification::centredRight);

    // patch name readout (.bl-patchname): dark well, green 10px, current program
    g.setColour(juce::Colour(0xff0c0f16));
    g.fillRoundedRectangle(patchNameArea.toFloat(), 6.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(patchNameArea.toFloat().reduced(0.5f), 6.0f, 1.0f);
    g.setColour(accentA());
    g.setFont(monoFont(10.0f));
    drawSpaced(g, proc.programName(proc.currentProgram()),
               patchNameArea.reduced(10, 0), 0.8f, juce::Justification::centred);

    // voice-mode hint (.bl-voicemode), two dim lines
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    auto vm = voiceModeArea;
    // web "MONO · LAST-NOTE" — ASCII middle dot substitute for the mono font
    drawSpaced(g, "MONO - LAST-NOTE", vm.removeFromTop(vm.getHeight() / 2), 1.4f);
    drawSpaced(g, "SLIDES RIDE THE ENVS", vm, 1.4f);

    // scope well (.hud-cell) + caption
    drawDisplayBox(g, scopeBox.toFloat(), 8.0f);
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    drawSpaced(g, "SCOPE", scopeBox.reduced(6, 3).removeFromTop(10), 2.0f,
               juce::Justification::right);

    // MIDI activity LED (.bl-midi)
    auto midiRow = midiArea;
    auto led = midiRow.removeFromLeft(13).withSizeKeepingCentre(7, 7).toFloat();
    g.setColour(proc.midiActive() ? accentA() : juce::Colour(0xff232936));
    g.fillEllipse(led);
    if (proc.midiActive()) { g.setColour(accentA().withAlpha(0.5f)); g.fillEllipse(led.expanded(2)); }
    g.setColour(col::textDim);
    g.setFont(monoFont(9.0f));
    g.drawText("MIDI", midiRow, juce::Justification::centredLeft);

    // SYNC tag under the BPM box — dim normally, lit while the host owns tempo
    g.setColour(proc.hostSynced() ? accentA() : col::textDim);
    g.setFont(monoFont(7.0f));
    drawSpaced(g, "SYNC", syncArea, 1.6f, juce::Justification::horizontallyCentred);
}

void BassHeader::resized() {
    // Web .bl-header: content box 1424x80, padding 7px 16px; items left-to-
    // right with 14px gaps, HUD block pushed right by margin-left:auto.
    auto r = getLocalBounds().reduced(16, 7);
    brandArea = r.removeFromLeft(224).withSizeKeepingCentre(224, 24);
    r.removeFromLeft(14 + 46);   // gap + "PATCH" label drawn in paint()

    // patch stepper: prev(28) 6 name(128) 6 next(28)
    auto pb = r.removeFromLeft(196).withSizeKeepingCentre(196, 28);
    prevBtn.setBounds(pb.removeFromLeft(28));
    pb.removeFromLeft(6);
    nextBtn.setBounds(pb.removeFromRight(28));
    pb.removeFromRight(6);
    patchNameArea = pb;

    r.removeFromLeft(14);
    voiceModeArea = r.removeFromLeft(140).withSizeKeepingCentre(140, 24);

    // right-aligned cluster: [scope 168x46] 12 [midi 42] 12 [tempo 86] 12 [knobs 117]
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
    scopeBox = r.removeFromRight(168).withSizeKeepingCentre(168, 46);
    scope.setBounds(scopeBox.reduced(1));
}

} // namespace fui
