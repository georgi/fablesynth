#include "SeqEditor.h"
#include "../ui/Controls.h"

// Web layout, re-measured 2026-07-18 from the running SQ-4 app at its current
// 1460px-wide / 764px-tall rack (src/seq/seq.css, #seq-rack; the anatomy is
// unchanged since the Task 9 port, but the web rack has since compacted --
// see .superpowers/sdd/typography-calibration.md for the live-DOM numbers):
//   rack              1460 x 764
//   header            (18,  14) 1424 x 86
//   track heads       (18, 109) 1424 x 60
//   scene grid        (18, 178) 1424 x 491   (6 rows, 73 tall, 82 step)
//   footer            (18, 669) 1424 x 65
//   hint line          18, 742
// The web fits its footer at y=656 because two of its content-auto rows are
// only 66px; JUCE's uniform 73px rows run to 178+5*82+73=661, so the footer
// (and hint) shift down 13px from the web's raw numbers to clear row 6.
// Scene grid columns: scene col (18, 218), then 4 track cols of 292 each,
// 9px gaps: x = 18 + 218 + 9 + i*(292 + 9).
//
// Focus mode reuses header + heads unchanged, collapses the scene grid to a
// single-row mini strip (73 tall, with the scene rail on its left), and drops the
// selected native device body into the freed space down to the footer slot,
// which hides. The collapse is eased over ~180ms — see SeqRack::applyLayout()
// (spec §2).

// ---- SeqRack ----
SeqRack::SeqRack(SeqAudioProcessor& p)
    : header(p), trackHeads(p), sceneGrid(p), footer(p), deviceFocus(p) {
    addAndMakeVisible(header);
    addAndMakeVisible(trackHeads);
    addAndMakeVisible(sceneGrid);
    addAndMakeVisible(footer);
    addAndMakeVisible(hint);
    addChildComponent(deviceFocus); // hidden until focus is entered
}

void SeqRack::enterFocus(int track, int scene) {
    focusMode_ = true;
    focusTrack_ = track;
    trackHeads.setFocusMode(true);
    trackHeads.setFocusedTrack(track);
    sceneGrid.setSingleRow(scene);
    deviceFocus.setTarget(scene, track);
    footer.setVisible(false);
    deviceFocus.setVisible(true);
    focusTarget_ = 1.0f;
    if (!isShowing()) { focusT_ = 1.0f; applyLayout(); }
    else startTimerHz(30);
}

void SeqRack::exitFocus() {
    focusMode_ = false;
    focusTrack_ = -1;
    trackHeads.setFocusMode(false);
    trackHeads.setFocusedTrack(-1);
    sceneGrid.clearSingleRow();
    deviceFocus.setTarget(-1, -1);
    footer.setVisible(true);
    hint.setVisible(true);
    deviceFocus.setVisible(false);
    focusTarget_ = 0.0f;
    if (!isShowing()) { focusT_ = 0.0f; applyLayout(); }
    else startTimerHz(30);
}

void SeqRack::setFocusScene(int scene) {
    if (!focusMode_) return;
    sceneGrid.setSingleRow(scene);
    deviceFocus.setTarget(scene, focusTrack_);
}

void SeqRack::resized() { applyLayout(); }

// Eased focus collapse — the JUCE analogue of the web's FLIP animation
// (seq.css sq-focus-in): the grid shrinks 630→96 while the device surface
// grows into the freed space. ~180ms at 30Hz; headless (never showing)
// snaps instantly so the host tests see final geometry synchronously.
void SeqRack::applyLayout() {
    const float t = focusT_ * focusT_ * (3.0f - 2.0f * focusT_); // smoothstep
    header.setBounds(18, 14, 1424, 86);
    trackHeads.setBounds(18, 109, 1424, 60);
    constexpr int gridY = 178, sessionGridH = 491, focusGridH = 73; // 491 = footerY(669) - gridY
    const int gridH = juce::roundToInt(sessionGridH + (focusGridH - sessionGridH) * t);
    sceneGrid.setBounds(18, gridY, 1424, gridH);
    const int devY = gridY + gridH + 8;
    // Hint stays fixed at its session y in both modes (calibration note:
    // "keep fixed -- simplest correct wins"); the device surface grows into
    // the freed space as the grid collapses, down to 8px above it. Both the
    // footer and hint sit 13px below their raw web numbers (656/733) so the
    // footer clears row 6 of JUCE's uniform 73px-tall scene rows (see the
    // file header comment).
    constexpr int hintY = 742, hintH = 14;
    deviceFocus.setBounds(18, devY, 1424, (hintY - 8) - devY);
    // Footer bounds are set unconditionally even though it's hidden in
    // focus mode — bounds on a hidden component are inert, so this stays
    // simple rather than branching on focusMode_.
    footer.setBounds(18, 669, 1424, 65);
    hint.setBounds(18, hintY, 1424, hintH);
}

