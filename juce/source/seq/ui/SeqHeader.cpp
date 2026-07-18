#include "SeqHeader.h"
#include "../../ui/Controls.h"
#include "../dsp/SeqFactory.h"

#include <array>
#include <cmath>

namespace fui {

// The SQ-4 host-param resolver for fui controls (Task 9's SeqEditor.cpp
// installed this as a placeholder; SeqHeader now owns the header, so it owns
// the install too) — same pattern as bassInfoLookup/drumInfoLookup.
namespace {
const fable::ParamInfo* seqInfoLookup(const std::string& pid) {
    for (const auto& d : fable::seqParamInfo())
        if (d.pid == pid) return &d;
    return nullptr;
}
const bool g_seqResolverInstalled = [] {
    setParamInfoResolver(&seqInfoLookup);
    return true;
}();
} // namespace

SeqHeader::SeqHeader(SeqAudioProcessor& p) : proc(p) {
    juce::String lastFamily;
    const auto& sessions = fable::factorySessionLibrary();
    for (int i = 0; i < proc.getNumPrograms(); ++i) {
        const juce::String family(sessions[(size_t)i].family);
        if (family != lastFamily) {
            library_.addSectionHeading(family);
            lastFamily = family;
        }
        library_.addItem(proc.getProgramName(i), i + 1);
    }
    library_.addItem("CUSTOM", proc.getNumPrograms() + 1);
    library_.setItemEnabled(proc.getNumPrograms() + 1, false);
    library_.onChange = [this] {
        const int index = library_.getSelectedId() - 1;
        if (index >= 0 && index < proc.getNumPrograms()) selectLibrarySession(index);
        shownLibrarySession_ = -2;
        refreshLibrarySelection();
    };
    addAndMakeVisible(library_);
    refreshLibrarySelection();
    startTimerHz(30);
}

// ---- actions (also the test handles) ---------------------------------------

void SeqHeader::playClick() {
    auto& conductor = proc.conductor();
    if (conductor.playing()) conductor.stopTransport();
    else conductor.startTransport();
    repaint();
}

void SeqHeader::loadClick() {
    chooser_ = std::make_unique<juce::FileChooser>("Load session", juce::File{}, "*.json");
    auto fcFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    // SafePointer guards against the header being torn down while the OS
    // dialog is open (same pattern as WavetableEditor::chooseFile).
    juce::Component::SafePointer<SeqHeader> safe(this);
    chooser_->launchAsync(fcFlags, [safe](const juce::FileChooser& fc) {
        if (safe == nullptr) return;
        auto* self = safe.getComponent();
        auto file = fc.getResult();
        if (file == juce::File{}) return;
        if (self->proc.applySessionJson(file.loadFileAsString())
            && self->onLibrarySessionChanged)
            self->onLibrarySessionChanged();
        self->shownLibrarySession_ = -2;
        self->refreshLibrarySelection();
        self->repaint();
    });
}

void SeqHeader::saveClick() {
    chooser_ = std::make_unique<juce::FileChooser>("Save session", juce::File{}, "*.json");
    auto fcFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting;
    juce::Component::SafePointer<SeqHeader> safe(this);
    chooser_->launchAsync(fcFlags, [safe](const juce::FileChooser& fc) {
        if (safe == nullptr) return;
        auto* self = safe.getComponent();
        auto file = fc.getResult();
        if (file == juce::File{}) return;
        file.replaceWithText(self->proc.currentSessionJson());
    });
}

void SeqHeader::selectLibrarySession(int index) {
    proc.applySessionPreset(index);
    if (onLibrarySessionChanged) onLibrarySessionChanged();
    shownLibrarySession_ = -2;
    refreshLibrarySelection();
    repaint();
}

void SeqHeader::refreshLibrarySelection() {
    const int session = proc.currentSessionPreset();
    if (shownLibrarySession_ == session) return;
    const bool changedExternally = shownLibrarySession_ != -2;
    library_.setSelectedId(session >= 0 ? session + 1 : proc.getNumPrograms() + 1,
                           juce::dontSendNotification);
    shownLibrarySession_ = session;
    // Host program changes and per-track patch steppers do not pass through
    // selectLibrarySession(). Keep any open native device bank aligned with the
    // session before its next edit can snapshot stale parameter values.
    // Ordinary clip/patch edits move a factory session to CUSTOM. They already
    // originated in the focused model, so reloading it here would overwrite
    // transient UI state (notably DR-1's selected pad patch). A non-custom
    // session appearing externally is a real recall and does need a reload.
    if (changedExternally && session >= 0 && onLibrarySessionChanged)
        onLibrarySessionChanged();
}

void SeqHeader::timerCallback() {
    refreshLibrarySelection();
    repaint();
}

void SeqHeader::quantStep(int d) {
    proc.conductor().cycleQuant(d);
    // Mirror the new value into the "quant" APVTS param so hosts can see it.
    // v1 stays one-directional (header -> conductor -> APVTS): host-driven
    // quant automation is not wired back into the conductor (Task 10 brief).
    if (auto* param = proc.apvts.getParameter("quant")) {
        const int idx = (int)proc.conductor().quant();
        param->setValueNotifyingHost(param->convertTo0to1((float)idx));
    }
    repaint();
}

// ---- value sources -----------------------------------------------------------

float SeqHeader::swingValue() const { return (float)proc.conductor().swing(); }

juce::RangedAudioParameter* SeqHeader::volParam() const {
    return dynamic_cast<juce::RangedAudioParameter*>(proc.apvts.getParameter("master"));
}
float SeqHeader::volValue() const {
    auto* p = volParam();
    return p ? p->getValue() : 0.75f;
}

// ---- mouse -------------------------------------------------------------------

void SeqHeader::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    if (playBtn.contains(pos))            playClick();
    else if (quantPrevBtn.contains(pos))  quantStep(-1);
    else if (quantNextBtn.contains(pos))  quantStep(+1);
    else if (loadBtn.contains(pos))       loadClick();
    else if (saveBtn.contains(pos))       saveClick();
    else if (swingKnob.contains(pos))     { dragging_ = Drag::Swing; lastY_ = e.position.y; }
    else if (volKnob.contains(pos)) {
        dragging_ = Drag::Vol;
        lastY_ = e.position.y;
        if (auto* p = volParam()) p->beginChangeGesture();
    }
}

