#include "SeqEditor.h"
#include "../ui/Controls.h"
#include "../ui/EditorSize.h"

// SQ-4 navigates in its logo amber (the web's seq.css --nav: var(--ac-b)),
// including inside the hosted devices.
static const bool g_seqNavAccentInstalled = [] {
    fui::navAccentOverride() = fui::col::acB;
    return true;
}();

// Compact native layout, retaining the web's half-height header and vertical
// rhythm while removing excess width from the four clip columns. Both views
// share one 1290 x 722 rack, so opening a device never resizes the window:
//   header            (18,  14) 1254 x 44
//   track heads       (18,  67) 1254 x 60
//   scene grid        (18, 136) 1254 x 477   (6 rows stretched to fill: 73
//                                             tall, 9 px gaps -- layoutRow)
//   footer            (18, 621) 1254 x 65
//   hint line          18, 694
// Scene grid columns: scene col (18, 218), then 4 track cols of 250 each,
// 9px gaps: x = 18 + 218 + 9 + i*(250 + 9).
//
// Focus mode (re-pitched 2026-07-21 to follow the web) keeps the header and
// the heads row -- the heads double as the instrument switcher there, the
// open device ringed -- and splits everything below y=136 into two columns:
//   launcher column   (18, 136)  218 x 550   (SceneGridView's singleRow_ mode:
//                                             "< SESSION" + the same 6 scene
//                                             cards the grid draws, stacked --
//                                             see paintFocusStrip)
//   device body      (245, 136) 1027 x 550
// The footer hides. The launcher keeps the grid's 218 lead-column width, so
// both views share one left edge and the scene cards are literally the same
// painter. The switch between the session and focus geometries is instant
// (no animation, per the web) — see SeqRack::applyLayout().

// ---- SeqRack ----
SeqRack::SeqRack(SeqAudioProcessor& p)
    : header(p), trackHeads(p), sceneGrid(p), footer(p), deviceFocus(p), masterFx(p), sceneChords(p) {
    addAndMakeVisible(header);
    addAndMakeVisible(trackHeads);
    addAndMakeVisible(sceneGrid);
    addAndMakeVisible(footer);
    addAndMakeVisible(hint);
    addChildComponent(deviceFocus); // hidden until focus is entered
    addChildComponent(masterFx);
    addChildComponent(sceneChords);
    sceneGrid.onSceneChords = [this](int s) { sceneChords.open(s); };
}

void SeqRack::toggleMasterFx() {
    if (focusMode_) return;
    masterFxMode_ = !masterFxMode_;
    sceneGrid.setVisible(!masterFxMode_);
    footer.setVisible(!masterFxMode_);
    masterFx.setVisible(masterFxMode_);
    applyLayout();
}

void SeqRack::enterFocus(int track, int scene) {
    masterFxMode_ = false;
    masterFx.setVisible(false);
    focusMode_ = true;
    focusTrack_ = track;
    // The heads row stays up in focus and becomes the instrument switcher —
    // clicking another head opens that device, and the open one is ringed.
    trackHeads.setFocusTrack(track);
    sceneGrid.setSingleRow(scene, track);
    deviceFocus.setTarget(scene, track);
    footer.setVisible(false);
    deviceFocus.setVisible(true);
    applyLayout();
}

void SeqRack::exitFocus() {
    focusMode_ = false;
    focusTrack_ = -1;
    trackHeads.setFocusTrack(-1);
    sceneGrid.clearSingleRow();
    deviceFocus.setTarget(-1, -1);
    footer.setVisible(true);
    hint.setVisible(true);
    deviceFocus.setVisible(false);
    applyLayout();
}

void SeqRack::setFocusScene(int scene) {
    if (!focusMode_) return;
    sceneGrid.setSingleRow(scene, focusTrack_);
    deviceFocus.setTarget(scene, focusTrack_);
}

void SeqRack::resized() { applyLayout(); }