void SeqRack::timerCallback() {
    const float step = 1.0f / (0.18f * 30.0f); // full sweep in ~180ms
    focusT_ = focusTarget_ > focusT_ ? juce::jmin(focusTarget_, focusT_ + step)
                                     : juce::jmax(focusTarget_, focusT_ - step);
    applyLayout();
    if (juce::approximatelyEqual(focusT_, focusTarget_)) stopTimer();
}

// ---- SeqEditor ----
SeqEditor::SeqEditor(SeqAudioProcessor& p)
    : juce::AudioProcessorEditor(p), proc_(p), rack(p) {
    setLookAndFeel(&lnf);
    addAndMakeVisible(rack);
    rack.setBounds(0, 0, SeqRack::LW, SeqRack::LH);

    // Focus wiring: a head click focuses that device, the scenes-card back
    // button exits, the cell ✎ opens exactly that clip, and the mini-strip
    // rail jumps scenes (docs/.../sq4-device-focus-design.md §2-§3).
    heads().onFocusTrack = [this](int t) { enterFocus(t); };
    heads().onExitFocus  = [this]() { exitFocus(); };
    grid().onEditClip    = [this](int s, int t) { enterFocus(t, s); };
    grid().onRailScene   = [this](int s) { focusScene(s); };
    header().onLibrarySessionChanged = [this] { deviceFocus().reloadPatchesFromSession(); };

    // Hint copy mirrors SeqApp.tsx's two .sq-hint strings; ✎ has no glyph
    // coverage, so the word EDIT stands in.
    rack.getHint().setProvider([this]() -> juce::String {
        const auto dot = juce::String::fromUTF8(" \xc2\xb7 ");
        if (focusTrack_ >= 0)
            return "MINI STRIP STAYS LIVE - TAP CELLS TO LAUNCH" + dot
                 + "EDIT RETARGETS THE EDITOR" + dot + "ESC BACK TO SESSION";
        return "TAP CLIP TO LAUNCH" + dot + "TAP AGAIN TO STOP" + dot
             + "LAUNCHES QUANTIZE TO " + header().quantLabel() + dot
             + "CMD-CLICK SELECTS" + dot + "DRAG MOVES (ALT COPIES)" + dot
             + "CMD-C/X/V/D/Z EDIT" + dot + "RIGHT-CLICK EMPTY CELL TO TOGGLE PASS-THROUGH";
    });

    setWantsKeyboardFocus(true);

    setResizable(true, true);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio((double)SeqRack::LW / SeqRack::LH);
    setResizeLimits(840, (int)(840 * (double)SeqRack::LH / SeqRack::LW),
                    2100, (int)(2100 * (double)SeqRack::LH / SeqRack::LW));
    setSize(1200, (int)(1200 * (double)SeqRack::LH / SeqRack::LW));
}

SeqEditor::~SeqEditor() { setLookAndFeel(nullptr); }

int SeqEditor::clampScene(int s) const {
    const int n = (int)proc_.conductor().session().scenes.size();
    return juce::jlimit(0, juce::jmax(0, n - 1), s);
}

void SeqEditor::enterFocus(int t, int s) {
    // Scene pick (store.ts enterFocus): explicit s wins, else the scene
    // currently owning the track, else the current/last focused scene, else 0.
    int scene;
    if (s >= 0)                                scene = s;
    else if (proc_.conductor().ownerOf(t) >= 0) scene = proc_.conductor().ownerOf(t);
    else if (focusTrack_ >= 0)                 scene = focusScene_;
    else                                       scene = lastFocusScene_;
    scene = clampScene(scene);

    focusTrack_ = t;
    focusScene_ = scene;
    rack.enterFocus(t, scene);
    // Only grab focus when we're actually on screen: grabbing keyboard focus on
    // a component with no peer (headless render, or before the editor is shown)
    // is a no-op that trips JUCE's isShowing()/isOnDesktop() assert in
    // grabKeyboardFocusInternal. Guarding here keeps the shortcut keys working
    // in a live editor while leaving offline paints (host tests) assertion-free.
    if (isShowing() || isOnDesktop()) grabKeyboardFocus();
}

