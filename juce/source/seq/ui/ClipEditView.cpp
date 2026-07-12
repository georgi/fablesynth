#include "ClipEditView.h"
#include "../../ui/Controls.h"
#include "../../drum/dsp/DrumKits.h"
#include "../dsp/SeqModel.h"

#include <algorithm>

namespace fui {

// ASCII stand-ins, same rationale as SceneGridView (headless snapshot fonts).
namespace {
constexpr const char* kMinus = "-";
constexpr const char* kPlus  = "+";
} // namespace

ClipEditView::ClipEditView(SeqAudioProcessor& p) : proc_(p) { startTimerHz(30); }

// ---- target / doc sync -------------------------------------------------------

fable::Machine ClipEditView::machine() const {
    const auto& tracks = proc_.conductor().session().tracks;
    return (track_ >= 0 && track_ < (int)tracks.size()) ? tracks[(size_t)track_].machine
                                                        : fable::Machine::DR1;
}

bool ClipEditView::live() const {
    return scene_ >= 0 && track_ >= 0 && proc_.conductor().ownerOf(track_) == scene_;
}

int ClipEditView::playStep() const {
    if (!live()) return -1;
    if (proc_.trackBar[track_].load() != editBar_) return -1;
    return proc_.trackStep[track_].load();
}

void ClipEditView::setTarget(int scene, int track) {
    scene_ = scene;
    track_ = track;
    editBar_ = 0;
    reload();
    resized();
    repaint();
}

void ClipEditView::reload() {
    hasClip_ = false;
    bytes_.clear();
    bars_ = 1;
    if (scene_ < 0 || track_ < 0) return;
    const auto& scenes = proc_.conductor().session().scenes;
    if (scene_ >= (int)scenes.size()) return;
    const auto& sc = scenes[(size_t)scene_];
    if (track_ >= (int)sc.hasClip.size() || !sc.hasClip[(size_t)track_]) return;
    const auto& clip = sc.clips[(size_t)track_];
    hasClip_ = true;
    bars_ = clip.bars;
    bytes_ = clip.bytes;
    lastWritten_ = bytes_;
    if (editBar_ >= bars_) editBar_ = bars_ - 1;
    if (editBar_ < 0) editBar_ = 0;
}

void ClipEditView::commit() {
    if (!editable()) return;
    lastWritten_ = bytes_;
    proc_.conductor().updateClipBytes(scene_, track_, bytes_, bars_);
    repaint();
}

// ---- edit handles (also the click targets) ----------------------------------

void ClipEditView::cycleDrumCell(int pad, int step) {
    if (!editable() || !isDrum() || pad < 0 || pad >= kPads || step < 0 || step >= kSteps) return;
    const size_t i = (size_t)(barOffset() + fable::sqDr1Idx(0, pad, step));
    if (i >= bytes_.size()) return;
    bytes_[i] = (uint8_t)((bytes_[i] + 1) % 3);
    commit();
}

void ClipEditView::toggleDrumCell(int pad, int step) {
    if (!editable() || !isDrum() || pad < 0 || pad >= kPads || step < 0 || step >= kSteps) return;
    const size_t i = (size_t)(barOffset() + fable::sqDr1Idx(0, pad, step));
    if (i >= bytes_.size()) return;
    bytes_[i] = bytes_[i] ? 0 : (uint8_t)1;
    commit();
}

void ClipEditView::toggleNoteCell(int lane, int step) {
    if (!editable() || isDrum() || lane < 0 || lane >= kLanes || step < 0 || step >= kSteps) return;
    const size_t o = (size_t)(barOffset() + fable::sqNoteIdx(0, step));
    if (o + 2 >= bytes_.size()) return;
    const bool on = (bytes_[o] & 1) != 0;
    if (on && bytes_[o + 1] == (uint8_t)lane) {
        // tap the lit note -> rest: clear on+acc+tie (web setStep {on,acc,slide}=false).
        bytes_[o] = (uint8_t)(bytes_[o] & ~0x07);
    } else {
        bytes_[o] = (uint8_t)(bytes_[o] | 1);
        bytes_[o + 1] = (uint8_t)lane;
    }
    commit();
}

void ClipEditView::toggleAcc(int step) {
    if (!editable() || isDrum() || step < 0 || step >= kSteps) return;
    const size_t o = (size_t)(barOffset() + fable::sqNoteIdx(0, step));
    if (o >= bytes_.size() || (bytes_[o] & 1) == 0) return; // web toggleStepAcc: no-op when off
    bytes_[o] = (uint8_t)(bytes_[o] ^ 2);
    commit();
}

void ClipEditView::toggleTie(int step) {
    if (!editable() || isDrum() || step < 0 || step >= kSteps) return;
    const size_t o = (size_t)(barOffset() + fable::sqNoteIdx(0, step));
    if (o >= bytes_.size() || (bytes_[o] & 1) == 0) return; // web toggleStepSlide: no-op when off
    bytes_[o] = (uint8_t)(bytes_[o] ^ 4);
    commit();
}

void ClipEditView::setOct(int step, int oct) {
    if (!editable() || isDrum() || step < 0 || step >= kSteps) return;
    const size_t o = (size_t)(barOffset() + fable::sqNoteIdx(0, step));
    if (o + 2 >= bytes_.size()) return;
    bytes_[o + 2] = (uint8_t)(juce::jlimit(-1, 1, oct) + 1);
    commit();
}

void ClipEditView::barsStep(int d) {
    if (!editable()) return;
    const int next = juce::jlimit(1, fable::SQ_HOSTED_MAX_BARS, bars_ + d);
    if (next == bars_) return;
    auto nb = fable::sqEmptyClip(machine(), next);
    std::copy(bytes_.begin(), bytes_.begin() + (long)std::min(bytes_.size(), nb.size()), nb.begin());
    bytes_ = std::move(nb);
    bars_ = next;
    if (editBar_ >= bars_) editBar_ = bars_ - 1;
    commit();
    resized();
}

void ClipEditView::barClick(int b) {
    if (b < 0 || b >= bars_) return;
    editBar_ = b;
    repaint();
}

void ClipEditView::createClipClick() {
    if (hasClip_ || scene_ < 0 || track_ < 0) return;
    proc_.conductor().createClip(scene_, track_);
    reload();
    resized();
    repaint();
}

// ---- 30 Hz doc watcher -------------------------------------------------------

void ClipEditView::timerCallback() {
    if (scene_ >= 0 && track_ >= 0) {
        const auto& scenes = proc_.conductor().session().scenes;
        if (scene_ < (int)scenes.size() && track_ < (int)scenes[(size_t)scene_].hasClip.size()) {
            const auto& sc = scenes[(size_t)scene_];
            const bool nowHas = sc.hasClip[(size_t)track_];
            // External change (create/remove/edit from elsewhere): pull it in,
            // but never clobber our own just-written bytes (== lastWritten_).
            if (nowHas != hasClip_ || (nowHas && sc.clips[(size_t)track_].bytes != lastWritten_)) {
                reload();
                resized();
            }
        }
    }
    repaint();
}

// ---- layout ------------------------------------------------------------------

void ClipEditView::layoutBars() {
    barsRow = { 0, 6, getWidth(), 30 };
    auto r = barsRow.reduced(12, 0);
    // stepper on the right: [-] N BARS [+]
    barsPlus  = r.removeFromRight(26).withSizeKeepingCentre(22, 22);
    r.removeFromRight(56); // "N BARS" readout
    barsMinus = r.removeFromRight(26).withSizeKeepingCentre(22, 22);
    // bar chips, laid out left-to-right ending just left of the stepper block.
    // Locked (>4-bar, view-only) clips show NO chips, just the lock banner
    // (web HostedClipBar.tsx: chips render only when clip.bars <= HOSTED_MAX_BARS).
    const int chipW = 24, gap = 4;
    const int nChips = editable() ? juce::jmin(bars_, (int)fable::SQ_HOSTED_MAX_BARS) : 0;
    int x = barsMinus.getX() - 16 - nChips * (chipW + gap);
    for (int b = 0; b < fable::SQ_HOSTED_MAX_BARS; ++b) {
        if (b < nChips) { barChip[b] = { x, barsRow.getY() + 4, chipW, 22 }; x += chipW + gap; }
        else barChip[b] = {};
    }
}

void ClipEditView::resized() {
    layoutBars();
    gridArea = { 0, 42, getWidth(), getHeight() - 42 };
    createBtn = juce::Rectangle<int>(0, 0, 220, 48).withCentre(getLocalBounds().getCentre());

    if (!isDrum()) {
        // reserve the bottom three sub-rows (OCT / ACC / TIE) below the lanes.
        auto g = gridArea;
        auto sub = g.removeFromBottom(88);
        g.removeFromBottom(2);
        gridArea = g; // lanes area only
        auto s = sub;
        octRow = s.removeFromTop(28); s.removeFromTop(2);
        accRow = s.removeFromTop(24); s.removeFromTop(2);
        tieRow = s.removeFromTop(24);
    }
}

// ---- mouse -------------------------------------------------------------------

void ClipEditView::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();

