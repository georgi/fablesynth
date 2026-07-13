#include "SeqEditor.h"
#include "../ui/Controls.h"

// Web layout, measured from the running SQ-4 app (src/seq/seq.css, #seq-rack
// at its 1460px max-width; page grid 218px repeat(4, 1fr), gap 9, padding
// 14/18/22; rack-relative px):
//   rack              1460 x 920
//   header            (18,  14) 1424 x 66
//   track heads       (18,  89) 1424 x 54
//   scene grid        (18, 152) 1424 x 630   (6 rows, 96 tall, 105 step)
//   footer            (18, 782) 1424 x 68
//   hint line          18, 858
// Scene grid columns: scene col (18, 218), then 4 track cols of 292 each,
// 9px gaps: x = 18 + 218 + 9 + i*(292 + 9).
//
// Focus mode reuses header + heads unchanged, collapses the scene grid to a
// single-row mini strip (row 96 + scene rail 24 = ~128 tall), and drops the
// selected native device body into the freed space down to the footer slot,
// which hides. Instant relayout, no FLIP (spec §2).

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
    hint.setVisible(false);
    deviceFocus.setVisible(true);
    resized();
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
    resized();
}

void SeqRack::setFocusScene(int scene) {
    if (!focusMode_) return;
    sceneGrid.setSingleRow(scene);
    deviceFocus.setTarget(scene, focusTrack_);
}

void SeqRack::resized() {
    header.setBounds(18, 14, 1424, 66);
    trackHeads.setBounds(18, 89, 1424, 54);
    if (focusMode_) {
        sceneGrid.setBounds(18, 152, 1424, 128);  // mini strip: row (96) + rail (24)
        deviceFocus.setBounds(18, 288, 1424, 618); // native device -> down to ~906
    } else {
        sceneGrid.setBounds(18, 152, 1424, 630);
        footer.setBounds(18, 782, 1424, 68);
        hint.setBounds(18, 858, 1424, 20);
        deviceFocus.setBounds(0, 0, LW, LH);
    }
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
    if (k == juce::KeyPress::escapeKey) {
        if (focusTrack_ >= 0) { exitFocus(); return true; }
        return false;
    }
    if (focusTrack_ < 0) return false;
    if (k == juce::KeyPress::upKey)   { focusScene(focusScene_ - 1); return true; }
    if (k == juce::KeyPress::downKey) { focusScene(focusScene_ + 1); return true; }
    const auto ch = k.getTextCharacter();
    if (ch >= '1' && ch <= '4') { enterFocus((int)(ch - '1')); return true; }
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