void SeqEditor::exitFocus() {
    if (focusTrack_ >= 0) lastFocusScene_ = focusScene_;
    focusTrack_ = focusScene_ = -1;
    rack.exitFocus();
}

void SeqEditor::focusScene(int s) {
    if (focusTrack_ < 0) return;
    focusScene_ = clampScene(s);
    rack.setFocusScene(focusScene_);
}

bool SeqEditor::keyPressed(const juce::KeyPress& k) {
    auto& g = rack.getGrid();
    const auto mods = k.getModifiers();
    const bool cmd = mods.isCommandDown();
    // Letter keycodes arrive uppercase from the OS but tests may build a
    // KeyPress from the lowercase character — normalise.
    int code = k.getKeyCode();
    if (code >= 'a' && code <= 'z') code -= 'a' - 'A';

    // Undo/redo (editing-concept: bounded snapshot stack over editing verbs)
    // work in session AND focus mode.
    if (cmd && code == 'Z') {
        if (mods.isShiftDown()) proc_.redo(); else proc_.undo();
        return true;
    }

    if (k == juce::KeyPress::escapeKey) {
        if (g.isDragActive()) { g.cancelActiveDrag(); return true; } // cancel the drag first
        if (focusTrack_ >= 0) { exitFocus(); return true; }
        if (g.hasSelection()) { g.clearSelection(); return true; }
        return false;
    }

    // Focus-mode keys keep their existing meanings (arrows = scene, digits =
    // device) — the grid verbs below are session-mode only.
    if (focusTrack_ >= 0) {
        if (k == juce::KeyPress::upKey)   { focusScene(focusScene_ - 1); return true; }
        if (k == juce::KeyPress::downKey) { focusScene(focusScene_ + 1); return true; }
        const auto ch = k.getTextCharacter();
        if (ch >= '1' && ch <= '4') { enterFocus((int)(ch - '1')); return true; }
        return false;
    }

    // Session-mode editing verbs over the grid selection.
    if (cmd && code == 'A') { g.selectAll(); return true; }
    if (cmd && code == 'C') { g.selCopy(); return true; }
    if (cmd && code == 'X') { g.selCut(); return true; }
    if (cmd && code == 'V') { g.selPaste(); return true; }
    if (cmd && code == 'D') { g.selDuplicate(); return true; }
    if (k.isKeyCode(juce::KeyPress::deleteKey) || k.isKeyCode(juce::KeyPress::backspaceKey)) {
        g.selDelete();
        return true;
    }
    const bool ext = mods.isShiftDown();
    if (k.isKeyCode(juce::KeyPress::upKey))    { g.moveSelection(-1, 0, ext); return true; }
    if (k.isKeyCode(juce::KeyPress::downKey))  { g.moveSelection(1, 0, ext); return true; }
    if (k.isKeyCode(juce::KeyPress::leftKey))  { g.moveSelection(0, -1, ext); return true; }
    if (k.isKeyCode(juce::KeyPress::rightKey)) { g.moveSelection(0, 1, ext); return true; }
    return false;
}

void SeqEditor::paint(juce::Graphics& g) {
    g.fillAll(fui::col::bg);
    // subtle top radial glow, like the web background
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff11141d), static_cast<float>(getWidth()) * 0.5f, -120.0f,
                                           fui::col::bg, static_cast<float>(getWidth()) * 0.5f,
                                           static_cast<float>(getHeight()) * 0.6f, true));
    g.fillRect(getLocalBounds());
}

void SeqEditor::resized() {
    const float sc = juce::jmin(static_cast<float>(getWidth()) / static_cast<float>(SeqRack::LW),
                               static_cast<float>(getHeight()) / static_cast<float>(SeqRack::LH));
    const float dx = (static_cast<float>(getWidth()) - static_cast<float>(SeqRack::LW) * sc) * 0.5f;
    const float dy = (static_cast<float>(getHeight()) - static_cast<float>(SeqRack::LH) * sc) * 0.5f;
    rack.setTransform(juce::AffineTransform::scale(sc).translated(dx, dy));
}