void SeqHeader::mouseDrag(const juce::MouseEvent& e) {
    if (dragging_ == Drag::None) return;
    const float dy = lastY_ - e.position.y;
    lastY_ = e.position.y;
    const float delta = dy * (e.mods.isShiftDown() ? 0.0008f : 0.005f);
    if (dragging_ == Drag::Swing) {
        proc.conductor().setSwing(juce::jlimit(0.0f, 1.0f, swingValue() + delta));
    } else if (dragging_ == Drag::Vol) {
        if (auto* p = volParam())
            p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, p->getValue() + delta));
    }
    repaint();
}

void SeqHeader::mouseUp(const juce::MouseEvent&) {
    if (dragging_ == Drag::Vol) { if (auto* p = volParam()) p->endChangeGesture(); }
    dragging_ = Drag::None;
}

void SeqHeader::mouseDoubleClick(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    if (swingKnob.contains(pos)) {
        proc.conductor().setSwing(0.0);
    } else if (volKnob.contains(pos)) {
        if (auto* p = volParam()) {
            // JUCE delivers a double-click as mouseDown/mouseUp/mouseDown/
            // mouseDoubleClick/mouseUp: the second mouseDown already opened a
            // change gesture (dragging_ == Drag::Vol) that the trailing
            // mouseUp will close, so don't nest a second begin/end pair here
            // -- just set the value. Only wrap our own pair if a gesture
            // somehow isn't already open (e.g. this fired without the usual
            // mouseDown, in a test).
            const bool gestureOpen = dragging_ == Drag::Vol;
            if (!gestureOpen) p->beginChangeGesture();
            p->setValueNotifyingHost(p->getDefaultValue());
            if (!gestureOpen) p->endChangeGesture();
        }
    }
    repaint();
}

// ---- layout --------------------------------------------------------------