    if (!hasClip_) {
        if (createBtn.contains(pos)) createClipClick();
        return;
    }

    if (editable()) {
        if (barsMinus.contains(pos)) { barsStep(-1); return; }
        if (barsPlus.contains(pos))  { barsStep(+1); return; }
    }
    for (int b = 0; b < bars_ && b < fable::SQ_HOSTED_MAX_BARS; ++b)
        if (!barChip[b].isEmpty() && barChip[b].contains(pos)) { barClick(b); return; }

    if (isDrum()) {
        const int labelW = 92;
        auto cells = gridArea.withTrimmedLeft(labelW);
        if (!cells.contains(pos)) return;
        const int step = (pos.x - cells.getX()) * kSteps / std::max(1, cells.getWidth());
        const int pad  = (pos.y - cells.getY()) * kPads  / std::max(1, cells.getHeight());
        if (step >= 0 && step < kSteps && pad >= 0 && pad < kPads) cycleDrumCell(pad, step);
        return;
    }

    // note machine: lanes grid + oct/acc/tie sub-rows.
    const int labelW = 40;
    auto lanes = gridArea.withTrimmedLeft(labelW);
    auto stepAt = [&](juce::Rectangle<int> row) {
        auto cols = row.withTrimmedLeft(labelW);
        return (pos.x - cols.getX()) * kSteps / std::max(1, cols.getWidth());
    };
    if (lanes.contains(pos)) {
        const int step = (pos.x - lanes.getX()) * kSteps / std::max(1, lanes.getWidth());
        const int row  = (pos.y - lanes.getY()) * kLanes / std::max(1, lanes.getHeight());
        const int note = kLanes - 1 - row; // top lane = B (11), bottom = root C (0)
        if (step >= 0 && step < kSteps && note >= 0 && note < kLanes) toggleNoteCell(note, step);
        return;
    }
    if (octRow.contains(pos)) {
        const int step = stepAt(octRow);
        if (step >= 0 && step < kSteps) {
            const size_t o = (size_t)(barOffset() + fable::sqNoteIdx(0, step));
            const int cur = o + 2 < bytes_.size() ? (int)bytes_[o + 2] - 1 : 0;
            setOct(step, cur >= 1 ? -1 : cur + 1); // 0 -> +1 -> -1 -> 0 (PitchSeq cycle)
        }
        return;
    }
    if (accRow.contains(pos)) { const int s = stepAt(accRow); if (s >= 0 && s < kSteps) toggleAcc(s); return; }
    if (tieRow.contains(pos)) { const int s = stepAt(tieRow); if (s >= 0 && s < kSteps) toggleTie(s); return; }
}

