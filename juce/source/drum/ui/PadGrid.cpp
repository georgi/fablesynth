#include "PadGrid.h"
#include "../../dsp/UserTables.h"

namespace fui {

static float parameterValue(DrumUiModel& model, const juce::String& id) {
    auto* p = model.parameters().parameter(id);
    return p ? p->convertFrom0to1(p->getValue()) : 0.0f;
}

// Web timings/velocities (PadGrid.tsx, store.ts, useDrumKeys.ts).
static constexpr int    kLitMs    = 180;    // pad-led.lit window
static constexpr float  kClickVel = 0.8f;   // store.selectPad audition
static constexpr float  kKeyVel   = 0.85f;  // useDrumKeys trigger
static constexpr int    kGap      = 8;      // .pad-grid gap

// QWERTY map — useDrumKeys.ts KEYMAP flattened so indexOfChar == pad index:
// zxcv = pads 0-3, asdf = 4-7, qwer = 8-11, 1234 = 12-15.
static const juce::String kKeyOrder("zxcvasdfqwer1234");

PadGrid::PadGrid(DrumUiModel& p) : proc(p) {
    formatMgr.registerBasicFormats();
    for (int i = 0; i < fable::DR_NPADS; ++i) lastTag_[(size_t)i] = -1;
    setInterceptsMouseClicks(true, false);
    startTimerHz(30);
}

#ifndef FABLE_HOSTED_UI
PadGrid::PadGrid(DrumAudioProcessor& p)
    : ownedModel(makeStandaloneDrumUiModel(p)), proc(*ownedModel) {
    formatMgr.registerBasicFormats();
    for (int i = 0; i < fable::DR_NPADS; ++i) lastTag_[(size_t)i] = -1;
    setInterceptsMouseClicks(true, false);
    startTimerHz(30);
}
#endif

PadGrid::~PadGrid() {
    stopTimer();
    if (keyHost_ != nullptr) keyHost_->removeKeyListener(this);
}

// Register on the top-level component so pads play regardless of which child
// has keyboard focus (the web listens on window).
void PadGrid::parentHierarchyChanged() {
    auto* top = getTopLevelComponent();
    if (top == this) top = nullptr;
    if (top == keyHost_.getComponent()) return;
    if (keyHost_ != nullptr) keyHost_->removeKeyListener(this);
    keyHost_ = top;
    if (keyHost_ != nullptr) keyHost_->addKeyListener(this);
}

// ---- geometry --------------------------------------------------------------
// Panel box (.panel): padding 8px 12px 10px; head 15px + 8px margin; then the
// 4x4 grid with 8px gaps (352x369 panel -> 76px square tiles, like the web).
juce::Rectangle<int> PadGrid::tileBounds(int i) const {
    auto r = getLocalBounds();
    r.removeFromLeft(12);  r.removeFromRight(12);
    r.removeFromTop(8);    r.removeFromBottom(10);
    r.removeFromTop(15 + 8);
    const float tw = (float)(r.getWidth()  - 3 * kGap) / 4.0f;
    const float th = (float)(r.getHeight() - 3 * kGap) / 4.0f;
    const int row = 3 - i / 4, col = i % 4;  // pad 01 bottom-left
    return juce::Rectangle<float>((float)r.getX() + (float)col * (tw + kGap),
                                  (float)r.getY() + (float)row * (th + kGap),
                                  tw, th).toNearestInt();
}

int PadGrid::padAt(juce::Point<int> pos) const {
    for (int i = 0; i < fable::DR_NPADS; ++i)
        if (tileBounds(i).contains(pos)) return i;
    return -1;
}

// ---- interaction ------------------------------------------------------------
void PadGrid::flash(int i) {
    hitMs_[(size_t)i] = juce::Time::getMillisecondCounter();
    repaint(tileBounds(i));
}

void PadGrid::mouseDown(const juce::MouseEvent& e) {
    const int i = padAt(e.getPosition());
    if (i < 0) return;
    proc.selectPad(i);          // web store.selectPad: select + audition
    proc.triggerPad(i, kClickVel);
    flash(i);
    repaint();                       // selection ring moved
}

bool PadGrid::keyPressed(const juce::KeyPress& k, juce::Component*) {
    const auto mods = k.getModifiers();
    if (mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown()) return false;
    if (k.getKeyCode() == juce::KeyPress::escapeKey) {   // useDrumKeys: Escape = stop
        proc.setSequencerPlaying(false);
        return true;
    }
    const auto c = juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
    const int i = kKeyOrder.indexOfChar(c);
    if (i < 0) return false;
    if (heldKeys_.count(k.getKeyCode()) > 0) return true; // swallow OS auto-repeat
    heldKeys_.insert(k.getKeyCode());
    proc.triggerPad(i, kKeyVel);
    flash(i);
    return true;
}

// ---- drop-WAV import ---------------------------------------------------------
static bool isAudioFile(const juce::String& path) {
    const auto ext = path.fromLastOccurrenceOf(".", false, false).toLowerCase();
    return ext == "wav" || ext == "aif" || ext == "aiff" || ext == "flac" || ext == "mp3";
}

bool PadGrid::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files)
        if (isAudioFile(f)) return true;
    return false;
}

