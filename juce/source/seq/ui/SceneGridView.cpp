#include "SceneGridView.h"
#include "../../ui/Controls.h"
#include "../dsp/SeqModel.h"

#include <cmath>

namespace fui {

// The JUCE default font has no reliable glyph coverage for the web's
// ▶ / ■ / ▷ / ≈ / ✎ symbols (and the headless snapshot test renders with
// whatever fonts the CI box has) — ASCII stand-ins, same call SeqHeader made.
namespace {
constexpr const char* kPlayGlyph = ">";
constexpr const char* kStopGlyph = "S";
constexpr const char* kIdleGlyph = ">";
constexpr const char* kPassGlyph = "~";
constexpr const char* kEditGlyph = "E";

// 0.8s pulse, matches the web's sq-qpulse keyframe (opacity 0.2..1).
float qpulse() {
    const double t = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    return 0.2f + 0.8f * (0.5f - 0.5f * std::cos(2.0 * juce::MathConstants<double>::pi * t / 0.8));
}
} // namespace

SceneGridView::SceneGridView(SeqAudioProcessor& p) : proc(p) { startTimerHz(30); }

// ---- actions (also the test handles) ---------------------------------------

bool SceneGridView::isPassThrough(int s, int t) const {
    const auto& scenes = proc.conductor().session().scenes;
    if (s < 0 || s >= (int)scenes.size()) return false;
    for (int x : scenes[(size_t)s].pass) if (x == t) return true;
    return false;
}

void SceneGridView::cellClick(int s, int t) {
    auto& cond = proc.conductor();
    const auto& scenes = cond.session().scenes;
    if (s < 0 || s >= (int)scenes.size() || t < 0 || t >= kTracks) return;

    if (!scenes[(size_t)s].hasClip[(size_t)t]) {
        if (!isPassThrough(s, t)) cond.stopTrack(t);
        return;
    }
    if (cond.ownerOf(t) == s) cond.stopTrack(t);
    else cond.launch(t, s);
}

void SceneGridView::cellRightClick(int s, int t) {
    const auto& scenes = proc.conductor().session().scenes;
    if (s < 0 || s >= (int)scenes.size() || t < 0 || t >= kTracks) return;
    if (scenes[(size_t)s].hasClip[(size_t)t]) return; // only empty cells toggle pass-through
    proc.conductor().togglePassThrough(s, t);
}

void SceneGridView::cellEditClick(int s, int t) { if (onEditClip) onEditClip(s, t); }

bool SceneGridView::cellAudible(int s, int t) const {
    const auto& cond = proc.conductor();
    return cond.ownerOf(t) == s && cond.trackAudible(t);
}

void SceneGridView::sceneLaunch(int s) { proc.conductor().launchScene(s); }
void SceneGridView::sceneMute(int s)   { proc.conductor().toggleSceneMute(s); }
void SceneGridView::sceneStop(int s)   { proc.conductor().stopScene(s); }

// ---- mouse -----------------------------------------------------------------

void SceneGridView::mouseDown(const juce::MouseEvent& e) {
    const auto pos = e.getPosition();
    const bool right = e.mods.isPopupMenu();

    if (singleRow_) {
        for (int s = 0; s < kScenes; ++s)
            if (railChip[s].contains(pos)) { if (onRailScene) onRailScene(s); return; }
    }

    for (int s = 0; s < kScenes; ++s) {
        if (singleRow_ && s != singleRowScene_) continue;
        if (launchBtn[s].contains(pos)) { sceneLaunch(s); return; }
        if (muteBtnR[s].contains(pos))  { sceneMute(s); return; }
        if (stopBtnR[s].contains(pos))  { sceneStop(s); return; }
        for (int t = 0; t < kTracks; ++t) {
            if (editGlyph[s][t].contains(pos)) { cellEditClick(s, t); return; }
            if (cellR[s][t].contains(pos)) {
                if (right) cellRightClick(s, t); else cellClick(s, t);
                return;
            }
        }
    }
}

// ---- layout ------------------------------------------------------------------

void SceneGridView::resized() {
    for (int s = 0; s < kScenes; ++s) {
        if (singleRow_ && s != singleRowScene_) continue;
        layoutRow(s);
    }
    if (singleRow_) layoutRail();
}

void SceneGridView::layoutRow(int s) {
    const int y = singleRow_ ? 0 : s * 105;
    sceneCardR[s] = { 0, y, 218, 96 };
    for (int t = 0; t < kTracks; ++t)
        cellR[s][t] = { 218 + 9 + t * (292 + 9), y, 292, 96 };

    // scene card: [launch] [id: num+name / status] [M] [S], then dots row.
    auto r = sceneCardR[s].reduced(10, 8);
    auto top = r.removeFromTop(32);
    launchBtn[s] = top.removeFromLeft(32);
    top.removeFromLeft(8);
    stopBtnR[s] = top.removeFromRight(22).withSizeKeepingCentre(22, 22);
    top.removeFromRight(6);
    muteBtnR[s] = top.removeFromRight(22).withSizeKeepingCentre(22, 22);
    top.removeFromRight(8);
    idArea[s] = top;
    r.removeFromTop(9);
    dotsArea[s] = r.removeFromTop(16);

    // clip cells: a 16x16 edit-glyph corner in the top-right.
    for (int t = 0; t < kTracks; ++t)
        editGlyph[s][t] = { cellR[s][t].getRight() - 22, cellR[s][t].getY() + 6, 16, 16 };
}

void SceneGridView::layoutRail() {
    railArea = { 0, 100, getWidth(), 24 };
    auto r = railArea;
    const int w = juce::jmin(48, r.getWidth() / kScenes);
    for (int s = 0; s < kScenes; ++s)
        railChip[s] = r.removeFromLeft(w).reduced(2);
}

// ---- paint -------------------------------------------------------------------

void SceneGridView::paint(juce::Graphics& g) {
    for (int s = 0; s < kScenes; ++s) {
        if (singleRow_ && s != singleRowScene_) continue;
        paintSceneCard(g, s);
        for (int t = 0; t < kTracks; ++t) paintCell(g, s, t);
    }
    if (singleRow_) paintRail(g);
}

void SceneGridView::paintSceneCard(juce::Graphics& g, int s) {
    const auto& cond = proc.conductor();
    const auto& scenes = cond.session().scenes;
    const auto& tracks = cond.session().tracks;
    const auto& sc = scenes[(size_t)s];

    int clipTracks = 0, owned = 0;
    bool queued = false;
    for (int t = 0; t < kTracks; ++t) {
        if (sc.hasClip[(size_t)t]) {
            clipTracks++;
            if (cond.ownerOf(t) == s) owned++;
        }
        if (cond.queueOf(t) == s) queued = true;
    }
    const bool muted = cond.sceneMuted(s);
    const bool liveAny = owned > 0;
    const bool full = liveAny && owned == clipTracks;
    const bool hot = liveAny && !muted;

    auto rf = sceneCardR[s].toFloat();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff151924), rf.getX(), rf.getY(),
                                           juce::Colour(0xff0c0f16), rf.getX(), rf.getBottom(), false));
    g.fillRoundedRectangle(rf, 10.0f);
    g.setColour(hot ? juce::Colour(0xff4dff9e).withAlpha(0.35f) : col::line);
    g.drawRoundedRectangle(rf.reduced(0.5f), 10.0f, 1.0f);

    // launch button
    const bool anyOwner = owned > 0;
    auto lb = launchBtn[s].toFloat();
    g.setColour(anyOwner ? juce::Colour(0xff0e3120) : juce::Colour(0xff11141c));
    g.fillRoundedRectangle(lb, 8.0f);
    g.setColour(anyOwner ? juce::Colour(0xff4dff9e).withAlpha(0.6f) : juce::Colours::white.withAlpha(0.1f));
    g.drawRoundedRectangle(lb.reduced(0.5f), 8.0f, 1.0f);
    g.setColour(queued ? col::text.withAlpha(qpulse()) : anyOwner ? juce::Colour(0xff4dff9e) : col::acN);
    g.setFont(monoFont(12.0f, true));
    g.drawText(kPlayGlyph, launchBtn[s], juce::Justification::centred);

    // num + name / status
    auto id = idArea[s];
    auto nameRow = id.removeFromTop(id.getHeight() / 2);
    g.setColour(col::textDim);
    g.setFont(monoFont(8.0f));
    g.drawText(juce::String(s + 1).paddedLeft('0', 2), nameRow.removeFromLeft(18), juce::Justification::centredLeft);
    g.setColour(col::text);
    g.setFont(dispFont(9.0f));
    g.drawText(juce::String(sc.name), nameRow, juce::Justification::centredLeft);

    juce::String status;
    juce::Colour statusColour = col::textDim;
    // web "LIVE · MUTED" -- ASCII middle dot substitute (BassHeader.cpp:175 convention)
    if (muted && liveAny)      { status = "LIVE - MUTED"; statusColour = col::acB; }
    else if (muted)            { status = "MUTED"; statusColour = col::acB; }
    else if (queued)           { status = "QUEUED"; statusColour = col::text; }
    else if (full)             { status = "LIVE"; statusColour = juce::Colour(0xff4dff9e); }
    else if (liveAny)          { status = "LIVE " + juce::String(owned) + "/" + juce::String(clipTracks); statusColour = juce::Colour(0xff4dff9e); }
    else                       { status = "READY"; statusColour = col::textDim; }
    g.setColour(statusColour);
    g.setFont(monoFont(7.0f));
    drawSpaced(g, status, id, 1.2f);

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
    drawToggle(muteBtnR[s], "M", muted, col::acB);
    drawToggle(stopBtnR[s], kStopGlyph, false, col::acB);

    // dots + clip count
    auto dr = dotsArea[s];
    for (int t = 0; t < kTracks; ++t) {
        auto d = dr.removeFromLeft(9).withSizeKeepingCentre(7, 7);
        dr.removeFromLeft(6);
        const bool has = t < (int)tracks.size() && sc.hasClip[(size_t)t];
        const juce::Colour tc { has ? tracks[(size_t)t].color : 0xff1a1f2au };
        const bool lit = has && cellAudible(s, t);
        g.setColour(lit ? tc : has ? tc.withAlpha(0.27f) : juce::Colour(0xff1a1f2a));
        g.fillEllipse(d.toFloat());
    }
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    juce::String cnt = juce::String(clipTracks) + (clipTracks == 1 ? " CLIP" : " CLIPS");
    g.drawText(cnt, dr, juce::Justification::centredRight);
}

