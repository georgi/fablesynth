#include "TrackHeadsView.h"
#include "../../ui/Controls.h"
#include "../../drum/dsp/DrumKits.h"
#include "../../bass/dsp/BassPatches.h"
#include "../../dsp/Presets.h"

namespace fui {

// ---- factory patch name/count tables, port of devices.ts:138-145 ----------
namespace {

int factoryPatchCount(fable::Machine m) {
    switch (m) {
        case fable::Machine::DR1: return (int)fable::factoryKits().size();
        case fable::Machine::BL1: return (int)fable::bassFactoryPatches().size();
        case fable::Machine::WT1: return (int)fable::factoryPresets().size();
    }
    return (int)fable::factoryPresets().size();
}

juce::String patchName(fable::Machine m, const fable::PatchRef& patch) {
    if (!patch.factory) return "CUSTOM";
    switch (m) {
        case fable::Machine::DR1: {
            const auto& v = fable::factoryKits();
            return (patch.index >= 0 && patch.index < (int)v.size())
                       ? juce::String(v[(size_t)patch.index].name) : "KIT ?";
        }
        case fable::Machine::BL1: {
            const auto& v = fable::bassFactoryPatches();
            return (patch.index >= 0 && patch.index < (int)v.size())
                       ? juce::String(v[(size_t)patch.index].name) : "PATCH ?";
        }
        case fable::Machine::WT1: {
            const auto& v = fable::factoryPresets();
            return (patch.index >= 0 && patch.index < (int)v.size())
                       ? juce::String(v[(size_t)patch.index].name) : "PRESET ?";
        }
    }
    return "PRESET ?";
}

const char* machineChip(fable::Machine m) {
    switch (m) { case fable::Machine::DR1: return "DR-1"; case fable::Machine::BL1: return "BL-1"; case fable::Machine::WT1: return "WT-1"; }
    return "WT-1";
}

} // namespace

TrackHeadsView::TrackHeadsView(SeqAudioProcessor& p) : proc(p) { startTimerHz(30); }

// ---- actions (also the test handles) ---------------------------------------

void TrackHeadsView::muteClick(int t) { proc.conductor().toggleTrackMute(t); repaint(); }
void TrackHeadsView::soloClick(int t) { proc.conductor().toggleSolo(t); repaint(); }

void TrackHeadsView::headClick(int t) {
    if (onFocusTrack) onFocusTrack(t);
}

void TrackHeadsView::patchStep(int t, int d) {
    const auto& tracks = proc.conductor().session().tracks;
    if (t < 0 || t >= (int)tracks.size()) return;
    const auto& tr = tracks[(size_t)t];
    const int count = factoryPatchCount(tr.machine);
    if (count <= 0) return;
    const int base = tr.patch.factory ? tr.patch.index : 0;
    const int next = ((base + d) % count + count) % count;
    proc.conductor().setTrackPatch(t, fable::PatchRef { true, next, {} });
    proc.applyTrackPatch(t);
    repaint();
}

// ---- value sources -----------------------------------------------------------

juce::RangedAudioParameter* TrackHeadsView::volParam(int t) const {
    return dynamic_cast<juce::RangedAudioParameter*>(proc.apvts.getParameter("vol" + juce::String(t)));
}
float TrackHeadsView::volValue(int t) const {
    auto* p = volParam(t);
    return p ? p->getValue() : 0.75f;
}

// ---- mouse -------------------------------------------------------------------

void TrackHeadsView::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    if (sceneCard.contains(pos)) return; // scene card is a static label, no longer a back button
    for (int t = 0; t < 4; ++t) {
        if (muteBtn[t].contains(pos))       { muteClick(t); return; }
        if (soloBtn[t].contains(pos))       { soloClick(t); return; }
        if (patchPrev[t].contains(pos))     { patchStep(t, -1); return; }
        if (patchNext[t].contains(pos))     { patchStep(t, +1); return; }
        if (volKnob[t].contains(pos)) {
            dragging_ = Drag::Vol;
            dragTrack_ = t;
            lastY_ = e.position.y;
            if (auto* p = volParam(t)) p->beginChangeGesture();
            return;
        }
        if (nameRow[t].contains(pos))       { headClick(t); return; }
    }
}

void TrackHeadsView::mouseDrag(const juce::MouseEvent& e) {
    if (dragging_ != Drag::Vol || dragTrack_ < 0) return;
    const float dy = lastY_ - e.position.y;
    lastY_ = e.position.y;
    const float delta = dy * (e.mods.isShiftDown() ? 0.0008f : 0.005f);
    if (auto* p = volParam(dragTrack_))
        p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, p->getValue() + delta));
    repaint();
}

void TrackHeadsView::mouseUp(const juce::MouseEvent&) {
    if (dragging_ == Drag::Vol && dragTrack_ >= 0) {
        if (auto* p = volParam(dragTrack_)) p->endChangeGesture();
    }
    dragging_ = Drag::None;
    dragTrack_ = -1;
}