void PadGrid::fileDragEnter(const juce::StringArray&, int x, int y) {
    dragOver_ = padAt({ x, y });
    repaint();
}
void PadGrid::fileDragMove(const juce::StringArray&, int x, int y) {
    const int i = padAt({ x, y });
    if (i != dragOver_) { dragOver_ = i; repaint(); }
}
void PadGrid::fileDragExit(const juce::StringArray&) {
    if (dragOver_ != -1) { dragOver_ = -1; repaint(); }
}

void PadGrid::filesDropped(const juce::StringArray& files, int x, int y) {
    const int i = padAt({ x, y });
    dragOver_ = -1;
    repaint();
    if (i < 0 || files.isEmpty()) return;
    for (const auto& f : files)          // web takes the first (audio) file
        if (isAudioFile(f)) { importFile(juce::File(f), i); break; }
}

// Web PadGrid.dropFile: decode -> mixToMono -> detectCycleLength ->
// sliceToFrames -> buildUserTable(name = file name upper-cased, 14 chars).
void PadGrid::importFile(const juce::File& file, int padIndex) {
    std::unique_ptr<juce::AudioFormatReader> reader(formatMgr.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0) return;
    // Analysis only needs a bounded window; cap ~10 s so a long clip can't
    // blow up the allocation (WT-1 WavetableEditor precedent).
    const auto maxSamples = (juce::int64)(reader->sampleRate * 10.0);
    const int n = (int)juce::jmin(maxSamples, reader->lengthInSamples);
    juce::AudioBuffer<float> buf((int)reader->numChannels, juce::jmax(1, n));
    reader->read(&buf, 0, n, 0, true, true);

    const auto mono = fable::mixToMono(buf.getArrayOfReadPointers(), buf.getNumChannels(), n);
    auto frames = fable::sliceToFrames(mono, fable::detectCycleLength(mono, reader->sampleRate));
    if (frames.empty()) frames = fable::singleCycleFrame(mono);
    if (frames.empty()) return;

    auto name = file.getFileName().toUpperCase().substring(0, 14); // web fileTableName
    if (name.isEmpty()) name = "USER";
    proc.addUserTableForPad(padIndex, fable::makeUserTable(name.toStdString(), frames));
    flash(padIndex);
}

// ---- animation --------------------------------------------------------------
void PadGrid::timerCallback() {
    // Hits reported by the audio thread (MIDI, sequencer, auditions).
    const juce::uint32 flags = proc.consumeHitFlags();
    const auto now = juce::Time::getMillisecondCounter();
    for (int i = 0; i < fable::DR_NPADS; ++i)
        if ((flags & (1u << i)) != 0) hitMs_[(size_t)i] = now;

    // Auto-repeat filter bookkeeping: physical key released -> allow re-trigger.
    for (auto it = heldKeys_.begin(); it != heldKeys_.end();)
        it = juce::KeyPress::isKeyCurrentlyDown(*it) ? std::next(it) : heldKeys_.erase(it);

    // Repaint only when something visible changed (LED window, tag, selection).
    juce::uint32 lit = 0;
    bool tagChanged = false;
    for (int i = 0; i < fable::DR_NPADS; ++i) {
        if (now - hitMs_[(size_t)i] < (juce::uint32)kLitMs) lit |= 1u << i;
        const auto pre = "pad" + juce::String(i) + ".";
        const int choke = (int)std::lround(parameterValue(proc, pre + "choke"));
        const int out   = (int)std::lround(parameterValue(proc, pre + "out"));
        const int tag = (choke << 8) | out;
        if (tag != lastTag_[(size_t)i]) { lastTag_[(size_t)i] = tag; tagChanged = true; }
    }
    const int sel = proc.selectedPad();
    if (lit != lastLit_ || sel != lastSel_ || tagChanged) {
        lastLit_ = lit;
        lastSel_ = sel;
        repaint();
    }
}

