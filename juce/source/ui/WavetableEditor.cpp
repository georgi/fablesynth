#include "WavetableEditor.h"
#include "../dsp/UserTables.h"

namespace fui {

// ============================ DrawPad ============================
DrawPad::DrawPad(juce::Colour ac) : pts(DRAW_N, 0.0f), accent(ac) {}

void DrawPad::clear() { std::fill(pts.begin(), pts.end(), 0.0f); lastIdx = -1; repaint(); }

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
    repaint();
}
void DrawPad::mouseDown(const juce::MouseEvent& e) { if (readOnly) return; lastIdx = -1; paintAt(e.position); }
void DrawPad::mouseDrag(const juce::MouseEvent& e) { if (readOnly) return; paintAt(e.position); }
void DrawPad::mouseUp(const juce::MouseEvent&) { lastIdx = -1; }

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
        g.setColour(accent.withAlpha(0.18f + d * 0.72f));
        g.strokePath(path, juce::PathStrokeType(d > 0.85f ? 1.6f : 1.0f,
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

// ============================ LibRow ============================
WavetableEditor::LibRow::LibRow(bool factory, juce::Colour accent) : isFactory(factory) {
    addAndMakeVisible(thumb);
    name.setFont(monoFont(11.0f)); name.setColour(juce::Label::textColourId, col::text);
    addAndMakeVisible(name);
    sub.setFont(monoFont(9.0f)); sub.setColour(juce::Label::textColourId, col::textDim);
    addAndMakeVisible(sub);
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
    name.setBounds(r.removeFromTop(r.getHeight() / 2));
    sub.setBounds(r);
}

// ============================ WavetableEditor ============================
WavetableEditor::WavetableEditor(FableAudioProcessor& p) : proc(p), drawPad(col::acA) {
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
    clearBtn.onClick = [this] { drawPad.clear(); };
    addChildComponent(clearBtn);
    styleBtn(createDrawBtn);
    createDrawBtn.onClick = [this] { createFromDraw(); };
    addChildComponent(createDrawBtn);

    listHeader.setFont(monoFont(9.0f));
    listHeader.setColour(juce::Label::textColourId, col::textDim);
    addAndMakeVisible(listHeader);

    setVisible(false);
}

juce::Colour WavetableEditor::accent() const { return oscIndex == 0 ? col::acA : col::acB; }

void WavetableEditor::openFor(int osc) {
    oscIndex = osc;
    tab = Tab::Audio;
    mode = AudioMode::Single;
    hasAudio = false;
    drawPad.setAccent(accent());
    audioPreview.setAccent(accent());
    audioSamples.clear();
    nameField.setText("", juce::dontSendNotification);
    statusLabel.setText("load an audio file to begin", juce::dontSendNotification);
    drawPad.clear();
    audioPreview.clear();
    setTab(Tab::Audio);
    setMode(AudioMode::Single);
    refreshList();
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
    int idx = proc.addUserTable(std::move(u));
    if (idx < 0) {
        // Pool is full — keep the editor open and surface why, rather than
        // closing as if the table had been added.
        setTab(Tab::Audio); // the status line lives on the audio tab
        statusLabel.setText("table pool full (max " + juce::String(proc.maxUserTables())
                            + ") — delete one first", juce::dontSendNotification);
        refreshList();
        return;
    }
    // Assign the new table to the oscillator that opened the editor.
    if (auto* p = proc.apvts.getParameter(oscIndex == 0 ? "oscA.table" : "oscB.table"))
        p->setValueNotifyingHost(p->convertTo0to1((float)idx));
    close();
}

static std::string finalName(const juce::String& raw) {
    juce::String s = raw.trim().toUpperCase();
    if (s.isEmpty()) s = "USER";
    return s.substring(0, 14).toStdString();
}

void WavetableEditor::createFromAudio() {
    auto frames = framesFromCurrentSettings();
    if (frames.empty()) return;
    commit(fable::makeUserTable(finalName(nameField.getText()), frames));
}

void WavetableEditor::createFromDraw() {
    std::vector<std::vector<float>> frames{ fable::frameFromDrawing(drawPad.points()) };
    commit(fable::makeUserTable(finalName(nameField.getText()), frames));
}

void WavetableEditor::refreshList() {
    listRows.clear();
    const auto& pool = proc.getUserTables();
    for (int i = 0; i < (int)pool.size(); ++i) {
        juce::String text = juce::String(pool[(size_t)i].name) + "   " + juce::String(pool[(size_t)i].frames) + "f";
        auto* row = new Row(text, [this, i] {
            proc.deleteUserTable(i);
            refreshList();
            layoutPanel();
        });
        listRows.add(row);
        addAndMakeVisible(row);
    }
    layoutPanel();
}

juce::Rectangle<int> WavetableEditor::panelBounds() const {
    const int w = juce::jmin(520, getWidth() - 40);
    const int h = juce::jmin(620, getHeight() - 40);
    return juce::Rectangle<int>(0, 0, w, h).withCentre(getLocalBounds().getCentre());
}

void WavetableEditor::resized() { layoutPanel(); }

void WavetableEditor::layoutPanel() {
    auto panel = panelBounds().reduced(18);

    auto head = panel.removeFromTop(26);
    closeBtn.setBounds(head.removeFromRight(24).reduced(2));
    panel.removeFromTop(8);

    auto tabs = panel.removeFromTop(24);
    tabAudio.setBounds(tabs.removeFromLeft(tabs.getWidth() / 2).reduced(2, 0));
    tabDraw.setBounds(tabs.reduced(2, 0));
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
        drawPad.setBounds(panel.removeFromTop(180));
        panel.removeFromTop(6);
        drawHint.setBounds(panel.removeFromTop(16));
        panel.removeFromTop(8);
        auto row = panel.removeFromTop(30);
        clearBtn.setBounds(row.removeFromLeft(row.getWidth() / 3).reduced(2, 0));
        createDrawBtn.setBounds(row.reduced(2, 0));
    }

    panel.removeFromTop(14);
    listHeader.setBounds(panel.removeFromTop(16));
    for (auto* row : listRows) {
        if (panel.getHeight() < 24) break;
        row->setBounds(panel.removeFromTop(22));
        panel.removeFromTop(4);
    }
    repaint();
}

void WavetableEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black.withAlpha(0.6f)); // dim backdrop

    auto pb = panelBounds().toFloat();
    drawPanel(g, pb, 12.0f);
    // accent top edge
    g.setColour(accent());
    g.fillRect(pb.getX() + 12.0f, pb.getY(), pb.getWidth() - 24.0f, 2.0f);

    // header title
    g.setColour(accent());
    g.setFont(dispFont(13.0f));
    auto titleArea = juce::Rectangle<int>((int)pb.getX() + 18, (int)pb.getY() + 16, (int)pb.getWidth() - 60, 22);
    drawSpaced(g, juce::String("WAVETABLE -> ") + (oscIndex == 0 ? "OSC A" : "OSC B"),
               titleArea, 1.0f, juce::Justification::centredLeft);

    // highlight the active tab / mode underline
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
    }
}

} // namespace fui