void SeqHeader::resized() {
    auto r = getLocalBounds().reduced(16, 8); // content row, ~1392 x 50

    logoArea = r.removeFromLeft(190);
    r.removeFromLeft(20);

    playBtn = r.removeFromLeft(62).withSizeKeepingCentre(62, 32);
    r.removeFromLeft(20);

    quantTagArea = r.removeFromLeft(40).withSizeKeepingCentre(40, 16);
    r.removeFromLeft(4);
    quantPrevBtn = r.removeFromLeft(18).withSizeKeepingCentre(18, 18);
    r.removeFromLeft(4);
    quantValArea = r.removeFromLeft(56).withSizeKeepingCentre(56, 20);
    r.removeFromLeft(4);
    quantNextBtn = r.removeFromLeft(18).withSizeKeepingCentre(18, 18);
    r.removeFromLeft(20);

    auto clockArea = r.removeFromLeft(130);
    beatsArea = clockArea.removeFromTop(clockArea.getHeight() / 2);
    clockLineArea = clockArea;

    auto knobsArea = r.removeFromRight(90).withSizeKeepingCentre(90, 44);
    swingKnob = knobsArea.removeFromLeft(45).withSizeKeepingCentre(40, 44);
    volKnob = knobsArea.withSizeKeepingCentre(40, 44);
    r.removeFromRight(14);
    scopeArea = r.removeFromRight(190).withSizeKeepingCentre(190, 46);
    r.removeFromRight(14);

    // LOAD/SAVE (JUCE-only surface, no web equivalent) sit right of the clock,
    // in the same flexible middle gap the clock area leaves before the scope.
    saveBtn = r.removeFromRight(48).withSizeKeepingCentre(48, 26);
    r.removeFromRight(6);
    loadBtn = r.removeFromRight(48).withSizeKeepingCentre(48, 26);

    r.removeFromRight(14);
    auto libraryArea = r.removeFromRight(224).withSizeKeepingCentre(224, 28);
    libraryLabelArea = libraryArea.removeFromLeft(54);
    libraryArea.removeFromLeft(6);
    library_.setBounds(libraryArea);
}

// ---- paint -----------------------------------------------------------------

void SeqHeader::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());

    // logo: FABLE (white) SEQ (amber) SQ-4 (dim tag) -- web .sq-logo
    auto lb = logoArea;
    g.setFont(dispFont(15.0f));
    g.setColour(col::text);
    drawSpaced(g, "FABLE", lb.removeFromLeft(60), 1.2f);
    g.setColour(col::acB);
    drawSpaced(g, "SEQ", lb.removeFromLeft(46), 1.2f);
    lb.removeFromLeft(10);
    g.setColour(col::textDim);
    g.setFont(monoFont(9.0f));
    drawSpaced(g, "SQ-4", lb, 2.5f);

    paintButtons(g);
    paintQuant(g);
    paintClock(g);
    paintScope(g);

    g.setColour(col::textDim);
    g.setFont(monoFont(8.0f));
    drawSpaced(g, "LIBRARY", libraryLabelArea, 1.2f, juce::Justification::centredRight);

    paintKnob(g, swingKnob, "SWING", swingValue());
    paintKnob(g, volKnob, "VOL", volValue());
}

void SeqHeader::paintButtons(juce::Graphics& g) {
    const bool playing = proc.conductor().playing();
    auto drawBtn = [&](juce::Rectangle<int> r, const juce::String& txt, bool on) {
        auto rf = r.toFloat();
        g.setColour(on ? juce::Colour(0xff0e3120) : juce::Colour(0xff11141c));
        g.fillRoundedRectangle(rf, 7.0f);
        g.setColour(on ? accentA().withAlpha(0.55f) : col::line);
        g.drawRoundedRectangle(rf.reduced(0.5f), 7.0f, 1.0f);
        g.setColour(on ? accentA() : col::text);
        g.setFont(monoFont(10.0f));
        g.drawText(txt, r, juce::Justification::centred);
    };
    // drawBtn keeps drawing text for other buttons; the transport gets an icon.
    drawBtn(playBtn, "", playing);
    {
        auto ir = playBtn.toFloat().withSizeKeepingCentre(11.0f, 11.0f);
        g.setColour(playing ? accentA() : col::text); // same fg colours drawBtn used for its text
        g.fillPath(playing ? iconStop(ir) : iconPlay(ir.reduced(1.0f, 0.0f)));
    }
    drawBtn(loadBtn, "LOAD", false);
    drawBtn(saveBtn, "SAVE", false);
}

void SeqHeader::paintQuant(juce::Graphics& g) {
    g.setColour(col::textDim);
    g.setFont(monoFont(8.0f));
    drawSpaced(g, "QUANT", quantTagArea, 1.6f);

    auto drawStep = [&](juce::Rectangle<int> r, bool pointsRight) {
        g.setColour(juce::Colour(0xff11141c));
        g.fillRoundedRectangle(r.toFloat(), 4.0f);
        g.setColour(col::line);
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 4.0f, 1.0f);
        g.setColour(col::textDim);
        g.strokePath(iconChevron(r.toFloat().withSizeKeepingCentre(5.0f, 9.0f), pointsRight),
                     juce::PathStrokeType(1.6f));
    };
    drawStep(quantPrevBtn, false);
    drawStep(quantNextBtn, true);

    const auto label = quantLabel();
    g.setColour(juce::Colour(0xff0a0d13));
    g.fillRoundedRectangle(quantValArea.toFloat(), 4.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(quantValArea.toFloat().reduced(0.5f), 4.0f, 1.0f);
    g.setColour(col::acB);
    g.setFont(monoFont(10.0f));
    g.drawText(label, quantValArea, juce::Justification::centred);
}