void SceneGridView::paintCell(juce::Graphics& g, int s, int t) {
    const auto& scenes = proc.conductor().session().scenes;
    if (scenes[(size_t)s].hasClip[(size_t)t]) paintFilledCell(g, s, t);
    else paintEmptyCell(g, s, t);
}

void SceneGridView::paintEmptyCell(juce::Graphics& g, int s, int t) {
    const auto& tracks = proc.conductor().session().tracks;
    const juce::Colour tc { tracks[(size_t)t].color };
    const bool pass = isPassThrough(s, t);

    auto rf = cellR[s][t].toFloat().reduced(0.5f);
    g.setColour(juce::Colours::black.withAlpha(0.16f));
    g.fillRoundedRectangle(rf, 10.0f);

    juce::Path outline;
    outline.addRoundedRectangle(rf, 10.0f);
    juce::PathStrokeType stroke(1.0f);
    float dashLengths[] = { pass ? 1.0f : 3.0f, pass ? 2.0f : 3.0f };
    juce::Path dashed;
    stroke.createDashedStroke(dashed, outline, dashLengths, 2);
    g.setColour(pass ? tc.withAlpha(0.28f) : juce::Colours::white.withAlpha(0.06f));
    g.fillPath(dashed);

    g.setColour(pass ? tc.withAlpha(0.6f) : juce::Colour(0xff333a48));
    g.setFont(monoFont(pass ? 13.0f : 10.0f, true));
    g.drawText(pass ? kPassGlyph : kStopGlyph, cellR[s][t], juce::Justification::centred);
}