// ---- paint -------------------------------------------------------------------
void PadGrid::paint(juce::Graphics& g) {
    drawPanel(g, getLocalBounds().toFloat());

    // .panel-head: h2 "PADS" (accent a), right-aligned dim hint
    auto head = getLocalBounds();
    head.removeFromLeft(12); head.removeFromRight(12); head.removeFromTop(8);
    head = head.removeFromTop(15);
    g.setColour(col::acA);
    g.setFont(dispFont(10.0f));
    drawSpaced(g, "PADS", head, 2.2f);
    g.setColour(col::textDim);
    g.setFont(monoFont(7.0f));
    // web: "DROP WAV → WAVETABLE" — ASCII arrow; JUCE's default mono font has
    // no U+2192 glyph and draws a fallback box instead.
    drawSpaced(g, "DROP WAV -> WAVETABLE", head, 1.1f, juce::Justification::right);

    const auto now = juce::Time::getMillisecondCounter();
    const int sel = proc.selectedPad();

    for (int i = 0; i < fable::DR_NPADS; ++i) {
        const auto t = tileBounds(i).toFloat();

        // .pad body: vertical gradient #1c212d -> #10131b, radius 10
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1c212d), t.getX(), t.getY(),
                                               juce::Colour(0xff10131b), t.getX(), t.getBottom(),
                                               false));
        g.fillRoundedRectangle(t, 10.0f);

        if (i == sel) {          // .pad.sel: cyan border + halo
            g.setColour(col::acA.withAlpha(0.3f));
            g.drawRoundedRectangle(t.expanded(1.0f), 11.0f, 2.0f);
            g.setColour(col::acA);
            g.drawRoundedRectangle(t.reduced(0.5f), 10.0f, 1.0f);
        } else if (i == dragOver_) {  // .pad.drag-over: amber
            g.setColour(col::acB.withAlpha(0.35f));
            g.drawRoundedRectangle(t.expanded(1.0f), 11.0f, 2.0f);
            g.setColour(col::acB);
            g.drawRoundedRectangle(t.reduced(0.5f), 10.0f, 1.0f);
        } else {
            g.setColour(col::line);
            g.drawRoundedRectangle(t.reduced(0.5f), 10.0f, 1.0f);
        }

        // .pad-num top-left
        g.setColour(col::textDim);
        g.setFont(monoFont(9.0f));
        auto numArea = t.toNearestInt().withTrimmedLeft(8).withTrimmedTop(7);
        drawSpaced(g, juce::String(i + 1).paddedLeft('0', 2), numArea.withHeight(10), 1.0f);

        // .pad-led top-right, 7px, lit for 180 ms after a hit
        const bool litNow = now - hitMs_[(size_t)i] < (juce::uint32)kLitMs;
        juce::Rectangle<float> led(t.getRight() - 8.0f - 7.0f, t.getY() + 8.0f, 7.0f, 7.0f);
        if (litNow) {
            g.setColour(col::acA.withAlpha(0.5f));
            g.fillEllipse(led.expanded(2.5f));
            g.setColour(col::acA);
        } else {
            g.setColour(juce::Colour(0xff232936));
        }
        g.fillEllipse(led);

        // .pad-meta: name over choke/out tag, anchored bottom-left
        const auto meta = t.toNearestInt().reduced(7, 0).withTrimmedBottom(7);
        g.setColour(col::text);
        g.setFont(monoFont(9.0f));
        g.drawText(proc.padName(i), meta.withTop(meta.getBottom() - 29).withHeight(16),
                   juce::Justification::centredLeft, true);

        const auto pre = "pad" + juce::String(i) + ".";
        const int choke = (int)std::lround(parameterValue(proc, pre + "choke"));
        const int out   = (int)std::lround(parameterValue(proc, pre + "out"));
        const auto& names = choke > 0 ? fable::CHOKE_NAMES : fable::OUT_NAMES;
        const int nameIdx = juce::jlimit(0, (int)names.size() - 1, choke > 0 ? choke : out);
        g.setColour(col::textDim);
        g.setFont(monoFont(7.0f));
        drawSpaced(g, juce::String(names[(size_t)nameIdx]),
                   meta.withTop(meta.getBottom() - 9), 0.9f);
    }
}

} // namespace fui
