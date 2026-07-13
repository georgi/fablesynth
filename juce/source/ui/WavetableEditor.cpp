#include "WavetableEditor.h"
#include "../PluginProcessor.h"
#include "../dsp/UserTables.h"
#include "../dsp/FrameOps.h"

namespace fui {

// ============================ DrawPad ============================
DrawPad::DrawPad(juce::Colour ac) : pts(DRAW_N, 0.0f), accent(ac) {}

void DrawPad::clear() { std::fill(pts.begin(), pts.end(), 0.0f); lastIdx = -1; repaint(); if (onEdit) onEdit(); }

void DrawPad::paintAt(juce::Point<float> p) {
    if (readOnly) return;
    const float w = (float)getWidth(), h = (float)getHeight();
    if (w <= 0 || h <= 0) return;
    int idx = juce::jlimit(0, DRAW_N - 1, (int)std::round((p.x / w) * (DRAW_N - 1)));
    float val = juce::jlimit(-1.0f, 1.0f, 1.0f - 2.0f * (p.y / h));
    if (snap) val = std::round(val * 8.0f) / 8.0f;
    if (lastIdx >= 0 && lastIdx != idx) {
        int lo = juce::jmin(lastIdx, idx), hi = juce::jmax(lastIdx, idx);
        float v0 = pts[lastIdx];
        for (int i = lo; i <= hi; ++i) {
            float t = (idx == lastIdx) ? 1.0f : (float)(i - lastIdx) / (idx - lastIdx);
            pts[i] = v0 + (val - v0) * t;
        }
    } else {
        pts[idx] = val;
    }
    if (brush == Brush::Smooth) smoothAround(idx);
    lastIdx = idx;
    repaint(); // live pad repaint; the frame sync fires once on mouseUp
}
void DrawPad::mouseDown(const juce::MouseEvent& e) { if (readOnly) return; lastIdx = -1; paintAt(e.position); }
void DrawPad::mouseDrag(const juce::MouseEvent& e) { if (readOnly) return; paintAt(e.position); }
void DrawPad::mouseUp(const juce::MouseEvent&) { lastIdx = -1; if (onEdit) onEdit(); }

void DrawPad::smoothAround(int idx, int rad) {
    auto src = pts;
    const int n = (int)pts.size();
    for (int i = juce::jmax(0, idx - rad); i <= juce::jmin(n - 1, idx + rad); ++i) {
        float s = 0; int cnt = 0;
        for (int j = -2; j <= 2; ++j) { int k = i + j; if (k >= 0 && k < n) { s += src[(size_t)k]; ++cnt; } }
        pts[(size_t)i] = s / cnt;
    }
}

void DrawPad::seed(int kind) {
    for (int i = 0; i < DRAW_N; ++i) {
        const float x = (float)i / DRAW_N; float v = 0;
        if (kind == 0) v = std::sin(2.0f * juce::MathConstants<float>::pi * x);
        else if (kind == 1) v = 2 * x - 1;
        else if (kind == 2) v = x < 0.5f ? 0.9f : -0.9f;
        else v = 1 - 4 * std::abs(x - 0.5f);
        pts[(size_t)i] = v;
    }
    lastIdx = -1; repaint();
    if (onEdit) onEdit();
}

void DrawPad::setPoints(const std::vector<float>& p) {
    if (p.empty()) return;
    for (int i = 0; i < DRAW_N; ++i)
        pts[(size_t)i] = p[(size_t)juce::jlimit(0, (int)p.size() - 1, (int)((float)i / DRAW_N * (int)p.size()))];
    lastIdx = -1; repaint();
}

void DrawPad::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    drawDisplayBox(g, b, 6.0f);
    const float w = b.getWidth(), h = b.getHeight();
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawHorizontalLine((int)(h * 0.5f), 0.0f, w);
    juce::Path path;
    for (int i = 0; i < DRAW_N; ++i) {
        float x = (i / (float)(DRAW_N - 1)) * w;
        float y = h * 0.5f - pts[i] * (h * 0.5f - 4.0f);
        if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
    }
    g.setColour(accent.withAlpha(0.25f));
    g.strokePath(path, juce::PathStrokeType(4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(accent);
    g.strokePath(path, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ============================ TablePreview ============================
void TablePreview::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    drawDisplayBox(g, b, 6.0f);
    if (frames.empty()) {
        g.setColour(col::textDim);
        g.setFont(monoFont(9.0f));
        g.drawText("load an audio file to preview", getLocalBounds(), juce::Justification::centred);
        return;
    }
    const int nf = (int)frames.size();
    const float w = b.getWidth(), h = b.getHeight();
    const float depthX = w * 0.20f, depthY = h * 0.40f;
    const float waveW = w * 0.70f, waveAmp = h * 0.18f;
    const float x0 = w * 0.06f, y0 = h * 0.80f;
    const int maxDraw = juce::jmin(nf, 48); // cap drawn rows for cheap repaint
    constexpr int PTS = 160;               // downsample each frame for drawing
    for (int k = maxDraw - 1; k >= 0; --k) { // back-to-front
        const int f = (nf == 1) ? 0 : (int)std::round((float)k / (maxDraw - 1) * (nf - 1));
        const float d = (maxDraw == 1) ? 1.0f : (float)k / (maxDraw - 1);
        const auto& frame = frames[(size_t)f];
        const int n = (int)frame.size();
        if (n < 2) continue;
        const float ox = x0 + d * depthX, oy = y0 - d * depthY;
        juce::Path path;
        for (int i = 0; i < PTS; ++i) {
            const int si = (int)((float)i / (PTS - 1) * (n - 1));
            const float x = ox + (i / (float)(PTS - 1)) * waveW;
            const float y = oy - frame[(size_t)si] * waveAmp;
            if (i == 0) path.startNewSubPath(x, y);
            else        path.lineTo(x, y);
        }
        const bool isCur = (f == current);
        if (isCur) { g.setColour(accent); }
        else       { g.setColour(accent.withAlpha(0.18f + d * 0.4f)); }
        g.strokePath(path, juce::PathStrokeType(isCur ? 1.8f : 1.0f,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

// ============================ TableThumb ============================
void TableThumb::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    drawDisplayBox(g, b, 4.0f);
    const int N = juce::jmin((int)viz.size(), fable::VIZ_N);
    if (N < 2) return;
    const float w = b.getWidth(), h = b.getHeight();
    juce::Path p;
    for (int i = 0; i < N; ++i) {
        float x = (float)i / (N - 1) * w;
        float y = h * 0.5f - viz[(size_t)i] * h * 0.38f;
        if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
    }
    g.setColour(selected ? accent : juce::Colour(0xff8893a8));
    g.strokePath(p, juce::PathStrokeType(1.2f));
}

// ============================ FrameStrip ============================
FrameStrip::FrameStrip() {
    addBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
    addBtn.setColour(juce::TextButton::textColourOffId, col::acA);
    addBtn.onClick = [this] { if (onAdd) onAdd(); };
    addAndMakeVisible(addBtn);
}
void FrameStrip::setFrames(const std::vector<std::vector<float>>& frames, int cur, juce::Colour ac, bool ro) {
    count = (int)frames.size(); current = cur; accent = ac; readOnly = ro;
    thumbs.clear();
    for (int i = 0; i < count; ++i) {
        auto* t = new TableThumb();
        std::vector<float> viz = fable::framePoints(frames[(size_t)i], fable::VIZ_N);
        t->setData(viz, accent, i == current);
        t->setInterceptsMouseClicks(false, false); // clicks/drag go to the strip
        thumbs.add(t);
        addChildComponent(t);
        t->setVisible(true);
    }
    addBtn.setVisible(!readOnly && count < fable::MAX_FRAMES);
    resized();
    repaint();
}
int FrameStrip::indexAtX(int x) const {
    int i = x / (cellW + gap);
    return juce::jlimit(0, juce::jmax(0, count - 1), i);
}
void FrameStrip::resized() {
    int x = 0;
    for (int i = 0; i < thumbs.size(); ++i) { thumbs[i]->setBounds(x, 2, cellW, getHeight() - 4); x += cellW + gap; }
    addBtn.setBounds(x, 2, 30, getHeight() - 4);
}
void FrameStrip::mouseDown(const juce::MouseEvent& e) {
    int i = indexAtX(e.x);
    if (i == current && !readOnly && count > 1) {
        auto cell = juce::Rectangle<int>(current * (cellW + gap), 2, cellW, getHeight() - 4);
        if (e.x >= cell.getRight() - 12 && e.y <= cell.getY() + 14) { if (onDelete) onDelete(i); return; }
    }
    if (i < count && onSelect) onSelect(i);
    dragFrom = readOnly ? -1 : i;
}
void FrameStrip::mouseDrag(const juce::MouseEvent& e) {
    if (dragFrom < 0) return;
    int to = indexAtX(e.x);
    if (to != dragFrom && onReorder) { onReorder(dragFrom, to); dragFrom = to; }
}
void FrameStrip::mouseUp(const juce::MouseEvent&) { dragFrom = -1; }
void FrameStrip::paint(juce::Graphics& g) {
    // selected-cell outline + a small delete affordance on the current cell.
    if (current < 0 || current >= count) return;
    auto r = juce::Rectangle<int>(current * (cellW + gap), 2, cellW, getHeight() - 4).toFloat();
    g.setColour(accent.withAlpha(0.55f));
    g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.2f);
    if (!readOnly && count > 1) {
        g.setColour(juce::Colour(0xffc46b6b));
        g.setFont(monoFont(10.0f));
        g.drawText("x", r.removeFromRight(12).removeFromTop(12), juce::Justification::centred);
    }
}

// ============================ LibRow ============================
WavetableEditor::LibRow::LibRow(bool factory, juce::Colour accent) : accentColour(accent), isFactory(factory) {
    // Display-only children must not eat clicks, or only the bare gaps between
    // them would reach the row's mouseDown (-> onSelect).
    addAndMakeVisible(thumb);
    thumb.setInterceptsMouseClicks(false, false);
    name.setFont(monoFont(11.0f)); name.setColour(juce::Label::textColourId, col::text);
    name.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(name);
    sub.setFont(monoFont(9.0f)); sub.setColour(juce::Label::textColourId, col::textDim);
    sub.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(sub);
    renameField.setFont(monoFont(11.0f));
    renameField.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0a0d13));
    renameField.setColour(juce::TextEditor::textColourId, accent);
    renameField.setInputRestrictions(14);
    addChildComponent(renameField);
    for (auto* b : { &rename, &dup, &del }) {
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
        b->setColour(juce::TextButton::textColourOffId, col::textDim);
    }
    addAndMakeVisible(dup);
    if (!factory) { addAndMakeVisible(rename); addAndMakeVisible(del); }
    juce::ignoreUnused(accent);
}
void WavetableEditor::LibRow::resized() {
    auto r = getLocalBounds();
    thumb.setBounds(r.removeFromLeft(46).reduced(0, 1));
    r.removeFromLeft(8);
    auto btns = r.removeFromRight(isFactory ? 24 : 72);
    if (!isFactory) {
        rename.setBounds(btns.removeFromLeft(22).reduced(1));
        btns.removeFromLeft(2);
    }
    dup.setBounds(btns.removeFromLeft(22).reduced(1));
    if (!isFactory) { btns.removeFromLeft(2); del.setBounds(btns.reduced(1)); }
    auto info = r;
    name.setBounds(info.removeFromTop(info.getHeight() / 2));
    sub.setBounds(info);
    renameField.setBounds(r.withSizeKeepingCentre(r.getWidth(), 22));
}
void WavetableEditor::LibRow::mouseDown(const juce::MouseEvent&) { if (onSelect) onSelect(); }
void WavetableEditor::LibRow::paint(juce::Graphics& g) {
    if (!selected) return;
    auto b = getLocalBounds().toFloat();
    g.setColour(accentColour.withAlpha(0.08f));
    g.fillRoundedRectangle(b, 8.0f);
    g.setColour(accentColour.withAlpha(0.45f));
    g.drawRoundedRectangle(b.reduced(0.5f), 8.0f, 1.0f);
}

// ============================ WavetableEditor ============================
WavetableEditor::WavetableEditor(WtUiModel& m) : model(m), drawPad(col::acA) {
    formatMgr.registerBasicFormats();
    setInterceptsMouseClicks(true, true);

    auto styleTab = [this](juce::TextButton& b, Tab t) {
        b.setClickingTogglesState(false);
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
        b.setColour(juce::TextButton::textColourOffId, col::textDim);
        b.onClick = [this, t] { setTab(t); };
        addAndMakeVisible(b);
    };
    styleTab(tabAudio, Tab::Audio);
    styleTab(tabDraw,  Tab::Draw);

    auto styleBtn = [this](juce::TextButton& b) {
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
        b.setColour(juce::TextButton::textColourOffId, col::text);
        addAndMakeVisible(b);
    };

    closeBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a1f2b));
    closeBtn.setColour(juce::TextButton::textColourOffId, col::textDim);
    closeBtn.onClick = [this] { close(); };
    addAndMakeVisible(closeBtn);

    nameLabel.setFont(monoFont(10.0f));
    nameLabel.setColour(juce::Label::textColourId, col::textDim);
    addAndMakeVisible(nameLabel);
    nameField.setFont(monoFont(12.0f));
    nameField.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0a0d13));
    nameField.setColour(juce::TextEditor::textColourId, col::text);
    nameField.setInputRestrictions(14);
    addAndMakeVisible(nameField);

    // audio tab
    styleBtn(fileBtn);
    fileBtn.onClick = [this] { chooseFile(); };
    auto styleMode = [this](juce::TextButton& b, AudioMode m) {
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
        b.setColour(juce::TextButton::textColourOffId, col::textDim);
        b.onClick = [this, m] { setMode(m); };
        addAndMakeVisible(b);
    };
    styleMode(modeSingle, AudioMode::Single);
    styleMode(modeAuto,   AudioMode::Auto);
    styleMode(modeFixed,  AudioMode::Fixed);

    fixedLabel.setFont(monoFont(10.0f));
    fixedLabel.setColour(juce::Label::textColourId, col::textDim);
    addAndMakeVisible(fixedLabel);
    fixedField.setText("2048", juce::dontSendNotification);
    fixedField.setFont(monoFont(12.0f));
    fixedField.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0a0d13));
    fixedField.setColour(juce::TextEditor::textColourId, col::text);
    fixedField.setInputRestrictions(6, "0123456789");
    fixedField.onTextChange = [this] { updatePreview(); };
    addAndMakeVisible(fixedField);

    addChildComponent(audioPreview);

    statusLabel.setFont(monoFont(10.0f));
    statusLabel.setColour(juce::Label::textColourId, col::textDim);
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel);

    hintLabel.setFont(monoFont(9.0f));
    hintLabel.setColour(juce::Label::textColourId, col::textDim);
    hintLabel.setJustificationType(juce::Justification::topLeft);
    hintLabel.setText("SINGLE CYCLE: whole clip = 1 frame. AUTO-DETECT: estimate pitch, "
                      "slice per cycle. FIXED LEN: slice every N samples. Up to "
                      + juce::String(fable::MAX_FRAMES) + " frames.",
                      juce::dontSendNotification);
    addAndMakeVisible(hintLabel);

    styleBtn(createAudioBtn);
    createAudioBtn.onClick = [this] { createFromAudio(); };

    // draw tab
    addChildComponent(drawPad);
    drawHint.setFont(monoFont(9.0f));
    drawHint.setColour(juce::Label::textColourId, col::textDim);
    drawHint.setText("Drag to draw one cycle. Band-limited on commit. -> 1 frame.",
                     juce::dontSendNotification);
    addChildComponent(drawHint);
    styleBtn(clearBtn);
    clearBtn.onClick = [this] {
        if (readOnlySel) return;
        drawPad.clear(); // fires onEdit -> syncCurrentFrame
    };
    addChildComponent(clearBtn);
    styleBtn(createDrawBtn);
    createDrawBtn.onClick = [this] { createFromDraw(); };
    addChildComponent(createDrawBtn);

    // draw tools — seed shapes, brush, snap.
    auto styleTool = [this](juce::TextButton& b) {
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
        b.setColour(juce::TextButton::textColourOffId, col::textDim);
        addChildComponent(b);
    };
    seedLabel.setFont(monoFont(9.0f));
    seedLabel.setColour(juce::Label::textColourId, col::textDim);
    addChildComponent(seedLabel);
    auto seedTo = [this](int kind) {
        if (readOnlySel) return;
        drawPad.seed(kind); // fires onEdit -> syncCurrentFrame
    };
    styleTool(seedSine);   seedSine.onClick   = [seedTo] { seedTo(0); };
    styleTool(seedSaw);    seedSaw.onClick    = [seedTo] { seedTo(1); };
    styleTool(seedSquare); seedSquare.onClick = [seedTo] { seedTo(2); };
    styleTool(seedTri);    seedTri.onClick    = [seedTo] { seedTo(3); };
    styleTool(brushPen);   brushPen.onClick   = [this] { drawPad.setBrush(DrawPad::Brush::Pen); repaint(); };
    styleTool(brushSmooth);brushSmooth.onClick= [this] { drawPad.setBrush(DrawPad::Brush::Smooth); repaint(); };
    styleTool(snapBtn);    snapBtn.onClick    = [this] { snapOn = !snapOn; drawPad.setSnap(snapOn); repaint(); };

    // library — title, count, search, + NEW.
    libTitle.setFont(dispFont(10.0f));
    libTitle.setColour(juce::Label::textColourId, col::textDim);
    addAndMakeVisible(libTitle);
    newBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff11141c));
    newBtn.setColour(juce::TextButton::textColourOffId, col::text);
    newBtn.onClick = [this] {
        std::vector<std::vector<float>> fs{ fable::frameFromDrawing(
            [this]{ drawPad.seed(0); return drawPad.points(); }()) };
        const int idx = (int)model.userTables().size();
        if (model.addUserTable(fable::makeUserTable("USER", fs)) < 0) return;
        selectedId = "u" + juce::String(idx); readOnlySel = false; drawPad.setReadOnly(false);
        nameField.setText("USER", juce::dontSendNotification);
        assignTable((int)model.factoryTables().size() + idx);
        loadFrames(fs);
        setTab(Tab::Draw);
        refreshLibrary();
    };
    addAndMakeVisible(newBtn);
    searchField.setFont(monoFont(11.0f));
    searchField.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff07090e));
    searchField.setColour(juce::TextEditor::textColourId, col::text);
    searchField.setTextToShowWhenEmpty("search tables", col::textDim);
    searchField.onTextChange = [this] { refreshLibrary(); };
    addAndMakeVisible(searchField);

    drawPad.onEdit = [this] { syncCurrentFrame(); };
    addChildComponent(frameStrip);
    frameStrip.onSelect  = [this](int i) { gotoFrame(i); };
    frameStrip.onAdd     = [this] { addFrameOp(); };
    frameStrip.onDelete  = [this](int i) { deleteFrameOp(i); };
    frameStrip.onReorder = [this](int a, int b) { reorderFrameOp(a, b); };
    addChildComponent(stackPreview);

    setVisible(false);
}