// Instant mode switch, no animation — matching the web, which switches
// focus mode without a transition. Session lays out the full table; focus
// keeps header + heads and splits everything below them into a narrow
// launcher rail and the device surface beside it.
void SeqRack::applyLayout() {
    constexpr int hintH = 14, contentY = 136;
    constexpr int hintY = LH - 14 /* bottom margin */ - hintH; // 722-14-14=694
    constexpr int contentBottom = hintY - 8;                    // 686
    constexpr int footerH = 65, footerY = contentBottom - footerH; // 621
    sceneChords.setBounds(0, 0, LW, LH);
    header.setBounds(18, 14, 1254, 44);
    trackHeads.setBounds(18, 67, 1254, 60); // stays visible in focus
    hint.setBounds(18, hintY, 1254, hintH);
    // Footer bounds are set unconditionally even though it's hidden in
    // focus mode — bounds on a hidden component are inert, so this stays
    // simple rather than branching twice.
    footer.setBounds(18, footerY, 1254, footerH);
    if (focusMode_) {
        // The launcher keeps the session grid's lead-column width so the two
        // views share one left edge (web: .sq-launcher width 218px, 9px gap).
        constexpr int railW = 218, railGap = 9;
        const int contentH = contentBottom - contentY;          // 550
        sceneGrid.setBounds(18, contentY, railW, contentH);
        deviceFocus.setBounds(18 + railW + railGap, contentY,
                              1254 - railW - railGap, contentH);
    } else if (masterFxMode_) {
        masterFx.setBounds(18, contentY, 1254, contentBottom - contentY); // grid + footer area
    } else {
        sceneGrid.setBounds(18, contentY, 1254, footerY - 8 - contentY);  // 477: six 73 px rows
    }
}

// ---- SeqEditor ----
SeqEditor::SeqEditor(SeqAudioProcessor& p)
    : juce::AudioProcessorEditor(p), proc_(p), rack(p), agentOverlay([&p]() -> fable::FableAgent& { return p.getAgent(); }) {
    setLookAndFeel(&lnf);
    addAndMakeVisible(rack);
    addAndMakeVisible(agentOverlay);
    agentOverlay.beforeStateRead = [this] { deviceFocus().flushPendingPatches(); };
    rack.setBounds(0, 0, SeqRack::LW, SeqRack::LH);

    // Focus wiring: a head click focuses that device (session mode only --
    // heads are hidden in focus), the focus strip's back chip exits, the
    // cell ✎ opens exactly that clip, and the focus strip's scene chips jump
    // scenes (docs/.../sq4-device-focus-design.md §2-§3, re-pitched
    // 2026-07-18 for the single-strip focus layout).
    heads().onFocusTrack = [this](int t) { enterFocus(t); };
    grid().onExitFocus   = [this]() { exitFocus(); };
    grid().onEditClip    = [this](int s, int t) { enterFocus(t, s); };
    grid().onRailScene   = [this](int s) { focusScene(s); };
    header().onLibrarySessionChanged = [this] { deviceFocus().reloadPatchesFromSession(); };
    header().onMasterFxToggle = [this] {
        if (focusTrack_ >= 0) exitFocus();
        rack.toggleMasterFx();
        header().repaint();
    };
    header().isMasterFxOpen = [this] { return rack.masterFxOpen(); };

    // Hint copy mirrors SeqApp.tsx's two .sq-hint strings.
    rack.getHint().setProvider([this]() -> juce::String {
        const auto dot = juce::String::fromUTF8(" \xc2\xb7 ");
        if (focusTrack_ >= 0)
            return "SCENE CHIPS RETARGET THE EDITOR" + dot
                 + "1-4 SWITCH DEVICE" + dot + "ESC BACK TO SESSION";
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
    const int width = fui::defaultEditorWidth(1080, (double)SeqRack::LH / SeqRack::LW);
    setSize(width, (int)(width * (double)SeqRack::LH / SeqRack::LW));
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
    if (agentOverlay.isPanelOpen()) return false;
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
    agentOverlay.setBounds(getLocalBounds());
    agentOverlay.toFront(false);
    // The rack's logical size is mode-dependent (session 1290x722, focus
    // 1290xLH -- SeqRack::logicalHeight()); the transform below just scales
    // that logical canvas to whatever the window currently is.
    const int lh = rack.logicalHeight();
    const float sc = juce::jmin(static_cast<float>(getWidth()) / static_cast<float>(SeqRack::LW),
                               static_cast<float>(getHeight()) / static_cast<float>(lh));
    const float dx = (static_cast<float>(getWidth()) - static_cast<float>(SeqRack::LW) * sc) * 0.5f;
    const float dy = (static_cast<float>(getHeight()) - static_cast<float>(lh) * sc) * 0.5f;
    rack.setTransform(juce::AffineTransform::scale(sc).translated(dx, dy));
}