void TrackHeadsView::mouseMove(const juce::MouseEvent& e) {
    // Track which head's name row the pointer is over so paintTrack can light
    // the edit (✎) affordance — the whole head is clickable to focus the
    // device, but nothing signalled that until now (web parity:
    // .sq-track-editglyph, hover-revealed).
    const auto pos = e.getPosition();
    int hit = -1;
    for (int t = 0; t < 4; ++t)
        if (nameRow[t].contains(pos)) { hit = t; break; }
    if (hit != hoverTrack_) { hoverTrack_ = hit; repaint(); }
}

void TrackHeadsView::mouseExit(const juce::MouseEvent&) {
    if (hoverTrack_ != -1) { hoverTrack_ = -1; repaint(); }
}

void TrackHeadsView::mouseDoubleClick(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    for (int t = 0; t < 4; ++t) {
        if (!volKnob[t].contains(pos)) continue;
        if (auto* p = volParam(t)) {
            // Same double-click-during-drag guard as SeqHeader's VOL knob:
            // the second mouseDown of the double-click already opened a
            // gesture that the trailing mouseUp will close.
            const bool gestureOpen = dragging_ == Drag::Vol && dragTrack_ == t;
            if (!gestureOpen) p->beginChangeGesture();
            p->setValueNotifyingHost(p->getDefaultValue());
            if (!gestureOpen) p->endChangeGesture();
        }
        repaint();
        return;
    }
}

// ---- layout --------------------------------------------------------------
// Component-local columns matching the rack grid table (Task 9): scene card
// x=0 w=218, track head i at x=218+9+i*(292+9) w=292 (i=0..3).

void TrackHeadsView::resized() {
    const int h = getHeight();
    sceneCard = { 0, 0, 218, h };

    for (int t = 0; t < 4; ++t) {
        auto r = juce::Rectangle<int>(218 + 9 + t * (292 + 9), 0, 292, h);
        trackHead[t] = r;

        auto content = r.reduced(10, 6); // css padding: 6px 10px
        led[t] = content.removeFromLeft(9).withSizeKeepingCentre(9, 9);
        content.removeFromLeft(8);

        // Web flex order is LED, name, M, S, knob (TrackHeads.tsx) — knob is
        // rightmost, so it comes off `content`'s right edge first.
        volKnob[t] = content.removeFromRight(34).withSizeKeepingCentre(34, 34);
        content.removeFromRight(8);
        soloBtn[t] = content.removeFromRight(22).withSizeKeepingCentre(22, 22);
        content.removeFromRight(8);
        muteBtn[t] = content.removeFromRight(22).withSizeKeepingCentre(22, 22);
        content.removeFromRight(8);

        // remaining `content` is the track-id column: name row on top, patch
        // stepper row below (css sq-track-name-row / sq-track-patch).
        nameRow[t] = content.removeFromTop(content.getHeight() / 2);
        auto patchRow = content;
        patchPrev[t] = patchRow.removeFromLeft(22).withSizeKeepingCentre(22, 22);
        patchNext[t] = patchRow.removeFromRight(22).withSizeKeepingCentre(22, 22);
    }
}

// ---- paint -----------------------------------------------------------------

void TrackHeadsView::paint(juce::Graphics& g) {
    paintScenesCard(g);
    for (int t = 0; t < 4; ++t) paintTrack(g, t);
}

void TrackHeadsView::paintScenesCard(juce::Graphics& g) {
    drawPanel(g, sceneCard.toFloat(), 10.0f);

    auto r = sceneCard.reduced(12, 8);
    auto titleArea = r.removeFromTop(14);
    g.setColour(col::text);
    g.setFont(dispFont(10.0f));
    drawSpaced(g, "SCENES", titleArea, 2.4f);
    g.setColour(col::textHint);
    g.setFont(monoFont(7.0f));
    const int scenes = (int)proc.conductor().session().scenes.size();
    const int tracks = (int)proc.conductor().session().tracks.size();
    drawSpaced(g, juce::String(scenes) + " SCENES - " + juce::String(tracks) + " TRACKS",
               r.removeFromTop(10), 1.6f);
}