WavetableEditor::WavetableEditor(FableAudioProcessor& p)
    : WavetableEditor(*new StandaloneWtUiModel(p)) {
    ownedModel.reset(&model);
}

juce::Colour WavetableEditor::accent() const { return oscIndex == 0 ? col::acA : col::acB; }

void WavetableEditor::openFor(int osc) {
    oscIndex = osc;
    tab = Tab::Draw;
    mode = AudioMode::Single;
    hasAudio = false;
    selectedId = {};
    readOnlySel = false;
    snapOn = false;
    drawPad.setAccent(accent());
    drawPad.setReadOnly(false);
    drawPad.setBrush(DrawPad::Brush::Pen);
    drawPad.setSnap(false);
    audioPreview.setAccent(accent());
    stackPreview.setAccent(accent());
    audioSamples.clear();
    nameField.setText("", juce::dontSendNotification);
    statusLabel.setText("load an audio file to begin", juce::dontSendNotification);
    drawPad.clear();
    loadFrames({ std::vector<float>((size_t)fable::SIZE, 0.0f) });
    audioPreview.clear();
    setTab(Tab::Draw);
    setMode(AudioMode::Single);
    refreshLibrary();
    setVisible(true);
    toFront(true);
    resized();
    repaint();
}

void WavetableEditor::close() { setVisible(false); }