juce::String SeqHeader::quantLabel() const {
    const auto q = proc.conductor().quant();
    return q == fable::Quant::Bar ? "1 BAR" : q == fable::Quant::Quarter ? "1/4" : "OFF";
}

void SeqHeader::paintClock(juce::Graphics& g) {
    const bool playing = proc.conductor().playing();
    const auto pos = proc.conductor().songPos();

    auto r = beatsArea;
    const int dotSize = 8, gap = 5;
    for (int i = 0; i < 4; ++i) {
        auto d = r.removeFromLeft(dotSize).withSizeKeepingCentre(dotSize, dotSize);
        r.removeFromLeft(gap);
        const bool on = playing && pos.beat == i;
        g.setColour(on ? col::acB : juce::Colour(0xff232936));
        g.fillEllipse(d.toFloat());
        if (on) { g.setColour(col::acB.withAlpha(0.5f)); g.fillEllipse(d.toFloat().expanded(2.0f)); }
    }

    g.setColour(col::textDim);
    g.setFont(monoFont(9.0f));
    juce::String bar = "BAR " + juce::String(pos.bar).paddedLeft('0', 2);
    juce::String bpm = juce::String((int)std::lround(proc.conductor().session().bpm)) + " BPM";
    g.drawText(bar + juce::String::fromUTF8(" \xc2\xb7 ") + bpm, clockLineArea, juce::Justification::centredLeft);
}

void SeqHeader::paintScope(juce::Graphics& g) {
    drawDisplayBox(g, scopeArea.toFloat());

    constexpr int N = 512;
    std::array<float, N> buf;
    proc.readScope(buf.data(), N);

    const float w = (float)scopeArea.getWidth(), h = (float)scopeArea.getHeight();
    const float x0 = (float)scopeArea.getX(), y0 = (float)scopeArea.getY();
    juce::Path path;
    for (int i = 0; i < N; ++i) {
        const float x = x0 + (static_cast<float>(i) / static_cast<float>(N - 1)) * w;
        const float y = y0 + h * 0.5f - buf[(size_t)i] * h * 0.46f;
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.setColour(accentA().withAlpha(0.9f));
    g.strokePath(path, juce::PathStrokeType(1.2f));

    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    drawSpaced(g, "SUM", scopeArea.reduced(6, 3).removeFromTop(10), 2.0f,
               juce::Justification::right);
}

void SeqHeader::paintKnob(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label, float v) {
    const float d = 22.0f;
    auto circle = juce::Rectangle<float>(0, 0, d, d).withCentre(
        juce::Point<float>((float)r.getCentreX(), (float)r.getY() + d * 0.5f));

    const float a0 = -135.0f, a1 = 135.0f;
    const float deg = a0 + (a1 - a0) * juce::jlimit(0.0f, 1.0f, v);
    // JUCE angles are clockwise from 12 o'clock, which already puts -135/+135
    // at the lower-left/lower-right "knob rest" positions -- no extra offset.
    auto toRad = [](float degv) { return juce::degreesToRadians(degv); };

    g.setColour(juce::Colour(0xff161b25));
    g.fillEllipse(circle);
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawEllipse(circle, 1.5f);

    const float rr = d * 0.5f - 2.0f;
    const juce::Point<float> c = circle.getCentre();
    juce::Path track, arc;
    track.addCentredArc(c.x, c.y, rr, rr, 0.0f, toRad(a0), toRad(a1), true);
    arc.addCentredArc(c.x, c.y, rr, rr, 0.0f, toRad(a0), toRad(deg), true);
    g.setColour(juce::Colours::white.withAlpha(0.09f));
    g.strokePath(track, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(accentA());
    g.strokePath(arc, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Point<float> tip = c.getPointOnCircumference(rr, toRad(deg));
    g.setColour(col::ptr);
    g.drawLine({ c, tip }, 1.6f);

    auto labelArea = r.withY(r.getY() + (int)d + 2).withHeight(10);
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    g.drawText(label, labelArea, juce::Justification::centred);
}

} // namespace fui