// ---- paint -------------------------------------------------------------------

juce::String ClipEditView::padLabel(int pad) const {
    const auto& tracks = proc_.conductor().session().tracks;
    if (track_ >= 0 && track_ < (int)tracks.size()) {
        const auto& patch = tracks[(size_t)track_].patch;
        const auto& kits = fable::factoryKits();
        if (patch.factory && patch.index >= 0 && patch.index < (int)kits.size()) {
            const auto& names = kits[(size_t)patch.index].padNames;
            if (pad >= 0 && pad < (int)names.size() && !names[(size_t)pad].empty())
                return juce::String(names[(size_t)pad]);
        }
    }
    return "PAD " + juce::String(pad + 1);
}

void ClipEditView::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat(), 12.0f);

    if (!hasClip_) { paintCreate(g); return; }

    paintBars(g);
    if (isDrum()) paintDrumGrid(g);
    else          paintNoteGrid(g);
}

void ClipEditView::paintCreate(juce::Graphics& g) {
    auto rf = createBtn.toFloat();
    g.setColour(juce::Colour(0xff11141c));
    g.fillRoundedRectangle(rf, 10.0f);
    g.setColour(col::acN.withAlpha(0.5f));
    g.drawRoundedRectangle(rf.reduced(0.5f), 10.0f, 1.0f);
    g.setColour(col::text);
    g.setFont(dispFont(12.0f));
    g.drawText(juce::String(kPlus) + " CREATE CLIP", createBtn, juce::Justification::centred);

    g.setColour(col::textDim);
    g.setFont(monoFont(8.0f));
    drawSpaced(g, "EMPTY SLOT - THE PATCH PANEL STILL EDITS THIS TRACK'S SOUND",
               createBtn.translated(0, 40).withHeight(14), 1.2f, juce::Justification::horizontallyCentred);
}