void WavetableEditor::mouseDown(const juce::MouseEvent& e) {
    // Click on the dimmed backdrop (outside the panel) closes the editor.
    if (!panelBounds().contains(e.getPosition())) close();
}

void WavetableEditor::setTab(Tab t) {
    tab = t;
    const bool audio = t == Tab::Audio;
    fileBtn.setVisible(audio); modeSingle.setVisible(audio); modeAuto.setVisible(audio);
    modeFixed.setVisible(audio); statusLabel.setVisible(audio); hintLabel.setVisible(audio);
    createAudioBtn.setVisible(audio);
    audioPreview.setVisible(audio);
    fixedLabel.setVisible(audio && mode == AudioMode::Fixed);
    fixedField.setVisible(audio && mode == AudioMode::Fixed);
    drawPad.setVisible(!audio); drawHint.setVisible(!audio);
    clearBtn.setVisible(!audio); createDrawBtn.setVisible(!audio);
    seedLabel.setVisible(!audio);
    seedSine.setVisible(!audio); seedSaw.setVisible(!audio);
    seedSquare.setVisible(!audio); seedTri.setVisible(!audio);
    brushPen.setVisible(!audio); brushSmooth.setVisible(!audio); snapBtn.setVisible(!audio);
    frameStrip.setVisible(!audio);
    stackPreview.setVisible(!audio);
    if (!audio) {
        drawHint.setText(readOnlySel
            ? "Read-only (factory). Duplicate to edit."
            : "Drag to draw the selected frame. POS morphs through frames on play.",
            juce::dontSendNotification);
        createDrawBtn.setButtonText(selectedId.isNotEmpty() && selectedId[0] == 'u' ? "UPDATE TABLE" : "CREATE TABLE");
        createDrawBtn.setEnabled(!readOnlySel);
        frameStrip.setFrames(frames, currentFrame, accent(), readOnlySel);
    }
    layoutPanel();
    repaint();
}