void TrackHeadsView::paintTrack(juce::Graphics& g, int t) {
    const auto& tracks = proc.conductor().session().tracks;
    if (t >= (int)tracks.size()) return;
    const auto& tr = tracks[(size_t)t];
    const juce::Colour tc { tr.color };

    auto rf = trackHead[t].toFloat();
    g.setGradientFill(juce::ColourGradient(col::panelHi, rf.getX(), rf.getY(),
                                           col::panelLo, rf.getX(), rf.getBottom(), false));
    g.fillRoundedRectangle(rf, 10.0f);
    g.setColour(col::line);
    g.drawRoundedRectangle(rf.reduced(0.5f), 10.0f, 1.0f);

    // LED
    const bool audible = proc.conductor().ownerOf(t) != -2;
    g.setColour(audible ? tc : juce::Colour(0xff232936));
    g.fillEllipse(led[t].toFloat());
    if (audible) { g.setColour(tc.withAlpha(0.5f)); g.fillEllipse(led[t].toFloat().expanded(2.5f)); }

    // name row: name + (hover) edit glyph + machine chip
    auto nr = nameRow[t];
    // A hovered head lights a faint track-tinted background and an edit (✎)
    // glyph so it's clear the whole head opens the device editor.
    const bool hovered = hoverTrack_ == t;
    if (hovered) {
        g.setColour(tc.withAlpha(0.10f));
        g.fillRoundedRectangle(nameRow[t].toFloat().expanded(3.0f, 2.0f), 6.0f);
    }
    auto nameArea = nr.removeFromLeft(nr.getWidth() * 3 / 5);
    auto editSlot = nameArea.removeFromRight(12);
    g.setColour(col::text);
    g.setFont(dispFont(10.0f));
    drawSpaced(g, juce::String(tr.name), nameArea, 1.6f);
    if (hovered) {
        g.setColour(tc);
        g.fillPath(iconPencil(editSlot.toFloat().withSizeKeepingCentre(9.0f, 9.0f)));
    }

    auto chip = nr.withSizeKeepingCentre(nr.getWidth(), 12);
    g.setColour(tc.withAlpha(0.33f));
    g.drawRoundedRectangle(chip.toFloat().reduced(0.5f), 3.0f, 1.0f);
    g.setColour(tc);
    g.setFont(monoFont(7.0f));
    g.drawText(machineChip(tr.machine), chip, juce::Justification::centred);

    // patch stepper: <  name  >
    auto drawArrow = [&](juce::Rectangle<int> r, bool pointsRight) {
        g.setColour(col::textDim);
        g.strokePath(iconChevron(r.toFloat().withSizeKeepingCentre(5.0f, 9.0f), pointsRight),
                     juce::PathStrokeType(1.6f));
    };
    drawArrow(patchPrev[t], false);
    drawArrow(patchNext[t], true);
    auto patchTextArea = juce::Rectangle<int>(patchPrev[t].getRight(), patchPrev[t].getY(),
                                               patchNext[t].getX() - patchPrev[t].getRight(),
                                               patchPrev[t].getHeight());
    g.setColour(col::textDim);
    g.setFont(monoFont(7.5f));
    g.drawText(patchName(tr.machine, tr.patch), patchTextArea, juce::Justification::centred);

    // M / S
    auto drawToggle = [&](juce::Rectangle<int> r, const char* txt, bool on, juce::Colour onColour) {
        auto rf2 = r.toFloat();
        g.setColour(on ? onColour : juce::Colour(0xff11141c));
        g.fillRoundedRectangle(rf2, 5.0f);
        g.setColour(on ? onColour : col::line);
        g.drawRoundedRectangle(rf2.reduced(0.5f), 5.0f, 1.0f);
        g.setColour(on ? col::bg : col::textDim);
        g.setFont(monoFont(9.0f, true));
        g.drawText(txt, r, juce::Justification::centred);
    };
    drawToggle(muteBtn[t], "M", proc.conductor().trackMuted(t), col::acB);
    drawToggle(soloBtn[t], "S", proc.conductor().soloed(t), col::acA);

    paintKnob(g, volKnob[t], volValue(t));
}

void TrackHeadsView::paintKnob(juce::Graphics& g, juce::Rectangle<int> r, float v) {
    const float d = 20.0f;
    auto circle = juce::Rectangle<float>(0, 0, d, d).withCentre(r.toFloat().getCentre());

    const float a0 = -135.0f, a1 = 135.0f;
    const float deg = a0 + (a1 - a0) * juce::jlimit(0.0f, 1.0f, v);
    auto toRad = [](float degv) { return juce::degreesToRadians(degv); };

    g.setColour(col::knobBody);
    g.fillEllipse(circle);
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawEllipse(circle, 1.2f);

    const float rr = d * 0.5f - 2.0f;
    const juce::Point<float> c = circle.getCentre();
    juce::Path track, arc;
    track.addCentredArc(c.x, c.y, rr, rr, 0.0f, toRad(a0), toRad(a1), true);
    arc.addCentredArc(c.x, c.y, rr, rr, 0.0f, toRad(a0), toRad(deg), true);
    g.setColour(juce::Colours::white.withAlpha(0.09f));
    g.strokePath(track, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(accentA());
    g.strokePath(arc, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Point<float> tip = c.getPointOnCircumference(rr, toRad(deg));
    g.setColour(col::ptr);
    g.drawLine({ c, tip }, 1.3f);
}

} // namespace fui