void ClipEditView::paintBars(juce::Graphics& g) {
    const auto& sc = proc_.conductor().session().scenes[(size_t)scene_];
    const auto& clip = sc.clips[(size_t)track_];

    auto nameArea = barsRow.withTrimmedLeft(12).withWidth(360);
    const juce::Colour tc { proc_.conductor().session().tracks[(size_t)track_].color };
    g.setColour(tc);
    g.setFont(dispFont(12.0f));
    drawSpaced(g, juce::String(clip.name), nameArea, 1.8f);

    if (!editable()) {
        g.setColour(col::acB);
        g.setFont(monoFont(9.0f, true));
        drawSpaced(g, juce::String(bars_) + "-BAR CLIP - VIEW ONLY",
                   barsRow.withTrimmedLeft(380).withWidth(320), 1.6f);
    }

    // bar chips
    for (int b = 0; b < bars_ && b < fable::SQ_HOSTED_MAX_BARS; ++b) {
        if (barChip[b].isEmpty()) continue;
        auto rf = barChip[b].toFloat();
        const bool cur = b == editBar_;
        g.setColour(cur ? tc.withAlpha(0.22f) : juce::Colour(0xff11141c));
        g.fillRoundedRectangle(rf, 5.0f);
        g.setColour(cur ? tc : col::line);
        g.drawRoundedRectangle(rf.reduced(0.5f), 5.0f, 1.0f);
        g.setColour(cur ? col::text : col::textDim);
        g.setFont(monoFont(9.0f, true));
        g.drawText(juce::String(b + 1), barChip[b], juce::Justification::centred);
    }

    // stepper (only on editable clips)
    auto drawMini = [&](juce::Rectangle<int> r, const char* txt, bool on) {
        auto rf2 = r.toFloat();
        g.setColour(juce::Colour(0xff11141c));
        g.fillRoundedRectangle(rf2, 5.0f);
        g.setColour(on ? col::line : col::line.withAlpha(0.4f));
        g.drawRoundedRectangle(rf2.reduced(0.5f), 5.0f, 1.0f);
        g.setColour(on ? col::text : col::textDim);
        g.setFont(monoFont(11.0f, true));
        g.drawText(txt, r, juce::Justification::centred);
    };
    if (editable()) {
        drawMini(barsMinus, kMinus, bars_ > 1);
        drawMini(barsPlus, kPlus, bars_ < fable::SQ_HOSTED_MAX_BARS);
        g.setColour(col::textDim);
        g.setFont(monoFont(8.0f));
        g.drawText(juce::String(bars_) + (bars_ > 1 ? " BARS" : " BAR"),
                   juce::Rectangle<int>(barsMinus.getRight(), barsRow.getY(),
                                        barsPlus.getX() - barsMinus.getRight(), barsRow.getHeight()),
                   juce::Justification::centred);
    }
}

void ClipEditView::paintDrumGrid(juce::Graphics& g) {
    const juce::Colour tc { proc_.conductor().session().tracks[(size_t)track_].color };
    const int labelW = 92;
    auto cells = gridArea.withTrimmedLeft(labelW);
    const float stepW = cells.getWidth() / (float)kSteps;
    const float rowH  = gridArea.getHeight() / (float)kPads;
    const int ph = playStep();

    for (int pad = 0; pad < kPads; ++pad) {
        const float y = gridArea.getY() + pad * rowH;
        g.setColour((pad % 2) ? col::textDim : col::text.withAlpha(0.7f));
        g.setFont(monoFont(8.0f));
        g.drawText(padLabel(pad), juce::Rectangle<float>((float)gridArea.getX() + 4, y, labelW - 6, rowH),
                   juce::Justification::centredLeft);
        for (int s = 0; s < kSteps; ++s) {
            const float x = cells.getX() + s * stepW;
            juce::Rectangle<float> cell(x + 1, y + 1, stepW - 2, rowH - 2);
            const uint8_t v = bytes_[(size_t)(barOffset() + fable::sqDr1Idx(0, pad, s))];
            const bool group = (s / 4) % 2 == 0;
            if (s == ph) g.setColour(tc.withAlpha(0.14f));
            else g.setColour(group ? juce::Colours::white.withAlpha(0.03f) : juce::Colours::black.withAlpha(0.12f));
            g.fillRoundedRectangle(cell, 3.0f);
            if (v) {
                g.setColour(v == 2 ? tc : tc.withAlpha(0.55f));
                g.fillRoundedRectangle(cell.reduced(1.5f), 3.0f);
            } else {
                g.setColour(col::line);
                g.drawRoundedRectangle(cell.reduced(0.5f), 3.0f, 1.0f);
            }
        }
    }
}