void WavetableEditor::setMode(AudioMode m) {
    mode = m;
    fixedLabel.setVisible(tab == Tab::Audio && m == AudioMode::Fixed);
    fixedField.setVisible(tab == Tab::Audio && m == AudioMode::Fixed);
    layoutPanel();   // showing/hiding the CYCLE row shifts the layout
    updatePreview(); // the slicing mode changes the frames
    repaint();
}

void WavetableEditor::chooseFile() {
    chooser = std::make_unique<juce::FileChooser>("Select an audio file",
                                                  juce::File{}, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    auto fcFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    // SafePointer guards against the editor being torn down while the OS dialog
    // is open (the callback would otherwise touch a destroyed component).
    juce::Component::SafePointer<WavetableEditor> safe(this);
    chooser->launchAsync(fcFlags, [safe](const juce::FileChooser& fc) {
        if (safe == nullptr) return;
        auto* self = safe.getComponent();
        auto file = fc.getResult();
        if (file == juce::File{}) return;
        std::unique_ptr<juce::AudioFormatReader> reader(self->formatMgr.createReaderFor(file));
        if (!reader) { self->statusLabel.setText("decode failed", juce::dontSendNotification); return; }
        // Cap how much we pull into memory so a very long clip can't blow up the
        // allocation (analysis only needs a bounded window anyway).
        constexpr int kMaxImportSamples = 1 << 22; // ~4M samples (~95 s @ 44.1k)
        int n = (int)juce::jmin((juce::int64)kMaxImportSamples, reader->lengthInSamples);
        juce::AudioBuffer<float> buf((int)reader->numChannels, juce::jmax(1, n));
        reader->read(&buf, 0, n, 0, true, true);
        const float* const* chans = buf.getArrayOfReadPointers();
        self->audioSamples = fable::mixToMono(chans, buf.getNumChannels(), n);
        self->audioSr = reader->sampleRate;
        self->hasAudio = true;
        if (self->nameField.getText().isEmpty())
            self->nameField.setText(file.getFileNameWithoutExtension().toUpperCase().substring(0, 14),
                                    juce::dontSendNotification);
        self->statusLabel.setText(juce::String(n / juce::jmax(1.0, self->audioSr), 2) + " s | "
                            + juce::String((int)self->audioSr) + " Hz | " + juce::String(n) + " samples",
                            juce::dontSendNotification);
        self->updatePreview();
    });
}

std::vector<std::vector<float>> WavetableEditor::framesFromCurrentSettings() const {
    if (!hasAudio || audioSamples.empty()) return {};
    if (mode == AudioMode::Single) return fable::singleCycleFrame(audioSamples);
    if (mode == AudioMode::Auto)
        return fable::sliceToFrames(audioSamples, fable::detectCycleLength(audioSamples, audioSr));
    int len = juce::jmax(2, fixedField.getText().getIntValue());
    return fable::sliceToFrames(audioSamples, (double)len);
}

void WavetableEditor::updatePreview() {
    if (tab == Tab::Audio) audioPreview.setFrames(framesFromCurrentSettings());
}

void WavetableEditor::commit(fable::UserTable u) {
    int idx = model.addUserTable(std::move(u));
    if (idx < 0) {
        // Pool is full — keep the editor open and surface why, rather than
        // closing as if the table had been added.
        setTab(Tab::Audio); // the status line lives on the audio tab
        statusLabel.setText("table pool full (max " + juce::String(model.maxUserTables())
                            + ") — delete one first", juce::dontSendNotification);
        refreshLibrary();
        return;
    }
    // Assign the new table to the oscillator that opened the editor.
    if (auto* p = model.parameters().parameter(oscIndex == 0 ? "oscA.table" : "oscB.table"))
        p->setValueNotifyingHost(p->convertTo0to1((float)idx));
    close();
}

static std::string finalName(const juce::String& raw) {
    juce::String s = raw.trim().toUpperCase();
    if (s.isEmpty()) s = "USER";
    return s.substring(0, 14).toStdString();
}

void WavetableEditor::createFromAudio() {
    auto fs = framesFromCurrentSettings();
    if (fs.empty()) return;
    selectedId = {}; readOnlySel = false; drawPad.setReadOnly(false);
    loadFrames(std::move(fs));
    setTab(Tab::Draw);
}

void WavetableEditor::createFromDraw() {
    if (readOnlySel) return;
    auto u = fable::makeUserTable(finalName(nameField.getText()), frames);
    if (selectedId.isNotEmpty() && selectedId[0] == 'u') {
        const int idx = selectedId.substring(1).getIntValue();
        model.updateUserTable(idx, std::move(u));
        assignTable((int)model.factoryTables().size() + idx);
        refreshLibrary();
        return;
    }
    commit(std::move(u));
}

void WavetableEditor::assignTable(int combinedIndex) {
    if (auto* p = model.parameters().parameter(oscIndex == 0 ? "oscA.table" : "oscB.table"))
        p->setValueNotifyingHost(p->convertTo0to1((float)combinedIndex));
}

void WavetableEditor::loadFrames(std::vector<std::vector<float>> fs) {
    if (fs.empty()) fs.emplace_back((size_t)fable::SIZE, 0.0f);
    frames = std::move(fs);
    currentFrame = 0;
    drawPad.setPoints(frames[0]);
    frameStrip.setFrames(frames, currentFrame, accent(), readOnlySel);
    stackPreview.setFrames(frames); stackPreview.setCurrent(currentFrame);
}
void WavetableEditor::gotoFrame(int i) {
    currentFrame = juce::jlimit(0, (int)frames.size() - 1, i);
    drawPad.setPoints(frames[(size_t)currentFrame]);
    frameStrip.setFrames(frames, currentFrame, accent(), readOnlySel);
    stackPreview.setCurrent(currentFrame);
}
void WavetableEditor::syncCurrentFrame() {
    if (readOnlySel || currentFrame < 0 || currentFrame >= (int)frames.size()) return;
    frames[(size_t)currentFrame] = fable::frameFromDrawing(drawPad.points());
    frameStrip.setFrames(frames, currentFrame, accent(), readOnlySel);
    stackPreview.setFrames(frames); stackPreview.setCurrent(currentFrame);
}
void WavetableEditor::addFrameOp() {
    if (readOnlySel) return;
    auto next = fable::duplicateFrame(frames, currentFrame);
    if (next.size() == frames.size()) { statusLabel.setText("max " + juce::String(fable::MAX_FRAMES) + " frames", juce::dontSendNotification); return; }
    frames = std::move(next);
    gotoFrame(currentFrame + 1);
}
void WavetableEditor::deleteFrameOp(int i) {
    if (readOnlySel) return;
    auto next = fable::deleteFrame(frames, i);
    if (next.size() == frames.size()) return;
    frames = std::move(next);
    gotoFrame(juce::jmin(i, (int)frames.size() - 1));
}
void WavetableEditor::reorderFrameOp(int from, int to) {
    if (readOnlySel) return;
    frames = fable::moveFrame(frames, from, to);
    gotoFrame(to);
}

void WavetableEditor::selectFactory(int i) {
    const auto& fac = model.factoryTables();
    if (i < 0 || i >= (int)fac.size()) return;
    selectedId = "f" + juce::String(i);
    nameField.setText(juce::String(fac[(size_t)i]->name), juce::dontSendNotification);
    readOnlySel = true; drawPad.setReadOnly(true);
    loadFrames(fable::framesFromGenerated(*fac[(size_t)i]));
    setTab(Tab::Draw);
    assignTable(i);
    refreshLibrary();
}

void WavetableEditor::selectUser(int i) {
    const auto& pool = model.userTables();
    if (i < 0 || i >= (int)pool.size()) return;
    selectedId = "u" + juce::String(i);
    nameField.setText(juce::String(pool[(size_t)i].name), juce::dontSendNotification);
    readOnlySel = false; drawPad.setReadOnly(false);
    loadFrames(fable::framesFromWave(pool[(size_t)i].wave, pool[(size_t)i].frames));
    setTab(Tab::Draw);
    assignTable((int)model.factoryTables().size() + i);
    refreshLibrary();
}

void WavetableEditor::refreshLibrary() {
    libRows.clear();
    const juce::String q = searchField.getText().trim().toUpperCase();
    auto matches = [&q](const juce::String& nm) { return q.isEmpty() || nm.toUpperCase().contains(q); };

    const auto& fac = model.factoryTables();
    for (int i = 0; i < (int)fac.size(); ++i) {
        const juce::String nm(fac[(size_t)i]->name);
        if (!matches(nm)) continue;
        const juce::String id = "f" + juce::String(i);
        auto* row = new LibRow(true, accent());
        row->selected = (selectedId == id);
        row->name.setText(nm, juce::dontSendNotification);
        row->sub.setText(juce::String(fac[(size_t)i]->frames) + "f - FACTORY", juce::dontSendNotification);
        std::vector<float> viz(fac[(size_t)i]->viz.begin(),
                               fac[(size_t)i]->viz.begin() + juce::jmin((int)fac[(size_t)i]->viz.size(), fable::VIZ_N));
        row->thumb.setData(viz, accent(), row->selected);
        row->onSelect = [this, i] { selectFactory(i); };
        row->dup.onClick = [this, i] { int ni = model.duplicateFactoryTable(i); if (ni >= 0) assignTable(ni); refreshLibrary(); };
        libRows.add(row);
        addAndMakeVisible(row);
    }

    const auto& pool = model.userTables();
    for (int i = 0; i < (int)pool.size(); ++i) {
        const juce::String nm(pool[(size_t)i].name);
        if (!matches(nm)) continue;
        const juce::String id = "u" + juce::String(i);
        auto* row = new LibRow(false, accent());
        row->selected = (selectedId == id);
        row->name.setText(nm, juce::dontSendNotification);
        row->sub.setText(juce::String(pool[(size_t)i].frames) + "f", juce::dontSendNotification);
        const auto& vz = pool[(size_t)i].table->viz;
        std::vector<float> viz(vz.begin(), vz.begin() + juce::jmin((int)vz.size(), fable::VIZ_N));
        row->thumb.setData(viz, accent(), row->selected);
        row->onSelect = [this, i] { selectUser(i); };
        row->dup.onClick = [this, i] { int ni = model.duplicateUserTable(i); if (ni >= 0) assignTable(ni); refreshLibrary(); };
        row->del.onClick = [this, i] {
            model.deleteUserTable(i);
            if (selectedId == ("u" + juce::String(i))) { selectedId = {}; readOnlySel = false; drawPad.setReadOnly(false); }
            refreshLibrary();
        };
        // inline rename: swap a TextEditor over the name slot, commit on focus loss / Enter.
        auto* re = &row->renameField;
        auto* nameLbl = &row->name;
        auto* subLbl = &row->sub;
        row->rename.onClick = [this, i, re, nameLbl, subLbl] {
            re->setText(nameLbl->getText(), juce::dontSendNotification);
            nameLbl->setVisible(false); subLbl->setVisible(false);
            re->setVisible(true); re->grabKeyboardFocus(); re->selectAll();
            auto commit = [this, i, re] {
                model.renameUserTable(i, re->getText().toStdString());
                refreshLibrary();
            };
            re->onReturnKey = commit;
            re->onFocusLost = commit;
            re->onEscapeKey = [this] { refreshLibrary(); };
        };
        libRows.add(row);
        addAndMakeVisible(row);
    }
    layoutPanel();
}

juce::Rectangle<int> WavetableEditor::panelBounds() const {
    const int w = juce::jmin(1180, getWidth() - 40);
    const int h = juce::jmin(760, getHeight() - 40);
    return juce::Rectangle<int>(0, 0, w, h).withCentre(getLocalBounds().getCentre());
}

void WavetableEditor::resized() { layoutPanel(); }

void WavetableEditor::layoutLibrary(juce::Rectangle<int> area) {
    area = area.reduced(10, 8);
    auto top = area.removeFromTop(26);
    libTitle.setBounds(top.removeFromLeft(120));
    newBtn.setBounds(top.removeFromRight(70));
    area.removeFromTop(6);
    searchField.setBounds(area.removeFromTop(30));
    area.removeFromTop(8);
    for (auto* row : libRows) {
        if (area.getHeight() < 40) { row->setBounds({}); continue; }
        row->setBounds(area.removeFromTop(42));
        area.removeFromTop(4);
    }
}

void WavetableEditor::layoutPanel() {
    auto full = panelBounds();
    auto head = full.removeFromTop(50);
    closeBtn.setBounds(head.removeFromRight(48).removeFromLeft(30).withSizeKeepingCentre(30, 30));

    auto lib = full.removeFromLeft(306);
    dividerX = lib.getRight();
    layoutLibrary(lib);

    auto panel = full.reduced(18);

    // DRAW first (mockup), then IMPORT AUDIO.
    auto tabs = panel.removeFromTop(24);
    tabDraw.setBounds(tabs.removeFromLeft(tabs.getWidth() / 2).reduced(2, 0));
    tabAudio.setBounds(tabs.reduced(2, 0));
    panel.removeFromTop(10);

    auto nameRow = panel.removeFromTop(22);
    nameLabel.setBounds(nameRow.removeFromLeft(54));
    nameField.setBounds(nameRow);
    panel.removeFromTop(10);

    if (tab == Tab::Audio) {
        fileBtn.setBounds(panel.removeFromTop(26));
        panel.removeFromTop(8);
        auto modes = panel.removeFromTop(24);
        int mw = modes.getWidth() / 3;
        modeSingle.setBounds(modes.removeFromLeft(mw).reduced(2, 0));
        modeAuto.setBounds(modes.removeFromLeft(mw).reduced(2, 0));
        modeFixed.setBounds(modes.reduced(2, 0));
        panel.removeFromTop(8);
        if (mode == AudioMode::Fixed) {
            auto fr = panel.removeFromTop(22);
            fixedLabel.setBounds(fr.removeFromLeft(54));
            fixedField.setBounds(fr.removeFromLeft(90));
            panel.removeFromTop(8);
        }
        statusLabel.setBounds(panel.removeFromTop(18));
        panel.removeFromTop(6);
        hintLabel.setBounds(panel.removeFromTop(46));
        panel.removeFromTop(10);
        audioPreview.setBounds(panel.removeFromTop(140));
        panel.removeFromTop(10);
        createAudioBtn.setBounds(panel.removeFromTop(30));
    } else {
        // Tools row (bottom) + hint, then strip, then [pad | stack] fill the rest.
        auto tools = panel.removeFromBottom(34);
        createDrawBtn.setBounds(tools.removeFromRight(150).reduced(2, 0));
        tools.removeFromRight(6);
        clearBtn.setBounds(tools.removeFromRight(70).reduced(2, 0));
        tools.removeFromRight(8);
        seedLabel.setBounds(tools.removeFromLeft(40));
        auto tool = [&tools](juce::Component& c, int w) { c.setBounds(tools.removeFromLeft(w).reduced(2, 0)); };
        tool(seedSine, 58); tool(seedSaw, 50); tool(seedSquare, 72); tool(seedTri, 50);
        tools.removeFromLeft(8);
        tool(brushPen, 50); tool(brushSmooth, 72); tool(snapBtn, 56);
        panel.removeFromBottom(8);
        drawHint.setBounds(panel.removeFromBottom(16));
        panel.removeFromBottom(8);
        frameStrip.setBounds(panel.removeFromBottom(34));
        panel.removeFromBottom(8);
        auto stack = panel.removeFromRight(220);
        stackPreview.setBounds(stack);
        panel.removeFromRight(12);
        drawPad.setBounds(panel); // fills the rest
    }
    repaint();
}

void WavetableEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black.withAlpha(0.6f)); // dim backdrop

    auto pb = panelBounds().toFloat();
    drawPanel(g, pb, 12.0f);
    // accent top edge (scanline)
    g.setColour(accent());
    g.fillRect(pb.getX() + 12.0f, pb.getY(), pb.getWidth() - 24.0f, 2.0f);

    // header (50px) — title left, divider below.
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.fillRect(pb.getX() + 12.0f, pb.getY() + 50.0f, pb.getWidth() - 24.0f, 1.0f);
    // library / editor divider.
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.fillRect((float)dividerX, pb.getY() + 50.0f, 1.0f, pb.getHeight() - 50.0f);

    // header title
    g.setColour(accent());
    g.setFont(dispFont(13.0f));
    auto titleArea = juce::Rectangle<int>((int)pb.getX() + 18, (int)pb.getY() + 14, (int)pb.getWidth() - 80, 22);
    drawSpaced(g, juce::String("WAVETABLE -> ") + (oscIndex == 0 ? "OSC A" : "OSC B"),
               titleArea, 1.0f, juce::Justification::centredLeft);

    // highlight the active tab / mode / tool underline
    auto underline = [&](juce::Component& c, bool on) {
        if (!c.isVisible()) return;
        if (!on) return;
        auto b = c.getBounds().toFloat();
        g.setColour(accent());
        g.fillRect(b.getX() + 2, b.getBottom() - 2, b.getWidth() - 4, 2.0f);
    };
    underline(tabAudio, tab == Tab::Audio);
    underline(tabDraw,  tab == Tab::Draw);
    if (tab == Tab::Audio) {
        underline(modeSingle, mode == AudioMode::Single);
        underline(modeAuto,   mode == AudioMode::Auto);
        underline(modeFixed,  mode == AudioMode::Fixed);
    } else {
        underline(brushPen,    drawPad.getBrush() == DrawPad::Brush::Pen);
        underline(brushSmooth, drawPad.getBrush() == DrawPad::Brush::Smooth);
        underline(snapBtn,     snapOn);
    }
}

} // namespace fui