void SceneGridView::paintFilledCell(juce::Graphics& g, int s, int t) {
    const auto& cond = proc.conductor();
    const auto& tracks = cond.session().tracks;
    const auto& sc = cond.session().scenes[(size_t)s];
    const auto& clip = sc.clips[(size_t)t];
    const juce::Colour tc { tracks[(size_t)t].color };

    const bool live = cond.ownerOf(t) == s;
    const bool queued = cond.queueOf(t) == s;
    // Port of ClipCell's `muted` (SceneRow.tsx): a live cell dims/shows MUTED
    // whenever the track isn't fully audible -- its own mute, another
    // track's solo, or its owning scene's mute -- not just a scene mute.
    const bool muted = live && !cellAudible(s, t);

    auto full = cellR[s][t];
    auto rf = full.toFloat().reduced(0.5f);
    if (live) {
        g.setGradientFill(juce::ColourGradient(tc.withAlpha(0.09f), rf.getX(), rf.getY(),
                                               juce::Colours::transparentBlack, rf.getX(), rf.getY() + rf.getHeight() * 0.46f, false));
        g.fillRoundedRectangle(rf, 10.0f);
    }
    g.setGradientFill(juce::ColourGradient(col::panelHi, rf.getX(), rf.getY(),
                                           col::panelLo, rf.getX(), rf.getBottom(), false));
    g.fillRoundedRectangle(rf, 10.0f);
    g.setColour(live ? tc.withAlpha(0.67f) : juce::Colours::white.withAlpha(0.08f));
    g.drawRoundedRectangle(rf, 10.0f, live ? 1.4f : 1.0f);

    const float bodyAlpha = muted ? 0.32f : live ? 1.0f : 0.72f;

    auto content = full.reduced(9, 7);
    auto head = content.removeFromTop(14);

    // eq / idle icon
    auto iconArea = head.removeFromLeft(16);
    if (live) {
        const int step = proc.trackStep[t].load();
        const int phase = step >= 0 ? step % 4 : 0;
        static const int heights[4][3] = { {6, 10, 8}, {4, 7, 5}, {6, 4, 10}, {8, 10, 6} };
        const auto& h = heights[(size_t)phase];
        int x = iconArea.getX();
        for (int i = 0; i < 3; ++i) {
            g.setColour(tc.withAlpha(bodyAlpha));
            g.fillRect(juce::Rectangle<int>(x, iconArea.getBottom() - h[(size_t)i], 3, h[(size_t)i]));
            x += 4;
        }
    } else {
        g.setColour(juce::Colour(0xff4a5266).withAlpha(bodyAlpha));
        g.setFont(monoFont(9.0f));
        g.drawText(kIdleGlyph, iconArea, juce::Justification::centredLeft);
    }
    head.removeFromLeft(6);

    // "{bars}B" chip on the right
    auto chip = head.removeFromRight(24);
    g.setColour(col::textDim.withAlpha(bodyAlpha));
    g.setFont(monoFont(7.0f));
    g.drawText(juce::String(clip.bars) + "B", chip, juce::Justification::centredRight);
    head.removeFromRight(4);

    g.setColour((live ? tc : col::acN).withAlpha(bodyAlpha));
    g.setFont(monoFont(9.5f));
    g.drawText(juce::String(clip.name), head, juce::Justification::centredLeft);

    content.removeFromTop(6);
    auto stepsArea = content.removeFromTop(20);
    if (!clip.bytes.empty()) {
        auto steps = fable::sqPreviewSteps(tracks[(size_t)t].machine, clip.bytes.data());
        const float bw = stepsArea.getWidth() / (float)fable::SQ_STEPS_PER_BAR;
        for (int i = 0; i < fable::SQ_STEPS_PER_BAR; ++i) {
            const auto& sb = steps[(size_t)i];
            const float bh = (float)juce::jlimit(2, 20, sb.h);
            juce::Rectangle<float> bar(stepsArea.getX() + i * bw + 1.0f, stepsArea.getBottom() - bh,
                                        juce::jmax(1.0f, bw - 2.0f), bh);
            g.setColour(sb.on ? tc.withAlpha(0.6f * bodyAlpha) : juce::Colours::white.withAlpha(0.08f * bodyAlpha));
            g.fillRoundedRectangle(bar, 1.0f);
        }
    }

    content.removeFromTop(6);
    auto progress = content.removeFromTop(3);
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.fillRoundedRectangle(progress.toFloat(), 1.5f);
    if (live && clip.bars > 0) {
        const int bar = proc.trackBar[t].load();
        const int step = proc.trackStep[t].load();
        if (bar >= 0 && step >= 0) {
            const int totalSteps = clip.bars * fable::SQ_STEPS_PER_BAR;
            const int pos = ((bar * fable::SQ_STEPS_PER_BAR + step) % totalSteps + totalSteps) % totalSteps;
            const float frac = juce::jlimit(0.0f, 1.0f, (float)pos / (float)totalSteps);
            auto lit = progress.withWidth((int)(progress.getWidth() * frac));
            g.setColour(tc);
            g.fillRoundedRectangle(lit.toFloat(), 1.5f);
        }
    }

    // edit glyph -- brief calls for hover-lit; this pass has no hover tracking,
    // so a steady dim glyph keeps the click target visible and legible.
    g.setColour(tc.withAlpha(0.85f));
    g.setFont(monoFont(9.0f, true));
    g.drawText(kEditGlyph, editGlyph[s][t], juce::Justification::centred);

    if (queued) {
        g.setColour(tc.withAlpha(qpulse()));
        g.drawRoundedRectangle(rf, 10.0f, 1.4f);
    }
    if (muted) {
        g.setColour(col::acB);
        g.setFont(monoFont(6.5f));
        drawSpaced(g, "MUTED", { full.getRight() - 60, full.getY() + 5, 40, 10 }, 1.4f, juce::Justification::right);
    }
}

void SceneGridView::paintRail(juce::Graphics& g) {
    const auto& cond = proc.conductor();
    for (int s = 0; s < kScenes; ++s) {
        auto r = railChip[s].toFloat();
        const bool current = s == singleRowScene_;
        bool live = false;
        for (int t = 0; t < kTracks; ++t) if (cond.ownerOf(t) == s) live = true;

        g.setColour(current ? juce::Colour(0xff11141c) : juce::Colour(0xff0a0d13));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(current ? col::text.withAlpha(0.4f) : col::line);
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
        g.setColour(col::textDim);
        g.setFont(monoFont(8.0f));
        g.drawText(juce::String(s + 1).paddedLeft('0', 2), railChip[s], juce::Justification::centred);
        if (live) {
            g.setColour(juce::Colour(0xff4dff9e));
            g.fillEllipse(juce::Rectangle<float>(4, 4).withCentre({ r.getRight() - 5.0f, r.getY() + 5.0f }));
        }
    }
}

} // namespace fui