void ClipEditView::paintNoteGrid(juce::Graphics& g) {
    const juce::Colour tc { proc_.conductor().session().tracks[(size_t)track_].color };
    static const char* kLaneNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int labelW = 40;
    auto lanes = gridArea.withTrimmedLeft(labelW);
    const float stepW = lanes.getWidth() / (float)kSteps;
    const float laneH = gridArea.getHeight() / (float)kLanes;
    const int ph = playStep();

    for (int r = 0; r < kLanes; ++r) {
        const int note = kLanes - 1 - r;
        const float y = gridArea.getY() + r * laneH;
        g.setColour(note == 0 ? tc.withAlpha(0.8f) : col::textDim);
        g.setFont(monoFont(7.5f));
        g.drawText(kLaneNames[note], juce::Rectangle<float>((float)gridArea.getX() + 4, y, labelW - 6, laneH),
                   juce::Justification::centredLeft);
        for (int s = 0; s < kSteps; ++s) {
            const float x = lanes.getX() + s * stepW;
            juce::Rectangle<float> cell(x + 1, y + 1, stepW - 2, laneH - 2);
            const size_t o = (size_t)(barOffset() + fable::sqNoteIdx(0, s));
            const bool on = (bytes_[o] & 1) != 0 && (int)bytes_[o + 1] == note;
            const bool acc = (bytes_[o] & 2) != 0;
            const bool group = (s / 4) % 2 == 0;
            if (s == ph) g.setColour(tc.withAlpha(0.12f));
            else g.setColour(note == 0 ? tc.withAlpha(0.05f)
                                       : group ? juce::Colours::white.withAlpha(0.03f)
                                               : juce::Colours::black.withAlpha(0.12f));
            g.fillRoundedRectangle(cell, 2.5f);
            if (on) {
                g.setColour(acc ? juce::Colours::white : tc);
                g.fillRoundedRectangle(cell.reduced(1.0f), 2.5f);
            }
        }
    }

    const char* tieLabel = machine() == fable::Machine::BL1 ? "SLD" : "TIE";
    auto paintSubRow = [&](juce::Rectangle<int> row, const char* label, int kind) {
        g.setColour(col::textDim);
        g.setFont(monoFont(7.5f));
        g.drawText(label, row.withWidth(labelW - 6).translated(4, 0), juce::Justification::centredLeft);
        auto cols = row.withTrimmedLeft(labelW);
        const float w = cols.getWidth() / (float)kSteps;
        for (int s = 0; s < kSteps; ++s) {
            const size_t o = (size_t)(barOffset() + fable::sqNoteIdx(0, s));
            juce::Rectangle<float> cell(cols.getX() + s * w + 1, (float)row.getY() + 1, w - 2, row.getHeight() - 2.0f);
            const bool on = (bytes_[o] & 1) != 0;
            g.setColour(juce::Colours::black.withAlpha(0.16f));
            g.fillRoundedRectangle(cell, 2.5f);
            if (kind == 0) { // oct
                const int oc = (int)bytes_[o + 2] - 1;
                g.setColour(oc != 0 ? tc : col::textDim);
                g.setFont(monoFont(8.0f, true));
                g.drawText(oc == 0 ? "0" : oc > 0 ? "+1" : "-1", cell.toNearestInt(), juce::Justification::centred);
            } else {
                const bool set = on && (bytes_[o] & (kind == 1 ? 2 : 4)) != 0;
                if (set) { g.setColour(kind == 1 ? juce::Colours::white : tc); g.fillRoundedRectangle(cell.reduced(3.0f), 2.5f); }
            }
        }
    };
    paintSubRow(octRow, "OCT", 0);
    paintSubRow(accRow, "ACC", 1);
    paintSubRow(tieRow, tieLabel, 2);
}

} // namespace fui
